package top.ironbox.cpfix

// STUB: interface only, parsing lands in the next commit (test-first).

sealed class Pdo {
    data class Fixed(val mv: Int, val maxMa: Int) : Pdo()
    data class Pps(val minMv: Int, val maxMv: Int, val maxMa: Int) : Pdo()
    data class Other(val raw: Long) : Pdo()

    fun describe(): String = ""
}

fun decodePdo(text: String): Pdo? = null

data class PatchLine(
    val id: String,
    val target: String,
    val addr: String,
    val expected: String,
    val actual: String,
    val state: String,
    val hits: Long,
) {
    val matches: Boolean get() = false
}

fun parsePatches(text: String): List<PatchLine> = emptyList()
fun parseConf(text: String): Map<String, Int> = emptyMap()
fun renderConf(values: Map<String, Int>): String = ""
fun parseDump(lines: List<String>): Map<String, String> = emptyMap()
fun dumpCommand(paths: List<String>): String = ""

data class Live(
    val batteryMv: Int?,
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
    val batteryMw: Long? get() = null
    val cpIbusTotalMa: Int? get() = null
    val cpInputMw: Long? get() = null
}

fun buildLive(m: Map<String, String>): Live =
    Live(null, null, null, null, null, null, null, null, null, null, null, null, null, emptyList(), null, null)

fun writeParamCommands(p: Param, value: Int, conf: Map<String, Int>): List<String> = emptyList()
