#!/bin/bash
set -e
cd "$(dirname "$0")/../kernel/virtual_device"

make
sudo insmod virtual_device.ko

sleep 0.5
ls -l /dev/vdev0
echo
echo "last kernel messages:"
sudo dmesg | tail -3
