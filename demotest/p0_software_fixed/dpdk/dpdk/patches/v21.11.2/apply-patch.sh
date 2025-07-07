# (C) 2001-2025 Altera Corporation. All rights reserved.
# Your use of Altera Corporation's design tools, logic functions and other 
# software and tools, and its AMPP partner logic functions, and any output 
# files from any of the foregoing (including device programming or simulation 
# files), and any associated documentation or information are expressly subject 
# to the terms and conditions of the Altera Program License Subscription 
# Agreement, Altera IP License Agreement, or other applicable 
# license agreement, including, without limitation, that your use is for the 
# sole purpose of programming logic devices manufactured by Altera and sold by 
# Altera or its authorized distributors.  Please refer to the applicable 
# agreement for further details.


#!/bin/bash
#
# Script to download and apply DPDK patchset
# on top of v17.11.2 stable tag.
#

DPDK_ROOT_DIR=$PWD/../../

thisdir=$(readlink -f $(dirname $0))

if [ -d dpdk-stable ]; then
	echo "DPDK Already cloned. Removing\n"
	rm -rf dpdk-stable
fi

# clone
git clone https://dpdk.org/git/dpdk-stable

cd dpdk-stable
git checkout v21.11.2

# Copy McDMA PMD
cp -rf ${DPDK_ROOT_DIR}/drivers/net/mcdma/ ./drivers/net/
echo "Copied DPDK PMD"

#Copy Example test application
cp -rf ${DPDK_ROOT_DIR}/examples/mcdma-test/ ./examples/

#Copy igb_uio
cp -rf  ${DPDK_ROOT_DIR}/kernel/linux/igb_uio ./kernel/linux/
echo "Copied KMOD driver"


pwd

# apply all patches
patches=$(ls ${thisdir}/*.patch | sort -n)
for p in $patches ; do
	echo "patch -p1 <$p"
        patch -p1 <$p
done

#Copy Test-pmd related files
cp -rf ${DPDK_ROOT_DIR}/app/test-pmd/ifc_mcdma_testpmd.c ./app/test-pmd/
cp -rf ${DPDK_ROOT_DIR}/app/test-pmd/ifc_mcdma_testpmd.h ./app/test-pmd/
echo "Copied Test-pmd related files"

echo "Done"
