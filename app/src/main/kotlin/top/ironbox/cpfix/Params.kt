package top.ironbox.cpfix

/**
 * Module parameters of cpfix.ko. Names, defaults and ranges mirror CPFIX_PARAMS in
 * kernel/cpfix_policy.h (ParamsTest compares them); the rest is presentation.
 */
data class Param(
    val name: String,
    val default: Int,
    val min: Int,
    val max: Int,
    val zeroOk: Boolean,
    val label: String,
    val stock: String,
    val unit: String = "",
    val step: Int = 1,
    /** Shown value = raw / scale (temperatures are in 0.1 °C). */
    val scale: Int = 1,
    /** Text for 0 when zeroOk. */
    val zeroLabel: String? = null,
    val isSwitch: Boolean = false,
) {
    fun clamp(v: Int): Int = if (zeroOk && v <= 0) 0 else v.coerceIn(min, max)

    /** Lowest slider position; one step below min stands for 0 when zeroOk. */
    val sliderMin: Int get() = if (zeroOk) min - step else min

    fun fromSlider(pos: Int): Int = if (zeroOk && pos < min) 0 else clamp(pos)
    fun toSlider(v: Int): Int = if (zeroOk && v == 0) sliderMin else clamp(v)

    fun format(v: Int): String = when {
        isSwitch -> if (v != 0) "开" else "关"
        zeroOk && v == 0 -> zeroLabel ?: "0"
        scale == 1 -> "$v $unit".trim()
        else -> "%.1f %s".format(v.toDouble() / scale, unit).trim()
    }
}

object Params {
    val all: List<Param> = listOf(
        Param("enable", 1, 0, 1, false, "启用修复", "关 = 原厂行为（探针保留，撤销功率上限）", isSwitch = true),
        Param("entry_vbat_margin_mv", 90, 0, 250, false, "进入快充的电压余量", "原厂按循环次数 90/200/250 mV",
            unit = "mV", step = 10),
        Param("entry_high_soc", 0, 50, 95, true, "进入快充的电量上限", "原厂按循环次数 DT 值(88)/80/70 %",
            unit = "%", zeroLabel = "跟随 DT（88 %）"),
        Param("fast_ramp", 1, 0, 1, false, "快速升压", "原厂 ≥800 次循环时步长锁定 20 mV", isSwitch = true),
        Param("taper_reduce_ma", 500, 100, 1500, false, "接近满电时每次降流", "原厂按循环次数 500/1000/1500 mA，隔次执行",
            unit = "mA", step = 100),
        Param("ffc_any_pps", 1, 0, 1, false, "任意 PPS 充电器开 FFC", "原厂需小米适配器认证", isSwitch = true),
        Param("thermal_override", 1, 0, 1, false, "放宽温控限流", "原厂按 mi_thermald 档位限流", isSwitch = true),
        Param("thermal_override_ma", 12400, 2000, 12400, false, "温控限流放宽到", "原厂档位 2000–12400 mA",
            unit = "mA", step = 100),
        Param("batt_temp_hi", 420, 300, 460, false, "电池温度达到此值恢复原厂温控", "旧脚本 42.0 °C",
            unit = "°C", step = 10, scale = 10),
        Param("batt_temp_lo", 410, 300, 460, false, "电池温度降到此值再放宽", "默认 41.0 °C（至少比上限低 1.0 °C）",
            unit = "°C", step = 10, scale = 10),
        Param("fcc_cap_ma", 0, 500, 12400, true, "手动充电电流上限", "原厂无此项",
            unit = "mA", step = 100, zeroLabel = "不限制"),
        Param("clamp_headroom_mv", 400, 0, 800, false, "请求电压余量",
            "原厂 0（请求最多比实测高 ibus × 350 mΩ）；此项在其上再加固定电压",
            unit = "mV", step = 20),
        Param("fv_drop_max_mv", 0, 0, 100, false, "循环次数降压上限", "原厂按循环次数降 100 mV（4.44 → 4.34 V）",
            unit = "mV", step = 10),
        Param("fv_ceiling_mv", 4440, 4000, 4440, false, "充电电压硬上限", "原厂 DT fv / fv_ffc 4440 mV，不允许更高",
            unit = "mV", step = 10),
        Param("hvchg_icl_ma", 2000, 500, 2000, false, "QC 快充输入电流", "原厂 1600 mA（HVCHG，A 口 QC2 9 V）",
            unit = "mA", step = 100),
    )

    val byName: Map<String, Param> = all.associateBy { it.name }
}

/** Below 2500 mA the vendor state machine holds the charge pump and buck charging takes over. */
fun fccCapWarning(capMa: Int): String? =
    if (capMa in 1 until 2500) "电荷泵会退出，改由 buck 充电" else null
