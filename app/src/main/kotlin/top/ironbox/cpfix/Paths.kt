package top.ironbox.cpfix

/** Every path the app reads or writes. */
object Paths {
    const val BATTERY = "/sys/class/power_supply/battery"
    const val CP_MASTER = "/sys/class/power_supply/cp_master"
    const val CP_SLAVE = "/sys/class/power_supply/cp_slave"
    const val PDM = "/sys/devices/platform/pd_cp_manager"
    const val ADAPTER = "/sys/class/Charging_Adapter/pd_adapter"
    const val CPFIX = "/sys/kernel/cpfix"
    const val CPFIX_MODULE = "/sys/module/cpfix"
    const val CPFIX_PARAMS = "$CPFIX_MODULE/parameters"
    const val DATA_DIR = "/data/adb/cpfix"
    const val CONF = "$DATA_DIR/cpfix.conf"
    const val LOG = "$DATA_DIR/cpfix.log"

    const val PDO_COUNT = 7

    fun pdo(n: Int) = "$ADAPTER/pdo$n"
    fun param(name: String) = "$CPFIX_PARAMS/$name"

    /** Files polled by the live page. */
    val live: List<String> = buildList {
        listOf("voltage_now", "current_now", "temp", "capacity",
            "constant_charge_voltage", "constant_charge_current", "charge_control_limit")
            .forEach { add("$BATTERY/$it") }
        listOf(CP_MASTER, CP_SLAVE).forEach { add("$it/cp_vbus"); add("$it/cp_ibus") }
        add("$PDM/request_vbus")
        add("$PDM/request_ibus")
        (1..PDO_COUNT).forEach { add(pdo(it)) }
        add("$CPFIX/fcc_effective")
        add("$CPFIX/thermal_active")
        add("$CPFIX/batt_temp")
    }
}
