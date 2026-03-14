#!/bin/sh
set -e

: "${MASTER_PORT:=6000}"
: "${DISCOVERY_PORT:=4000}"

cd /app/build

./master 0 100
