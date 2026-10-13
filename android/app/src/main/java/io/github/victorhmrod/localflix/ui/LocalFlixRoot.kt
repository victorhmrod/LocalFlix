package io.github.victorhmrod.localflix.ui

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationBarItemDefaults
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LifecycleEventEffect
import androidx.navigation.NavGraph.Companion.findStartDestination
import androidx.navigation.NavType
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import androidx.navigation.navArgument
import io.github.victorhmrod.localflix.app.AppContainer
import io.github.victorhmrod.localflix.core.Id
import io.github.victorhmrod.localflix.core.PlaybackEvent
import io.github.victorhmrod.localflix.core.PlaybackListener
import io.github.victorhmrod.localflix.core.StartMode
import io.github.victorhmrod.localflix.core.FailureKind
import io.github.victorhmrod.localflix.core.formatClock
import io.github.victorhmrod.localflix.ui.screens.CollectionScreen
import io.github.victorhmrod.localflix.ui.screens.CollectionsScreen
import io.github.victorhmrod.localflix.ui.screens.HomeScreen
import io.github.victorhmrod.localflix.ui.screens.MovieScreen
import io.github.victorhmrod.localflix.ui.screens.MoviesScreen
import io.github.victorhmrod.localflix.ui.screens.PlayerScreen
import io.github.victorhmrod.localflix.ui.screens.SettingsScreen
import io.github.victorhmrod.localflix.ui.screens.ShowScreen
import io.github.victorhmrod.localflix.ui.screens.ShowsScreen
import io.github.victorhmrod.localflix.ui.theme.LocalFlixColors

object Routes {
    const val HOME = "home"
    const val SHOWS = "shows"
    const val MOVIES = "movies"
    const val COLLECTIONS = "collections"
    const val SETTINGS = "settings"
    const val PLAYER = "player"
    const val SHOW = "show/{id}"
    const val MOVIE = "movie/{id}"
    const val COLLECTION = "collection/{id}"

    fun show(id: Id) = "show/$id"

    fun movie(id: Id) = "movie/$id"

    fun collection(id: Id) = "collection/$id"
}

private data class Tab(val route: String, val label: String, val icon: ImageVector)

private val Tabs = listOf(
    Tab(Routes.HOME, "Home", AppIcons.Home),
    Tab(Routes.SHOWS, "TV shows", AppIcons.Tv),
    Tab(Routes.MOVIES, "Movies", AppIcons.Movie),
    Tab(Routes.COLLECTIONS, "Collections", AppIcons.VideoLibrary),
    Tab(Routes.SETTINGS, "Settings", AppIcons.Settings),
)

private val VideoMimeTypes = arrayOf("video/*", "application/x-matroska", "application/octet-stream")

@Composable
fun LocalFlixRoot(container: AppContainer) {
    val nav = rememberNavController()
    val snackbar = remember { androidx.compose.material3.SnackbarHostState() }
    val scope = rememberCoroutineScope()

    // ---- system pickers, behind a callback API
    var videosCb by remember { mutableStateOf<((List<Uri>) -> Unit)?>(null) }
    var videoCb by remember { mutableStateOf<((Uri?) -> Unit)?>(null) }
    var folderCb by remember { mutableStateOf<((Uri?) -> Unit)?>(null) }
    var imageCb by remember { mutableStateOf<((Uri?) -> Unit)?>(null) }
    var subtitleCb by remember { mutableStateOf<((Uri?) -> Unit)?>(null) }
    val videosLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { videosCb?.invoke(it); videosCb = null }
    val videoLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { videoCb?.invoke(it); videoCb = null }
    val folderLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { folderCb?.invoke(it); folderCb = null }
    val imageLauncher = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) { imageCb?.invoke(it); imageCb = null }
    val subtitleLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { subtitleCb?.invoke(it); subtitleCb = null }
    val pickers = remember {
        Pickers(
            videos = { cb -> videosCb = cb; videosLauncher.launch(VideoMimeTypes) },
            video = { cb -> videoCb = cb; videoLauncher.launch(VideoMimeTypes) },
            folder = { cb -> folderCb = cb; folderLauncher.launch(null) },
            image = { cb -> imageCb = cb; imageLauncher.launch("image/*") },
            subtitle = { cb -> subtitleCb = cb; subtitleLauncher.launch(arrayOf("*/*")) },
        )
    }
    val actions = remember { AppActions(container, nav, snackbar, scope, pickers) }

    // Playback problems and "resume or start over?" are answered with dialogs wherever the user is.
    DisposableEffect(container) {
        val l = PlaybackListener { e ->
            when (e) {
                is PlaybackEvent.ResumeDecisionNeeded -> actions.dialog = GlobalDialog.ResumeChoice(e.entry, e.positionMs)
                is PlaybackEvent.Failed -> actions.dialog = GlobalDialog.PlaybackFailed(e.entry, e.failure)
                else -> {}
            }
        }
        container.playback.addListener(l)
        onDispose { container.playback.removeListener(l) }
    }
    LaunchedEffect(container) { container.errors.collect { actions.message("Database problem: $it") } }

    // Leaving the screen pauses the video and keeps the place.
    LifecycleEventEffect(Lifecycle.Event.ON_STOP) {
        if (container.playback.isActive) {
            container.playback.setPaused(true)
            container.playback.flushProgress()
        }
    }

    CompositionLocalProvider(LocalContainer provides container, LocalActions provides actions) {
        Surface(Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
            val entry by nav.currentBackStackEntryAsState()
            val route = entry?.destination?.route
            val topLevel = route in Tabs.map { it.route }
            Scaffold(
                containerColor = MaterialTheme.colorScheme.background,
                contentWindowInsets = WindowInsets(0),
                snackbarHost = { SnackbarHost(snackbar) },
                bottomBar = {
                    if (topLevel) {
                        NavigationBar(containerColor = LocalFlixColors.Surface) {
                            for (t in Tabs) {
                                NavigationBarItem(
                                    selected = route == t.route,
                                    onClick = {
                                        nav.navigate(t.route) {
                                            popUpTo(nav.graph.findStartDestination().id) { saveState = true }
                                            launchSingleTop = true
                                            restoreState = true
                                        }
                                    },
                                    icon = { Icon(t.icon, t.label) },
                                    label = { Text(t.label, maxLines = 1) },
                                    colors = NavigationBarItemDefaults.colors(
                                        selectedIconColor = Color.White, selectedTextColor = LocalFlixColors.Accent,
                                        indicatorColor = LocalFlixColors.Accent, unselectedIconColor = LocalFlixColors.TextMuted,
                                        unselectedTextColor = LocalFlixColors.TextMuted,
                                    ),
                                )
                            }
                        }
                    }
                },
            ) { padding ->
                NavHost(nav, startDestination = Routes.HOME, modifier = Modifier.padding(padding)) {
                    composable(Routes.HOME) {
                        HomeScreen(
                            onOpenShow = { nav.navigate(Routes.show(it)) },
                            onOpenMovie = { nav.navigate(Routes.movie(it)) },
                            onShowsTab = { nav.navigate(Routes.SHOWS) },
                            onMoviesTab = { nav.navigate(Routes.MOVIES) },
                        )
                    }
                    composable(Routes.SHOWS) { ShowsScreen(onOpenShow = { nav.navigate(Routes.show(it)) }) }
                    composable(Routes.MOVIES) { MoviesScreen(onOpenMovie = { nav.navigate(Routes.movie(it)) }) }
                    composable(Routes.COLLECTIONS) { CollectionsScreen(onOpenCollection = { nav.navigate(Routes.collection(it)) }) }
                    composable(Routes.SETTINGS) { SettingsScreen() }
                    composable(Routes.SHOW, arguments = listOf(navArgument("id") { type = NavType.LongType })) {
                        ShowScreen(it.arguments!!.getLong("id"), onBack = { nav.popBackStack() })
                    }
                    composable(Routes.MOVIE, arguments = listOf(navArgument("id") { type = NavType.LongType })) {
                        MovieScreen(it.arguments!!.getLong("id"), onBack = { nav.popBackStack() })
                    }
                    composable(Routes.COLLECTION, arguments = listOf(navArgument("id") { type = NavType.LongType })) {
                        CollectionScreen(it.arguments!!.getLong("id"), onBack = { nav.popBackStack() })
                    }
                    composable(Routes.PLAYER) { PlayerScreen(onClose = { nav.popBackStack() }) }
                }
            }
            GlobalDialogHost(actions)
            BusyOverlay(actions)
        }
    }
}

@Composable
private fun BusyOverlay(actions: AppActions) {
    val text = actions.busy ?: return
    Box(Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.5f)).pointerInput(Unit) { awaitPointerEventScope { while (true) awaitPointerEvent() } }, contentAlignment = Alignment.Center) {
        Column(
            Modifier.width(280.dp).background(LocalFlixColors.SurfaceRaised, RoundedCornerShape(16.dp)).padding(20.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            CircularProgressIndicator()
            Text(text, color = LocalFlixColors.Text)
            TextButton({ actions.cancelBusy() }) { Text("Cancel") }
        }
    }
}

@Composable
private fun GlobalDialogHost(actions: AppActions) {
    val container = actions.container
    when (val d = actions.dialog) {
        null -> {}
        is GlobalDialog.Message -> InfoDialog(d.title, d.text, actions::dismissDialog)
        is GlobalDialog.ImportSummary -> AlertDialog(
            onDismissRequest = actions::dismissDialog,
            title = { Text(d.title) },
            text = {
                Column {
                    Text(d.headline, fontWeight = FontWeight.Bold)
                    Text("Skipped:", Modifier.padding(top = 12.dp), color = MaterialTheme.colorScheme.onSurfaceVariant)
                    LazyColumn(Modifier.fillMaxWidth().height(180.dp)) {
                        items(d.details) { Text(it, Modifier.padding(vertical = 4.dp), style = MaterialTheme.typography.bodySmall) }
                    }
                }
            },
            confirmButton = { TextButton(actions::dismissDialog) { Text("OK") } },
        )
        is GlobalDialog.ResumeChoice -> AlertDialog(
            onDismissRequest = actions::dismissDialog,
            title = { Text(d.entry.displayTitle()) },
            text = { Text("You stopped at ${formatClock(d.positionMs)}.") },
            confirmButton = {
                TextButton({ actions.dismissDialog(); actions.playQueueEntry(d.entry.mediaFileId, StartMode.RESUME) }) { Text("Resume at ${formatClock(d.positionMs)}") }
            },
            dismissButton = {
                TextButton({ actions.dismissDialog(); actions.playQueueEntry(d.entry.mediaFileId, StartMode.FROM_START) }) { Text("Start over") }
            },
        )
        is GlobalDialog.PlaybackFailed -> {
            val missing = d.failure.kind == FailureKind.MISSING_FILE
            val body = when (d.failure.kind) {
                FailureKind.MISSING_FILE -> "The file for \"${d.entry.displayTitle()}\" cannot be opened:\n${d.failure.detail}\n\nIt may have been moved, renamed or deleted, or LocalFlix lost access to it. If you locate it again, your progress and place in the order are kept."
                FailureKind.LOAD_ERROR -> "\"${d.entry.displayTitle()}\" cannot be played: ${d.failure.detail}"
                FailureKind.NOT_IN_QUEUE -> "That item is not part of the current playback queue."
            }
            AlertDialog(
                onDismissRequest = actions::dismissDialog,
                title = { Text(if (missing) "File not found" else "Cannot play") },
                text = { Text(body) },
                confirmButton = {
                    if (missing) {
                        TextButton({
                            actions.dismissDialog()
                            actions.locateFile(d.entry.mediaFileId) { actions.playQueueEntry(d.entry.mediaFileId, StartMode.RESUME) }
                        }) { Text("Locate file…") }
                    } else {
                        TextButton(actions::dismissDialog) { Text("Close") }
                    }
                },
                dismissButton = {
                    Row {
                        if (container.playback.hasNext) {
                            TextButton({ actions.dismissDialog(); container.playbackUi.dismissFailure(); container.playback.next() }) { Text("Skip to next") }
                        }
                        if (missing) TextButton({ actions.dismissDialog(); container.playbackUi.dismissFailure() }) { Text("Close") }
                    }
                },
            )
        }
        is GlobalDialog.ReconnectOffer -> AlertDialog(
            onDismissRequest = actions::dismissDialog,
            title = { Text("Other files are missing too") },
            text = { Text("${d.remaining} more file${if (d.remaining == 1) " is" else "s are"} still missing. If they moved to the same place, pick that folder and LocalFlix will reconnect them together.") },
            confirmButton = { TextButton({ actions.dismissDialog(); actions.reconnectFolder() }) { Text("Pick folder…") } },
            dismissButton = { TextButton(actions::dismissDialog) { Text("Not now") } },
        )
        is GlobalDialog.ReconnectResult -> {
            val r = d.report
            InfoDialog(
                "Reconnect folder",
                buildString {
                    append("Looked through ${r.scanned} video${if (r.scanned == 1) "" else "s"}.\n")
                    append("Reconnected: ${r.reconnected}\n")
                    append("Still missing: ${r.stillMissing}")
                    if (r.stillMissing > 0 && r.reconnected == 0) append("\n\nNo file in that folder matched a missing entry by name. Try the folder that holds them, or locate files one by one from their menu.")
                    if (r.problems.isNotEmpty()) append("\n\nNot readable:\n" + r.problems.take(5).joinToString("\n"))
                },
                actions::dismissDialog,
            )
        }
    }
}
