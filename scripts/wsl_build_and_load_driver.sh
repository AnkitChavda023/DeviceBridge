#!/bin/bash
set -e

SRC=/opt/wslk/src
PROJECT="$(cd "$(dirname "$0")/.." && pwd)"
LOG="$PROJECT/docs/driver_build_log.txt"
CC13=gcc-13

if [ "$(id -u)" -ne 0 ]; then
    echo "run as root: sudo bash scripts/wsl_build_and_load_driver.sh"
    exit 1
fi

exec > >(tee "$LOG") 2>&1

echo "running kernel: $(uname -r)"

if ! command -v $CC13 >/dev/null; then
    apt-get install -y gcc-13 g++-13 gcc-13-plugin-dev
fi
$CC13 --version | head -1

cd "$SRC"
rm -rf "$SRC/.git"
: > "$SRC/.scmversion"
make CC=$CC13 mrproper
zcat /proc/config.gz > "$SRC/.config"
make CC=$CC13 olddefconfig

check_config() {
    want=$(zcat /proc/config.gz | grep -E "^$1=")
    have=$(grep -E "^$1=" "$SRC/.config" || true)
    if [ "$want" != "$have" ]; then
        echo "config mismatch for $1: running '$want', build tree '$have'"
        exit 1
    fi
}

check_config CONFIG_ACPI_AC
check_config CONFIG_8139TOO
check_config CONFIG_LOCALVERSION
check_config CONFIG_MODVERSIONS

make CC=$CC13 -j"$(nproc)"

release=$(make CC=$CC13 -s kernelrelease)
echo "build tree release: $release"
if [ "$release" != "$(uname -r)" ]; then
    echo "release does not match the running kernel"
    exit 1
fi

shipped=/lib/modules/$(uname -r)/kernel/arch/x86/kvm/kvm-amd.ko
for sym in module_layout _printk mutex_lock; do
    a=$(modprobe --dump-modversions "$shipped" | awk -v s=$sym '$2==s {print $1}')
    b=$(awk -v s=$sym '$2==s {print $1}' "$SRC/Module.symvers")
    echo "checksum $sym: running $a, build tree $b"
    if [ "$a" != "$b" ]; then
        echo "checksum differs for $sym, the module would not load"
        exit 1
    fi
done

cd "$PROJECT"
make -C kernel/virtual_device clean KDIR="$SRC" CC=$CC13 || true
make -C kernel/virtual_device KDIR="$SRC" CC=$CC13

rmmod virtual_device 2>/dev/null || true
insmod kernel/virtual_device/virtual_device.ko
sleep 0.5

echo "module loaded:"
lsmod | grep virtual_device
grep vdev /proc/devices
ls -l /dev/vdev0
dmesg | tail -3
echo "DRIVER LOADED"
