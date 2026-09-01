#!/bin/sh
# /usr/tests/launchctl-snapshot.sh — dump launchd's job table, fenced and
# labelled, so a reviewer can diff the daemon population across a test job.
#
#   usage: launchctl-snapshot.sh <phase-label>
#
# Every on-image test job calls this at its start and at its end. The point is
# the DIFFERENCE: a daemon that was up at BEGIN and is gone (or has a new PID,
# i.e. it crashed and KeepAlive restarted it) at END was killed by the tests in
# between, and nothing else in the suite would say so. The Mach stress rounds
# in particular are exactly the kind of load that takes a daemon down without
# any single marker failing.
#
# Bounded, never fatal. `launchctl list` talks to PID 1 over Mach, and a wedged
# launchd would leave it in an uninterruptible receive that SIGTERM cannot
# reap -- so `timeout(1)` and `$(...)` are both wrong here. Run it in the
# background with a kill budget and move on regardless of process state; this
# is the same pattern the LAUNCHCTL-LIST check uses, and for the same reason.
#
# This is diagnostic output only. It asserts nothing and emits no OK/FAIL
# marker: daemon-state.sh is what actually gates. Keeping the two separate
# means a snapshot can never turn a lane red on its own.

set -u

PHASE=${1:-unspecified}
BUDGET=${2:-30}
OUT=/tmp/launchctl_snapshot.$$

echo "=== launchctl list ($PHASE) ==="

/bin/launchctl list > "$OUT" 2>&1 &
list_pid=$!

i=0
while [ "$i" -lt "$BUDGET" ] && kill -0 "$list_pid" 2>/dev/null; do
    sleep 1
    i=$((i + 1))
done

if kill -0 "$list_pid" 2>/dev/null; then
    kill -9 "$list_pid" 2>/dev/null || true
    echo "(launchctl list still running after ${BUDGET}s — likely a D-state Mach"
    echo " receive against a wedged PID 1; partial output follows)"
    cat "$OUT" 2>/dev/null || true
else
    wait "$list_pid" 2>/dev/null || echo "(launchctl list exited non-zero)"
    cat "$OUT" 2>/dev/null || true
fi

rm -f "$OUT"
echo "=== end launchctl list ($PHASE) ==="
