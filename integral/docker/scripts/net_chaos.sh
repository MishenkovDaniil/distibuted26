#!/bin/bash
set -e

# Universal network chaos tool for Docker containers.
#
# Usage:
#   ./net_chaos.sh <command> <container> [options]
#
# Commands:
#   delay   <container> <ms> [jitter_ms]      - add latency
#   loss    <container> <percent>             - packet loss (IP level)
#   dup     <container> <percent>             - packet duplication
#   corrupt <container> <percent>             - packet corruption
#   mixed   <container> <loss%> <delay_ms>    - combined loss + delay
#   drop    <container>                       - full iptables DROP (simulates disconnect)
#   undrop  <container>                       - remove iptables DROP
#   pause   <container>                       - docker pause (freeze process)
#   unpause <container>                       - docker unpause
#   reset   <container>                       - remove all tc rules
#   show    <container>                       - show current tc/iptables rules

usage() {
    sed -n '4,20p' "$0" | sed 's/^# \?//'
    exit 1
}

run_in_ns() {
    local container="$1"; shift
    local pid
    pid=$(docker inspect -f '{{.State.Pid}}' "$container" 2>/dev/null)
    if [ -z "$pid" ] || [ "$pid" = "0" ]; then
        echo "error: container '$container' not running" >&2
        return 1
    fi
    nsenter -t "$pid" -n -- bash -c "$*"
}

tc_reset() {
    run_in_ns "$1" 'tc qdisc del dev eth0 root 2>/dev/null; echo "tc rules cleared on eth0"'
}

tc_apply() {
    local container="$1"; shift
    run_in_ns "$container" "tc qdisc del dev eth0 root 2>/dev/null; tc qdisc add dev eth0 root netem $*"
    echo "applied: netem $* on $container"
}

[ $# -lt 1 ] && usage

CMD="$1"; shift

case "$CMD" in
    delay)
        [ $# -lt 2 ] && { echo "usage: $0 delay <container> <ms> [jitter_ms]"; exit 1; }
        CONTAINER="$1"; DELAY="$2"; JITTER="${3:-0}"
        if [ "$JITTER" != "0" ]; then
            tc_apply "$CONTAINER" "delay ${DELAY}ms ${JITTER}ms"
        else
            tc_apply "$CONTAINER" "delay ${DELAY}ms"
        fi
        ;;
    loss)
        [ $# -lt 2 ] && { echo "usage: $0 loss <container> <percent>"; exit 1; }
        tc_apply "$1" "loss $2%"
        ;;
    dup)
        [ $# -lt 2 ] && { echo "usage: $0 dup <container> <percent>"; exit 1; }
        tc_apply "$1" "duplicate $2%"
        ;;
    corrupt)
        [ $# -lt 2 ] && { echo "usage: $0 corrupt <container> <percent>"; exit 1; }
        tc_apply "$1" "corrupt $2%"
        ;;
    mixed)
        [ $# -lt 3 ] && { echo "usage: $0 mixed <container> <loss%> <delay_ms>"; exit 1; }
        tc_apply "$1" "loss $2% delay ${3}ms"
        ;;
    drop)
        [ $# -lt 1 ] && { echo "usage: $0 drop <container>"; exit 1; }
        run_in_ns "$1" '
            iptables -A INPUT -j DROP
            iptables -A OUTPUT -j DROP
        '
        echo "iptables DROP applied on $1 (full disconnect)"
        ;;
    undrop)
        [ $# -lt 1 ] && { echo "usage: $0 undrop <container>"; exit 1; }
        run_in_ns "$1" '
            iptables -F INPUT
            iptables -F OUTPUT
        '
        echo "iptables rules flushed on $1 (reconnected)"
        ;;
    pause)
        [ $# -lt 1 ] && { echo "usage: $0 pause <container>"; exit 1; }
        docker pause "$1"
        echo "container $1 paused"
        ;;
    unpause)
        [ $# -lt 1 ] && { echo "usage: $0 unpause <container>"; exit 1; }
        docker unpause "$1"
        echo "container $1 unpaused"
        ;;
    reset)
        [ $# -lt 1 ] && { echo "usage: $0 reset <container>"; exit 1; }
        tc_reset "$1" 2>/dev/null || true
        run_in_ns "$1" 'iptables -F INPUT 2>/dev/null; iptables -F OUTPUT 2>/dev/null' 2>/dev/null || true
        echo "all rules cleared on $1"
        ;;
    show)
        [ $# -lt 1 ] && { echo "usage: $0 show <container>"; exit 1; }
        echo "=== tc qdisc ==="
        run_in_ns "$1" 'tc qdisc show dev eth0'
        echo "=== iptables ==="
        run_in_ns "$1" 'iptables -L -n 2>/dev/null' || echo "(no iptables)"
        ;;
    *)
        usage
        ;;
esac
