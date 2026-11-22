#!/system/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Unload cpfix. /data/adb/cpfix (settings, log) stays; a flag left by the 0.2.17 zip, which
# stopped the old unthrottle script, is removed.
[ -d /sys/module/cpfix ] && rmmod cpfix
rm -f /data/adb/charge_unthrottle.disable
