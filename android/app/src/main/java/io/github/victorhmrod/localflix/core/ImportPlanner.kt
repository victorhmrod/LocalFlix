package io.github.victorhmrod.localflix.core

import java.util.TreeMap

/** A video found by the file picker or while walking a granted folder. */
data class ScannedFile(
    val uri: String,
    val docKey: String, // "authority/documentId": the same file has the same key however it was granted
    val displayName: String,
    val relativePath: String, // inside the granted folder, '/'-separated; just the name for single files
    val folderChain: List<String>, // nearest first: parent folder, ..., the folder the user picked
    val sizeBytes: Long,
    val mtimeMs: Long,
    val treeUri: String, // the granted folder, "" for single files
) {
    /** Text used to order files naturally. */
    val sortKey: String get() = relativePath.ifEmpty { displayName }
}

data class PlanOptions(
    val detectSeasonFromNames: Boolean = true,
    val detectSeasonFromFolders: Boolean = true,
)

/** One file offered for import, already analysed by the filename parser. */
data class ImportItem(
    val file: ScannedFile,
    val title: String, // display title
    val episodeNumber: Int = 0,
    val seasonNumber: Int = -1, // -1 = no season detected, use the fallback season
    val groupIndex: Int = 0, // keeps the planner's ordering stable
)

data class DuplicateInfo(val label: String, val existingAs: String)

object ImportPlanner {
    val VIDEO_EXTENSIONS: Set<String> = setOf(
        "mp4", "mkv", "avi", "mov", "webm", "m4v", "wmv", "flv", "ts", "m2ts", "mpg", "mpeg", "ogv", "3gp", "divx", "vob", "mts",
    )

    /** Providers often report generic MIME types for containers like mkv, so the extension counts as well. */
    fun isVideo(displayName: String, mimeType: String?): Boolean =
        (mimeType?.startsWith("video/") == true) || extensionOf(displayName) in VIDEO_EXTENSIONS

    /**
     * Orders scanned files into the playback order a new batch of episodes starts with: grouped by detected season,
     * inside a group by episode number when every file has one, otherwise by natural file-name order.
     */
    fun planEpisodes(files: List<ScannedFile>, options: PlanOptions = PlanOptions()): List<ImportItem> {
        val groups = TreeMap<Int, MutableList<ImportItem>>() // season number (-1 = none) -> items, ascending
        for (f in files) {
            val parsed = parseFileName(f.displayName)
            var season = -1
            if (options.detectSeasonFromNames && parsed.season >= 0) {
                season = parsed.season
            } else if (options.detectSeasonFromFolders) {
                // The nearest folder that names a season wins ("Season 2/Extras/ep.mkv" -> 2).
                for (folder in f.folderChain) {
                    val s = seasonFromFolderName(folder)
                    if (s >= 0) {
                        season = s
                        break
                    }
                }
            }
            groups.getOrPut(season) { mutableListOf() } += ImportItem(f, parsed.title, parsed.episode, season)
        }

        val plan = ArrayList<ImportItem>()
        for (items in groups.values) {
            val allNumbered = items.all { it.episodeNumber > 0 }
            val sorted = items.sortedWith { a, b ->
                if (allNumbered && a.episodeNumber != b.episodeNumber) {
                    a.episodeNumber.compareTo(b.episodeNumber)
                } else {
                    naturalCompare(a.file.sortKey, b.file.sortKey)
                }
            }
            plan += sorted
        }
        return plan.mapIndexed { i, item -> item.copy(groupIndex = i) }
    }

    fun planMovies(files: List<ScannedFile>): List<ImportItem> =
        files
            .map { f ->
                val base = baseName(f.displayName)
                ImportItem(f, prettifyTitle(base).ifEmpty { base })
            }
            .sortedWith { a, b -> naturalCompare(a.file.sortKey, b.file.sortKey) }
            .mapIndexed { i, item -> item.copy(groupIndex = i) }
}
