#!/usr/bin/env bash
set -euo pipefail

MODULE="kernel_hashtable_search"
KO="${MODULE}.ko"
SYS="/sys/module/${MODULE}/parameters"

cleanup() {
    rmmod "${MODULE}" 2>/dev/null || true
}

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

cleanup
insmod "./${KO}" array_size=256
trap cleanup EXIT

printf 'Initial result: '
cat "${SYS}/result"

echo 42 > "${SYS}/search"
printf 'Search 42: '
cat "${SYS}/result"

found_value=""
for bucket in $(seq 0 63); do
    echo "${bucket}" > "${SYS}/bucket_id"
    dump=$(cat "${SYS}/bucket_dump")
    echo "${dump}"
    value=$(echo "${dump}" | awk -F: '{print $2}' | awk '{print $1}')
    if [[ -n "${value}" ]]; then
        found_value="${value}"
        break
    fi
done

[[ -n "${found_value}" ]] || fail "could not find non-empty bucket"

echo "${found_value}" > "${SYS}/search"
result=$(cat "${SYS}/result")
echo "Search existing value ${found_value}: ${result}"
echo "${result}" | grep -q "found=1 value=${found_value}" || \
    fail "existing value was not found"

if echo 64 > "${SYS}/bucket_id" 2>/dev/null; then
    fail "invalid bucket_id accepted"
else
    echo "Invalid bucket_id rejected"
fi

if echo abc > "${SYS}/search" 2>/dev/null; then
    fail "invalid search value accepted"
else
    echo "Invalid search value rejected"
fi

echo 1 > "${SYS}/rebuild"
echo "Rebuild completed"
cat "${SYS}/result"

cleanup
trap - EXIT

echo "OK"
