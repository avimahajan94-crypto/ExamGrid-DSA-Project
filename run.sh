#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
CXX="${CXX:-g++}"
"$CXX" -std=c++17 -O2 -Wall -Wextra -pedantic main.cpp -o examgrid
exec ./examgrid
