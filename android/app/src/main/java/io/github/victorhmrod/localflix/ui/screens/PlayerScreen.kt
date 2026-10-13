package io.github.victorhmrod.localflix.ui.screens

import android.content.pm.ActivityInfo
import android.os.SystemClock
import androidx.activity.compose.BackHandler
import androidx.activity.compose.LocalActivity
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableDoubleStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.media3.ui.AspectRatioFrameLayout
import androidx.media3.ui.PlayerView
import io.github.victorhmrod.localflix.core.PlaybackState
import io.github.victorhmrod.localflix.core.PlayerOrientation
import io.github.victorhmrod.localflix.core.TRACK_OFF
import io.github.victorhmrod.localflix.core.TrackInfo
import io.github.victorhmrod.localflix.core.TrackType
import io.github.victorhmrod.localflix.core.formatClock
import io.github.victorhmrod.localflix.ui.AppIcon
import io.github.victorhmrod.localflix.ui.AppIcons
import io.github.victorhmrod.localflix.ui.LocalActions
import io.github.victorhmrod.localflix.ui.LocalContainer
import io.github.victorhmrod.localflix.ui.PrimaryButton
import io.github.victorhmrod.localflix.ui.SecondaryButton
import io.github.victorhmrod.localflix.ui.theme.LocalFlixColors
import kotlinx.coroutines.delay
import java.util.Locale
import kotlin.math.ceil
import kotlin.math.max

private const val SEEK_STEP_MS = 10_000L
private val Speeds = listOf(0.5, 0.75, 1.0, 1.25, 1.5, 2.0)

@Composable
fun PlayerScreen(onClose: () -> Unit) {
    val container = LocalContainer.current
    val actions = LocalActions.current
    val ui = container.playbackUi
    val pc = container.playback
    val view = LocalView.current
    val activity = LocalActivity.current

    var controlsVisible by remember { mutableStateOf(true) }
    var interaction by remember { mutableLongStateOf(0L) }
    var seekDrag by remember { mutableStateOf<Float?>(null) }
    var toast by remember { mutableStateOf<String?>(null) }
    var toastId by remember { mutableLongStateOf(0L) }

    fun showToast(text: String) {
        toast = text
        toastId++
    }

    fun poke() {
        interaction = SystemClock.uptimeMillis()
        controlsVisible = true
    }

    fun close() {
        pc.stop()
        onClose()
    }

    // Immersive full screen and the orientation the user chose, restored on the way out.
    val orientation = container.settings.playerOrientation
    DisposableEffect(orientation) {
        val window = activity?.window
        val controller = window?.let { WindowCompat.getInsetsController(it, view) }
        controller?.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        controller?.hide(WindowInsetsCompat.Type.systemBars())
        activity?.requestedOrientation =
            if (orientation == PlayerOrientation.LANDSCAPE) ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE else ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
        onDispose {
            controller?.show(WindowInsetsCompat.Type.systemBars())
            activity?.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
        }
    }

    BackHandler { close() }

    // Controls fade away while playing; any touch brings them back.
    LaunchedEffect(controlsVisible, interaction, ui.state, seekDrag != null) {
        if (controlsVisible && ui.state == PlaybackState.PLAYING && seekDrag == null) {
            delay(3500)
            controlsVisible = false
        }
    }
    LaunchedEffect(ui.notice) {
        ui.notice?.let {
            showToast(it)
            ui.notice = null
        }
    }
    LaunchedEffect(toastId) {
        if (toast != null) {
            delay(1800)
            toast = null
        }
    }
    // Nothing left to show (stopped from elsewhere, or the last item ended and was dismissed).
    LaunchedEffect(ui.state, ui.finished, ui.endedNext, ui.failure) {
        if (ui.state == PlaybackState.IDLE && !ui.finished && ui.endedNext == null && ui.failure == null && actions.dialog == null) {
            delay(600)
            if (ui.state == PlaybackState.IDLE && actions.dialog == null) onClose()
        }
    }

    Box(Modifier.fillMaxSize().background(Color.Black)) {
        AndroidView(
            factory = { ctx ->
                PlayerView(ctx).apply {
                    useController = false
                    resizeMode = AspectRatioFrameLayout.RESIZE_MODE_FIT
                    setShowBuffering(PlayerView.SHOW_BUFFERING_NEVER)
                    player = container.backend.player
                }
            },
            update = { it.keepScreenOn = ui.state == PlaybackState.PLAYING || ui.state == PlaybackState.LOADING },
            onRelease = { it.player = null },
            modifier = Modifier.fillMaxSize(),
        )

        // Tap shows/hides the controls; double-tap on the left/right half jumps back/forward.
        Box(
            Modifier.fillMaxSize().pointerInput(Unit) {
                detectTapGestures(
                    onTap = { if (controlsVisible) controlsVisible = false else poke() },
                    onDoubleTap = { offset ->
                        if (ui.state == PlaybackState.PLAYING || ui.state == PlaybackState.PAUSED) {
                            val back = offset.x < size.width / 2
                            pc.seekByMs(if (back) -SEEK_STEP_MS else SEEK_STEP_MS)
                            showToast(if (back) "−10 s" else "+10 s")
                        }
                    },
                )
            },
        )

        if (ui.state == PlaybackState.LOADING) {
            CircularProgressIndicator(Modifier.align(Alignment.Center).size(48.dp), color = Color.White)
        }

        AnimatedVisibility(controlsVisible, enter = fadeIn(), exit = fadeOut(), modifier = Modifier.fillMaxSize()) {
            Controls(
                seekDrag = seekDrag,
                onSeekDrag = { seekDrag = it; poke() },
                onSeekDone = { value ->
                    seekDrag = null
                    if (ui.durationMs > 0) pc.seekToMs((value * ui.durationMs).toLong())
                    poke()
                },
                onClose = ::close,
                onInteract = ::poke,
                onToast = ::showToast,
            )
        }

        toast?.let {
            Text(
                it,
                Modifier.align(Alignment.TopCenter).statusBarsPadding().padding(top = 56.dp)
                    .background(Color.Black.copy(alpha = 0.7f), RoundedCornerShape(20.dp)).padding(horizontal = 16.dp, vertical = 8.dp),
                color = Color.White,
            )
        }

        // ------------------------------------------------------------ what happens next
        val next = ui.upNext
        if (next != null) {
            val remaining by produceState(0, ui.upNextAt) {
                while (true) {
                    value = max(0, ceil((ui.upNextAt - SystemClock.uptimeMillis()) / 1000.0).toInt())
                    delay(150)
                }
            }
            CenterCard(Modifier.align(Alignment.BottomEnd).navigationBarsPadding().padding(24.dp)) {
                Text("Next episode in $remaining s", style = MaterialTheme.typography.labelLarge, color = LocalFlixColors.TextMuted)
                Text(next.displayTitle(), fontWeight = FontWeight.SemiBold, maxLines = 2, overflow = TextOverflow.Ellipsis, color = Color.White)
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.padding(top = 8.dp)) {
                    PrimaryButton("Play now", { pc.advanceNow() }, icon = AppIcons.PlayArrow)
                    SecondaryButton("Cancel", { pc.cancelAutoAdvance() })
                }
            }
        }
        val after = ui.endedNext
        if (after != null) {
            CenterCard(Modifier.align(Alignment.Center)) {
                Text("Finished", style = MaterialTheme.typography.labelLarge, color = LocalFlixColors.TextMuted)
                Text("Up next: ${after.displayTitle()}", fontWeight = FontWeight.SemiBold, color = Color.White)
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.padding(top = 8.dp)) {
                    PrimaryButton("Play next", { actions.playQueueEntry(after.mediaFileId, io.github.victorhmrod.localflix.core.StartMode.RESUME) }, icon = AppIcons.PlayArrow)
                    SecondaryButton("Close", ::close)
                }
            }
        }
        if (ui.finished) {
            CenterCard(Modifier.align(Alignment.Center)) {
                Text("That was the last one", fontWeight = FontWeight.SemiBold, color = Color.White)
                Text("${ui.entry.contextTitle.ifEmpty { ui.entry.title }} is finished.", color = LocalFlixColors.TextMuted)
                SecondaryButton("Close", ::close, Modifier.padding(top = 8.dp))
            }
        }
    }
}

@Composable
private fun CenterCard(modifier: Modifier = Modifier, content: @Composable () -> Unit) {
    Column(
        modifier.width(300.dp).clip(RoundedCornerShape(14.dp)).background(Color(0xE61F232C)).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) { content() }
}

@Composable
private fun Controls(
    seekDrag: Float?,
    onSeekDrag: (Float) -> Unit,
    onSeekDone: (Float) -> Unit,
    onClose: () -> Unit,
    onInteract: () -> Unit,
    onToast: (String) -> Unit,
) {
    val container = LocalContainer.current
    val ui = container.playbackUi
    val pc = container.playback
    val playing = ui.state == PlaybackState.PLAYING
    val ready = ui.state == PlaybackState.PLAYING || ui.state == PlaybackState.PAUSED
    val duration = ui.durationMs
    val fraction = seekDrag ?: if (duration > 0) (ui.positionMs.toFloat() / duration).coerceIn(0f, 1f) else 0f
    val shownPosition = if (seekDrag != null && duration > 0) (seekDrag * duration).toLong() else ui.positionMs

    Box(Modifier.fillMaxSize()) {
        // top
        Row(
            Modifier.fillMaxWidth().background(Brush.verticalGradient(listOf(Color.Black.copy(alpha = 0.75f), Color.Transparent)))
                .statusBarsPadding().padding(horizontal = 8.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            PlayerButton(AppIcons.ArrowBack, "Back", onClose)
            Column(Modifier.weight(1f).padding(start = 4.dp)) {
                if (ui.entry.contextTitle.isNotEmpty()) {
                    Text(ui.entry.contextTitle, style = MaterialTheme.typography.labelMedium, color = Color.White.copy(alpha = 0.75f), maxLines = 1, overflow = TextOverflow.Ellipsis)
                }
                Text(ui.entry.title, color = Color.White, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
        }
        // centre
        Row(Modifier.align(Alignment.Center), horizontalArrangement = Arrangement.spacedBy(20.dp), verticalAlignment = Alignment.CenterVertically) {
            PlayerButton(AppIcons.SkipPrevious, "Previous", { pc.previous(); onInteract() }, enabled = ui.hasPrevious, size = 44.dp)
            SeekButton(AppIcons.Replay, "Back 10 seconds", enabled = ready) { pc.seekByMs(-SEEK_STEP_MS); onToast("−10 s"); onInteract() }
            Box(
                Modifier.size(68.dp).clip(CircleShape).background(Color.Black.copy(alpha = 0.55f)).pointerInput(ready) {
                    detectTapGestures(onTap = { if (ready) { pc.togglePause(); onInteract() } })
                },
                contentAlignment = Alignment.Center,
            ) { AppIcon(if (playing) AppIcons.Pause else AppIcons.PlayArrow, if (playing) "Pause" else "Play", tint = Color.White, size = 40.dp) }
            SeekButton(AppIcons.Forward, "Forward 10 seconds", enabled = ready) { pc.seekByMs(SEEK_STEP_MS); onToast("+10 s"); onInteract() }
            PlayerButton(AppIcons.SkipNext, "Next", { pc.next(); onInteract() }, enabled = ui.hasNext, size = 44.dp)
        }
        // bottom
        Column(
            Modifier.align(Alignment.BottomCenter).fillMaxWidth().background(Brush.verticalGradient(listOf(Color.Transparent, Color.Black.copy(alpha = 0.8f))))
                .navigationBarsPadding().padding(horizontal = 16.dp, vertical = 8.dp),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(formatClock(shownPosition), color = Color.White, style = MaterialTheme.typography.labelMedium)
                Slider(
                    value = fraction,
                    onValueChange = { if (ready && duration > 0) onSeekDrag(it) },
                    onValueChangeFinished = { seekDrag?.let(onSeekDone) },
                    enabled = ready && duration > 0,
                    colors = SliderDefaults.colors(
                        thumbColor = Color.White, activeTrackColor = LocalFlixColors.Accent, inactiveTrackColor = Color.White.copy(alpha = 0.3f),
                    ),
                    modifier = Modifier.weight(1f).padding(horizontal = 8.dp),
                )
                Text(formatClock(duration), color = Color.White, style = MaterialTheme.typography.labelMedium)
            }
            Row(horizontalArrangement = Arrangement.spacedBy(4.dp), verticalAlignment = Alignment.CenterVertically) {
                TrackMenuButton(AppIcons.Audiotrack, "Audio", TrackType.AUDIO, onInteract)
                TrackMenuButton(AppIcons.Subtitles, "Subtitles", TrackType.SUBTITLE, onInteract)
                SpeedMenuButton(onInteract)
            }
        }
    }
}

@Composable
private fun PlayerButton(icon: androidx.compose.ui.graphics.vector.ImageVector, description: String, onClick: () -> Unit, enabled: Boolean = true, size: androidx.compose.ui.unit.Dp = 40.dp) {
    Box(
        Modifier.size(48.dp).clip(CircleShape).pointerInput(enabled) { detectTapGestures(onTap = { if (enabled) onClick() }) },
        contentAlignment = Alignment.Center,
    ) { AppIcon(icon, description, tint = if (enabled) Color.White else Color.White.copy(alpha = 0.3f), size = size.coerceAtMost(32.dp)) }
}

@Composable
private fun SeekButton(icon: androidx.compose.ui.graphics.vector.ImageVector, description: String, enabled: Boolean, onClick: () -> Unit) {
    Box(
        Modifier.size(52.dp).clip(CircleShape).pointerInput(enabled) { detectTapGestures(onTap = { if (enabled) onClick() }) },
        contentAlignment = Alignment.Center,
    ) {
        AppIcon(icon, description, tint = if (enabled) Color.White else Color.White.copy(alpha = 0.3f), size = 36.dp)
        Text("10", color = if (enabled) Color.White else Color.White.copy(alpha = 0.3f), style = MaterialTheme.typography.labelSmall, fontWeight = FontWeight.Bold)
    }
}

private fun languageName(code: String): String =
    Locale.forLanguageTag(code).getDisplayLanguage(Locale.ENGLISH).takeIf { it.isNotEmpty() && !it.equals(code, ignoreCase = true) } ?: code

private fun TrackInfo.menuLabel() = label(::languageName)

@Composable
private fun TrackMenuButton(icon: androidx.compose.ui.graphics.vector.ImageVector, label: String, type: TrackType, onInteract: () -> Unit) {
    val container = LocalContainer.current
    val actions = LocalActions.current
    val ui = container.playbackUi
    val pc = container.playback
    var open by remember { mutableStateOf(false) }
    val tracks = ui.tracks.filter { it.type == type }
    Box {
        Row(
            Modifier.clip(RoundedCornerShape(18.dp)).pointerInput(Unit) { detectTapGestures(onTap = { open = true; onInteract() }) }.padding(horizontal = 12.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            AppIcon(icon, label, tint = Color.White, size = 22.dp)
            Text(label, Modifier.padding(start = 6.dp), color = Color.White, style = MaterialTheme.typography.labelLarge)
        }
        DropdownMenu(open, { open = false }) {
            val noneSelected = tracks.none { it.selected }
            DropdownMenuItem(
                text = { Text((if (noneSelected) "✓ " else "") + "Off") },
                onClick = { open = false; if (type == TrackType.AUDIO) pc.selectAudioTrack(TRACK_OFF) else pc.selectSubtitleTrack(TRACK_OFF) },
            )
            for (t in tracks) {
                DropdownMenuItem(
                    text = { Text((if (t.selected) "✓ " else "") + t.menuLabel()) },
                    onClick = { open = false; if (type == TrackType.AUDIO) pc.selectAudioTrack(t.id) else pc.selectSubtitleTrack(t.id) },
                )
            }
            if (type == TrackType.SUBTITLE) {
                DropdownMenuItem(text = { Text("Add subtitle file…") }, onClick = {
                    open = false
                    actions.pickers.subtitle { uri ->
                        if (uri != null) {
                            val name = uri.lastPathSegment.orEmpty()
                            if (!pc.addExternalSubtitle(uri.toString(), name)) actions.message("That subtitle format is not supported (use .srt, .vtt, .ass or .ttml).")
                        }
                    }
                })
            }
        }
    }
}

@Composable
private fun SpeedMenuButton(onInteract: () -> Unit) {
    val container = LocalContainer.current
    val pc = container.playback
    var open by remember { mutableStateOf(false) }
    var current by remember { mutableDoubleStateOf(container.backend.speed) }
    Box {
        Row(
            Modifier.clip(RoundedCornerShape(18.dp)).pointerInput(Unit) { detectTapGestures(onTap = { open = true; onInteract() }) }.padding(horizontal = 12.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            AppIcon(AppIcons.Speed, "Speed", tint = Color.White, size = 22.dp)
            Text("${current}x".replace(".0x", "x"), Modifier.padding(start = 6.dp), color = Color.White, style = MaterialTheme.typography.labelLarge)
        }
        DropdownMenu(open, { open = false }) {
            for (s in Speeds) {
                DropdownMenuItem(
                    text = { Text((if (kotlin.math.abs(s - current) < 0.01) "✓ " else "") + "${s}x".replace(".0x", "x")) },
                    onClick = { open = false; pc.setSpeed(s); current = container.backend.speed },
                )
            }
        }
    }
}
