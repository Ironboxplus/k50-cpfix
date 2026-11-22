#!/system/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Load cpfix.ko at boot with the parameters saved in /data/adb/cpfix/cpfix.conf.
MODDIR=${0%/*}
DATA=/data/adb/cpfix
CONF=$DATA/cpfix.conf
LOG=$DATA/cpfix.log
# keys accepted from cpfix.conf: the module parameters, nothing else
KEYS="enable entry_vbat_margin_mv entry_high_soc fast_ramp taper_reduce_ma ffc_any_pps thermal_override thermal_override_ma batt_temp_hi batt_temp_lo fcc_cap_ma clamp_headroom_mv fv_drop_max_mv fv_ceiling_mv hvchg_icl_ma"

# The old /data/adb/service.d/k50_charge_unthrottle.sh keeps running beside cpfix: P6 does not
# reach the buck charging current, which follows the charge_control_limit level. Undo the flag
# with which the 0.2.17 zip stopped it.
rm -f /data/adb/charge_unthrottle.disable

[ -f "$MODDIR/disable" ] && exit 0
[ -f "$MODDIR/remove" ] && exit 0
[ "$(getprop persist.sys.safemode)" != 1 ] || exit 0
[ "$(getprop ro.sys.safemode)" != 1 ] || exit 0

mkdir -p "$DATA"
log() { echo "$(date '+%F %T') $*" >> "$LOG"; }
# The manager shows the description on the module card, so a failed boot must not look like a good one.
set_description() {
  sed -i "s#^description=.*#description=$1#" "$MODDIR/module.prop" 2>/dev/null
}
# keep the log short
[ -f "$LOG" ] && [ "$(wc -l < "$LOG")" -gt 400 ] && tail -n 200 "$LOG" > "$LOG.tmp" && mv -f "$LOG.tmp" "$LOG"

log "service start, $(grep '^version=' "$MODDIR/module.prop")"

if [ -d /sys/module/cpfix ]; then
  log "cpfix already loaded"
  set_description "loaded earlier | $(cat /sys/kernel/cpfix/version 2>/dev/null)"
  exit 0
fi

i=0
while [ ! -d /sys/devices/platform/pd_cp_manager ] && [ $i -lt 120 ]; do
  sleep 1
  i=$((i + 1))
done
if [ ! -d /sys/devices/platform/pd_cp_manager ]; then
  log "pd_cp_manager did not appear in 120 s"
  set_description "failed: pd_cp_manager missing"
  exit 1
fi

ARGS=""
if [ -f "$CONF" ]; then
  while IFS='=' read -r k v; do
    case "$k" in ''|'#'*) continue ;; esac
    case " $KEYS " in *" $k "*) ;; *) log "ignored unknown key '$k'"; continue ;; esac
    case "$v" in ''|*[!0-9]*) log "ignored non-numeric $k='$v'"; continue ;; esac
    ARGS="$ARGS $k=$v"
  done < "$CONF"
fi

log "insmod cpfix.ko$ARGS"
# shellcheck disable=SC2086 # ARGS is a list of KEY=digits words
OUT=$(insmod "$MODDIR/cpfix.ko" $ARGS 2>&1)
rc=$?
[ -n "$OUT" ] && log "$OUT"
dmesg | grep 'cpfix: ' | tail -n 20 >> "$LOG"
if [ "$rc" = 0 ]; then
  n=$(grep -c . /sys/kernel/cpfix/patches 2>/dev/null)
  log "loaded, $n patch points"
  set_description "loaded, $n patch points verified | $(cat /sys/kernel/cpfix/version 2>/dev/null)"
else
  log "insmod failed: rc=$rc"
  set_description "load failed (rc=$rc), stock behaviour; see $LOG"
fi
exit "$rc"
