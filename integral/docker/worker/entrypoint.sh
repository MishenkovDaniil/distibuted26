#!/bin/sh
set -e

: "${WORKER_PORT:=5000}"
: "${DISCOVERY_PORT:=4000}"
: "${MASTER_PORT:=6000}"

cd /app/build

# Run worker binary (adjust name/path as in your CMakeLists)
./worker
