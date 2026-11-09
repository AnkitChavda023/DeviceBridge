#!/bin/bash
cd "$(dirname "$0")/.."

make -j8 all driver-test || exit 1
./scripts/run_tests.sh build || exit 1

make -j8 SAN=thread tests || exit 1
./scripts/run_tests.sh build-tsan || exit 1

make -j8 SAN=address tests || exit 1
./scripts/run_tests.sh build-asan || exit 1

echo
echo "ALL CHECKS PASSED"
