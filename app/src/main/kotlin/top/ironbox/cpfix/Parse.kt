package top.ironbox.cpfix

/* Pure parsing and formatting; no Android or shell access, covered by JVM unit tests. */

sealed class Pdo {
    data class Fixed(val mv: Int, val maxMa: Int) : Pdo()
    data class Pps(val minMv: Int, val maxMv: Int, val maxMa: Int) : Pdo()
    data class Other(val raw: Long) : Pdo()

    fun describe(): String = when (this) {
        is Fixed -> "固定 %.1f V，最大 %.2f A".format(mv / 1000.0, maxMa / 1000.0)
        is Pps -> "PPS %.1f–%.1f V，最大 %.2f A".format(minMv / 1000.0, maxMv / 1000.0, maxMa / 1000.0)
        is Other -> "其他类型 0x%08x".format(raw)
    }
}

/** Decode one USB PD power data object as the adapter sysfs prints it (hex, optional 0x). */
fun decodePdo(text: String): Pdo? {
    val raw = text.trim().removePrefix("0x").removePrefix("0X").toLongOrNull(16) ?: return null
    if (raw == 0L || raw > 0xffffffffL) return null
    return when ((raw shr 30) and 3) {
        0L -> Pdo.Fixed(mv = (((raw shr 10) and 0x3ff) * 50).toInt(), maxMa = ((raw and 0x3ff) * 10).toInt())
        3L -> if ((raw shr 28) and 3 == 0L) {
            Pdo.Pps(
                minMv = (((raw shr 8) and 0xff) * 100).toInt(),
                maxMv = (((raw shr 17) and 0xff) * 100).toInt(),
                maxMa = ((raw and 0x7f) * 50).toInt(),
            )
        } else {
            Pdo.Other(raw)
        }
        else -> Pdo.Other(raw)
    }
}

/** One line of /sys/kernel/cpfix/patches: id target addr expected actual state hits */
data class PatchLine(
    val id: String,
    val target: String,
    val addr: String,
    val expected: String,
    val actual: String,
    val state: String,
    val hits: Long,
) {
    val matches: Boolean get() = expected.equals(actual, ignoreCase = true)
}

fun parsePatches(text: String): List<PatchLine> = text.lines().mapNotNull { line ->
    val f = line.trim().split(Regex("\\s+"))
    if (f.size != 7) return@mapNotNull null
    val hits = f[6].toLongOrNull() ?: return@mapNotNull null
    PatchLine(f[0], f[1], f[2], f[3], f[4], f[5], hits)
}

/** KEY=VALUE lines; only known parameter names with non-negative integer values. */
fun parseConf(text: String): Map<String, Int> {
    val out = LinkedHashMap<String, Int>()
    for (line in text.lines()) {
        val s = line.trim()
        if (s.isEmpty() || s.startsWith("#")) continue
        val i = s.indexOf('=')
        if (i <= 0) continue
        val k = s.substring(0, i).trim()
        val v = s.substring(i + 1).trim()
        if (k !in Params.byName || v.isEmpty() || !v.all { it.isDigit() }) continue
        out[k] = v.toIntOrNull() ?: continue
    }
    return out
}

fun renderConf(values: Map<String, Int>): String = buildString {
    append("# cpfix.ko parameters, read by service.sh at boot; written by the cpfix app\n")
    for (p in Params.all) values[p.name]?.let { append("${p.name}=$it\n") }
}

/** "path=value" lines from [dumpCommand]; empty values are dropped. */
fun parseDump(lines: List<String>): Map<String, String> = buildMap {
    for (line in lines) {
        val i = line.indexOf('=')
        if (i <= 0) continue
        val v = line.substring(i + 1).trim()
        if (v.isNotEmpty()) put(line.substring(0, i), v)
    }
}

/** One shell command that prints "path=value" for each path (first line, at most 256 bytes). */
fun dumpCommand(paths: List<String>): String =
    "for f in ${paths.joinToString(" ") { "'$it'" }}; do " +
        "echo \"\$f=\$(head -c 256 \"\$f\" 2>/dev/null | head -n 1)\"; done"

data class Live(
    val batteryMv: Int?,
    /** Positive while charging (current_now is negative then). */
    val batteryChargeMa: Int?,
    val batteryTemp: Int?,
    val soc: Int?,
    val cpMasterVbusMv: Int?,
    val cpMasterIbusMa: Int?,
    val cpSlaveVbusMv: Int?,
    val cpSlaveIbusMa: Int?,
    val requestVbusMv: Int?,
    val requestIbusMa: Int?,
    val ccv: Int?,
    val ccc: Int?,
    val chargeControlLimit: Int?,
    val pdos: List<Pair<Int, Pdo>>,
    val fccEffectiveMa: Int?,
    val thermalActive: Int?,
) {
    val batteryMw: Long? get() = mul(batteryMv, batteryChargeMa)
    val cpIbusTotalMa: Int? get() = sum(cpMasterIbusMa, cpSlaveIbusMa)
    val cpInputMw: Long?
        get() {
            val m = mul(cpMasterVbusMv, cpMasterIbusMa)
            val s = mul(cpSlaveVbusMv, cpSlaveIbusMa)
            return if (m == null && s == null) null else (m ?: 0) + (s ?: 0)
        }

    private fun mul(mv: Int?, ma: Int?): Long? = if (mv == null || ma == null) null else mv.toLong() * ma / 1000
    private fun sum(a: Int?, b: Int?): Int? = if (a == null && b == null) null else (a ?: 0) + (b ?: 0)
}

fun buildLive(m: Map<String, String>): Live {
    fun int(path: String) = m[path]?.trim()?.toIntOrNull()
    fun bat(name: String) = int("${Paths.BATTERY}/$name")
    return Live(
        batteryMv = bat("voltage_now")?.div(1000),
        batteryChargeMa = bat("current_now")?.let { -it / 1000 },
        batteryTemp = bat("temp"),
        soc = bat("capacity"),
        cpMasterVbusMv = int("${Paths.CP_MASTER}/cp_vbus"),
        cpMasterIbusMa = int("${Paths.CP_MASTER}/cp_ibus"),
        cpSlaveVbusMv = int("${Paths.CP_SLAVE}/cp_vbus"),
        cpSlaveIbusMa = int("${Paths.CP_SLAVE}/cp_ibus"),
        requestVbusMv = int("${Paths.PDM}/request_vbus"),
        requestIbusMa = int("${Paths.PDM}/request_ibus"),
        ccv = bat("constant_charge_voltage"),
        ccc = bat("constant_charge_current"),
        chargeControlLimit = bat("charge_control_limit"),
        pdos = (1..Paths.PDO_COUNT).mapNotNull { n -> m[Paths.pdo(n)]?.let(::decodePdo)?.let { n to it } },
        fccEffectiveMa = int("${Paths.CPFIX}/fcc_effective"),
        thermalActive = int("${Paths.CPFIX}/thermal_active"),
    )
}

/**
 * Shell commands that apply one parameter: write the live sysfs value (fails harmlessly when the
 * module is not loaded) and save cpfix.conf atomically. Values are integers, so nothing needs quoting.
 */
fun writeParamCommands(p: Param, value: Int, conf: Map<String, Int>): List<String> {
    val v = p.clamp(value)
    val text = renderConf(conf + (p.name to v))
    val tmp = "${Paths.CONF}.tmp"
    return listOf(
        "echo $v > ${Paths.param(p.name)}",
        "mkdir -p ${Paths.DATA_DIR}",
        "printf '%s' '${text.replace("'", "")}' > $tmp",
        "mv -f $tmp ${Paths.CONF}",
    )
}
