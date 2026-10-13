package io.github.victorhmrod.localflix.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import io.github.victorhmrod.localflix.app.PickedSource
import io.github.victorhmrod.localflix.core.CardKind
import io.github.victorhmrod.localflix.core.ContextType
import io.github.victorhmrod.localflix.core.ContinueItem
import io.github.victorhmrod.localflix.core.Id
import io.github.victorhmrod.localflix.core.LibraryCard
import io.github.victorhmrod.localflix.core.MediaKind
import io.github.victorhmrod.localflix.core.QueueContext
import io.github.victorhmrod.localflix.core.StartMode
import io.github.victorhmrod.localflix.ui.AppIcons
import io.github.victorhmrod.localflix.ui.ConfirmDialog
import io.github.victorhmrod.localflix.ui.ContinueCard
import io.github.victorhmrod.localflix.ui.EmptyState
import io.github.victorhmrod.localflix.ui.LocalActions
import io.github.victorhmrod.localflix.ui.MenuEntry
import io.github.victorhmrod.localflix.ui.PosterCard
import io.github.victorhmrod.localflix.ui.PrimaryButton
import io.github.victorhmrod.localflix.ui.SecondaryButton
import io.github.victorhmrod.localflix.ui.SectionTitle
import io.github.victorhmrod.localflix.ui.TextPromptDialog
import io.github.victorhmrod.localflix.ui.libraryState

@Composable
fun ScreenHeader(title: String, modifier: Modifier = Modifier, trailing: @Composable () -> Unit = {}) {
    Row(
        modifier.fillMaxWidth().statusBarsPadding().padding(start = 16.dp, end = 8.dp, top = 12.dp, bottom = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(title, style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold, modifier = Modifier.weight(1f))
        trailing()
    }
}

// ---------------------------------------------------------------------- home

@Composable
fun HomeScreen(onOpenShow: (Id) -> Unit, onOpenMovie: (Id) -> Unit, onShowsTab: () -> Unit, onMoviesTab: () -> Unit) {
    val actions = LocalActions.current
    val cont by libraryState(emptyList<ContinueItem>()) { continueWatching() }
    val recent by libraryState(emptyList<LibraryCard>()) { recentlyAdded() }
    val hasContent by libraryState<Boolean?>(null) { showSummaries().isNotEmpty() || movies().isNotEmpty() }

    Column(Modifier.fillMaxSize()) {
        ScreenHeader("LocalFlix")
        when {
            hasContent == null -> Box(Modifier.fillMaxSize())
            hasContent == false -> EmptyState(
                "Your library is empty",
                "Add the videos you already have. They stay where they are: LocalFlix never copies, moves or deletes them.",
            ) {
                Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    PrimaryButton("Create a show", onShowsTab, Modifier.fillMaxWidth(), AppIcons.Tv)
                    SecondaryButton("Add movies", onMoviesTab, Modifier.fillMaxWidth(), AppIcons.Movie)
                }
            }
            else -> LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(bottom = 24.dp)) {
                if (cont.isNotEmpty()) {
                    item { SectionTitle("Continue watching") }
                    item {
                        LazyRow(contentPadding = PaddingValues(horizontal = 16.dp), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                            items(cont, key = { "${it.kind}-${it.showId}-${it.movieId}" }) { item ->
                                val ctx = continueContext(item)
                                ContinueCard(
                                    item,
                                    onResume = { actions.play(ctx, item.mediaFileId, StartMode.RESUME) },
                                    menu = buildList {
                                        add(MenuEntry(if (item.positionMs > 0) "Resume" else "Play") { actions.play(ctx, item.mediaFileId, StartMode.RESUME) })
                                        if (item.positionMs > 0) add(MenuEntry("Start from the beginning") { actions.play(ctx, item.mediaFileId, StartMode.FROM_START) })
                                        if (item.kind == MediaKind.EPISODE) add(MenuEntry("Open show") { onOpenShow(item.showId) }) else add(MenuEntry("Open movie") { onOpenMovie(item.movieId) })
                                        if (item.missing) add(MenuEntry("Locate missing file…", dividerBefore = true) { actions.locateFile(item.mediaFileId) })
                                    },
                                )
                            }
                        }
                    }
                }
                item { SectionTitle("Recently added") }
                item {
                    if (recent.isEmpty()) {
                        Text("Nothing here yet.", Modifier.padding(horizontal = 16.dp), color = MaterialTheme.colorScheme.onSurfaceVariant)
                    } else {
                        LazyRow(contentPadding = PaddingValues(horizontal = 16.dp), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                            items(recent, key = { "${it.kind}-${it.id}" }) { card ->
                                PosterCard(card, Modifier.width(130.dp), onClick = { if (card.kind == CardKind.SHOW) onOpenShow(card.id) else onOpenMovie(card.id) })
                            }
                        }
                    }
                }
            }
        }
    }
}

private fun continueContext(item: ContinueItem) =
    if (item.kind == MediaKind.EPISODE) QueueContext(ContextType.SHOW, item.showId) else QueueContext(ContextType.MOVIE, item.movieId)

// ---------------------------------------------------------------------- shows

@Composable
fun ShowsScreen(onOpenShow: (Id) -> Unit) {
    val actions = LocalActions.current
    val summaries by libraryState(null as List<io.github.victorhmrod.localflix.core.ShowSummary>?) { showSummaries() }
    var creating by remember { mutableStateOf(false) }
    var removing by remember { mutableStateOf<Pair<Id, String>?>(null) }

    Box(Modifier.fillMaxSize()) {
        Column(Modifier.fillMaxSize()) {
            ScreenHeader("TV shows")
            val list = summaries
            when {
                list == null -> Box(Modifier.fillMaxSize())
                list.isEmpty() -> EmptyState("No shows yet", "Create a show, then add its episodes from files or a whole folder.") {
                    PrimaryButton("New show", { creating = true }, icon = AppIcons.Add)
                }
                else -> LazyVerticalGrid(
                    GridCells.Adaptive(130.dp),
                    Modifier.fillMaxSize(),
                    contentPadding = PaddingValues(start = 16.dp, end = 16.dp, top = 8.dp, bottom = 96.dp),
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                    verticalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    items(list, key = { it.show.id }) { s ->
                        val card = LibraryCard(
                            CardKind.SHOW, s.show.id, s.show.title,
                            if (s.episodeCount == 1) "1 episode" else "${s.episodeCount} episodes",
                            s.show.coverFile, s.show.updatedAt,
                            if (s.episodeCount > 0) s.completedCount.toDouble() / s.episodeCount else 0.0,
                            s.episodeCount > 0 && s.completedCount >= s.episodeCount,
                        )
                        PosterCard(
                            card, onClick = { onOpenShow(s.show.id) },
                            menu = listOf(
                                MenuEntry("Open") { onOpenShow(s.show.id) },
                                MenuEntry("Remove from library…", destructive = true, dividerBefore = true) { removing = s.show.id to s.show.title },
                            ),
                        )
                    }
                }
            }
        }
        if (summaries?.isNotEmpty() == true) {
            FloatingActionButton({ creating = true }, Modifier.align(Alignment.BottomEnd).padding(16.dp), containerColor = MaterialTheme.colorScheme.primary) {
                androidx.compose.material3.Icon(AppIcons.Add, "New show", tint = androidx.compose.ui.graphics.Color.White)
            }
        }
    }

    if (creating) {
        TextPromptDialog("New show", "Title", "", "Create", onConfirm = { title ->
            creating = false
            actions.ioResult({ createShow(title) }) { id -> if (id != 0L) onOpenShow(id) }
        }, onDismiss = { creating = false })
    }
    removing?.let { (id, title) ->
        ConfirmDialog(
            "Remove show?",
            "\"$title\" and its episodes are removed from the library, with their order and progress. Your video files are not touched.",
            "Remove", destructive = true,
            onConfirm = { removing = null; actions.io { deleteShow(id) } },
            onDismiss = { removing = null },
        )
    }
}

// ---------------------------------------------------------------------- movies

@Composable
fun MoviesScreen(onOpenMovie: (Id) -> Unit) {
    val actions = LocalActions.current
    val movies by libraryState(null as List<io.github.victorhmrod.localflix.core.Movie>?) { movies() }
    var addMenu by remember { mutableStateOf(false) }
    var removing by remember { mutableStateOf<io.github.victorhmrod.localflix.core.Movie?>(null) }

    fun addFiles() = actions.pickers.videos { uris -> if (uris.isNotEmpty()) actions.importMovies(PickedSource.Files(uris)) }

    fun addFolder() = actions.pickers.folder { uri -> if (uri != null) actions.importMovies(PickedSource.Folder(uri)) }

    Box(Modifier.fillMaxSize()) {
        Column(Modifier.fillMaxSize()) {
            ScreenHeader("Movies")
            val list = movies
            when {
                list == null -> Box(Modifier.fillMaxSize())
                list.isEmpty() -> EmptyState("No movies yet", "Pick movie files, or a folder that holds them. Each file becomes one movie.") {
                    Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                        PrimaryButton("Add files", ::addFiles, Modifier.fillMaxWidth(), AppIcons.Movie)
                        SecondaryButton("Add folder", ::addFolder, Modifier.fillMaxWidth(), AppIcons.Folder)
                    }
                }
                else -> LazyVerticalGrid(
                    GridCells.Adaptive(130.dp),
                    Modifier.fillMaxSize(),
                    contentPadding = PaddingValues(start = 16.dp, end = 16.dp, top = 8.dp, bottom = 96.dp),
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                    verticalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    items(list, key = { it.id }) { m ->
                        val card = LibraryCard(
                            CardKind.MOVIE, m.id, m.title, if (m.progress.started()) "Resume ${io.github.victorhmrod.localflix.core.formatClock(m.progress.positionMs)}" else "Movie",
                            m.posterFile, m.createdAt, m.progress.fraction(), m.progress.completed, m.file.missing,
                        )
                        PosterCard(
                            card, onClick = { onOpenMovie(m.id) },
                            menu = buildList {
                                add(MenuEntry(if (m.progress.started()) "Resume" else "Play") { actions.play(QueueContext(ContextType.MOVIE, m.id), m.file.id, StartMode.AUTO) })
                                add(MenuEntry("Open") { onOpenMovie(m.id) })
                                if (m.file.missing) add(MenuEntry("Locate missing file…") { actions.locateFile(m.file.id) })
                                add(MenuEntry("Remove from library…", destructive = true, dividerBefore = true) { removing = m })
                            },
                        )
                    }
                }
            }
        }
        if (movies?.isNotEmpty() == true) {
            Box(Modifier.align(Alignment.BottomEnd).padding(16.dp)) {
                FloatingActionButton({ addMenu = true }, containerColor = MaterialTheme.colorScheme.primary) {
                    androidx.compose.material3.Icon(AppIcons.Add, "Add movies", tint = androidx.compose.ui.graphics.Color.White)
                }
                DropdownMenu(addMenu, { addMenu = false }) {
                    DropdownMenuItem(text = { Text("Add files…") }, onClick = { addMenu = false; addFiles() })
                    DropdownMenuItem(text = { Text("Add folder…") }, onClick = { addMenu = false; addFolder() })
                }
            }
        }
    }

    removing?.let { m ->
        ConfirmDialog(
            "Remove movie?",
            "\"${m.title}\" is removed from the library, with its progress. The video file is not touched.",
            "Remove", destructive = true,
            onConfirm = { removing = null; actions.io { deleteMovie(m.id) } },
            onDismiss = { removing = null },
        )
    }
}
