#!/bin/sh
set -e

APP_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
PIDFILE="$APP_DIR/data/didimdwiki.test.pid"
LOGFILE="$APP_DIR/data/didimdwiki.test.log"
BIN="$APP_DIR/didimdwiki"
LOGIN_REQUIRED=""

stop() {
	if [ -f "$PIDFILE" ]; then
		pid=$(cat "$PIDFILE")
		kill "$pid" 2>/dev/null || true
		i=0
		while kill -0 "$pid" 2>/dev/null && [ $i -lt 50 ]; do
			i=$((i + 1))
			sleep 0.1
		done
		rm -f "$PIDFILE"
	fi
}

start() {
	cd "$APP_DIR"
	# Must match store_verify_dirs() in src/models/store.c: it checks these
	# exist but deliberately never creates them (that's systemd's or, here,
	# our job).
	mkdir -p data/pages/.rev data/uploads/.meta data/templates data/tmp
	"$BIN" ${LOGIN_REQUIRED:+login_required=1} > "$LOGFILE" 2>&1 &
	echo $! > "$PIDFILE"

	i=0
	while [ $i -lt 50 ]; do
		if grep -q "listening on" "$LOGFILE" 2>/dev/null; then
			return 0
		fi
		if grep -qE "fatal|Address already in use|EADDRINUSE" "$LOGFILE" 2>/dev/null; then
			cat "$LOGFILE" >&2
			return 1
		fi
		i=$((i + 1))
		sleep 0.1
	done

	echo "server did not come up in time" >&2
	cat "$LOGFILE" >&2
	return 1
}

CMD="${1:-}"
shift || true

for arg in "$@"; do
	case "$arg" in
	--fresh)          FRESH=1 ;;
	--login-required) LOGIN_REQUIRED=1 ;;
	esac
done

case "$CMD" in
start)
	[ "${FRESH:-0}" = "1" ] && rm -rf "$APP_DIR/data"
	start
	;;
stop)
	stop
	;;
restart)
	stop
	[ "${FRESH:-0}" = "1" ] && rm -rf "$APP_DIR/data"
	start
	;;
*)
	echo "usage: $0 start|stop|restart [--fresh] [--login-required]" >&2
	exit 1
	;;
esac
