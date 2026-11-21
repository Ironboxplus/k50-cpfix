#!/system/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Action button: patch status and the present charging power.
B=/sys/class/power_supply/battery
rd() { cat "$1" 2>/dev/null || echo NA; }
if [ ! -d /sys/module/cpfix ]; then
  echo "cpfix not loaded (stock behaviour)"
  tail -n 10 /data/adb/cpfix/cpfix.log 2>/dev/null
else
  echo "cpfix $(rd /sys/kernel/cpfix/version)"
  echo "id target addr expected actual state hits"
  cat /sys/kernel/cpfix/patches
  echo "fcc_effective=$(rd /sys/kernel/cpfix/fcc_effective) mA thermal_active=$(rd /sys/kernel/cpfix/thermal_active) batt_temp=$(rd /sys/kernel/cpfix/batt_temp) (0.1 C)"
fi
v=$(rd $B/voltage_now)
i=$(rd $B/current_now)
case "$v$i" in
  *NA*) echo "battery: unreadable" ;;
  # current_now is negative while charging
  *) echo "battery: $((v / 1000)) mV, $((-i / 1000)) mA charging, $(( (v / 1000) * (-i / 1000) / 1000 )) mW, $(rd $B/temp) (0.1 C), $(rd $B/capacity) %" ;;
esac
