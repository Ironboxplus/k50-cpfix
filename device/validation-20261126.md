# 2026-11-26 runtime and CI checkpoint

This is a development checkpoint, not final repair acceptance.

## Code and CI

* `d678ed8`: Actions 45775146859 passed all jobs, including 282 policy checks,
  163 startup checks, 22 patch points, their offset negative control, the startup
  instruction-context contract, app/scripts and packaging.
* `1a60d74`: Actions 45777616794 passed all jobs. It publishes only completed
  observation snapshots, reports uninitialised age as -1, labels disabled
  startup hooks as `observe`, and prevents a bypass KEEP decision from counting
  as healthy 2:1 operation. This commit is not loaded on the phone yet.
* Baseline behavioral regression: 45773849582 compiled successfully and failed
  assertions on false startup and unbounded unloaded ramp. This is distinct
  from the discarded 207f7c4 test, which failed to compile.

## Phone configuration and reversible trial

SSH profile `k50` currently resolves to 10.10.10.2; ADB is also reachable at
10.10.10.2:5555. Verified rubens, OS2.0.10.0.ULNCNXM, kernel
5.10.209-android12-9-00019-g4ea09a298bb4-ab12292661.

Before observation, the loaded module was 0.2.19-c7d00f3928da. The prior
parameters were saved in `/data/local/tmp/cpfix-before-d678ed8.args`; the old
installed module was copied to `/data/local/tmp/cpfix-rollback-c7d00f3.ko`.
The persistent KSU installation was not replaced.

At kernel time 8995, 0.2.24-d678ed843d99 was temporarily loaded with all previous
parameters and `startup_guard=0`. All 22 kprobes registered and verified their
saved instructions. `enable=1` preserves the pre-existing cpfix features;
`startup_guard=0` means the new controller has not been enabled. Its recovery
count remained zero. No firmware partition was written.

To restore the prior runtime module, construct arguments from the saved file,
unload the observer, and load the saved rollback module with those arguments.
The original KSU module also remains the next-boot installation. Do not pass
the new startup_guard parameter to the old module.

After completing the bounded observation, the temporary module was unloaded
and 0.2.19-c7d00f3928da was restored using every saved parameter. Version readback
confirmed restoration. The phone is no longer running the observation candidate;
the new guard was never enabled during this trial.

## QC observations

The user confirmed a USB-A/QC source. Before and after the observer load, the
vendor log showed HVCHG with AICR=2000mA, MIVR=8000mV and thermally reduced
CC=500/1500mA. Later the source was online at about 8.887V with CC=1000mA.
These are programmed limits and sampled voltage, not proof of maximum input
power. Battery temperature was initially 42.2C and later 40.9C; thermal override
was inactive during the cooldown hysteresis. This is not cool-battery QC
full-power acceptance.

## 135W source observation and exact startup trigger

The user then connected a 135W charger. Do not infer its negotiated power from
the nameplate. PDM reported APDO current capability 5000mA.

The sampling chain was observed in real calls: after 114 PDM passes, S1-S4/S6
each had 114 hits and S5 had 228 (one converter read per pump per pass).
Completed snapshots had valid=0x3f. Later snapshots showed both converters
switching (0x04/0x04), command bits enabled=3 and no proposed recovery.

The initial long wait was **FCC entry gating**, not demonstrated PD negotiation
failure. The USB_DCP field alone was misleading: during the wait, successful
type=3 requests for 5000mV/3000mA were already logged, e.g. at 9433.559823.

At 9503.513481, battery temperature was 40.0C, target_fcc=1000mA and
thermal_limit_fcc=1000mA; bypass_enable=1 and the entry floor was 1600mA, so PDM
remained in HOLD. At 9503.964484, target_fcc became 9920mA and thermal_limit_fcc
became 12400mA. Bypass was switched off and the state sequence was:

* 9503.964543 ENTRY -> INIT_VBUS
* 9504.668440 INIT_VBUS -> ENABLE_CP
* 9505.016567 ENABLE_CP -> TUNE

The actual state transition took about 1.05 seconds after the gate cleared.
The existing 42/40C hysteresis accounts for the cooldown wait; the new guard
was disabled throughout. At TUNE entry, measured pump current total was
1160mA. Later battery current reached -4738mA in one read (negative is charging).
In the saved successful interval from 9505 onward, all 160 converter register
observations were 0x04. None of this demonstrates low-SOC repair acceptance.

At 9760 the kernel logged unplug/type Unknown and TUNE -> ENTRY; at 9768 another
plug-in occurred, followed by HVCHG at 9771. Current source protocol is therefore
QC again. The last startup snapshot subsequently aged beyond 120 seconds: it
must not be presented as a current running pump state. Live pump readings at
that point were VBUS=7mV and IBUS=3mA per pump.

## Saved evidence (workspace parent)

* `output/cpfix/cpfix-observe-d678ed8-klog.txt`: QC and module registration.
* `output/cpfix/cpfix-observer-d678ed8-samples.txt`: 30 bounded observations,
  including the cooldown-to-TUNE transition. Collector completed normally.
* `output/cpfix/cpfix-observe-135w-klog.txt`: detailed entry and healthy TUNE.

## Still outstanding

* Validate the active repair, including corrected startup voltage and bounded
  recovery, on the phone; so far only observation mode has been exercised.
* Reproduce or observe the actual low-SOC failure conditions without disabling
  protections, falsifying the fuel gauge, or forcibly draining the phone.
* Confirm actual buck charging after failure recovery and the separate PD-reset
  re-entry path. State transition alone is insufficient.
* Complete cool-battery QC power acceptance and assess remaining QC/P9 work
  against the existing user-approved limits; CC=2500mA remains accepted.
* Do not promote the development implementation to main or persistent install
  on the strength of these observation-mode results.
