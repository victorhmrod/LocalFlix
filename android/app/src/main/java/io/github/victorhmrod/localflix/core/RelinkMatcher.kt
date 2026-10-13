package io.github.victorhmrod.localflix.core

/** A library file that can no longer be opened. */
data class MissingRef(
    val mediaFileId: Id,
    val docKey: String,
    val relativePath: String,
    val displayName: String,
    val sizeBytes: Long,
)

data class RelinkProposal(val mediaFileId: Id, val oldLabel: String, val newFile: ScannedFile)

/**
 * Finds, among the files of a folder the user just granted, the ones that missing library entries moved to.
 * Matching never guesses: the file name must be the same, the candidate with the longest common trailing path
 * wins, and ties are broken by file size; anything still ambiguous is left for the user to locate by hand.
 * This is the Android counterpart of "relink the whole moved folder tree" on the desktop.
 */
object RelinkMatcher {
    private val separators = Regex("[/:\\\\]")

    /** Path parts used for comparison: the provider's document id when it embeds a path, else the relative path. */
    fun components(docKey: String, relativePath: String, displayName: String): List<String> {
        val docId = docKey.substringAfter('/', "")
        // Only ids that embed a folder path are comparable (primary:Movies/Show/ep1.mkv); opaque ids (msf:1234) are not.
        if (docId.contains('/')) return docId.split(separators).filter { it.isNotEmpty() }
        val fromRelative = relativePath.split('/').filter { it.isNotEmpty() }
        return fromRelative.ifEmpty { listOf(displayName) }
    }

    private fun commonTail(a: List<String>, b: List<String>): Int {
        var n = 0
        while (n < a.size && n < b.size && a[a.size - 1 - n].equals(b[b.size - 1 - n], ignoreCase = true)) n++
        return n
    }

    fun propose(
        missing: List<MissingRef>,
        candidates: List<ScannedFile>,
        knownDocKeys: Set<String>, // files already in the library; they cannot become somebody else's replacement
    ): List<RelinkProposal> {
        val byName = candidates.groupBy { it.displayName.lowercase() }
        val used = HashSet<String>()
        data class Pick(val m: MissingRef, val c: ScannedFile, val score: Int, val sameSize: Boolean)

        val picks = ArrayList<Pick>()
        for (m in missing) {
            val pool = byName[m.displayName.lowercase()].orEmpty()
                .filter { it.docKey == m.docKey || it.docKey !in knownDocKeys }
            if (pool.isEmpty()) continue
            val oldParts = components(m.docKey, m.relativePath, m.displayName)
            val scored = pool.map { c ->
                Pick(m, c, commonTail(oldParts, components(c.docKey, c.relativePath, c.displayName)), c.sizeBytes == m.sizeBytes && m.sizeBytes > 0)
            }
            val best = scored.maxWith(compareBy<Pick> { it.score }.thenBy { it.sameSize })
            val rivals = scored.filter { it.score == best.score && it.sameSize == best.sameSize }
            if (rivals.size == 1) picks += best
        }
        // Strongest matches first, so a weaker one cannot steal a file that fits another entry better.
        val out = ArrayList<RelinkProposal>()
        for (p in picks.sortedWith(compareByDescending<Pick> { it.score }.thenByDescending { it.sameSize })) {
            if (!used.add(p.c.docKey)) continue
            out += RelinkProposal(p.m.mediaFileId, p.m.relativePath.ifEmpty { p.m.displayName }, p.c)
        }
        return out
    }
}
