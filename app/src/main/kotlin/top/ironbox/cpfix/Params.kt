package top.ironbox.cpfix

// STUB: interface only, the table and clamping land in the next commit (test-first).
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
    val scale: Int = 1,
    val zeroLabel: String? = null,
    val isSwitch: Boolean = false,
) {
    fun clamp(v: Int): Int = v
    val sliderMin: Int get() = min
    fun fromSlider(pos: Int): Int = pos
    fun toSlider(v: Int): Int = v
    fun format(v: Int): String = "$v"
}

object Params {
    val all: List<Param> = emptyList()
    val byName: Map<String, Param> = all.associateBy { it.name }
}

fun fccCapWarning(capMa: Int): String? = null
