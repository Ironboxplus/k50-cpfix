#!/system/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Unload cpfix and hand control back to the old unthrottle script. /data/adb/cpfix (settings, log) stays.
[ -d /sys/module/cpfix ] && rmmod cpfix
rm -f /data/adb/charge_unthrottle.disable
