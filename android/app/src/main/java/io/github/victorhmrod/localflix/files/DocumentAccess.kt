package io.github.victorhmrod.localflix.files

import android.content.ContentResolver
import android.content.Intent
import android.net.Uri
import android.provider.DocumentsContract
import io.github.victorhmrod.localflix.core.ImportPlanner
import io.github.victorhmrod.localflix.core.ScannedFile

/** Result of walking a granted folder or reading picked files: the videos found, and what could not be used. */
class ScanResult(
    val files: List<ScannedFile>,
    val rejected: List<String>, // "name — reason"
    val cancelled: Boolean = false,
)

/**
 * Everything LocalFlix does with files goes through the system's Storage Access Framework: the user grants a single
 * file or a whole folder, we keep the grant (persistable permission) and the content URI, and the player streams
 * straight from it. Nothing is ever copied, moved or deleted.
 */
class DocumentAccess(private val resolver: ContentResolver) {
    // ------------------------------------------------------------------ permissions

    /** Keeps read access to [uri] across restarts. Returns false if the provider does not allow persisting. */
    fun takePersistableRead(uri: Uri): Boolean = try {
        resolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        true
    } catch (_: SecurityException) {
        false
    }

    /** Persisted grants are capped per app, so grants nothing in the library refers to any more are given back. */
    fun releaseUnused(inUse: (Uri) -> Boolean) {
        for (p in resolver.persistedUriPermissions) {
            if (!inUse(p.uri)) {
                try {
                    resolver.releasePersistableUriPermission(p.uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
                } catch (_: SecurityException) {
                    // already gone
                }
            }
        }
    }

    fun persistedGrantCount(): Int = resolver.persistedUriPermissions.size

    // ------------------------------------------------------------------ existence

    /** True when the document can still be reached with the access we hold (not moved, deleted or revoked). */
    fun exists(uri: String): Boolean = try {
        resolver.query(Uri.parse(uri), arrayOf(DocumentsContract.Document.COLUMN_DOCUMENT_ID), null, null, null)?.use { it.count > 0 } == true
    } catch (_: Exception) {
        false // SecurityException (grant lost), IllegalArgumentException, FileNotFound... all mean "cannot open it"
    }

    // ------------------------------------------------------------------ identity

    /** Same file => same key, however it was granted ("authority/documentId"). */
    fun docKey(uri: Uri): String {
        val authority = uri.authority.orEmpty()
        val docId = try {
            DocumentsContract.getDocumentId(uri)
        } catch (_: IllegalArgumentException) {
            uri.lastPathSegment.orEmpty()
        }
        return "$authority/$docId"
    }

    // ------------------------------------------------------------------ single documents

    private val docColumns = arrayOf(
        DocumentsContract.Document.COLUMN_DISPLAY_NAME,
        DocumentsContract.Document.COLUMN_MIME_TYPE,
        DocumentsContract.Document.COLUMN_SIZE,
        DocumentsContract.Document.COLUMN_LAST_MODIFIED,
    )

    /** A document the user picked directly (no folder around it that we may look into). */
    fun describeDocument(uri: Uri): Pair<ScannedFile?, String?> {
        return try {
            resolver.query(uri, docColumns, null, null, null)?.use { c ->
                if (!c.moveToFirst()) return null to "not found"
                val name = c.getString(0) ?: uri.lastPathSegment.orEmpty()
                val mime = c.getString(1)
                val size = if (c.isNull(2)) 0L else c.getLong(2)
                val mtime = if (c.isNull(3)) 0L else c.getLong(3)
                when {
                    mime == DocumentsContract.Document.MIME_TYPE_DIR -> null to "$name — this is a folder"
                    size <= 0 && !c.isNull(2) -> null to "$name — file is empty"
                    else -> ScannedFile(
                        uri = uri.toString(), docKey = docKey(uri), displayName = name, relativePath = name,
                        folderChain = emptyList(), sizeBytes = size, mtimeMs = mtime, treeUri = "",
                    ) to null
                }
            } ?: (null to "not available")
        } catch (e: Exception) {
            null to "${uri.lastPathSegment.orEmpty()} — ${e.message ?: "cannot be read"}"
        }
    }

    /** Videos among the picked documents. Non-video picks are reported, not silently dropped. */
    fun scanDocuments(uris: List<Uri>, isCancelled: () -> Boolean = { false }): ScanResult {
        val files = ArrayList<ScannedFile>()
        val rejected = ArrayList<String>()
        val seen = HashSet<String>()
        for (uri in uris) {
            if (isCancelled()) return ScanResult(files, rejected, cancelled = true)
            val (file, problem) = describeDocument(uri)
            if (file == null) {
                rejected += problem ?: uri.toString()
            } else if (!seen.add(file.docKey)) {
                continue
            } else if (!ImportPlanner.isVideo(file.displayName, resolver.getType(uri))) {
                rejected += "${file.displayName} — not a video file"
            } else {
                files += file
            }
        }
        return ScanResult(files, rejected)
    }

    // ------------------------------------------------------------------ folders

    /** Display name of a granted folder, for the folder chain used to guess seasons. */
    fun treeName(treeUri: Uri): String {
        val root = DocumentsContract.buildDocumentUriUsingTree(treeUri, DocumentsContract.getTreeDocumentId(treeUri))
        return try {
            resolver.query(root, arrayOf(DocumentsContract.Document.COLUMN_DISPLAY_NAME), null, null, null)?.use {
                if (it.moveToFirst()) it.getString(0) else null
            }
        } catch (_: Exception) {
            null
        } ?: ""
    }

    /**
     * Walks a granted folder (sub-folders included) and returns every video in it. One provider query per folder,
     * not per file, so thousands of files scan quickly.
     */
    fun scanTree(treeUri: Uri, onProgress: (Int) -> Unit = {}, isCancelled: () -> Boolean = { false }): ScanResult {
        val files = ArrayList<ScannedFile>()
        val rejected = ArrayList<String>()
        val rootName = treeName(treeUri)
        val rootDocId = DocumentsContract.getTreeDocumentId(treeUri)
        var cancelled = false

        fun walk(parentDocId: String, relativeDir: List<String>) {
            if (cancelled) return
            val children = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, parentDocId)
            val cols = arrayOf(
                DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE,
                DocumentsContract.Document.COLUMN_SIZE,
                DocumentsContract.Document.COLUMN_LAST_MODIFIED,
            )
            val subDirs = ArrayList<Pair<String, String>>() // (docId, name)
            try {
                resolver.query(children, cols, null, null, null)?.use { c ->
                    while (c.moveToNext()) {
                        if (isCancelled()) {
                            cancelled = true
                            return
                        }
                        val docId = c.getString(0)
                        val name = c.getString(1) ?: continue
                        val mime = c.getString(2)
                        if (mime == DocumentsContract.Document.MIME_TYPE_DIR) {
                            if (!name.startsWith(".")) subDirs += docId to name
                            continue
                        }
                        if (name.startsWith("._") || !ImportPlanner.isVideo(name, mime)) continue
                        val size = if (c.isNull(3)) 0L else c.getLong(3)
                        val relative = (relativeDir + name).joinToString("/")
                        if (size <= 0 && !c.isNull(3)) {
                            rejected += "$relative — file is empty"
                            continue
                        }
                        val docUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, docId)
                        files += ScannedFile(
                            uri = docUri.toString(),
                            docKey = "${docUri.authority}/$docId",
                            displayName = name,
                            relativePath = relative,
                            folderChain = relativeDir.reversed() + rootName,
                            sizeBytes = size,
                            mtimeMs = if (c.isNull(4)) 0L else c.getLong(4),
                            treeUri = treeUri.toString(),
                        )
                        if (files.size % 25 == 0) onProgress(files.size)
                    }
                }
            } catch (e: Exception) {
                rejected += "${relativeDir.joinToString("/").ifEmpty { rootName }} — ${e.message ?: "cannot be read"}"
                return
            }
            for ((docId, name) in subDirs) walk(docId, relativeDir + name)
        }

        walk(rootDocId, emptyList())
        onProgress(files.size)
        return ScanResult(files, rejected, cancelled)
    }
}
