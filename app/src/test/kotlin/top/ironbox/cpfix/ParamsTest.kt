package top.ironbox.cpfix

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ParamsTest {
    private data class CParam(val name: String, val def: Int, val min: Int, val max: Int, val zeroOk: Boolean)

    /** CPFIX_PARAMS from the kernel header, the source of truth for names and ranges. */
    private fun kernelParams(): List<CParam> {
        val header = File("../kernel/cpfix_policy.h")
        assertTrue("missing ${header.absolutePath}", header.isFile)
        val re = Regex("""^\s*X\((\w+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*([01])\)""")
        return header.readLines().mapNotNull { re.find(it) }.map {
            val (n, d, lo, hi, z) = it.destructured
            CParam(n, d.toInt(), lo.toInt(), hi.toInt(), z == "1")
        }
    }

    @Test
    fun matchesKernelHeader() {
        val c = kernelParams()
        assertEquals(14, c.size)
        assertEquals(c, Params.all.map { CParam(it.name, it.default, it.min, it.max, it.zeroOk) })
    }

    @Test
    fun clampLikeTheKernel() {
        val cap = Params.byName.getValue("fcc_cap_ma")
        assertEquals(0, cap.clamp(-1))
        assertEquals(0, cap.clamp(0))
        assertEquals(500, cap.clamp(1))
        assertEquals(500, cap.clamp(500))
        assertEquals(12400, cap.clamp(12400))
        assertEquals(12400, cap.clamp(12401))
        val head = Params.byName.getValue("clamp_headroom_mv")
        assertEquals(0, head.clamp(-1))
        assertEquals(0, head.clamp(0))
        assertEquals(800, head.clamp(801))
        assertEquals(400, head.default)
    }

    @Test
    fun sliderPositionsCoverRangeAndOff() {
        val soc = Params.byName.getValue("entry_high_soc")
        assertTrue(soc.sliderMin < soc.min)
        assertEquals(0, soc.fromSlider(soc.sliderMin))     // the extra position below min is "off"
        assertEquals(soc.sliderMin, soc.toSlider(0))
        assertEquals(50, soc.fromSlider(50))
        assertEquals(88, soc.toSlider(88))
        val margin = Params.byName.getValue("entry_vbat_margin_mv")
        assertEquals(margin.min, margin.sliderMin)
        Params.all.filter { !it.isSwitch }.forEach {
            assertEquals("${it.name} step must divide the range", 0, (it.max - it.sliderMin) % it.step)
        }
    }

    @Test
    fun lowFccCapWarns() {
        val cap = Params.byName.getValue("fcc_cap_ma")
        assertTrue(fccCapWarning(2400) != null)
        assertEquals(null, fccCapWarning(2500))
        assertEquals(null, fccCapWarning(0))
        assertEquals("fcc_cap_ma", cap.name)
    }
}
