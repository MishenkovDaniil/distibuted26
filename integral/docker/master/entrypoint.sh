#!/bin/sh
set -e

: "${MASTER_PORT:=6000}"
: "${DISCOVERY_PORT:=4000}"
: "${INTEGRAL_LEFT:=0}"
: "${INTEGRAL_RIGHT:=10000}"

cd /app/build

./master $INTEGRAL_LEFT $INTEGRAL_RIGHT
