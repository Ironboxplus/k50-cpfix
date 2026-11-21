# k50-cpfix

Charge-pump fix for the Redmi K50 (rubens, HyperOS OS2.0.10, GKI android12-5.10).

The vendor `pd_cp_manager` / `mtk_charger_framework` modules throttle the 2×SC8551 charge pump by battery cycle count and gate FFC behind Xiaomi's adapter authentication. `cpfix.ko` uses kprobes on verified instructions of the running vendor modules to restore the new-battery behaviour, without touching voltage limits or safety exits. Unloading it restores stock behaviour.

- `kernel/` – the LKM
- `ksu/` – KernelSU module scripts
- `app/` – Compose control app
- `device/` – device facts (GKI symvers, vendor CRCs), see `device/README.md`
- `tests/` – patch-point, CRC and policy tests; `tests/fixtures/vendor/` holds the GPL vendor modules the patch points are verified against
