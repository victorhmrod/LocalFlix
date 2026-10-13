package io.github.victorhmrod.localflix.ui.screens

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import io.github.victorhmrod.localflix.app.PickedSource
import io.github.victorhmrod.localflix.core.ContextType
import io.github.victorhmrod.localflix.core.Episode
import io.github.victorhmrod.localflix.core.Id
import io.github.victorhmrod.localflix.core.INVALID_ID
import io.github.victorhmrod.localflix.core.QueueContext
import io.github.victorhmrod.localflix.core.ResumeReason
import io.github.victorhmrod.localflix.core.ResumeTarget
import io.github.victorhmrod.localflix.core.Season
import io.github.victorhmrod.localflix.core.SeasonLayout
import io.github.victorhmrod.localflix.core.Show
import io.github.victorhmrod.localflix.core.StartMode
import io.github.victorhmrod.localflix.core.episodeLabel
import io.github.victorhmrod.localflix.core.formatClock
import io.github.victorhmrod.localflix.ui.AddToCollectionDialog
import io.github.victorhmrod.localflix.ui.AppIcon
import io.github.victorhmrod.localflix.ui.AppIconButton
import io.github.victorhmrod.localflix.ui.AppIcons
import io.github.victorhmrod.localflix.ui.ConfirmDialog
import io.github.victorhmrod.localflix.ui.CoverImage
import io.github.victorhmrod.localflix.ui.EditShowDialog
import io.github.victorhmrod.localflix.ui.LocalActions
import io.github.victorhmrod.localflix.ui.LocalContainer
import io.github.victorhmrod.localflix.ui.MenuEntry
import io.github.victorhmrod.localflix.ui.OverflowMenu
import io.github.victorhmrod.localflix.ui.PrimaryButton
import io.github.victorhmrod.localflix.ui.ProgressLine
import io.github.victorhmrod.localflix.ui.SecondaryButton
import io.github.victorhmrod.localflix.ui.TextPromptDialog
import io.github.victorhmrod.localflix.ui.TwoFieldDialog
import io.github.victorhmrod.localflix.ui.libraryState
import io.github.victorhmrod.localflix.ui.theme.LocalFlixColors
import kotlin.math.roundToInt

private class ShowData(val show: Show?, val seasons: List<Season>, val episodes: List<Episode>, val resume: ResumeTarget)

private sealed interface ShowDialog {
    data object Edit : ShowDialog

    data object NewSeason : ShowDialog

    data object AddToCollection : ShowDialog

    data object RemoveShow : ShowDialog

    data object ResetProgress : ShowDialog

    data class RenameEpisode(val episode: Episode) : ShowDialog

    data class RenameSeason(val season: Season) : ShowDialog

    data class RemoveSeason(val season: Season) : ShowDialog

    data class RemoveEpisodes(val ids: List<Id>) : ShowDialog
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun ShowScreen(showId: Id, onBack: () -> Unit) {
    val actions = LocalActions.current
    val container = LocalContainer.current
    val data by libraryState(null as ShowData?, showId) { ShowData(show(showId), seasons(showId), episodes(showId), resumeTarget(showId)) }
    var dialog by remember { mutableStateOf<ShowDialog?>(null) }
    var reordering by rememberSaveable { mutableStateOf(false) }
    var selected by remember { mutableStateOf(setOf<Id>()) }

    LaunchedEffect(showId) { container.importer.verifyShow(showId) }

    val d = data
    if (d == null) {
        Box(Modifier.fillMaxSize())
        return
    }
    val show = d.show
    if (show == null) {
        // removed from elsewhere (or never existed)
        LaunchedEffect(Unit) { onBack() }
        return
    }
    val ctx = QueueContext(ContextType.SHOW, showId)
    val selectedEpisodes = d.episodes.filter { it.id in selected }
    val selecting = selected.isNotEmpty()

    fun addFiles(seasonId: Id) = actions.pickers.videos { uris ->
        if (uris.isNotEmpty()) actions.importEpisodes(showId, seasonId, PickedSource.Files(uris))
    }

    fun addFolder(seasonId: Id) = actions.pickers.folder { uri -> if (uri != null) actions.importEpisodes(showId, seasonId, PickedSource.Folder(uri)) }

    Column(Modifier.fillMaxSize()) {
        // ------------------------------------------------------------ top bar
        Row(Modifier.fillMaxWidth().statusBarsPadding().padding(horizontal = 4.dp, vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            if (selecting) {
                AppIconButton(AppIcons.Close, "Clear selection", { selected = emptySet() })
                Text("${selected.size} selected", Modifier.weight(1f), fontWeight = FontWeight.SemiBold)
                AppIconButton(AppIcons.CheckCircle, "Mark as watched", {
                    actions.io { setWatched(selectedEpisodes.map { it.file.id }, true) }
                    selected = emptySet()
                })
                OverflowMenu(
                    buildList {
                        add(MenuEntry("Mark as unwatched") { actions.io { setWatched(selectedEpisodes.map { it.file.id }, false) }; selected = emptySet() })
                        add(MenuEntry("Reset progress") { actions.io { resetProgress(selectedEpisodes.map { it.file.id }) }; selected = emptySet() })
                        for (s in d.seasons) {
                            add(MenuEntry("Move to ${s.title}", dividerBefore = s == d.seasons.first()) {
                                actions.io { moveToSeason(showId, selected.toList(), s.id) }
                                selected = emptySet()
                            })
                        }
                        add(MenuEntry("Remove from library…", destructive = true, dividerBefore = true) { dialog = ShowDialog.RemoveEpisodes(selected.toList()) })
                    },
                )
            } else {
                AppIconButton(AppIcons.ArrowBack, "Back", onBack)
                Text(
                    if (reordering) "Reorder episodes" else show.title,
                    Modifier.weight(1f), fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis,
                )
                if (reordering) {
                    TextButton({ reordering = false }) { Text("Done") }
                } else {
                    AppIconButton(AppIcons.DragHandle, "Reorder episodes", { reordering = true })
                    var addMenu by remember { mutableStateOf(false) }
                    Box {
                        AppIconButton(AppIcons.Add, "Add episodes", { addMenu = true })
                        DropdownMenu(addMenu, { addMenu = false }) {
                            DropdownMenuItem(text = { Text("Add files…") }, onClick = { addMenu = false; addFiles(INVALID_ID) })
                            DropdownMenuItem(text = { Text("Add folder…") }, onClick = { addMenu = false; addFolder(INVALID_ID) })
                            HorizontalDivider()
                            DropdownMenuItem(text = { Text("New season…") }, onClick = { addMenu = false; dialog = ShowDialog.NewSeason })
                        }
                    }
                    OverflowMenu(
                        listOf(
                            MenuEntry("Edit details…") { dialog = ShowDialog.Edit },
                            MenuEntry("Add to collection…") { dialog = ShowDialog.AddToCollection },
                            MenuEntry("Check that files still exist") { actions.recheckFiles() },
                            MenuEntry("Mark all as watched", dividerBefore = true) { actions.io { setWatched(episodes(showId).map { it.file.id }, true) } },
                            MenuEntry("Mark all as unwatched") { actions.io { setWatched(episodes(showId).map { it.file.id }, false) } },
                            MenuEntry("Reset progress…") { dialog = ShowDialog.ResetProgress },
                            MenuEntry("Remove show from library…", destructive = true, dividerBefore = true) { dialog = ShowDialog.RemoveShow },
                        ),
                    )
                }
            }
        }

        if (reordering) {
            ReorderList(
                d.seasons, d.episodes,
                onApply = { layout -> actions.io { applyLayout(showId, layout) } },
                onMoveSeason = { seasonId, to -> actions.io { moveSeason(seasonId, to) } },
            )
        } else {
            LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(bottom = 32.dp)) {
                item {
                    ShowHeader(show, d, onPlay = {
                        val t = d.resume
                        if (t.valid()) actions.play(ctx, t.mediaFileId, if (t.allCompleted()) StartMode.FROM_START else StartMode.AUTO)
                    })
                }
                if (d.episodes.isEmpty()) {
                    item {
                        Column(Modifier.fillMaxWidth().padding(24.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                            Text("No episodes yet", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
                            Text(
                                "Add files, or a folder with sub-folders like \"Season 1\". The order is yours to change afterwards.",
                                Modifier.padding(vertical = 8.dp), color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                                PrimaryButton("Add files", { addFiles(INVALID_ID) }, icon = AppIcons.Movie)
                                SecondaryButton("Add folder", { addFolder(INVALID_ID) }, icon = AppIcons.Folder)
                            }
                        }
                    }
                }
                for ((si, season) in d.seasons.withIndex()) {
                    val eps = d.episodes.filter { it.seasonId == season.id }
                    item(key = "season-${season.id}") {
                        SeasonHeader(
                            season, eps, si, d.seasons.size,
                            onPlay = { eps.firstOrNull()?.let { actions.play(ctx, it.file.id, StartMode.AUTO) } },
                            onAddFiles = { addFiles(season.id) },
                            onAddFolder = { addFolder(season.id) },
                            onRename = { dialog = ShowDialog.RenameSeason(season) },
                            onMove = { delta -> actions.io { moveSeason(season.id, si + delta) } },
                            onRemove = { dialog = ShowDialog.RemoveSeason(season) },
                        )
                    }
                    itemsIndexed(eps, key = { _, e -> "ep-${e.id}" }) { index, e ->
                        EpisodeRow(
                            e, index, selected = e.id in selected, selecting = selecting,
                            onClick = {
                                if (selecting) selected = if (e.id in selected) selected - e.id else selected + e.id
                                else actions.play(ctx, e.file.id, StartMode.AUTO)
                            },
                            onLongClick = { selected = if (e.id in selected) selected - e.id else selected + e.id },
                            menu = listOf(
                                MenuEntry("Play") { actions.play(ctx, e.file.id, StartMode.AUTO) },
                                MenuEntry("Play from the beginning") { actions.play(ctx, e.file.id, StartMode.FROM_START) },
                                MenuEntry("Rename…", dividerBefore = true) { dialog = ShowDialog.RenameEpisode(e) },
                                MenuEntry("Move up", enabled = !(si == 0 && index == 0)) { actions.io { moveEpisodeRelative(e, -1, d) } },
                                MenuEntry("Move down", enabled = !(si == d.seasons.lastIndex && index == eps.lastIndex)) { actions.io { moveEpisodeRelative(e, +1, d) } },
                                MenuEntry(if (e.progress.completed) "Mark as unwatched" else "Mark as watched", dividerBefore = true) {
                                    actions.io { setWatched(listOf(e.file.id), !e.progress.completed) }
                                },
                                MenuEntry("Reset progress") { actions.io { resetProgress(listOf(e.file.id)) } },
                                MenuEntry("Locate missing file…", enabled = e.file.missing, dividerBefore = true) { actions.locateFile(e.file.id) },
                                MenuEntry("Remove from library…", destructive = true, dividerBefore = true) { dialog = ShowDialog.RemoveEpisodes(listOf(e.id)) },
                            ),
                        )
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------------- dialogs
    when (val dlg = dialog) {
        null -> {}
        ShowDialog.Edit -> EditShowDialog(show, onDismiss = { dialog = null })
        ShowDialog.NewSeason -> TextPromptDialog("New season", "Title", "Season ${(d.seasons.maxOfOrNull { it.number } ?: 0) + 1}", "Create", onConfirm = { t ->
            dialog = null
            actions.io { addSeason(showId, t) }
        }, onDismiss = { dialog = null })
        ShowDialog.AddToCollection -> AddToCollectionDialog(io.github.victorhmrod.localflix.core.CollectionItemType.SHOW, showId) { dialog = null }
        ShowDialog.RemoveShow -> ConfirmDialog(
            "Remove show?",
            "\"${show.title}\" and its episodes are removed from the library, with their order and progress. Your video files are not touched.",
            "Remove", destructive = true,
            onConfirm = { dialog = null; actions.io { deleteShow(showId) }; onBack() },
            onDismiss = { dialog = null },
        )
        ShowDialog.ResetProgress -> ConfirmDialog(
            "Reset progress?", "Every episode of \"${show.title}\" becomes unwatched and forgets where you stopped.", "Reset",
            onConfirm = { dialog = null; actions.io { resetShowProgress(showId) } }, onDismiss = { dialog = null },
        )
        is ShowDialog.RenameEpisode -> TwoFieldDialog(
            "Edit episode", "Title", dlg.episode.title, "Episode number", if (dlg.episode.episodeNumber > 0) dlg.episode.episodeNumber.toString() else "",
            onConfirm = { t, n -> dialog = null; actions.io { updateEpisode(dlg.episode.id, t, n) } }, onDismiss = { dialog = null },
        )
        is ShowDialog.RenameSeason -> TwoFieldDialog(
            "Edit season", "Title", dlg.season.title, "Season number (0 = specials)", dlg.season.number.toString(),
            onConfirm = { t, n -> dialog = null; actions.io { updateSeason(dlg.season.id, t, n) } }, onDismiss = { dialog = null },
        )
        is ShowDialog.RemoveSeason -> ConfirmDialog(
            "Remove season?", "\"${dlg.season.title}\" and its episodes are removed from the library. Your video files are not touched.", "Remove",
            destructive = true, onConfirm = { dialog = null; actions.io { deleteSeason(dlg.season.id) } }, onDismiss = { dialog = null },
        )
        is ShowDialog.RemoveEpisodes -> ConfirmDialog(
            "Remove from library?",
            "${dlg.ids.size} episode${if (dlg.ids.size == 1) "" else "s"} will be removed from the library, with progress. Your video files are not touched.",
            "Remove", destructive = true,
            onConfirm = { dialog = null; selected = emptySet(); actions.io { removeEpisodes(dlg.ids) } },
            onDismiss = { dialog = null },
        )
    }
}

/** Moves the selected episodes to the end of [toSeasonId], keeping their relative order. */
private fun io.github.victorhmrod.localflix.data.LibraryRepository.moveToSeason(showId: Id, episodeIds: List<Id>, toSeasonId: Id) {
    val moving = episodeIds.toSet()
    val eps = episodes(showId)
    val layout = seasons(showId).map { s ->
        val keep = eps.filter { it.seasonId == s.id && it.id !in moving }.map { it.id }
        SeasonLayout(s.id, if (s.id == toSeasonId) keep + eps.filter { it.id in moving }.map { it.id } else keep)
    }
    applyLayout(showId, layout)
}

/** One step up (-1) or down (+1) in the playback order, crossing into the neighbouring season at the edges. */
private fun io.github.victorhmrod.localflix.data.LibraryRepository.moveEpisodeRelative(e: Episode, delta: Int, d: ShowData) {
    val rows = flatten(d.seasons, d.episodes)
    val from = rows.indexOfFirst { it is Row.Item && it.episode.id == e.id }
    if (from < 0) return
    val moved = moveRow(rows, from, from + delta) ?: return
    applyLayout(e.showId, toLayout(moved))
}

// ---------------------------------------------------------------------- header, rows

@Composable
private fun ShowHeader(show: Show, d: ShowData, onPlay: () -> Unit) {
    val total = d.episodes.size
    val watched = d.episodes.count { it.progress.completed }
    Column(Modifier.fillMaxWidth().padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Row(horizontalArrangement = Arrangement.spacedBy(16.dp)) {
            CoverImage(show.coverFile, show.title, Modifier.width(110.dp).height(165.dp))
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Text(show.title, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.Bold)
                Text(
                    "${d.seasons.size} season${if (d.seasons.size == 1) "" else "s"} · $total episode${if (total == 1) "" else "s"} · $watched watched",
                    style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                if (total > 0) ProgressLine(watched.toFloat() / total, Modifier.padding(vertical = 4.dp))
                if (show.description.isNotEmpty()) {
                    Text(show.description, style = MaterialTheme.typography.bodySmall, maxLines = 5, overflow = TextOverflow.Ellipsis)
                }
            }
        }
        val t = d.resume
        if (t.valid()) {
            val ep = d.episodes.firstOrNull { it.id == t.episodeId }
            val label = ep?.let { episodeLabel(it) }.orEmpty()
            val text = when (t.reason) {
                ResumeReason.IN_PROGRESS -> "Continue · $label · ${formatClock(t.positionMs)}"
                ResumeReason.NEXT_UNFINISHED -> "Continue · $label"
                ResumeReason.NOT_STARTED -> "Start watching · $label"
                ResumeReason.ALL_COMPLETED -> "Watch again from the start"
                ResumeReason.NONE -> ""
            }
            PrimaryButton(text, onPlay, Modifier.fillMaxWidth(), AppIcons.PlayArrow)
        }
    }
}

@Composable
private fun SeasonHeader(
    season: Season,
    episodes: List<Episode>,
    index: Int,
    count: Int,
    onPlay: () -> Unit,
    onAddFiles: () -> Unit,
    onAddFolder: () -> Unit,
    onRename: () -> Unit,
    onMove: (Int) -> Unit,
    onRemove: () -> Unit,
) {
    Row(
        Modifier.fillMaxWidth().padding(start = 16.dp, end = 4.dp, top = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(season.title, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
            Text(
                "${episodes.size} episode${if (episodes.size == 1) "" else "s"} · ${episodes.count { it.progress.completed }} watched",
                style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        OverflowMenu(
            listOf(
                MenuEntry("Play this season", enabled = episodes.isNotEmpty(), onClick = onPlay),
                MenuEntry("Add files to this season…", dividerBefore = true, onClick = onAddFiles),
                MenuEntry("Add folder to this season…", onClick = onAddFolder),
                MenuEntry("Rename / renumber…", dividerBefore = true, onClick = onRename),
                MenuEntry("Move season up", enabled = index > 0) { onMove(-1) },
                MenuEntry("Move season down", enabled = index < count - 1) { onMove(+1) },
                MenuEntry("Remove season from library…", destructive = true, dividerBefore = true, onClick = onRemove),
            ),
        )
    }
    HorizontalDivider(Modifier.padding(horizontal = 16.dp), color = LocalFlixColors.Border)
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun EpisodeRow(
    e: Episode,
    index: Int,
    selected: Boolean,
    selecting: Boolean,
    onClick: () -> Unit,
    onLongClick: () -> Unit,
    menu: List<MenuEntry>,
) {
    val bg = if (selected) LocalFlixColors.Accent.copy(alpha = 0.22f) else Color.Transparent
    Row(
        Modifier.fillMaxWidth().background(bg).combinedClickable(onClick = onClick, onLongClick = onLongClick).padding(start = 16.dp, end = 4.dp, top = 8.dp, bottom = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            if (e.episodeNumber > 0) e.episodeNumber.toString() else "${index + 1}",
            Modifier.width(32.dp), color = LocalFlixColors.TextMuted, fontWeight = FontWeight.SemiBold,
        )
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
            Text(
                e.title, fontWeight = FontWeight.Medium, maxLines = 2, overflow = TextOverflow.Ellipsis,
                color = if (e.file.missing) LocalFlixColors.TextMuted else LocalFlixColors.Text,
            )
            if (e.file.missing) {
                Text("File missing — use the menu to locate it", style = MaterialTheme.typography.bodySmall, color = LocalFlixColors.Warning)
            } else {
                Text(e.file.label, style = MaterialTheme.typography.bodySmall, color = LocalFlixColors.TextMuted, maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
            if (e.progress.started()) {
                ProgressLine(e.progress.fraction().toFloat(), Modifier.padding(top = 4.dp))
            }
        }
        when {
            e.file.missing -> AppIcon(AppIcons.Warning, "File missing", Modifier.padding(horizontal = 6.dp), LocalFlixColors.Warning, 20.dp)
            e.progress.completed -> AppIcon(AppIcons.CheckCircle, "Watched", Modifier.padding(horizontal = 6.dp), LocalFlixColors.Success, 20.dp)
        }
        if (!selecting) OverflowMenu(menu) else Spacer(Modifier.width(48.dp))
    }
}

// ---------------------------------------------------------------------- reordering

private sealed interface Row {
    val key: String

    data class Header(val season: Season) : Row {
        override val key get() = "s${season.id}"
    }

    data class Item(val episode: Episode) : Row {
        override val key get() = "e${episode.id}"
    }
}

private fun flatten(seasons: List<Season>, episodes: List<Episode>): List<Row> =
    seasons.flatMap { s -> listOf<Row>(Row.Header(s)) + episodes.filter { it.seasonId == s.id }.map { Row.Item(it) } }

/** Takes the episode at [from] out and puts it at [to]; the first row must stay a season header. Null = no change. */
private fun moveRow(rows: List<Row>, from: Int, to: Int): List<Row>? {
    if (rows[from] !is Row.Item) return null
    val target = to.coerceIn(1, rows.size - 1)
    if (target == from) return null
    val list = rows.toMutableList()
    val row = list.removeAt(from)
    list.add(target.coerceAtMost(list.size), row)
    return list
}

private fun toLayout(rows: List<Row>): List<SeasonLayout> {
    val out = ArrayList<Pair<Id, MutableList<Id>>>()
    for (r in rows) {
        when (r) {
            is Row.Header -> out += r.season.id to mutableListOf()
            is Row.Item -> out.last().second += r.episode.id
        }
    }
    return out.map { SeasonLayout(it.first, it.second) }
}

private val ReorderRowHeight = 64.dp

@Composable
private fun ReorderList(
    seasons: List<Season>,
    episodes: List<Episode>,
    onApply: (List<SeasonLayout>) -> Unit,
    onMoveSeason: (Id, Int) -> Unit,
) {
    var rows by remember(seasons, episodes) { mutableStateOf(flatten(seasons, episodes)) }
    var draggingKey by remember { mutableStateOf<String?>(null) }
    val dragOffset = remember { mutableFloatStateOf(0f) }
    val rowPx = with(LocalDensity.current) { ReorderRowHeight.toPx() }
    val currentRows by rememberUpdatedState(rows)

    fun commit(newRows: List<Row>) {
        rows = newRows
        onApply(toLayout(newRows))
    }

    Column(Modifier.fillMaxSize()) {
        Text(
            "Drag the handle, or use the arrows. Dropping an episode under another season's title moves it there.",
            Modifier.padding(horizontal = 16.dp, vertical = 8.dp), style = MaterialTheme.typography.bodySmall, color = LocalFlixColors.TextMuted,
        )
        LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(bottom = 32.dp)) {
            itemsIndexed(rows, key = { _, r -> r.key }) { index, row ->
                val dragging = draggingKey == row.key
                val shape = RoundedCornerShape(10.dp)
                Row(
                    Modifier
                        .fillMaxWidth()
                        .height(ReorderRowHeight)
                        .offset { IntOffset(0, if (dragging) dragOffset.floatValue.roundToInt() else 0) }
                        .then(if (dragging) Modifier.shadow(8.dp, shape).alpha(0.95f) else Modifier)
                        .background(if (row is Row.Header) LocalFlixColors.Surface else LocalFlixColors.Background, shape)
                        .padding(horizontal = 8.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    when (row) {
                        is Row.Header -> {
                            val si = seasons.indexOfFirst { it.id == row.season.id }
                            Text(row.season.title, Modifier.weight(1f).padding(start = 8.dp), fontWeight = FontWeight.Bold)
                            AppIconButton(AppIcons.ArrowUpward, "Move season up", { onMoveSeason(row.season.id, si - 1) }, tint = if (si > 0) LocalFlixColors.Text else LocalFlixColors.Border)
                            AppIconButton(AppIcons.ArrowDownward, "Move season down", { onMoveSeason(row.season.id, si + 1) }, tint = if (si < seasons.size - 1) LocalFlixColors.Text else LocalFlixColors.Border)
                        }
                        is Row.Item -> {
                            Box(
                                Modifier
                                    .size(48.dp)
                                    .pointerInput(row.key) {
                                        detectDragGestures(
                                            onDragStart = { draggingKey = row.key; dragOffset.floatValue = 0f },
                                            onDrag = { change, amount ->
                                                change.consume()
                                                dragOffset.floatValue += amount.y
                                            },
                                            onDragEnd = {
                                                val list = currentRows
                                                val from = list.indexOfFirst { it.key == row.key }
                                                val steps = (dragOffset.floatValue / rowPx).roundToInt()
                                                draggingKey = null
                                                dragOffset.floatValue = 0f
                                                if (from >= 0 && steps != 0) moveRow(list, from, from + steps)?.let(::commit)
                                            },
                                            onDragCancel = { draggingKey = null; dragOffset.floatValue = 0f },
                                        )
                                    },
                                contentAlignment = Alignment.Center,
                            ) { AppIcon(AppIcons.DragHandle, "Drag to reorder", tint = LocalFlixColors.TextMuted) }
                            Column(Modifier.weight(1f)) {
                                Text(row.episode.title, maxLines = 1, overflow = TextOverflow.Ellipsis, fontWeight = FontWeight.Medium)
                                Text(episodeLabel(row.episode).takeIf { it != row.episode.title }.orEmpty().ifEmpty { row.episode.file.label },
                                    style = MaterialTheme.typography.bodySmall, color = LocalFlixColors.TextMuted, maxLines = 1, overflow = TextOverflow.Ellipsis)
                            }
                            AppIconButton(AppIcons.ArrowUpward, "Move up", { moveRow(rows, index, index - 1)?.let(::commit) })
                            AppIconButton(AppIcons.ArrowDownward, "Move down", { moveRow(rows, index, index + 1)?.let(::commit) })
                        }
                    }
                }
            }
        }
    }
}
