#!/bin/bash
set -e

sudo apt-get update
sudo apt-get install -y build-essential cmake git make gdb valgrind strace

if sudo apt-get install -y linux-headers-$(uname -r); then
    echo "kernel headers installed"
else
    echo "no linux-headers package for this kernel (normal on WSL2), skipping"
fi

echo
echo "compiler:       $(g++ --version | head -1)"
echo "make:           $(make --version | head -1)"
echo "kernel:         $(uname -r)"
if [ -d /lib/modules/$(uname -r)/build ]; then
    echo "kernel headers: found, the driver can be built with: make kernel"
else
    echo "kernel headers: not found, the driver needs the WSL2 steps or an Ubuntu VM (see README)"
fi
