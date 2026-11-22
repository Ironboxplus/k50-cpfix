#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# The old /data/adb/service.d/k50_charge_unthrottle.sh keeps running beside cpfix: P6 does not
# cover the buck path (info+0xa3c comes from the charge_control_limit level), so nothing may
# disable it. service.sh only removes a flag left by the 0.2.17 zip, once, before insmod;
# uninstall.sh removes it as well. No script creates it.
#   tests/ksu_flag_test.sh [service.sh] [uninstall.sh]
SERVICE=${1:-ksu/service.sh}
UNINSTALL=${2:-ksu/uninstall.sh}
FLAG=/data/adb/charge_unthrottle.disable
passed=0
failed=0

ok() { passed=$((passed + 1)); echo "ok   $1"; }
fail() { failed=$((failed + 1)); echo "FAIL $1"; }
# the script without comment lines
code() { grep -v '^[[:space:]]*#' "$1"; }

if code "$SERVICE" | grep -q 'OLD_FLAG'; then fail "$SERVICE: OLD_FLAG still used"; else ok "$SERVICE: no OLD_FLAG"; fi
if code "$SERVICE" | grep -q 'touch'; then fail "$SERVICE: touches a file"; else ok "$SERVICE: no touch"; fi
n=$(code "$SERVICE" | grep -c 'charge_unthrottle')
r=$(code "$SERVICE" | grep -cxF "rm -f $FLAG")
if [ "$n" = 1 ] && [ "$r" = 1 ]; then
  ok "$SERVICE: the flag appears once, as rm -f $FLAG"
else
  fail "$SERVICE: $n line(s) mention charge_unthrottle, $r of them exactly 'rm -f $FLAG'"
fi
rm_line=$(grep -nxF "rm -f $FLAG" "$SERVICE" | head -n1 | cut -d: -f1)
ins_line=$(grep -n 'insmod ' "$SERVICE" | grep -v '^[0-9]*:[[:space:]]*#' | head -n1 | cut -d: -f1)
if [ -n "$rm_line" ] && [ -n "$ins_line" ] && [ "$rm_line" -lt "$ins_line" ]; then
  ok "$SERVICE: rm at line $rm_line, before insmod at line $ins_line"
else
  fail "$SERVICE: rm line '${rm_line:-none}' not before insmod line '${ins_line:-none}'"
fi
if code "$UNINSTALL" | grep -qxF "rm -f $FLAG"; then ok "$UNINSTALL: removes the flag"; else fail "$UNINSTALL: does not remove the flag"; fi
if code "$UNINSTALL" | grep -q 'touch'; then fail "$UNINSTALL: touches a file"; else ok "$UNINSTALL: no touch"; fi

echo "passed $passed failed $failed total $((passed + failed))"
[ "$failed" = 0 ]
