#!/bin/bash
# Run this from within a bash shell
# x86_64 is for simulation do not enable RK platform
export AIQ_BUILD_HOST_DIR=/worksapce/Banana-Devel/Banana-Pi-RK3568/BPI-R2PRO-BSP/buildroot/output/rockchip_rk356x_recovery/host
TOOLCHAIN_FILE=$(pwd)/../../cmake/toolchains/aarch64_linux_buildroot.cmake
SOURCE_PATH=$(pwd)/../../
OUTPUT=$(pwd)/output/aarch64

mkdir -p $OUTPUT
pushd $OUTPUT

cmake -G "Ninja" \
    -DCMAKE_BUILD_TYPE=Release \
    -DARCH="aarch64" \
    -DRKPLATFORM=OFF \
    -DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN_FILE \
    -DCMAKE_SKIP_RPATH=TRUE \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=YES \
    -DISP_HW_VERSION=${ISP_HW_VERSION} \
    $SOURCE_PATH \
&& ninja -j$(nproc)

popd
