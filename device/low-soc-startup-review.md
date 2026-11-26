# Low-SOC startup failure: evidence and repair constraints

Status: diagnosis of the voltage-ramp mechanism, not a deployed repair.
Reviewed 2026-11-26 against the saved device log and vendor modules.

## Correction to the previous proposal

Commit `207f7c4` added tests assuming that `2 * VBAT + 600 mV` was a universal
adapter-request ceiling. That assumption was not established. The 400/600 mV
values in this device's logs are the **startup window**, not a bound on adapter
voltage while carrying current through the cable. The tests also called a
nonexistent function; Actions run 45770261044 failed at compilation, not at a
behavioral assertion. Their removal is a correction, not proof of a repair.

The November 23 workspace journal already records the non-switching-pump
failure. This review verifies it against instructions and chronology; it is not
a new discovery or evidence that all charging faults have one cause.

## Trace chronology

Source: `../output/cpfix/klog_nofast.txt` relative to the repository parent
workspace (the actual file is `remote/output/cpfix/klog_nofast.txt`). Times are
kernel monotonic seconds. Line numbers below refer to that saved file.

| Time | Observation | Consequence |
| --- | --- | --- |
| 321687.415 | SOC 12; BMS VBAT 3708 mV; CP VBAT 3642 mV; ENTRY to INIT_VBUS | Entry request calculated as 7816 mV |
| 321687.669 | Request 7816 mV / 4500 mA, ret 0 (24540) | Request accepted; not a measurement of current |
| 321687.978 | Both converter registers 0x0A = 0x10 (24547-24548) | ERRORHI set, switching clear, before enabling |
| 321687.993 | CP VBUS 7991 mV, CP VBAT 3225 mV, BMS VBAT 3718 mV (24552-24558) | Software accepts BMS-based window and enters ENABLE_CP |
| 321688.338 | CHG_EN readback 1/1, converter 0x10/0x10, total IBUS 56 mA (24580-24589) | Enters TUNE although neither pump is switching |
| 321688.380 | Request increases to 7916 mV (24608) | Low-current control loop raises voltage |
| 321689.624 | CP VBAT 3272 mV, BMS VBAT now 3250 mV, IBUS 55 mA, final_step 5 | BMS discrepancy closes, but TUNE does not repeat startup validation |
| 321767.035 | Request 10796 mV / 4500 mA, ret 0 (26170); IBUS still 55 mA | About 10.8 V requested without useful pump charging |
| 321768.265 | Converter becomes 0x18 (26176) | ERRORHI plus soft-start-timeout flag; subsequent enable readback clears |
| 321770.088 | After normal EXIT/ENTRY, converter 0x04 (26281) | A later startup really switches; this is a useful positive control |

The initial discrepancy is observed, not a diagnosis of battery impedance or a
malicious firmware change. ENTRY disables the buck power path before enabling
the pumps; BMS and CP readings then diverge transiently. Distinguishing sensor
latency, load-induced sag, and other contributors still needs timestamped
read-status evidence. There is no basis to disable a protection because the two
readings differ.

## Installed-module instruction evidence

Vendor ELF snapshots: `tests/fixtures/vendor/pd_cp_manager.ko` and the workspace
file `output/cpfix/vb/lib_modules_sc8551.ko`. These were inspected with
llvm-objdump; no hash comparison was used.

* `ops_sc8551_get_charge_enable` starts at .text 0x2224. At 0x2250 it selects
  register 0x0C, calls `regmap_read` at 0x2264, shifts its byte right by seven at
  0x2270, and stores the result through the caller's boolean pointer at 0x2274.
  The API reports the enable command. That is not inherently an API defect;
  using it as proof of successful conversion is the problematic integration.
* `sc8551_dump_important_regs` starts at 0x384. It reads register 0x0A at 0x3bc;
  0x3c4 tests the return code, with the read value still at SP+4. This is an
  existing read that a runtime observer can capture without doing I2C in a
  kprobe handler. The function also has other callers, so task/device scoping
  and read-result checking are required.
* `pdm_main_sm` starts at 0x103c. Its two `charger_dev_is_enabled` calls are at
  0x10c0 and 0x10d0. Their outputs land at pdpm+0x226 and +0x228. The pump VBUS
  and VBAT fields are +0x23c and +0x240; BMS VBAT is +0x260.
* At 0x1e54..0x1e7c the INIT_VBUS window comparison multiplies BMS VBAT by the
  mode ratio. It does not consult the converter's ERRORHI/ERRORLO status.
* At 0x1a0c..0x1a30 the ENABLE_CP success branch checks those enable booleans
  and bypass-mode agreement. At 0x1a58 it selects TUNE (state 3). There is no
  converter-switching check in this branch.
* At 0x2210..0x2224 TUNE chooses the minimum of four voltage/current error
  steps. At 0x2294 it adds final_step * 20 mV to the previous request. The
  upper bound at this point is the APDO/DT limit, not the startup window.
* The cycle-count restriction at 0x2234..0x223c normally limits a positive
  step to one on this aged battery. Existing P2 can remove that restriction.
  The log shows +100 mV steps and final_step 5; the filename `nofast` does not
  establish that P2 was disabled during this earlier logged interval.

SC8551 register meanings are corroborated by this driver header:
https://github.com/Rohail33/Realking_xiaomi_xaga/blob/267394110efabd659eda20183a41f386fbb552c0/drivers/power/xm_charge/sc8551.h
0x0A bit 4 = ERRORHI, bit 3 = SS_TIMEOUT, bit 2 = SWITCHING; 0x0C bit 7 = CHG_EN.
This is driver-source corroboration, not a substitute for a manufacturer
datasheet covering reset/clear semantics and analogue thresholds.

## What the existing patches do and do not explain

P2 can accelerate the upward requests. It does not originate the false
ENABLE_CP-to-TUNE transition. P7 follows measured VBUS and adds cable allowance;
in this failing trace VBUS is already above the request, so even the stock
350 mOhm clamp would not prevent the upward sequence. Neither patch validates
that conversion has started. Their effects must remain in the regression matrix.

## Repair contract

1. Keep command-enable and converter-ready as separate facts. Do not change the
   global `is_enabled` API to mean switching for all clients.
2. INIT_VBUS must not accept a numeric window contradicted by a fresh hardware
   input-window error. Correct the startup setpoint using validated current
   measurements and the hardware result, with bounded adjustment and recovery.
3. ENABLE_CP must wait for successful conversion before entering TUNE. A
   pending startup is not yet a charging-current deficit.
4. TUNE must not increase voltage when switching is lost or the sample is
   invalid. Use a bounded recovery through the existing pump-disable / fallback
   path, preserving thermal, overvoltage and overcurrent decisions.
5. Failed PD requests must not be treated as successful setpoints. The later
   PD reset/re-entry failure is a separate part of the repair, not explained by
   this startup trace alone.

Runtime implementation is not yet release-ready. Remaining integration work:
scope fresh converter samples to the correct PDM invocation and pump; check
I2C errors and ADC validity; establish bounded recovery and PD-reset behavior;
verify every binary hook and failure path in Actions; then validate actual
switching/current and fallback on the phone. No new repair has been loaded.
