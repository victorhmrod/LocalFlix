package io.github.victorhmrod.localflix.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import io.github.victorhmrod.localflix.core.ContinueItem
import io.github.victorhmrod.localflix.core.LibraryCard
import io.github.victorhmrod.localflix.core.formatClock
import io.github.victorhmrod.localflix.ui.theme.LocalFlixColors

/** A show or movie tile: cover, title and watched share. */
@Composable
fun PosterCard(card: LibraryCard, modifier: Modifier = Modifier, onClick: () -> Unit, menu: List<MenuEntry> = emptyList()) {
    Column(modifier.clip(RoundedCornerShape(12.dp)).clickable(onClick = onClick)) {
        Box {
            CoverImage(card.imageFile, card.title, Modifier.fillMaxWidth().aspectRatio(2f / 3f))
            if (card.allWatched) {
                Box(
                    Modifier.align(Alignment.TopEnd).padding(6.dp).size(24.dp).background(LocalFlixColors.Success, CircleShape),
                    contentAlignment = Alignment.Center,
                ) { AppIcon(AppIcons.Check, "Watched", tint = Color.White, size = 16.dp) }
            } else if (card.missing) {
                Box(
                    Modifier.align(Alignment.TopEnd).padding(6.dp).size(24.dp).background(LocalFlixColors.Warning, CircleShape),
                    contentAlignment = Alignment.Center,
                ) { AppIcon(AppIcons.Warning, "File missing", tint = Color.Black, size = 14.dp) }
            }
            if (menu.isNotEmpty()) {
                Box(Modifier.align(Alignment.TopStart)) { OverflowMenu(menu, tint = Color.White) }
            }
            if (card.progress > 0.0 && !card.allWatched) {
                ProgressLine(card.progress.toFloat(), Modifier.align(Alignment.BottomCenter).padding(horizontal = 8.dp, vertical = 6.dp))
            }
        }
        Text(
            card.title,
            style = MaterialTheme.typography.bodyMedium,
            fontWeight = FontWeight.SemiBold,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.padding(top = 8.dp, start = 2.dp, end = 2.dp),
        )
        Text(
            card.subtitle,
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.padding(start = 2.dp, end = 2.dp, bottom = 4.dp),
        )
    }
}

/** "Continue Watching" tile: a wide cover with a resume point and progress. */
@Composable
fun ContinueCard(item: ContinueItem, onResume: () -> Unit, menu: List<MenuEntry>, modifier: Modifier = Modifier) {
    Column(modifier.width(260.dp).clip(RoundedCornerShape(12.dp)).background(LocalFlixColors.Surface).clickable(onClick = onResume)) {
        Box {
            CoverImage(item.coverFile, item.title, Modifier.fillMaxWidth().aspectRatio(16f / 9f), cornerRadius = 0.dp)
            Box(
                Modifier.align(Alignment.Center).size(44.dp).background(Color.Black.copy(alpha = 0.55f), CircleShape),
                contentAlignment = Alignment.Center,
            ) { AppIcon(AppIcons.PlayArrow, "Resume", tint = Color.White, size = 28.dp) }
            if (item.missing) {
                Box(
                    Modifier.align(Alignment.TopEnd).padding(8.dp).size(24.dp).background(LocalFlixColors.Warning, CircleShape),
                    contentAlignment = Alignment.Center,
                ) { AppIcon(AppIcons.Warning, "File missing", tint = Color.Black, size = 14.dp) }
            }
            ProgressLine(item.fraction().toFloat(), Modifier.align(Alignment.BottomCenter))
        }
        Row(Modifier.padding(start = 12.dp, top = 8.dp, bottom = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                Text(item.title, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
                if (item.subtitle.isNotEmpty()) {
                    Text(item.subtitle, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 1, overflow = TextOverflow.Ellipsis)
                }
                Text(
                    if (item.positionMs > 0) "Resume at ${formatClock(item.positionMs)}" else "Up next",
                    style = MaterialTheme.typography.bodySmall,
                    color = LocalFlixColors.Accent,
                )
            }
            OverflowMenu(menu)
        }
    }
}
