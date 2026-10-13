package io.github.victorhmrod.localflix.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.State
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import io.github.victorhmrod.localflix.app.AppContainer
import io.github.victorhmrod.localflix.data.LibraryRepository
import io.github.victorhmrod.localflix.ui.theme.LocalFlixColors
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

val LocalContainer = staticCompositionLocalOf<AppContainer> { error("AppContainer not provided") }
val LocalActions = staticCompositionLocalOf<AppActions> { error("AppActions not provided") }

/**
 * Reads from the library on a background thread and again whenever the library changes. The previous value stays on
 * screen while the new one loads, so lists never flash empty.
 */
@Composable
fun <T> libraryState(initial: T, vararg keys: Any?, load: LibraryRepository.() -> T): State<T> {
    val container = LocalContainer.current
    val revision by container.revision.collectAsStateWithLifecycle()
    return produceState(initial, revision, *keys) {
        value = withContext(Dispatchers.IO) { container.library.load() }
    }
}

/** A flat, deterministic tint per title for covers that have no picture. */
fun tintFor(title: String): Color {
    val palette = listOf(
        0xFF3F6BD1, 0xFF8E5BD1, 0xFFD1693F, 0xFF3FA6A0, 0xFFC24E7B, 0xFF5E8F3A, 0xFFB38A1F, 0xFF4C78A8,
    )
    return Color(palette[(title.lowercase().hashCode() and 0x7fffffff) % palette.size].toInt())
}

fun initialsOf(title: String): String {
    val words = title.trim().split(Regex("\\s+")).filter { it.isNotEmpty() }
    return when {
        words.isEmpty() -> "?"
        words.size == 1 -> words[0].take(2).uppercase()
        else -> (words[0].take(1) + words[1].take(1)).uppercase()
    }
}

/** A cover/poster picture, or a flat tile with the title's initials. */
@Composable
fun CoverImage(coverFile: String, title: String, modifier: Modifier = Modifier, cornerRadius: Dp = 10.dp) {
    val container = LocalContainer.current
    val bitmap by produceState<ImageBitmap?>(null, coverFile) {
        value = if (coverFile.isEmpty()) {
            null
        } else {
            withContext(Dispatchers.IO) { container.coverStore.load(coverFile, 480)?.asImageBitmap() }
        }
    }
    val shape = RoundedCornerShape(cornerRadius)
    Box(modifier.clip(shape).background(tintFor(title)), contentAlignment = Alignment.Center) {
        val b = bitmap
        if (b != null) {
            Image(b, contentDescription = null, contentScale = ContentScale.Crop, modifier = Modifier.fillMaxSize())
        } else {
            Text(initialsOf(title), color = Color.White.copy(alpha = 0.85f), fontSize = 28.sp, fontWeight = FontWeight.Bold)
        }
    }
}

@Composable
fun ProgressLine(fraction: Float, modifier: Modifier = Modifier, color: Color = MaterialTheme.colorScheme.primary) {
    Box(modifier.height(3.dp).fillMaxWidth().background(LocalFlixColors.Border, RoundedCornerShape(2.dp))) {
        Box(Modifier.fillMaxSize(fraction.coerceIn(0f, 1f)).background(color, RoundedCornerShape(2.dp)))
    }
}

@Composable
fun SectionTitle(text: String, modifier: Modifier = Modifier, trailing: (@Composable () -> Unit)? = null) {
    Row(modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(text, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold, modifier = Modifier.weight(1f))
        trailing?.invoke()
    }
}

@Composable
fun EmptyState(title: String, body: String, modifier: Modifier = Modifier, actions: @Composable () -> Unit = {}) {
    Column(
        modifier.fillMaxSize().padding(32.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Text(title, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold)
        Text(
            body,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(top = 8.dp, bottom = 20.dp),
        )
        actions()
    }
}

@Composable
fun AppIcon(icon: ImageVector, contentDescription: String?, modifier: Modifier = Modifier, tint: Color = LocalFlixColors.Text, size: Dp = 24.dp) {
    Icon(icon, contentDescription, modifier.size(size), tint = tint)
}

@Composable
fun AppIconButton(icon: ImageVector, contentDescription: String, onClick: () -> Unit, modifier: Modifier = Modifier, tint: Color = LocalFlixColors.Text) {
    IconButton(onClick, modifier) { Icon(icon, contentDescription, tint = tint) }
}

data class MenuEntry(
    val label: String,
    val enabled: Boolean = true,
    val destructive: Boolean = false,
    val dividerBefore: Boolean = false,
    val onClick: () -> Unit,
)

/** The overflow ("more") button with its menu. */
@Composable
fun OverflowMenu(entries: List<MenuEntry>, modifier: Modifier = Modifier, tint: Color = LocalFlixColors.TextMuted) {
    var open by remember { mutableStateOf(false) }
    Box(modifier) {
        IconButton({ open = true }) { Icon(AppIcons.MoreVert, "More", tint = tint) }
        DropdownMenu(open, { open = false }) {
            for (e in entries) {
                if (e.dividerBefore) androidx.compose.material3.HorizontalDivider()
                DropdownMenuItem(
                    text = { Text(e.label, color = if (e.destructive) LocalFlixColors.Danger else Color.Unspecified) },
                    enabled = e.enabled,
                    onClick = {
                        open = false
                        e.onClick()
                    },
                )
            }
        }
    }
}

@Composable
fun ConfirmDialog(
    title: String,
    text: String,
    confirmLabel: String,
    onConfirm: () -> Unit,
    onDismiss: () -> Unit,
    destructive: Boolean = false,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = { Text(text) },
        confirmButton = {
            TextButton(onConfirm) { Text(confirmLabel, color = if (destructive) LocalFlixColors.Danger else MaterialTheme.colorScheme.primary) }
        },
        dismissButton = { TextButton(onDismiss) { Text("Cancel") } },
    )
}

@Composable
fun InfoDialog(title: String, text: String, onDismiss: () -> Unit, buttonLabel: String = "OK") {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = { Text(text) },
        confirmButton = { TextButton(onDismiss) { Text(buttonLabel) } },
    )
}

/** One or two text fields in a dialog (a name, or a name and a number). */
@Composable
fun TextPromptDialog(
    title: String,
    label: String,
    initial: String,
    confirmLabel: String,
    onConfirm: (String) -> Unit,
    onDismiss: () -> Unit,
    singleLine: Boolean = true,
    numeric: Boolean = false,
    allowEmpty: Boolean = false,
) {
    var value by remember { mutableStateOf(initial) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            OutlinedTextField(
                value = value,
                onValueChange = { value = it },
                label = { Text(label) },
                singleLine = singleLine,
                minLines = if (singleLine) 1 else 3,
                keyboardOptions = KeyboardOptions(keyboardType = if (numeric) KeyboardType.Number else KeyboardType.Text),
                modifier = Modifier.fillMaxWidth(),
            )
        },
        confirmButton = {
            TextButton({ onConfirm(value) }, enabled = allowEmpty || value.isNotBlank()) { Text(confirmLabel) }
        },
        dismissButton = { TextButton(onDismiss) { Text("Cancel") } },
    )
}

@Composable
fun TwoFieldDialog(
    title: String,
    firstLabel: String,
    firstInitial: String,
    secondLabel: String,
    secondInitial: String,
    onConfirm: (String, Int) -> Unit,
    onDismiss: () -> Unit,
) {
    var first by remember { mutableStateOf(firstInitial) }
    var second by remember { mutableStateOf(secondInitial) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(first, { first = it }, label = { Text(firstLabel) }, singleLine = true, modifier = Modifier.fillMaxWidth())
                OutlinedTextField(
                    second,
                    { second = it.filter(Char::isDigit).take(4) },
                    label = { Text(secondLabel) },
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        },
        confirmButton = { TextButton({ onConfirm(first, second.toIntOrNull() ?: 0) }, enabled = first.isNotBlank()) { Text("Save") } },
        dismissButton = { TextButton(onDismiss) { Text("Cancel") } },
    )
}

@Composable
fun PrimaryButton(text: String, onClick: () -> Unit, modifier: Modifier = Modifier, icon: ImageVector? = null, enabled: Boolean = true) {
    Button(
        onClick,
        modifier,
        enabled = enabled,
        shape = RoundedCornerShape(10.dp),
        colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.primary),
    ) {
        if (icon != null) {
            Icon(icon, null, Modifier.size(20.dp))
            androidx.compose.foundation.layout.Spacer(Modifier.size(8.dp))
        }
        Text(text, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}

@Composable
fun SecondaryButton(text: String, onClick: () -> Unit, modifier: Modifier = Modifier, icon: ImageVector? = null, enabled: Boolean = true) {
    androidx.compose.material3.OutlinedButton(
        onClick,
        modifier,
        enabled = enabled,
        shape = RoundedCornerShape(10.dp),
        border = androidx.compose.foundation.BorderStroke(1.dp, LocalFlixColors.Border),
    ) {
        if (icon != null) {
            Icon(icon, null, Modifier.size(20.dp))
            androidx.compose.foundation.layout.Spacer(Modifier.size(8.dp))
        }
        Text(text, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}
