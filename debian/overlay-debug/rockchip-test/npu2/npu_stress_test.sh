#!/bin/bash

while true
do
    /rockchip-test/npu2/aarch64/rknn_common_test /rockchip-test/npu2/model/RK3588/vgg16_max_pool_fp16.rknn /rockchip-test/npu2/model/dog_224x224.jpg 10;
done
