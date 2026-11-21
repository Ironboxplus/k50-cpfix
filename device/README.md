# Device facts

Target: Redmi K50 (rubens), HyperOS OS2.0.10.0.ULNCNXM, kernel `5.10.209-android12-9-00019-g4ea09a298bb4-ab12292661`.

| File | Source | sha256 |
|---|---|---|
| `vmlinux-12292661.symvers` | GKI build 12292661 (`ci.android.com/builds/submitted/12292661/kernel_aarch64/latest/raw/vmlinux.symvers`), the build id in the running kernel's release string. All 40 vmlinux CRCs imported by the vendor `pd_cp_manager.ko` match it. | e0807cb013c1d114eaee90bab331de8dedca85204ae3bd09a698e7079521eef4 |
| `mtk_charger_framework.symvers` | CRCs of the votable API, read from the `__versions` section of the vendor `pd_cp_manager.ko` (which loads on this kernel, so they match the exporting module). | - |
| `../tests/fixtures/vendor/pd_cp_manager.ko` | `lib/modules/` of the device's `vendor_boot_b` ramdisk; `license=GPL`, `scmversion=gc3a32237c779`. | 729ba1248607b331e25430d47eb1ff9679efcfce4ca7f872c4bafca06dfc43f7 |
| `../tests/fixtures/vendor/mtk_charger_framework.ko` | same ramdisk, same scmversion. | e5e8bb6d46e40ad739f005efa6dd87ae5847b4694a9b69a5b8bcb90666ef5973 |

The kernel has `CONFIG_MODVERSIONS=y`, `CONFIG_MODULE_SIG` unset, `CONFIG_CFI_CLANG=y`, `CONFIG_LTO_CLANG_FULL=y`, `CONFIG_SHADOW_CALL_STACK=y`, `CONFIG_TRIM_UNUSED_KSYMS=y`, `CONFIG_KPROBES=y`, `CONFIG_MODULE_UNLOAD=y`.
With modversions the loader compares vermagic only after the release string, so a module built from another android12-5.10 tree loads if every imported CRC matches.
