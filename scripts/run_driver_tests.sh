#!/bin/bash
cd "$(dirname "$0")/.."
make driver-test
./build/tests/driver/driver_test /dev/vdev0
code=$?
echo
echo "kernel messages after the test:"
sudo -n dmesg 2>/dev/null | tail -5
exit $code
