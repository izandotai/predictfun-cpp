#!/usr/bin/env sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
build_dir=${PREDICTFUN_BUILD_DIR:-$repo_root/build/release}
runtime_dir=${PREDICTFUN_RUNTIME_DIR:-$repo_root/runtime/btc-liquidity}
env_file=${PREDICTFUN_ENV_FILE:-$repo_root/../polymarket-terminal/.env.local}
interval_ms=${PREDICTFUN_SAMPLE_INTERVAL_MS:-5000}
foreground=0

# Release builds use the MinGW runtime.  Make startup independent of the
# caller's shell profile (login shells do not necessarily include this path).
if [ -d /mingw64/bin ]; then
  PATH=/mingw64/bin:$PATH
  export PATH
fi

usage() {
  printf '%s\n' \
    "usage: $0 [--foreground] [--interval-ms N] [--runtime-dir DIR] [--env-file FILE]" \
    "       collection is read-only; it never links or sends an order"
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --foreground) foreground=1 ;;
    --interval-ms) shift; interval_ms=${1:?missing interval} ;;
    --runtime-dir) shift; runtime_dir=${1:?missing runtime directory} ;;
    --env-file) shift; env_file=${1:?missing env file} ;;
    -h|--help) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
  esac
  shift
done

case "$interval_ms" in
  ''|*[!0-9]*) printf 'invalid interval: %s\n' "$interval_ms" >&2; exit 2 ;;
esac
if [ "$interval_ms" -lt 1000 ]; then
  printf 'interval must be at least 1000 ms\n' >&2
  exit 2
fi

probe_source=$build_dir/predictfun_btc_liquidity_probe.exe
if [ ! -x "$probe_source" ]; then
  printf 'collector binary is missing: %s\nBuild the release preset first.\n' "$probe_source" >&2
  exit 1
fi

if [ -z "${PREDICT_FUN_API_KEY:-}" ] && [ -f "$env_file" ]; then
  key_line=$(grep -m 1 '^PREDICT_FUN_API_KEY=' "$env_file" || true)
  if [ -n "$key_line" ]; then
    PREDICT_FUN_API_KEY=${key_line#PREDICT_FUN_API_KEY=}
    PREDICT_FUN_API_KEY=$(printf '%s' "$PREDICT_FUN_API_KEY" | tr -d '\r')
    case "$PREDICT_FUN_API_KEY" in
      \"*\") PREDICT_FUN_API_KEY=${PREDICT_FUN_API_KEY#\"}; PREDICT_FUN_API_KEY=${PREDICT_FUN_API_KEY%\"} ;;
      \'*\') PREDICT_FUN_API_KEY=${PREDICT_FUN_API_KEY#\'}; PREDICT_FUN_API_KEY=${PREDICT_FUN_API_KEY%\'} ;;
    esac
    export PREDICT_FUN_API_KEY
  fi
fi
if [ -z "${PREDICT_FUN_API_KEY:-}" ]; then
  printf 'PREDICT_FUN_API_KEY is not set and was not found in %s\n' "$env_file" >&2
  exit 1
fi

mkdir -p "$runtime_dir"
probe=$runtime_dir/predictfun_btc_liquidity_probe-live.exe
journal=$runtime_dir/btc-5m-15m-liquidity-v2.jsonl
status=$runtime_dir/status.json
stdout_log=$runtime_dir/collector.stdout.log
stderr_log=$runtime_dir/collector.stderr.log
pid_file=$runtime_dir/collector.pid

if [ -f "$status" ]; then
  old_pid=$(sed -n 's/.*"pid":\([0-9][0-9]*\).*/\1/p' "$status")
  if [ -n "$old_pid" ] && powershell.exe -NoProfile -Command \
      "\$p = Get-Process -Id $old_pid -ErrorAction SilentlyContinue; if (\$p -and \$p.ProcessName -like 'predictfun_btc_liquidity*') { exit 0 } else { exit 1 }"; then
    printf 'collector already running pid=%s\nstatus=%s\njournal=%s\n' "$old_pid" "$status" "$journal"
    exit 0
  fi
fi
if [ -f "$pid_file" ]; then
  rm -f "$pid_file"
fi

# Windows locks a running executable.  Run an immutable runtime snapshot so
# the source build remains linkable while long-lived collection continues.
cp -f "$probe_source" "$probe"
chmod +x "$probe"

run_probe() {
  exec "$probe" --both --samples 0 --interval-ms "$interval_ms" \
    --jsonl "$journal" --status-json "$status" --quiet
}

if [ "$foreground" -eq 1 ]; then
  printf 'Predict.fun BTC 5m/15m read-only collector\njournal=%s\nstatus=%s\nPress Ctrl-C to stop.\n' "$journal" "$status"
  run_probe
fi

run_probe >"$stdout_log" 2>"$stderr_log" &
collector_pid=$!
printf '%s\n' "$collector_pid" > "$pid_file"
sleep 2
native_pid=''
if [ -f "$status" ]; then
  native_pid=$(sed -n 's/.*"pid":\([0-9][0-9]*\).*/\1/p' "$status")
fi
if [ -n "$native_pid" ]; then
  printf '%s\n' "$native_pid" > "$pid_file"
fi
if [ -z "$native_pid" ] || ! powershell.exe -NoProfile -Command \
    "\$p = Get-Process -Id $native_pid -ErrorAction SilentlyContinue; if (\$p -and \$p.ProcessName -like 'predictfun_btc_liquidity*') { exit 0 } else { exit 1 }"; then
  rm -f "$pid_file"
  printf 'collector failed to start:\n' >&2
  tail -n 20 "$stderr_log" >&2 || true
  exit 1
fi

printf 'Predict.fun BTC liquidity collector started\npid=%s\njournal=%s\nstatus=%s\nlogs=%s\n' \
  "$native_pid" "$journal" "$status" "$runtime_dir"
