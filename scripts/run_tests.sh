#!/bin/bash
BUILD=${1:-build}
cd "$(dirname "$0")/.."

passed=0
failed=0
skipped=0
failed_names=""

for test in $BUILD/tests/unit/* $BUILD/tests/integration/* $BUILD/tests/concurrency/* $BUILD/tests/failure_injection/*; do
    [ -x "$test" ] || continue
    timeout 120 "$test" > /tmp/test_output.txt 2>&1
    code=$?
    if [ $code -eq 0 ]; then
        passed=$((passed + 1))
        echo "PASS  $test  ($(tail -1 /tmp/test_output.txt))"
    elif [ $code -eq 77 ]; then
        skipped=$((skipped + 1))
        echo "SKIP  $test"
    else
        failed=$((failed + 1))
        failed_names="$failed_names $test"
        echo "FAIL  $test (exit $code)"
        cat /tmp/test_output.txt
    fi
done

echo
echo "suites passed: $passed, failed: $failed, skipped: $skipped"
[ $failed -eq 0 ]
