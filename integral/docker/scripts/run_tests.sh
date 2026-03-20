#!/bin/bash

# Test scenarios for the integral master-worker system.
#
# Usage: sudo ./run_tests.sh [scenario_number]
#
# Environment:
#   INTEGRAL_RIGHT  - upper bound for integral (default: 200)
#   WORKERS_COUNT   - number of workers (default: 3)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CHAOS="$SCRIPT_DIR/net_chaos.sh"
COMPOSE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

WORKERS_COUNT="${WORKERS_COUNT:-3}"

export INTEGRAL_LEFT=0
export INTEGRAL_RIGHT="${INTEGRAL_RIGHT:-200}"

# Helpers

cd "$COMPOSE_DIR"

get_container() {
    docker compose ps -a --format '{{.Name}}' | grep "$1" | sort | sed -n "${2:-1}p"
}

get_workers() {
    docker compose ps --status running --format '{{.Name}}' 2>/dev/null | grep worker | sort
}

wait_for_workers() {
    local expected="${1:-$WORKERS_COUNT}"
    local timeout=15
    echo "[*] Waiting for $expected workers to start..."
    for i in $(seq 1 "$timeout"); do
        local count
        count=$(get_workers | wc -l)
        if [ "$count" -ge "$expected" ]; then
            echo "[*] $count workers running after ${i}s."
            return 0
        fi
        sleep 1
    done
    local count
    count=$(get_workers | wc -l)
    echo "[!] Only $count/$expected workers running after ${timeout}s."
    return 1
}

start_stack() {
    echo "[*] Starting stack: $WORKERS_COUNT workers, integral [${INTEGRAL_LEFT}, ${INTEGRAL_RIGHT}]"
    docker compose down --timeout 5 2>/dev/null || true
    sleep 1
    docker compose up --build --scale worker="$WORKERS_COUNT" -d 2>&1 | tail -5
    if ! wait_for_workers "$WORKERS_COUNT"; then
        echo "[!] Not all workers started, continuing with what we have"
    fi
}

stop_stack() {
    # Unpause any paused containers first (docker compose down can't stop paused containers)
    for c in $(docker compose ps --format '{{.Name}}' 2>/dev/null); do
        docker unpause "$c" 2>/dev/null || true
    done
    docker compose down --timeout 10 2>/dev/null || true
    sleep 1
}

show_result() {
    local master
    master=$(get_container master)
    if [ -z "$master" ]; then
        echo "  [!] Could not find master container"
        return
    fi
    echo ""
    echo "  ========= MASTER LOGS ========="
    docker logs "$master" 2>&1 | grep -E '(INFO|ERROR|result)' | tail -20
    echo "  ==============================="
    echo ""
}

wait_for_master() {
    local master timeout_sec
    master=$(get_container master)
    timeout_sec="${1:-60}"

    if [ -z "$master" ]; then
        echo "  [!] Could not find master container"
        return 1
    fi

    echo "[*] Waiting for master to finish (max ${timeout_sec}s)..."
    for i in $(seq 1 "$timeout_sec"); do
        local state
        state=$(docker inspect -f '{{.State.Status}}' "$master" 2>/dev/null || echo "unknown")
        if [ "$state" = "exited" ]; then
            local exit_code
            exit_code=$(docker inspect -f '{{.State.ExitCode}}' "$master" 2>/dev/null || echo "?")
            echo "[*] Master finished after ${i}s (exit code: ${exit_code})."
            return 0
        fi
        sleep 1
    done
    echo "[!] Master did not finish within ${timeout_sec}s."
    echo "[!] Last master logs:"
    docker logs --tail 5 "$master" 2>&1 | sed 's/^/    /'
    return 1
}

separator() {
    echo ""
    echo "=========================================="
    echo "$1"
    echo "$2"
    echo "=========================================="
}

# ---------- Scenarios ----------

scenario_1() {
    separator "SCENARIO 1: Latency on one worker (6000ms)" \
              "Delay > TIMEOUT -> tasks reassigned to other workers"
    start_stack

    local target
    target=$(get_workers | head -1)
    echo "[+] Applying 6000ms delay on $target"
    "$CHAOS" delay "$target" 6000

    wait_for_master 60 || true
    show_result
    stop_stack
}

scenario_2() {
    separator "SCENARIO 2: Full disconnect of one worker" \
              "iptables DROP -> tasks requeued to remaining workers"
    start_stack

    local target
    target=$(get_workers | head -1)
    echo "[+] Dropping all traffic on $target"
    "$CHAOS" drop "$target"

    wait_for_master 60 || true
    show_result
    stop_stack
}

scenario_3() {
    separator "SCENARIO 3: Pause/unpause worker (stale response)" \
              "Freeze -> timeout -> unpause -> stale answer rejected (execution_id)"
    start_stack

    local target
    target=$(get_workers | head -1)
    echo "[+] Pausing $target"
    "$CHAOS" pause "$target"

    echo "[+] Waiting 5s for timeouts to trigger..."
    sleep 5

    echo "[+] Unpausing $target (will send stale results)"
    "$CHAOS" unpause "$target"

    wait_for_master 60 || true
    show_result
    stop_stack
}

scenario_4() {
    separator "SCENARIO 4: Packet loss on all workers (30%)" \
              "TCP retransmissions -> slower but should complete"
    start_stack

    for w in $(get_workers); do
        echo "[+] Applying 30% loss on $w"
        "$CHAOS" loss "$w" 30
    done

    wait_for_master 120 || true
    show_result
    stop_stack
}

scenario_5() {
    separator "SCENARIO 5: Disconnect all but one worker" \
              "Single remaining worker must handle all tasks"
    start_stack

    local workers
    workers=($(get_workers))
    local keep="${workers[0]}"
    echo "[+] Keeping only $keep alive, dropping others:"
    for w in "${workers[@]:1}"; do
        echo "    dropping $w"
        "$CHAOS" drop "$w"
    done

    wait_for_master 120 || true
    show_result
    stop_stack
}

scenario_6() {
    separator "SCENARIO 6: Packet duplication (50%)" \
              "TCP dedup at transport level, tests recv_all logic"
    start_stack

    for w in $(get_workers); do
        echo "[+] Applying 50% duplication on $w"
        "$CHAOS" dup "$w" 50
    done

    wait_for_master 60 || true
    show_result
    stop_stack
}

scenario_7() {
    separator "SCENARIO 7: Mixed degradation" \
              "Worker 1: 20% loss + 500ms | Worker 2: paused | Worker 3: normal"
    if [ "$WORKERS_COUNT" -lt 3 ]; then
        WORKERS_COUNT=3
    fi
    start_stack

    local workers
    workers=($(get_workers))
    if [ ${#workers[@]} -lt 3 ]; then
        echo "[!] Only ${#workers[@]} workers running, need 3. Skipping."
        stop_stack
        return
    fi

    echo "[+] ${workers[0]}: 20% loss + 500ms delay"
    "$CHAOS" mixed "${workers[0]}" 20 500
    echo "[+] ${workers[1]}: paused (frozen)"
    "$CHAOS" pause "${workers[1]}"
    echo "[+] ${workers[2]}: normal (no chaos)"

    wait_for_master 120 || true
    show_result
    stop_stack
}

# ---------- Main ----------

echo "Compose dir: $COMPOSE_DIR"
echo "Integral:    [$INTEGRAL_LEFT, $INTEGRAL_RIGHT]"
echo "Workers:     $WORKERS_COUNT"
echo ""

if [ -n "$1" ]; then
    "scenario_$1"
else
    for i in 1 2 3 4 5 6 7; do
        "scenario_$i"
        echo ""
    done
    echo "===== ALL SCENARIOS COMPLETE ====="
fi
