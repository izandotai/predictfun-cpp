#!/usr/bin/env sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
runtime_dir=${PREDICTFUN_RUNTIME_DIR:-$repo_root/runtime/btc-liquidity}
pid_file=$runtime_dir/collector.pid
status_file=$runtime_dir/status.json

pid=''
if [ -f "$status_file" ]; then
  pid=$(sed -n 's/.*"pid":\([0-9][0-9]*\).*/\1/p' "$status_file")
fi
if [ -z "$pid" ] && [ -f "$pid_file" ]; then
  pid=$(tr -cd '0-9' < "$pid_file")
fi
if [ -z "$pid" ]; then
  printf 'collector is not running\n'
  exit 0
fi
if ! powershell.exe -NoProfile -Command \
    "\$p = Get-Process -Id $pid -ErrorAction SilentlyContinue; if (\$p -and \$p.ProcessName -like 'predictfun_btc_liquidity*') { exit 0 } else { exit 1 }"; then
  rm -f "$pid_file"
  printf 'collector was not running; stale pid file removed\n'
  exit 0
fi

MSYS2_ARG_CONV_EXCL='*' taskkill.exe /PID "$pid" /F >/dev/null 2>&1
count=0
while powershell.exe -NoProfile -Command \
  "\$p = Get-Process -Id $pid -ErrorAction SilentlyContinue; if (\$p -and \$p.ProcessName -like 'predictfun_btc_liquidity*') { exit 0 } else { exit 1 }" && [ "$count" -lt 50 ]; do
  sleep 0.1
  count=$((count + 1))
done
if powershell.exe -NoProfile -Command \
    "\$p = Get-Process -Id $pid -ErrorAction SilentlyContinue; if (\$p -and \$p.ProcessName -like 'predictfun_btc_liquidity*') { exit 0 } else { exit 1 }"; then
  printf 'collector did not stop within 5 seconds (pid=%s)\n' "$pid" >&2
  exit 1
fi
rm -f "$pid_file"
if [ -f "$status_file" ]; then
  temporary=$status_file.tmp
  sed 's/"state":"[^"]*"/"state":"stopped"/' "$status_file" > "$temporary"
  mv -f "$temporary" "$status_file"
fi
printf 'collector stopped pid=%s; journal was preserved\n' "$pid"
