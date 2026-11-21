#!/system/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Install-time checks: cpfix.ko patches fixed offsets of one vendor build, so refuse any other.
# Sourced by the manager's installer, which provides ui_print, abort, set_perm and MODPATH.
SCM=gc3a32237c779
[ "$KSU" = true ] || abort "- KernelSU required."
[ "$ARCH" = arm64 ] || abort "- arm64 only."
DEV=$(getprop ro.product.device)
[ "$DEV" = rubens ] || abort "- device is '$DEV', this module is for rubens (Redmi K50) only."
REL=$(uname -r)
case "$REL" in
  5.10.209-android12-9*) ;;
  *) abort "- kernel '$REL' is not 5.10.209-android12-9*." ;;
esac
for m in pd_cp_manager mtk_charger_framework; do
  s=$(cat "/sys/module/$m/scmversion" 2>/dev/null)
  [ "$s" = "$SCM" ] || abort "- $m scmversion is '${s:-missing}', expected $SCM."
done
[ -s "$MODPATH/cpfix.ko" ] || abort "- cpfix.ko missing from the zip."
set_perm_recursive "$MODPATH" 0 0 0755 0644
for f in service.sh uninstall.sh action.sh; do
  set_perm "$MODPATH/$f" 0 0 0755
done
touch "$MODPATH/skip_mount"
mkdir -p /data/adb/cpfix
ui_print "- rubens, kernel $REL, vendor modules $SCM: OK"
ui_print "- cpfix.ko loads at boot (service.sh); settings in /data/adb/cpfix/cpfix.conf"
ui_print "- log: /data/adb/cpfix/cpfix.log"
