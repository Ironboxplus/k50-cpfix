package top.ironbox.cpfix.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import top.ironbox.cpfix.Live
import top.ironbox.cpfix.Param
import top.ironbox.cpfix.Params
import top.ironbox.cpfix.PatchLine
import top.ironbox.cpfix.Paths
import top.ironbox.cpfix.Root
import top.ironbox.cpfix.buildLive
import top.ironbox.cpfix.fccCapWarning
import top.ironbox.cpfix.parsePatches

private const val NA = "—"
private fun Int?.or(unit: String) = this?.let { "$it $unit" } ?: NA
private fun Long?.or(unit: String) = this?.let { "$it $unit" } ?: NA
private fun tempText(t: Int?) = t?.let { "%.1f °C".format(it / 10.0) } ?: NA

@Composable
private fun Page(content: @Composable () -> Unit) {
    Column(Modifier.verticalScroll(rememberScrollState()).padding(vertical = 6.dp)) { content() }
}

@Composable
fun LiveScreen() {
    var live by remember { mutableStateOf<Live?>(null) }
    LaunchedEffect(Unit) {
        while (isActive) {
            live = buildLive(Root.dump(Paths.live))
            delay(1000)
        }
    }
    val l = live ?: return Page { Section("实时") { Text("读取中…") } }
    Page {
        Section("电池") {
            ValueRow("电压", l.batteryMv.or("mV"))
            ValueRow("充电电流", l.batteryChargeMa.or("mA"))
            ValueRow("充电功率", l.batteryMw.or("mW"))
            ValueRow("温度", tempText(l.batteryTemp))
            ValueRow("电量", l.soc.or("%"))
        }
        Section("电荷泵（SC8551 ×2）") {
            ValueRow("主泵 VBUS / IBUS", "${l.cpMasterVbusMv.or("mV")} / ${l.cpMasterIbusMa.or("mA")}")
            ValueRow("副泵 VBUS / IBUS", "${l.cpSlaveVbusMv.or("mV")} / ${l.cpSlaveIbusMa.or("mA")}")
            ValueRow("总线电流合计", l.cpIbusTotalMa.or("mA"))
            ValueRow("输入功率合计", l.cpInputMw.or("mW"))
            ValueRow("PPS 请求电压", l.requestVbusMv.or("mV"))
            ValueRow("PPS 请求电流", l.requestIbusMa.or("mA"))
        }
        Section("充电限制") {
            ValueRow("恒压值 constant_charge_voltage", l.ccv.or("mV"))
            ValueRow("恒流值 constant_charge_current", l.ccc.or("mA"))
            ValueRow("温控档位 charge_control_limit", l.chargeControlLimit?.toString() ?: NA)
            ValueRow("FCC 生效值（cpfix）", l.fccEffectiveMa.or("mA"))
            ValueRow("温控放宽生效（cpfix）", l.thermalActive?.let { if (it != 0) "是" else "否" } ?: NA)
        }
        Section("适配器 PDO") {
            if (l.pdos.isEmpty()) Text("未连接 PD 适配器")
            l.pdos.forEach { (n, pdo) -> ValueRow("PDO$n", pdo.describe()) }
        }
    }
}

@Composable
fun PatchesScreen() {
    var loaded by remember { mutableStateOf<Boolean?>(null) }
    var version by remember { mutableStateOf("") }
    var patches by remember { mutableStateOf<List<PatchLine>>(emptyList()) }
    var log by remember { mutableStateOf<List<String>>(emptyList()) }
    LaunchedEffect(Unit) {
        while (isActive) {
            loaded = Root.moduleLoaded()
            version = Root.read("${Paths.CPFIX}/version").trim()
            patches = parsePatches(Root.read("${Paths.CPFIX}/patches"))
            log = Root.tail(Paths.LOG, 50)
            delay(2000)
        }
    }
    Page {
        Section("模块") {
            ValueRow("cpfix.ko", when (loaded) { null -> "读取中…"; true -> "已加载"; false -> "未加载（原厂行为）" })
            if (version.isNotEmpty()) ValueRow("版本", version, mono = true)
        }
        Section("补丁点") {
            if (patches.isEmpty()) Text("无数据")
            patches.forEach { p ->
                ValueRow("${p.id} ${p.target}", "${p.state}，命中 ${p.hits} 次", mono = true)
                if (!p.matches) {
                    Text("指令不符：期望 ${p.expected}，实际 ${p.actual}", color = MaterialTheme.colorScheme.error)
                }
            }
        }
        Section("cpfix.log（末尾 50 行）") {
            Text(
                if (log.isEmpty()) "无日志" else log.joinToString("\n"),
                fontFamily = FontFamily.Monospace,
                style = MaterialTheme.typography.bodySmall,
            )
        }
    }
}

@Composable
fun SettingsScreen() {
    val scope = rememberCoroutineScope()
    var values by remember { mutableStateOf<Map<String, Int>>(emptyMap()) }
    var loaded by remember { mutableStateOf(false) }
    var status by remember { mutableStateOf("") }
    LaunchedEffect(Unit) {
        loaded = Root.moduleLoaded()
        // live values when the module runs, otherwise the saved file, otherwise defaults
        val saved = Root.readConf()
        val live = if (loaded) Root.readParams() else emptyMap()
        values = Params.all.associate { it.name to (live[it.name] ?: saved[it.name] ?: it.default) }
    }
    fun commit(p: Param, v: Int) {
        values = values + (p.name to p.clamp(v))
        scope.launch {
            val ok = Root.writeParam(p, v)
            status = if (ok) {
                if (loaded) "${p.label}：已生效并保存" else "${p.label}：已保存，下次加载 cpfix 时生效"
            } else {
                "${p.label}：保存失败（没有 root？）"
            }
        }
    }
    Page {
        Section("参数") {
            Text(
                if (loaded) "修改立即写入 ${Paths.CPFIX_PARAMS}，并保存到 ${Paths.CONF}"
                else "cpfix 未加载：修改只保存到 ${Paths.CONF}",
                style = MaterialTheme.typography.bodySmall,
            )
            if (status.isNotEmpty()) Text(status, style = MaterialTheme.typography.bodySmall)
            if (values.isEmpty()) Text("读取中…")
        }
        if (values.isNotEmpty()) {
            Section("调整") {
                Params.all.forEach { p ->
                    val v = values[p.name] ?: p.default
                    val note = "范围 ${p.format(p.min)}–${p.format(p.max)}" +
                        (if (p.zeroOk) "，或${p.zeroLabel}" else "") + "；默认 ${p.format(p.default)}；${p.stock}"
                    if (p.isSwitch) {
                        SwitchRow(p.label, p.stock, v != 0) { commit(p, if (it) 1 else 0) }
                    } else {
                        SliderRow(
                            label = p.label,
                            note = note,
                            value = p.toSlider(v),
                            min = p.sliderMin,
                            max = p.max,
                            step = p.step,
                            format = { p.format(p.fromSlider(it)) },
                            warning = if (p.name == "fcc_cap_ma") { pos -> fccCapWarning(p.fromSlider(pos)) } else { _ -> null },
                        ) { commit(p, p.fromSlider(it)) }
                    }
                }
            }
        }
    }
}
