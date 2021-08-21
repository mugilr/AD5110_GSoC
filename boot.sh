#! /bin/bash

BOOT_DIR="/media/mugil/BOOT"
ROOT_DIR="/media/mugil/rootfs"
KERNEL_SRC_DIR="/home/mugil/linux/linux"
KERNEL_BUILT_DIR="${KERNEL_SRC_DIR}/arch/arm/boot"

#make ARCH=arm adi_bcm2709_defconfig
#cd $KERNEL_SRC_DIR
#sudo ARCH=arm CROSS_COMPILE=/home/mugil/Downloads/gcc-linaro-7.4.1-2019.02-x86_64_arm-linux-gnueabi/bin/arm-linux-gnueabi- make zImage modules dtbs -j8


if [[ ! -d $BOOT_DIR ]]; then 
	echo "boot mount not found!"
	exit 1
fi

if [[ ! -d $ROOT_DIR ]]; then 
	echo "root mount not found!"
	exit 1
fi

echo "Writing the kernel and the dtbs to the SD card..."
#sleep 1
cp ${KERNEL_BUILT_DIR}/zImage ${BOOT_DIR}/kernel7.img
cp ${KERNEL_BUILT_DIR}/dts/*.dtb ${BOOT_DIR}/
cp ${KERNEL_BUILT_DIR}/dts/overlays/*.dtbo ${BOOT_DIR}/overlays/

echo "Installing kernel modules..."
#sleep 1
cd $KERNEL_SRC_DIR
sudo make INSTALL_MOD_PATH=$ROOT_DIR modules_install 

sleep 1

echo "Syncing the write buffers..."
#sleep 1
rsync $BOOT_DIR/
rsync $ROOT_DIR/

sleep 1

echo "umounting..."
#sleep 1
sudo umount $BOOT_DIR
sudo umount $ROOT_DIR
