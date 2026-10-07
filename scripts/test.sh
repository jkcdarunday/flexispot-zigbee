#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_binary="$(mktemp /tmp/flexispot-tests.XXXXXX)"
trap 'rm -f "$test_binary"' EXIT
"${CXX:-g++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -g tests/desk_protocol_test.cpp -o "$test_binary"
"$test_binary"
