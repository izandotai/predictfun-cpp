#!/usr/bin/env sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
build_dir=${PREDICTFUN_BUILD_DIR:-$repo_root/build/release}
runtime_dir=${PREDICTFUN_RUNTIME_DIR:-$repo_root/runtime/btc-liquidity}
if [ -d /mingw64/bin ]; then
  PATH=/mingw64/bin:$PATH
  export PATH
fi
pid_file=$runtime_dir/collector.pid
status=$runtime_dir/status.json
journal=$runtime_dir/btc-5m-15m-liquidity-v2.jsonl
report=$build_dir/predictfun_btc_liquidity_report.exe

status_pid=''
if [ -f "$status" ]; then
  status_pid=$(sed -n 's/.*"pid":\([0-9][0-9]*\).*/\1/p' "$status")
fi
if [ -n "$status_pid" ] && powershell.exe -NoProfile -Command \
    "\$p = Get-Process -Id $status_pid -ErrorAction SilentlyContinue; if (\$p -and \$p.ProcessName -like 'predictfun_btc_liquidity*') { exit 0 } else { exit 1 }"; then
  printf 'collector RUNNING pid=%s\n' "$status_pid"
elif [ -f "$pid_file" ]; then
  printf 'collector NOT RUNNING (stale pid file)\n'
else
  printf 'collector NOT RUNNING\n'
fi

if [ -f "$status" ]; then
  printf '\nSTATUS\n'
  tr -d '\r' < "$status"
fi
if [ -s "$journal" ] && [ -x "$report" ]; then
  printf '\n'
  "$report" "$journal"
else
  printf '\nNo liquidity evidence is available yet.\n'
fi
