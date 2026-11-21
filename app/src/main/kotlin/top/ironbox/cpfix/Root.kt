package top.ironbox.cpfix

import com.topjohnwu.superuser.Shell
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/** Root shell access through libsu; every call runs off the main thread. */
object Root {
    suspend fun available(): Boolean = withContext(Dispatchers.IO) { Shell.getShell().isRoot }

    private suspend fun run(vararg cmds: String): Shell.Result =
        withContext(Dispatchers.IO) { Shell.cmd(*cmds).exec() }

    suspend fun dump(paths: List<String>): Map<String, String> = parseDump(run(dumpCommand(paths)).out)

    suspend fun read(path: String): String = run("cat '$path' 2>/dev/null").out.joinToString("\n")

    suspend fun tail(path: String, lines: Int): List<String> = run("tail -n $lines '$path' 2>/dev/null").out

    suspend fun moduleLoaded(): Boolean = run("[ -d ${Paths.CPFIX_MODULE} ]").isSuccess

    suspend fun readParams(): Map<String, Int> =
        dump(Params.all.map { Paths.param(it.name) })
            .mapNotNull { (path, v) -> v.toIntOrNull()?.let { path.substringAfterLast('/') to it } }
            .toMap()

    suspend fun readConf(): Map<String, Int> = parseConf(read(Paths.CONF))

    /** Apply one parameter live and persist it. True when the conf file was written. */
    suspend fun writeParam(p: Param, value: Int): Boolean {
        val cmds = writeParamCommands(p, value, readConf())
        run(cmds.first())                       // live value; fails when cpfix is not loaded
        return run(*cmds.drop(1).toTypedArray()).isSuccess
    }
}
