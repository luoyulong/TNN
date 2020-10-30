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

#include "test/unit_test/layer_test/layer_test.h"
#include "test/unit_test/unit_test_common.h"
#include "test/unit_test/utils/network_helpers.h"
#include "tnn/utils/dims_vector_utils.h"

namespace TNN_NS {

class BatchNormScaleLayerTest : public LayerTest,
                                public ::testing::WithParamInterface<std::tuple<int, int, int, bool, bool>> {};

INSTANTIATE_TEST_SUITE_P(LayerTest, BatchNormScaleLayerTest,
                         ::testing::Combine(BASIC_BATCH_CHANNEL_SIZE,
                                            // share channel
                                            testing::Values(false, true),
                                            // has bias
                                            testing::Values(true, false)));

TEST_P(BatchNormScaleLayerTest, BatchNormScaleLayer) {
    // get param
    int batch          = std::get<0>(GetParam());
    int channel        = std::get<1>(GetParam());
    int input_size     = std::get<2>(GetParam());
    bool share_channel = std::get<3>(GetParam());
    bool has_bias      = std::get<4>(GetParam());

    DeviceType dev = ConvertDeviceType(FLAGS_dt);
    // blob desc
    auto inputs_desc  = CreateInputBlobsDesc(batch, channel, input_size, 1, DATA_TYPE_FLOAT);
    auto outputs_desc = CreateOutputBlobsDesc(1, DATA_TYPE_FLOAT);

    // param
    LayerParam param;
    param.name = "BatchNorm";

    // resource
    BatchNormLayerResource resource;
    int k_count = share_channel ? 1 : channel;
    RawBuffer filter_k(k_count * sizeof(float));
    float* k_data = filter_k.force_to<float*>();
    InitRandom(k_data, k_count, 1.0f);
    resource.scale_handle = filter_k;
    if (has_bias) {
        RawBuffer bias(k_count * sizeof(float));
        float* bias_data = bias.force_to<float*>();
        InitRandom(bias_data, k_count, 1.0f);
        resource.bias_handle = bias;
    }

    Run(LAYER_BATCH_NORM, &param, &resource, inputs_desc, outputs_desc);
    param.name = "Scale";
    Run(LAYER_SCALE, &param, &resource, inputs_desc, outputs_desc);
}

TEST_P(BatchNormScaleLayerTest, BatchNormScaleLayerWithProto) {
    // get param
    int batch      = std::get<0>(GetParam());
    int channel    = std::get<1>(GetParam());
    int input_size = std::get<2>(GetParam());

    DeviceType dev = ConvertDeviceType(FLAGS_dt);

    // generate proto string
    std::string head = GenerateHeadProto({batch, channel, input_size, input_size});

    std::ostringstream ostr1;
    ostr1 << "\""
          << "BatchNormCxx layer_name 1 1 input output "
          << ",\"";
    std::string proto1 = head + ostr1.str();
    RunWithProto(proto1);

    std::ostringstream ostr2;
    ostr2 << "\""
          << "Scale layer_name 1 1 input output "
          << ",\"";
    std::string proto2 = head + ostr2.str();
    RunWithProto(proto2);
}

}  // namespace TNN_NS
