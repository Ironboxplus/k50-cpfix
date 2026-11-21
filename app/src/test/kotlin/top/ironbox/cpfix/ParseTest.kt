package top.ironbox.cpfix

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class ParseTest {
    @Test
    fun ppsPdoOfTheUsersCharger() {
        // pdo5 read from the device: PPS 3.3-11 V 4.5 A
        assertEquals(Pdo.Pps(minMv = 3300, maxMv = 11000, maxMa = 4500), decodePdo("c0dc215a"))
        assertEquals(Pdo.Pps(minMv = 3300, maxMv = 11000, maxMa = 4500), decodePdo("0xC0DC215A\n"))
    }

    @Test
    fun fixedPdo() {
        // 5 V 3 A: voltage 100 x 50 mV in bits 19:10, current 300 x 10 mA in bits 9:0
        val raw = (100 shl 10) or 300
        assertEquals(Pdo.Fixed(mv = 5000, maxMa = 3000), decodePdo(raw.toString(16)))
        // 9 V 2 A with dual-role flags in the high bits
        val raw9 = (1 shl 29) or (180 shl 10) or 200
        assertEquals(Pdo.Fixed(mv = 9000, maxMa = 2000), decodePdo(raw9.toString(16)))
    }

    @Test
    fun emptyAndOtherPdos() {
        assertNull(decodePdo(""))
        assertNull(decodePdo("0"))
        assertNull(decodePdo("00000000"))
        assertNull(decodePdo("zz"))
        // battery supply (type 01)
        assertEquals(Pdo.Other(0x40000000L), decodePdo("40000000"))
        // APDO that is not SPR PPS (EPR AVS, bits 29:28 = 01)
        assertEquals(Pdo.Other(0xd0000000L), decodePdo("d0000000"))
    }

    @Test
    fun pdoDescriptions() {
        assertEquals("PPS 3.3–11.0 V，最大 4.50 A", Pdo.Pps(3300, 11000, 4500).describe())
        assertEquals("固定 9.0 V，最大 2.00 A", Pdo.Fixed(9000, 2000).describe())
    }

    @Test
    fun patchesLines() {
        val text = """
            P1 pd_cp_manager:pdm_check_condition+0x94 ffffffc0091a2980 0xb9424e68 0xb9424e68 active 1234
            P6 mtk_charger_framework:vote+0x0 ffffffc009200000 0xd503233f 0xd503233f verified 0

            garbage line
        """.trimIndent()
        val p = parsePatches(text)
        assertEquals(2, p.size)
        assertEquals(
            PatchLine("P1", "pd_cp_manager:pdm_check_condition+0x94", "ffffffc0091a2980",
                "0xb9424e68", "0xb9424e68", "active", 1234L),
            p[0],
        )
        assertEquals("verified", p[1].state)
        assertEquals(0L, p[1].hits)
        assertTrue(p[0].matches)
        assertTrue(parsePatches("P2 a+0x0 0 0x1 0x2 active 5").single().let { !it.matches })
    }

    @Test
    fun confParseKeepsOnlyKnownNumericKeys() {
        val text = """
            # comment
            enable=1
            fcc_cap_ma=3000
            clamp_headroom_mv = 600
            unknown=5
            fast_ramp=yes
            taper_reduce_ma=-1
            entry_high_soc=
        """.trimIndent()
        assertEquals(mapOf("enable" to 1, "fcc_cap_ma" to 3000, "clamp_headroom_mv" to 600), parseConf(text))
    }

    @Test
    fun confRoundTripInParamOrder() {
        val values = mapOf("fcc_cap_ma" to 2000, "enable" to 0)
        val text = renderConf(values)
        assertEquals(values, parseConf(text))
        val lines = text.lines().filter { it.isNotBlank() && !it.startsWith("#") }
        assertEquals(listOf("enable=0", "fcc_cap_ma=2000"), lines)
    }

    @Test
    fun dumpParsing() {
        val m = parseDump(listOf("/a/b=123 ", "/c=", "/d=x=y", "noequals"))
        assertEquals("123", m["/a/b"])
        assertNull(m["/c"])
        assertEquals("x=y", m["/d"])
        assertEquals(2, m.size)
    }

    @Test
    fun dumpCommandQuotesPaths() {
        val cmd = dumpCommand(listOf("/sys/a", "/sys/b"))
        assertTrue(cmd.contains("'/sys/a' '/sys/b'"))
    }

    @Test
    fun liveFromDump() {
        val m = mapOf(
            "${Paths.BATTERY}/voltage_now" to "4200000",
            "${Paths.BATTERY}/current_now" to "-3000000",
            "${Paths.BATTERY}/temp" to "325",
            "${Paths.BATTERY}/capacity" to "55",
            "${Paths.CP_MASTER}/cp_vbus" to "8400",
            "${Paths.CP_MASTER}/cp_ibus" to "1000",
            "${Paths.CP_SLAVE}/cp_vbus" to "8380",
            "${Paths.CP_SLAVE}/cp_ibus" to "900",
            "${Paths.PDM}/request_vbus" to "8500",
            Paths.pdo(1) to "0001912c",
            Paths.pdo(5) to "c0dc215a",
            "${Paths.CPFIX}/thermal_active" to "1",
        )
        val live = buildLive(m)
        assertEquals(4200, live.batteryMv)
        assertEquals(3000, live.batteryChargeMa)        // positive while charging
        assertEquals(12600L, live.batteryMw)
        assertEquals(325, live.batteryTemp)
        assertEquals(55, live.soc)
        assertEquals(1900, live.cpIbusTotalMa)
        assertEquals(8400L * 1000 / 1000 + 8380L * 900 / 1000, live.cpInputMw)
        assertEquals(8500, live.requestVbusMv)
        assertNull(live.requestIbusMa)
        assertEquals(listOf(1, 5), live.pdos.map { it.first })
        assertEquals(Pdo.Fixed(5000, 3000), live.pdos[0].second)
        assertEquals(1, live.thermalActive)
        assertNull(live.fccEffectiveMa)
    }

    @Test
    fun paramWriteCommands() {
        val p = Params.byName.getValue("fcc_cap_ma")
        val cmds = writeParamCommands(p, 2000, mapOf("enable" to 1))
        assertEquals("echo 2000 > ${Paths.CPFIX_PARAMS}/fcc_cap_ma", cmds[0])
        assertTrue(cmds.any { it.contains("fcc_cap_ma=2000") && it.contains("enable=1") })
        assertTrue(cmds.last().startsWith("mv -f "))
    }
}
