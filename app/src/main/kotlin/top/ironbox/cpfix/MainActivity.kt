package top.ironbox.cpfix

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.topjohnwu.superuser.Shell
import top.ironbox.cpfix.ui.LiveScreen
import top.ironbox.cpfix.ui.PatchesScreen
import top.ironbox.cpfix.ui.SettingsScreen

class MainActivity : ComponentActivity() {
    companion object {
        init {
            Shell.setDefaultBuilder(Shell.Builder.create().setTimeout(10))
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            val ctx = LocalContext.current
            val scheme = if (isSystemInDarkTheme()) dynamicDarkColorScheme(ctx) else dynamicLightColorScheme(ctx)
            MaterialTheme(colorScheme = scheme) { App() }
        }
    }
}

private val tabs = listOf("实时", "补丁", "设置")

@Composable
private fun App() {
    var tab by rememberSaveable { mutableIntStateOf(0) }
    var root by remember { mutableStateOf<Boolean?>(null) }
    LaunchedEffect(Unit) { root = Root.available() }
    Scaffold(
        bottomBar = {
            NavigationBar {
                tabs.forEachIndexed { i, name ->
                    NavigationBarItem(
                        selected = tab == i,
                        onClick = { tab = i },
                        icon = { Text(name.take(1), style = MaterialTheme.typography.titleMedium) },
                        label = { Text(name) },
                    )
                }
            }
        },
    ) { pad ->
        Box(Modifier.fillMaxSize().padding(pad)) {
            when (root) {
                null -> Text("正在请求 root 权限…", Modifier.padding(16.dp))
                false -> Text("没有 root 权限：请在 KernelSU 管理器里授予本应用超级用户权限后重开。", Modifier.padding(16.dp))
                true -> when (tab) {
                    0 -> LiveScreen()
                    1 -> PatchesScreen()
                    else -> SettingsScreen()
                }
            }
        }
    }
}
