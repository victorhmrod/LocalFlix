package io.github.victorhmrod.localflix.ui.screens

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import io.github.victorhmrod.localflix.BuildConfig
import io.github.victorhmrod.localflix.core.Collection
import io.github.victorhmrod.localflix.core.CollectionEntry
import io.github.victorhmrod.localflix.core.CollectionItemType
import io.github.victorhmrod.localflix.core.ContextType
import io.github.victorhmrod.localflix.core.Id
import io.github.victorhmrod.localflix.core.Movie
import io.github.victorhmrod.localflix.core.PlayerOrientation
import io.github.victorhmrod.localflix.core.QueueContext
import io.github.victorhmrod.localflix.core.StartMode
import io.github.victorhmrod.localflix.core.formatClock
import io.github.victorhmrod.localflix.core.formatFileSize
import io.github.victorhmrod.localflix.ui.AddToCollectionDialog
import io.github.victorhmrod.localflix.ui.AppIcon
import io.github.victorhmrod.localflix.ui.AppIconButton
import io.github.victorhmrod.localflix.ui.AppIcons
import io.github.victorhmrod.localflix.ui.ConfirmDialog
import io.github.victorhmrod.localflix.ui.CoverImage
import io.github.victorhmrod.localflix.ui.EditMovieDialog
import io.github.victorhmrod.localflix.ui.EmptyState
import io.github.victorhmrod.localflix.ui.LocalActions
import io.github.victorhmrod.localflix.ui.LocalContainer
import io.github.victorhmrod.localflix.ui.MenuEntry
import io.github.victorhmrod.localflix.ui.OverflowMenu
import io.github.victorhmrod.localflix.ui.PrimaryButton
import io.github.victorhmrod.localflix.ui.ProgressLine
import io.github.victorhmrod.localflix.ui.SecondaryButton
import io.github.victorhmrod.localflix.ui.SectionTitle
import io.github.victorhmrod.localflix.ui.TextPromptDialog
import io.github.victorhmrod.localflix.ui.libraryState
import io.github.victorhmrod.localflix.ui.theme.LocalFlixColors

// ---------------------------------------------------------------------- movie

@Composable
fun MovieScreen(movieId: Id, onBack: () -> Unit) {
    val actions = LocalActions.current
    val movie by libraryState(null as Movie?, movieId) { movie(movieId) }
    var editing by remember { mutableStateOf(false) }
    var collecting by remember { mutableStateOf(false) }
    var removing by remember { mutableStateOf(false) }
    val m = movie
    if (m == null) {
        Box(Modifier.fillMaxSize())
        return
    }
    val ctx = QueueContext(ContextType.MOVIE, m.id)

    Column(Modifier.fillMaxSize()) {
        Row(Modifier.fillMaxWidth().statusBarsPadding().padding(horizontal = 4.dp, vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            AppIconButton(AppIcons.ArrowBack, "Back", onBack)
            Text(m.title, Modifier.weight(1f), fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
            OverflowMenu(
                buildList {
                    add(MenuEntry("Edit details…") { editing = true })
                    add(MenuEntry("Add to collection…") { collecting = true })
                    if (m.file.missing) add(MenuEntry("Locate missing file…", dividerBefore = true) { actions.locateFile(m.file.id) })
                    add(MenuEntry(if (m.progress.completed) "Mark as unwatched" else "Mark as watched", dividerBefore = true) {
                        actions.io { setWatched(listOf(m.file.id), !m.progress.completed) }
                    })
                    add(MenuEntry("Reset progress") { actions.io { resetProgress(listOf(m.file.id)) } })
                    add(MenuEntry("Remove from library…", destructive = true, dividerBefore = true) { removing = true })
                },
            )
        }
        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
            Row(horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                CoverImage(m.posterFile, m.title, Modifier.width(130.dp).height(195.dp))
                Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    Text(m.title, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)
                    val parts = buildList {
                        if (m.file.durationMs > 0) add(formatClock(m.file.durationMs))
                        if (m.file.sizeBytes > 0) add(formatFileSize(m.file.sizeBytes))
                    }
                    if (parts.isNotEmpty()) Text(parts.joinToString(" · "), color = MaterialTheme.colorScheme.onSurfaceVariant, style = MaterialTheme.typography.bodySmall)
                    when {
                        m.progress.completed -> Text("Watched", color = LocalFlixColors.Success)
                        m.progress.started() -> {
                            Text("Stopped at ${formatClock(m.progress.positionMs)}", color = LocalFlixColors.Accent)
                            ProgressLine(m.progress.fraction().toFloat())
                        }
                    }
                    Text(m.file.label, style = MaterialTheme.typography.bodySmall, color = LocalFlixColors.TextMuted, maxLines = 3, overflow = TextOverflow.Ellipsis)
                }
            }
            if (m.file.missing) {
                Text("This file cannot be opened any more. If it moved, locate it again; your progress is kept.", color = LocalFlixColors.Warning)
                PrimaryButton("Locate missing file…", { actions.locateFile(m.file.id) }, Modifier.fillMaxWidth(), AppIcons.Warning)
            } else {
                PrimaryButton(
                    if (m.progress.started()) "Resume · ${formatClock(m.progress.positionMs)}" else if (m.progress.completed) "Watch again" else "Play",
                    { actions.play(ctx, m.file.id, if (m.progress.completed) StartMode.FROM_START else StartMode.AUTO) },
                    Modifier.fillMaxWidth(), AppIcons.PlayArrow,
                )
                if (m.progress.started()) SecondaryButton("Start from the beginning", { actions.play(ctx, m.file.id, StartMode.FROM_START) }, Modifier.fillMaxWidth())
            }
            if (m.description.isNotEmpty()) Text(m.description)
        }
    }
    if (editing) EditMovieDialog(m) { editing = false }
    if (collecting) AddToCollectionDialog(CollectionItemType.MOVIE, m.id) { collecting = false }
    if (removing) {
        ConfirmDialog(
            "Remove movie?", "\"${m.title}\" is removed from the library, with its progress. The video file is not touched.", "Remove", destructive = true,
            onConfirm = { removing = false; actions.io { deleteMovie(m.id) }; onBack() }, onDismiss = { removing = false },
        )
    }
}

// ---------------------------------------------------------------------- collections

@Composable
fun CollectionsScreen(onOpenCollection: (Id) -> Unit) {
    val actions = LocalActions.current
    val collections by libraryState(null as List<Collection>?) { collections() }
    var creating by remember { mutableStateOf(false) }
    Box(Modifier.fillMaxSize()) {
        Column(Modifier.fillMaxSize()) {
            ScreenHeader("Collections")
            val list = collections
            when {
                list == null -> Box(Modifier.fillMaxSize())
                list.isEmpty() -> EmptyState("No collections yet", "A collection plays shows and movies one after another, in the order you choose.") {
                    PrimaryButton("New collection", { creating = true }, icon = AppIcons.Add)
                }
                else -> LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(bottom = 96.dp)) {
                    items(list, key = { it.id }) { c ->
                        Row(
                            Modifier.fillMaxWidth().clickable { onOpenCollection(c.id) }.padding(horizontal = 16.dp, vertical = 14.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            AppIcon(AppIcons.VideoLibrary, null, tint = LocalFlixColors.Accent)
                            Column(Modifier.weight(1f).padding(start = 16.dp)) {
                                Text(c.name, fontWeight = FontWeight.SemiBold)
                                Text(
                                    if (c.entryCount == 1) "1 item" else "${c.entryCount} items",
                                    style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
                                )
                            }
                        }
                        HorizontalDivider(Modifier.padding(horizontal = 16.dp), color = LocalFlixColors.Border)
                    }
                }
            }
        }
        if (collections?.isNotEmpty() == true) {
            FloatingActionButton({ creating = true }, Modifier.align(Alignment.BottomEnd).padding(16.dp), containerColor = MaterialTheme.colorScheme.primary) {
                androidx.compose.material3.Icon(AppIcons.Add, "New collection", tint = Color.White)
            }
        }
    }
    if (creating) {
        TextPromptDialog("New collection", "Name", "", "Create", onConfirm = { name ->
            creating = false
            actions.ioResult({ createCollection(name) }) { id -> if (id != 0L) onOpenCollection(id) }
        }, onDismiss = { creating = false })
    }
}

private sealed interface CollectionDialog {
    data object Rename : CollectionDialog

    data object Delete : CollectionDialog

    data object AddShow : CollectionDialog

    data object AddMovie : CollectionDialog
}

@Composable
fun CollectionScreen(collectionId: Id, onBack: () -> Unit) {
    val actions = LocalActions.current
    val collection by libraryState(null as Collection?, collectionId) { collection(collectionId) }
    val entries by libraryState(emptyList<CollectionEntry>(), collectionId) { collectionEntries(collectionId) }
    var dialog by remember { mutableStateOf<CollectionDialog?>(null) }
    val c = collection
    if (c == null) {
        Box(Modifier.fillMaxSize())
        return
    }
    val ctx = QueueContext(ContextType.COLLECTION, collectionId)

    fun open(e: CollectionEntry) {
        // Shows open their page, movies start playing (as on the desktop).
        if (e.type == CollectionItemType.SHOW) actions.nav.navigate(io.github.victorhmrod.localflix.ui.Routes.show(e.itemId))
        else actions.ioResult({ movie(e.itemId) }) { m -> if (m != null) actions.play(QueueContext(ContextType.MOVIE, m.id), m.file.id, StartMode.AUTO) }
    }

    fun playAll() = actions.ioResult({ buildQueue(ctx) }) { queue ->
        if (queue.isEmpty()) {
            actions.message("This collection has nothing to play yet.")
        } else {
            // first unfinished item, or the very first one if everything has been watched
            val next = queue.firstOrNull { !it.completed }
            actions.play(ctx, (next ?: queue.first()).mediaFileId, if (next == null) StartMode.FROM_START else StartMode.RESUME)
        }
    }

    fun move(index: Int, delta: Int) {
        val ids = entries.map { it.id }.toMutableList()
        val to = index + delta
        if (to !in ids.indices) return
        ids.add(to, ids.removeAt(index))
        actions.io { setCollectionOrder(collectionId, ids) }
    }

    Column(Modifier.fillMaxSize()) {
        Row(Modifier.fillMaxWidth().statusBarsPadding().padding(horizontal = 4.dp, vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            AppIconButton(AppIcons.ArrowBack, "Back", onBack)
            Text(c.name, Modifier.weight(1f), fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
            OverflowMenu(
                listOf(
                    MenuEntry("Add show…") { dialog = CollectionDialog.AddShow },
                    MenuEntry("Add movie…") { dialog = CollectionDialog.AddMovie },
                    MenuEntry("Rename…", dividerBefore = true) { dialog = CollectionDialog.Rename },
                    MenuEntry("Delete collection…", destructive = true, dividerBefore = true) { dialog = CollectionDialog.Delete },
                ),
            )
        }
        LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(bottom = 24.dp)) {
            item {
                Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Text(
                        if (entries.isEmpty()) "Empty. Add shows and movies to build your sequence."
                        else "${entries.size} item${if (entries.size == 1) "" else "s"}, played in this order.",
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                    PrimaryButton("Play collection", ::playAll, Modifier.fillMaxWidth(), AppIcons.PlayArrow, enabled = entries.isNotEmpty())
                }
            }
            itemsIndexed(entries, key = { _, e -> e.id }) { index, e ->
                Row(
                    Modifier.fillMaxWidth().clickable { open(e) }.padding(start = 16.dp, end = 4.dp, top = 6.dp, bottom = 6.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    AppIcon(if (e.type == CollectionItemType.SHOW) AppIcons.Tv else AppIcons.Movie, null, tint = LocalFlixColors.TextMuted)
                    Column(Modifier.weight(1f).padding(start = 16.dp)) {
                        Text(e.title.ifEmpty { "(removed)" }, fontWeight = FontWeight.Medium, maxLines = 2, overflow = TextOverflow.Ellipsis)
                        Text(if (e.type == CollectionItemType.SHOW) "TV show" else "Movie", style = MaterialTheme.typography.bodySmall, color = LocalFlixColors.TextMuted)
                    }
                    AppIconButton(AppIcons.ArrowUpward, "Move up", { move(index, -1) }, tint = if (index > 0) LocalFlixColors.Text else LocalFlixColors.Border)
                    AppIconButton(AppIcons.ArrowDownward, "Move down", { move(index, +1) }, tint = if (index < entries.lastIndex) LocalFlixColors.Text else LocalFlixColors.Border)
                    OverflowMenu(listOf(MenuEntry("Remove from collection", destructive = true) { actions.io { removeFromCollection(e.id, collectionId) } }))
                }
                HorizontalDivider(Modifier.padding(horizontal = 16.dp), color = LocalFlixColors.Border)
            }
        }
    }

    when (dialog) {
        null -> {}
        CollectionDialog.Rename -> TextPromptDialog("Rename collection", "Name", c.name, "Save", onConfirm = { n ->
            dialog = null
            actions.io { updateCollection(collectionId, n, c.description) }
        }, onDismiss = { dialog = null })
        CollectionDialog.Delete -> ConfirmDialog(
            "Delete collection?", "\"${c.name}\" is deleted. The shows and movies in it stay in your library.", "Delete", destructive = true,
            onConfirm = { dialog = null; actions.io { deleteCollection(collectionId) }; onBack() }, onDismiss = { dialog = null },
        )
        CollectionDialog.AddShow -> PickItemDialog(
            "Add show",
            load = { showSummaries().map { it.show.id to it.show.title } },
            onPick = { id -> actions.io { addToCollection(collectionId, CollectionItemType.SHOW, id) } },
            onDismiss = { dialog = null },
        )
        CollectionDialog.AddMovie -> PickItemDialog(
            "Add movie",
            load = { movies().map { it.id to it.title } },
            onPick = { id -> actions.io { addToCollection(collectionId, CollectionItemType.MOVIE, id) } },
            onDismiss = { dialog = null },
        )
    }
}

@Composable
private fun PickItemDialog(title: String, load: io.github.victorhmrod.localflix.data.LibraryRepository.() -> List<Pair<Id, String>>, onPick: (Id) -> Unit, onDismiss: () -> Unit) {
    val items by libraryState(null as List<Pair<Id, String>>?) { load() }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            val list = items
            when {
                list == null -> Text("Loading…")
                list.isEmpty() -> Text("Nothing to add yet.", color = MaterialTheme.colorScheme.onSurfaceVariant)
                else -> LazyColumn(Modifier.fillMaxWidth().height((52 * minOf(list.size, 6)).dp)) {
                    items(list, key = { it.first }) { (id, name) ->
                        Text(
                            name,
                            Modifier.fillMaxWidth().clickable { onPick(id); onDismiss() }.padding(vertical = 14.dp),
                            maxLines = 1, overflow = TextOverflow.Ellipsis,
                        )
                    }
                }
            }
        },
        confirmButton = { TextButton(onDismiss) { Text("Close") } },
    )
}

// ---------------------------------------------------------------------- settings

@Composable
fun SettingsScreen() {
    val container = LocalContainer.current
    val actions = LocalActions.current
    val s = container.settings
    // Settings are read once per recomposition trigger below; every change writes through and bumps `version`.
    var version by remember { mutableStateOf(0) }
    fun changed(block: () -> Unit) {
        block()
        version++
    }
    @Suppress("UNUSED_VARIABLE") val v = version
    val missingCount by libraryState(0) { missingFiles().size }
    val fileCount by libraryState(0) { allMediaFiles().size }

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
        ScreenHeader("Settings")

        SectionTitle("Playback")
        SwitchRow("Play the next episode automatically", s.autoPlayNext) { changed { s.setAutoPlayNext(it) } }
        if (s.autoPlayNext) {
            SliderRow("Countdown before the next episode", "${s.autoPlayCountdownSeconds} s", s.autoPlayCountdownSeconds.toFloat(), 0f..15f, 14) {
                changed { s.setAutoPlayCountdownSeconds(it.toInt()) }
            }
        }
        SwitchRow("Resume where I stopped, without asking", s.resumeAutomatically, "When off, you choose between resuming and starting over.") { changed { s.setResumeAutomatically(it) } }
        SwitchRow("Skip episodes I have completed", s.skipCompleted, "Only when moving on automatically; the Next button always follows your order.") { changed { s.setSkipCompleted(it) } }
        SwitchRow("Skip openings automatically", s.autoSkipIntro) { changed { s.setAutoSkipIntro(it) } }
        SwitchRow("Skip endings automatically", s.autoSkipEnding) { changed { s.setAutoSkipEnding(it) } }
        Text(
            "Openings and endings are found through the chapters stored in the video file (titled Opening, OP, Intro, Ending, ED, Credits…). " +
                "While the player is inside one, a Skip button appears; these options press it for you. " +
                "Skipping an ending that lasts until the end of the file finishes the episode, so the next one starts as usual. " +
                "Files without such chapters have nothing to skip.",
            Modifier.padding(horizontal = 16.dp, vertical = 4.dp), style = MaterialTheme.typography.bodySmall, color = LocalFlixColors.TextMuted,
        )
        ChoiceRow("Default playback speed", listOf(0.5, 0.75, 1.0, 1.25, 1.5, 2.0), s.playbackSpeed, { "${it}x".replace(".0x", "x") }) { changed { s.setPlaybackSpeed(it) } }
        ChoiceRow(
            "Screen while playing", PlayerOrientation.entries, s.playerOrientation,
            { if (it == PlayerOrientation.LANDSCAPE) "Landscape" else "Follow the device" },
        ) { changed { s.setPlayerOrientation(it) } }
        LanguageRow("Preferred audio language", s.preferredAudioLanguage) { changed { s.setPreferredAudioLanguage(it) } }
        LanguageRow("Preferred subtitle language", s.preferredSubtitleLanguage) { changed { s.setPreferredSubtitleLanguage(it) } }
        Text(
            "Languages use codes such as eng, por, jpn. Leave empty to use what the file marks as default.",
            Modifier.padding(horizontal = 16.dp, vertical = 4.dp), style = MaterialTheme.typography.bodySmall, color = LocalFlixColors.TextMuted,
        )

        SectionTitle("Library", Modifier.padding(top = 12.dp))
        Column(Modifier.padding(horizontal = 16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Text(
                "LocalFlix never copies, moves or deletes your videos. It keeps access to the files and folders you picked, and only looks for ones that were moved, renamed or sit on a removed card.",
                style = MaterialTheme.typography.bodySmall, color = LocalFlixColors.TextMuted,
            )
            Text(
                if (fileCount == 0) "No files in the library." else if (missingCount == 0) "$fileCount files, all can be opened." else "$fileCount files, $missingCount cannot be opened.",
                color = if (missingCount > 0) LocalFlixColors.Warning else MaterialTheme.colorScheme.onSurface,
            )
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                SecondaryButton("Check files", { actions.recheckFiles() }, icon = AppIcons.Refresh)
                SecondaryButton("Reconnect folder…", { actions.reconnectFolder() }, icon = AppIcons.Folder)
            }
            Text(
                "Reconnect folder: pick the folder where moved files now live (or the same one after reinstalling the app). Missing entries are matched by file name and path; order and progress are kept.",
                style = MaterialTheme.typography.bodySmall, color = LocalFlixColors.TextMuted,
            )
        }

        SectionTitle("About", Modifier.padding(top = 12.dp))
        Column(Modifier.padding(horizontal = 16.dp, vertical = 4.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text("LocalFlix ${BuildConfig.VERSION_NAME}", fontWeight = FontWeight.SemiBold)
            Text(
                "An offline video player with a Netflix-style library. Free software, AGPL-3.0. No account, no internet, no tracking.",
                style = MaterialTheme.typography.bodySmall, color = LocalFlixColors.TextMuted,
            )
            Text("Playback by Media3 / ExoPlayer (Apache 2.0).", style = MaterialTheme.typography.bodySmall, color = LocalFlixColors.TextMuted)
        }
        Spacer(Modifier.height(32.dp))
    }
}

@Composable
private fun SwitchRow(label: String, checked: Boolean, hint: String? = null, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth().clickable { onChange(!checked) }.padding(horizontal = 16.dp, vertical = 10.dp), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f).padding(end = 12.dp)) {
            Text(label)
            if (hint != null) Text(hint, style = MaterialTheme.typography.bodySmall, color = LocalFlixColors.TextMuted)
        }
        Switch(checked, onChange)
    }
}

@Composable
private fun SliderRow(label: String, valueText: String, value: Float, range: ClosedFloatingPointRange<Float>, steps: Int, onChange: (Float) -> Unit) {
    Column(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 6.dp)) {
        Row {
            Text(label, Modifier.weight(1f))
            Text(valueText, color = LocalFlixColors.Accent)
        }
        Slider(value, onChange, valueRange = range, steps = steps)
    }
}

@Composable
private fun <T> ChoiceRow(label: String, options: List<T>, current: T, text: (T) -> String, onPick: (T) -> Unit) {
    var open by remember { mutableStateOf(false) }
    Box {
        Row(Modifier.fillMaxWidth().clickable { open = true }.padding(horizontal = 16.dp, vertical = 14.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(label, Modifier.weight(1f))
            Text(text(current), color = LocalFlixColors.Accent)
        }
        androidx.compose.material3.DropdownMenu(open, { open = false }) {
            for (o in options) {
                androidx.compose.material3.DropdownMenuItem(text = { Text(text(o)) }, onClick = { open = false; onPick(o) })
            }
        }
    }
}

@Composable
private fun LanguageRow(label: String, value: String, onChange: (String) -> Unit) {
    var editing by remember { mutableStateOf(false) }
    Row(Modifier.fillMaxWidth().clickable { editing = true }.padding(horizontal = 16.dp, vertical = 14.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, Modifier.weight(1f))
        Text(value.ifEmpty { "Automatic" }, color = LocalFlixColors.Accent)
    }
    if (editing) {
        TextPromptDialog(label, "Language code (e.g. eng, por)", value, "Save", onConfirm = { editing = false; onChange(it) }, onDismiss = { editing = false }, allowEmpty = true)
    }
}
