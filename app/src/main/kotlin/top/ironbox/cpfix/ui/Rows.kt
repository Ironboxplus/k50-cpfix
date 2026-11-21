package top.ironbox.cpfix.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import kotlin.math.roundToInt

/** Shared rows used by every page. */

@Composable
fun Section(title: String, content: @Composable () -> Unit) {
    Card(Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 6.dp)) {
        Column(Modifier.padding(12.dp)) {
            Text(title, style = MaterialTheme.typography.titleMedium)
            content()
        }
    }
}

@Composable
fun ValueRow(label: String, value: String, mono: Boolean = false) {
    Row(
        Modifier.fillMaxWidth().padding(vertical = 3.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(label, style = MaterialTheme.typography.bodyMedium, modifier = Modifier.weight(1f))
        Text(
            value,
            style = MaterialTheme.typography.bodyMedium,
            fontFamily = if (mono) FontFamily.Monospace else null,
        )
    }
}

@Composable
fun SwitchRow(label: String, note: String?, checked: Boolean, enabled: Boolean = true, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth().padding(vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(label, style = MaterialTheme.typography.bodyLarge)
            note?.let { Text(it, style = MaterialTheme.typography.bodySmall) }
        }
        Switch(checked = checked, onCheckedChange = onChange, enabled = enabled)
    }
}

/**
 * Integer slider over [min, max] in [step]s. [format] renders a position (including any
 * special lowest position); [onCommit] fires once when the finger lifts.
 */
@Composable
fun SliderRow(
    label: String,
    note: String?,
    value: Int,
    min: Int,
    max: Int,
    step: Int,
    format: (Int) -> String,
    enabled: Boolean = true,
    warning: (Int) -> String? = { null },
    onCommit: (Int) -> Unit,
) {
    var pos by remember(value) { mutableFloatStateOf(value.toFloat()) }
    val snapped = (min + ((pos - min) / step).roundToInt() * step).coerceIn(min, max)
    Column(Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(label, style = MaterialTheme.typography.bodyLarge, modifier = Modifier.weight(1f))
            Text(format(snapped), style = MaterialTheme.typography.bodyLarge)
        }
        note?.let { Text(it, style = MaterialTheme.typography.bodySmall) }
        Slider(
            value = pos,
            onValueChange = { pos = it },
            onValueChangeFinished = { onCommit(snapped) },
            valueRange = min.toFloat()..max.toFloat(),
            steps = ((max - min) / step - 1).coerceAtLeast(0),
            enabled = enabled,
        )
        Row {
            Text(format(min), style = MaterialTheme.typography.labelSmall, modifier = Modifier.weight(1f))
            Text(format(max), style = MaterialTheme.typography.labelSmall)
        }
        warning(snapped)?.let {
            Text(it, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall)
        }
    }
}
