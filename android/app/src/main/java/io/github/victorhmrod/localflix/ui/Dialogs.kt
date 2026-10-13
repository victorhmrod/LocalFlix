package io.github.victorhmrod.localflix.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import io.github.victorhmrod.localflix.core.Collection
import io.github.victorhmrod.localflix.core.CollectionItemType
import io.github.victorhmrod.localflix.core.Id
import io.github.victorhmrod.localflix.core.Movie
import io.github.victorhmrod.localflix.core.Show
import io.github.victorhmrod.localflix.ui.theme.LocalFlixColors

/** Name, description and picture of a show. */
@Composable
fun EditShowDialog(show: Show, onDismiss: () -> Unit) {
    val actions = LocalActions.current
    var title by remember { mutableStateOf(show.title) }
    var description by remember { mutableStateOf(show.description) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Edit show") },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(title, { title = it }, label = { Text("Title") }, singleLine = true, modifier = Modifier.fillMaxWidth())
                OutlinedTextField(description, { description = it }, label = { Text("Description") }, minLines = 3, modifier = Modifier.fillMaxWidth())
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    SecondaryButton("Choose cover…", { actions.chooseShowCover(show.id) }, icon = AppIcons.Image)
                    if (show.coverFile.isNotEmpty()) SecondaryButton("Remove", { actions.io { clearShowCover(show.id) } })
                }
            }
        },
        confirmButton = {
            TextButton({
                onDismiss()
                actions.io {
                    renameShow(show.id, title)
                    setShowDescription(show.id, description)
                }
            }, enabled = title.isNotBlank()) { Text("Save") }
        },
        dismissButton = { TextButton(onDismiss) { Text("Cancel") } },
    )
}

@Composable
fun EditMovieDialog(movie: Movie, onDismiss: () -> Unit) {
    val actions = LocalActions.current
    var title by remember { mutableStateOf(movie.title) }
    var description by remember { mutableStateOf(movie.description) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Edit movie") },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(title, { title = it }, label = { Text("Title") }, singleLine = true, modifier = Modifier.fillMaxWidth())
                OutlinedTextField(description, { description = it }, label = { Text("Description") }, minLines = 3, modifier = Modifier.fillMaxWidth())
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    SecondaryButton("Choose poster…", { actions.chooseMoviePoster(movie.id) }, icon = AppIcons.Image)
                    if (movie.posterFile.isNotEmpty()) SecondaryButton("Remove", { actions.io { clearMoviePoster(movie.id) } })
                }
            }
        },
        confirmButton = {
            TextButton({
                onDismiss()
                actions.io { updateMovie(movie.id, title, description) }
            }, enabled = title.isNotBlank()) { Text("Save") }
        },
        dismissButton = { TextButton(onDismiss) { Text("Cancel") } },
    )
}

/** Pick a collection (or make one) to put a show or movie into. */
@Composable
fun AddToCollectionDialog(type: CollectionItemType, itemId: Id, onDismiss: () -> Unit) {
    val actions = LocalActions.current
    val collections by libraryState(emptyList<Collection>()) { collections() }
    var creating by remember { mutableStateOf(false) }
    if (creating) {
        TextPromptDialog("New collection", "Name", "", "Create", onConfirm = { name ->
            creating = false
            onDismiss()
            actions.ioResult({
                val id = createCollection(name)
                if (id != 0L) addToCollection(id, type, itemId) else false
            }) { ok -> actions.message(if (ok) "Added to \"$name\"." else "Could not create the collection.") }
        }, onDismiss = { creating = false })
        return
    }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Add to collection") },
        text = {
            if (collections.isEmpty()) {
                Text("You have no collections yet.", color = MaterialTheme.colorScheme.onSurfaceVariant)
            } else {
                LazyColumn(Modifier.fillMaxWidth().height((56 * minOf(collections.size, 5)).dp)) {
                    items(collections, key = { it.id }) { c ->
                        Row(
                            Modifier.fillMaxWidth().clickable {
                                onDismiss()
                                actions.ioResult({ addToCollection(c.id, type, itemId) }) { ok ->
                                    actions.message(if (ok) "Added to \"${c.name}\"." else "Already in \"${c.name}\".")
                                }
                            }.padding(vertical = 14.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Text(c.name, Modifier.weight(1f), fontWeight = FontWeight.Medium)
                            Text("${c.entryCount}", color = LocalFlixColors.TextMuted)
                            Column(Modifier.width(4.dp)) {}
                        }
                    }
                }
            }
        },
        confirmButton = { TextButton({ creating = true }) { Text("New collection…") } },
        dismissButton = { TextButton(onDismiss) { Text("Cancel") } },
    )
}
