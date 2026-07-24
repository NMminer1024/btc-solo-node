#!/bin/sh
#
# RGB panel cycle test for Rockbase.
# The companion binary should be placed next to this script as rgb-cycle.bin.
#

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BIN="${SCRIPT_DIR}/rgb-cycle.bin"
PIDFILE=/tmp/rockbase-rgb-cycle.pid
LOGFILE=/tmp/rockbase-rgb-cycle.log

is_running() {
	[ -f "$PIDFILE" ] || return 1
	pid=$(cat "$PIDFILE" 2>/dev/null)
	[ -n "$pid" ] || return 1
	kill -0 "$pid" 2>/dev/null
}

start_cycle() {
	if [ ! -x "$BIN" ]; then
		echo "missing binary: $BIN"
		return 1
	fi

	if is_running; then
		echo "rgb cycle already running (pid $(cat "$PIDFILE"))"
		return 0
	fi

	"$BIN" >>"$LOGFILE" 2>&1 &
	echo $! >"$PIDFILE"
	echo "rgb cycle started (pid $!)"
}

stop_cycle() {
	if ! is_running; then
		rm -f "$PIDFILE"
		echo "rgb cycle is not running"
		return 0
	fi

	pid=$(cat "$PIDFILE")
	kill "$pid" 2>/dev/null || true
	sleep 1
	kill -9 "$pid" 2>/dev/null || true
	rm -f "$PIDFILE"
	echo "rgb cycle stopped"
}

show_status() {
	if is_running; then
		echo "running (pid $(cat "$PIDFILE"))"
	else
		echo "stopped"
	fi

	if [ -f "$LOGFILE" ]; then
		echo "log: $LOGFILE"
	fi
}

case "${1:-run}" in
run)
	[ -x "$BIN" ] || {
		echo "missing binary: $BIN"
		exit 1
	}
	exec "$BIN"
	;;
start)
	start_cycle
	;;
stop)
	stop_cycle
	;;
restart)
	stop_cycle
	start_cycle
	;;
status)
	show_status
	;;
log)
	if [ -f "$LOGFILE" ]; then
		tail -n 50 "$LOGFILE"
	else
		echo "log file not found: $LOGFILE"
	fi
	;;
*)
	echo "Usage: $0 {run|start|stop|restart|status|log}"
	exit 1
	;;
esac
