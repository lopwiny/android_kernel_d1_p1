#!/bin/bash
##############################################################################
#
#                           Kernel Build Script
#
##############################################################################
# 2014-06-21 Shev_t       : modified
# 2011-10-24 effectivesky : modified
# 2010-12-29 allydrop     : created
##############################################################################

##############################################################################
# set toolchain
##############################################################################
export ARCH=arm
export SUBARCH=arm
# Linaro GCC 5.3.1 sources: http://releases.linaro.org/components/toolchain/binaries/5.3-2016.05/arm-linux-gnueabi/gcc-linaro-5.3.1-2016.05-i686_arm-linux-gnueabi.tar.xz
export CROSS_COMPILE=~/AndroidSources/gcc-linaro-5.3.1-2016.05/bin/arm-linux-gnueabi-
export LOCALVERSION="-mm"
export USE_CCACHE=1
export CCACHE_DIR=~/.ccache/front-kernel
export MY_CONFIG=huawei_omap4_defconfig
ccache -M 5G

##############################################################################
# set variables
##############################################################################
export KERNELDIR=`pwd`
KERNEL_OUT=$KERNELDIR/obj/KERNEL_OBJ
STRIP=${CROSS_COMPILE}strip

##############################################################################
# make zImage
##############################################################################
mkdir -p $KERNEL_OUT
mkdir -p $KERNEL_OUT/tmp/kernel
mkdir -p $KERNEL_OUT/tmp/system/lib/modules

make O=$KERNEL_OUT $MY_CONFIG
make -j10 O=$KERNEL_OUT

if [ -f $KERNEL_OUT/arch/arm/boot/zImage ]
then
    cp -f $KERNEL_OUT/arch/arm/boot/zImage ./
    mv -f $KERNEL_OUT/arch/arm/boot/zImage $KERNEL_OUT/tmp/kernel/zImage
fi

if [ -f ./zImage ]
then
    CURRENT_DATE=`date +%Y%m%d-%H%M`
    KERNEL_FNAME=kernel$LOCALVERSION-$CURRENT_DATE-cma.zip
    cp ./android/blank_any_kernel.zip $KERNEL_FNAME
    pushd $KERNEL_OUT/tmp
    zip -r ../../../$KERNEL_FNAME ./kernel/
    if [ ! "$SGX_MODULE" ]
    then
        zip -r ../../../$KERNEL_FNAME ./system/
    fi
fi
