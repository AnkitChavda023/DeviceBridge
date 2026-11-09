#!/bin/bash
sudo rmmod virtual_device
echo "last kernel messages:"
sudo dmesg | tail -3
if [ -e /dev/vdev0 ]; then
    echo "WARNING: /dev/vdev0 still exists"
else
    echo "/dev/vdev0 removed"
fi
