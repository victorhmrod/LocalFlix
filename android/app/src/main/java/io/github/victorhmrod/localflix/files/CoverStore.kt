package io.github.victorhmrod.localflix.files

import android.content.ContentResolver
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import java.io.File
import java.io.IOException
import java.util.UUID

/**
 * Covers and posters are the only things LocalFlix copies: the user's own picture of a show, scaled down, into the
 * app's private storage. (Videos are never copied.)
 */
class CoverStore(private val resolver: ContentResolver, private val dir: File) {
    /** Returns the stored file name, or null with a reason in [error]. */
    fun store(source: Uri, error: (String) -> Unit = {}): String? {
        return try {
            val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            resolver.openInputStream(source)?.use { BitmapFactory.decodeStream(it, null, bounds) }
            if (bounds.outWidth <= 0 || bounds.outHeight <= 0) {
                error("That file is not a readable image.")
                return null
            }
            var sample = 1
            while (bounds.outWidth / (sample * 2) >= MAX_SIDE || bounds.outHeight / (sample * 2) >= MAX_SIDE) sample *= 2
            val bitmap = resolver.openInputStream(source)?.use {
                BitmapFactory.decodeStream(it, null, BitmapFactory.Options().apply { inSampleSize = sample })
            }
            if (bitmap == null) {
                error("That file is not a readable image.")
                return null
            }
            if (!dir.isDirectory && !dir.mkdirs()) {
                error("Cannot create the covers folder.")
                return null
            }
            val name = UUID.randomUUID().toString() + ".jpg"
            File(dir, name).outputStream().use { bitmap.compress(Bitmap.CompressFormat.JPEG, 90, it) }
            bitmap.recycle()
            name
        } catch (e: IOException) {
            error("Cannot read the image: ${e.message}")
            null
        }
    }

    /** Decoded for display at roughly [targetPx] on its long side, or null if the file is gone/unreadable. */
    fun load(name: String, targetPx: Int): Bitmap? {
        val f = File(dir, name)
        if (!f.isFile) return null
        return try {
            val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            BitmapFactory.decodeFile(f.path, bounds)
            var sample = 1
            while (bounds.outWidth / (sample * 2) >= targetPx && bounds.outHeight / (sample * 2) >= targetPx) sample *= 2
            BitmapFactory.decodeFile(f.path, BitmapFactory.Options().apply { inSampleSize = sample })
        } catch (_: RuntimeException) {
            null
        }
    }

    private companion object {
        const val MAX_SIDE = 1280
    }
}
