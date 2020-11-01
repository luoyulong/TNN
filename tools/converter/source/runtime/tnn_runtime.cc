// Tencent is pleased to support the open source community by making TNN available.
//
// Copyright (C) 2020 THL A29 Limited, a Tencent company. All rights reserved.
//
// Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// https://opensource.org/licenses/BSD-3-Clause
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

#include "tnn_runtime.h"

#include "include/tnn/core/common.h"
#include "include/tnn/core/instance.h"
#include "tnn/utils/data_type_utils.h"
#include "tnn/utils/dims_vector_utils.h"

namespace TNN_CONVERTER {

TnnRuntime::TnnRuntime() {
    // initial network config
    network_config_.network_type = TNN_NS::NETWORK_TYPE_DEFAULT;
    network_config_.device_type  = TNN_NS::DEVICE_NAIVE;
    network_config_.precision    = TNN_NS::PRECISION_AUTO;
    network_config_.library_path = {};
    // initial model config
    model_config_.model_type = TNN_NS::MODEL_TYPE_TNN;
    // fake mode config params
    model_config_.params = {};
}
TnnRuntime::~TnnRuntime() {
    // do nothing
}

TNN_NS::Status TnnRuntime::run(std::shared_ptr<TNN_NS::AbstractModelInterpreter> interpreter) {
    // create input shape map
    TNN_NS::DefaultModelInterpreter* tnn_interpreter =
        (dynamic_cast<TNN_NS::DefaultModelInterpreter*>(interpreter.get()));
    TNN_NS::InputShapesMap& input_shapes_map = tnn_interpreter->GetNetStructure()->inputs_shape_map;
    auto instance                            = std::make_shared<TNN_NS::Instance>(network_config_, model_config_);
    auto status                              = instance->Init(interpreter, input_shapes_map);
    if (status != TNN_NS::TNN_OK) {
        LOGE("Converter Runtime: instance init failed!\n");
        return status;
    }
    TNN_NS::BlobMap input_blob_map;
    TNN_NS::BlobMap output_blob_map;
    void* command_queue;
    instance->GetAllInputBlobs(input_blob_map);
    instance->GetAllOutputBlobs(output_blob_map);
    instance->GetCommandQueue(&command_queue);
    // create mat and converter
    // format type 0: DATA_TYPE_FLOAT
    TNN_NS::MatMap input_mat_map = CreateBlobMatMap(input_blob_map, 0);
    InitInputMatMap(input_mat_map);
    auto input_converters_map = CreateBlobConverterMap(input_blob_map);
    auto input_params_map     = CreateConvertParamMap(input_mat_map);
    // mat format NCHW_FLOAT
    TNN_NS::MatMap output_mat_map = CreateBlobMatMap(output_blob_map, 0);
    auto output_converters_map    = CreateBlobConverterMap(output_blob_map);
    auto output_params_map        = CreateConvertParamMap(output_mat_map);

    for (const auto& iter : input_converters_map) {
        auto name           = iter.first;
        auto blob_converter = iter.second;
        blob_converter->ConvertFromMatAsync(*input_mat_map[name], output_params_map[name], command_queue);
    }
    status = instance->Forward();
    if (status != TNN_NS::TNN_OK) {
        LOGE("Converter Runtime: instance forward failed\n");
        return status;
    }

    return TNN_NS::TNN_OK;
}

TNN_NS::MatMap TnnRuntime::CreateBlobMatMap(TNN_NS::BlobMap& blob_map, int format_type) {
    TNN_NS::MatMap mat_map;
    for (const auto& iter : blob_map) {
        auto name                  = iter.first;
        TNN_NS::Blob* device_blob  = iter.second;
        TNN_NS::BlobDesc blob_desc = device_blob->GetBlobDesc();
        // Format Types: (0: NCHW FLOAT), (1: 8UC3), (2: 8UC1)
        TNN_NS::DataType data_type = TNN_NS::DATA_TYPE_INT8;
        TNN_NS::MatType mat_type;
        if (format_type == 0) {
            data_type = TNN_NS::DATA_TYPE_FLOAT;
            mat_type  = TNN_NS::NCHW_FLOAT;
        } else if (format_type == 1) {
            mat_type = TNN_NS::N8UC3;
        } else if (format_type == 2) {
            mat_type = TNN_NS::NGRAY;
        }

        int bytes = TNN_NS::DimsVectorUtils::Count(blob_desc.dims) * TNN_NS::DataTypeUtils::GetBytesSize(data_type);
        void* mat_data = malloc(bytes);
        auto mat       = std::make_shared<TNN_NS::Mat>(TNN_NS::DEVICE_NAIVE, mat_type, blob_desc.dims, mat_data);

        mat_map[name] = mat;
    }
    return mat_map;
}

void TnnRuntime::InitInputMatMap(TNN_NS::MatMap& mat_map) {
    for (const auto& iter : mat_map) {
        auto name      = iter.first;
        auto mat       = iter.second;
        void* mat_data = mat->GetData();
        int data_count = TNN_NS::DimsVectorUtils::Count(mat->GetDims());
        auto mat_type  = mat->GetMatType();
        for (int i = 0; i < data_count; i++) {
            if (mat_type == TNN_NS::NCHW_FLOAT) {
                reinterpret_cast<float*>(mat_data)[i] = (float)(rand() % 256 - 128) / 128.0f;
            } else {
                reinterpret_cast<uint8_t*>(mat_data)[i] = (rand() % 256);
            }
        }
    }
}

std::map<std::string, std::shared_ptr<TNN_NS::BlobConverter>> TnnRuntime::CreateBlobConverterMap(
    TNN_NS::BlobMap& blob_map) {
    std::map<std::string, std::shared_ptr<TNN_NS::BlobConverter>> converter_map;
    for (auto iter : blob_map) {
        auto blob_converter       = std::make_shared<TNN_NS::BlobConverter>(iter.second);
        converter_map[iter.first] = blob_converter;
    }
    return converter_map;
}

std::map<std::string, TNN_NS::MatConvertParam> TnnRuntime::CreateConvertParamMap(TNN_NS::MatMap& mat_map) {
    std::map<std::string, TNN_NS::MatConvertParam> param_map;
    for (auto iter : mat_map) {
        TNN_NS::MatConvertParam param;
        auto name     = iter.first;
        auto mat      = iter.second;
        auto mat_type = mat->GetMatType();
        auto dims     = mat->GetDims();
        if (mat_type != TNN_NS::NCHW_FLOAT) {
            std::fill(param.scale.begin(), param.scale.end(), 1.0f / 255.0f);
            std::fill(param.bias.begin(), param.bias.end(), 0);
        } else if (dims[1] > 4) {
            param.scale = std::vector<float>(dims[1], 1);
            param.bias  = std::vector<float>(dims[1], 0);
        }
        param_map[name] = param;
    }
    return param_map;
}

}  // namespace TNN_CONVERTER