#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_binary="$(mktemp /tmp/flexispot-tests.XXXXXX)"
trap 'rm -f "$test_binary"' EXIT
for test_source in tests/desk_protocol_test.cpp tests/status_indicator_test.cpp tests/connection_health_test.cpp; do
  "${CXX:-g++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -g "$test_source" -o "$test_binary"
  "$test_binary"
done
