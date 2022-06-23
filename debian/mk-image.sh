#!/bin/bash -e

TARGET_ROOTFS_DIR=./binary
MOUNTPOINT=./rootfs
ROOTFSIMAGE=linaro-rootfs.img

echo Making rootfs!

if [ -e ${ROOTFSIMAGE} ]; then
	rm ${ROOTFSIMAGE}
fi
if [ -e ${MOUNTPOINT} ]; then
	rm -r ${MOUNTPOINT}
fi

sudo ./post-build.sh $TARGET_ROOTFS_DIR

# Create directories
mkdir ${MOUNTPOINT}
dd if=/dev/zero of=${ROOTFSIMAGE} bs=1M count=0 seek=4000

finish() {
	sudo umount ${MOUNTPOINT} || true
	echo -e "\e[31m MAKE ROOTFS FAILED.\e[0m"
	exit -1
}

echo Format rootfs to ext4
mkfs.ext4 ${ROOTFSIMAGE}

echo Mount rootfs to ${MOUNTPOINT}
sudo mount  ${ROOTFSIMAGE} ${MOUNTPOINT}
trap finish ERR

echo Copy rootfs to ${MOUNTPOINT}
sudo cp -rfp ${TARGET_ROOTFS_DIR}/*  ${MOUNTPOINT}

echo Umount rootfs
sudo umount ${MOUNTPOINT}

echo Rootfs Image: ${ROOTFSIMAGE}

e2fsck -p -f ${ROOTFSIMAGE}
resize2fs -M ${ROOTFSIMAGE}
