package io.github.victorhmrod.localflix.data

import android.content.Context
import androidx.room.Database
import androidx.room.Room
import androidx.room.RoomDatabase
import androidx.room.migration.Migration

/**
 * The library database. Schema changes always ship as explicit [Migration]s (never a destructive fallback), so a
 * user's order, progress and settings survive every update; the exported schemas live in `app/schemas`.
 */
@Database(
    entities = [
        MediaFileEntity::class,
        ShowEntity::class,
        SeasonEntity::class,
        EpisodeEntity::class,
        MovieEntity::class,
        CollectionEntity::class,
        CollectionEntryEntity::class,
        ProgressEntity::class,
        SettingEntity::class,
    ],
    version = 1,
    exportSchema = true,
)
abstract class LocalFlixDatabase : RoomDatabase() {
    abstract fun dao(): LibraryDao

    companion object {
        const val FILE_NAME = "library.db"

        /** Forward-only upgrade steps, in order. Empty while the schema is at version 1. */
        val MIGRATIONS: Array<Migration> = emptyArray()

        fun open(context: Context, name: String? = FILE_NAME): LocalFlixDatabase {
            val builder = if (name == null) {
                Room.inMemoryDatabaseBuilder(context, LocalFlixDatabase::class.java)
            } else {
                Room.databaseBuilder(context, LocalFlixDatabase::class.java, name)
            }
            return builder
                .addMigrations(*MIGRATIONS)
                // The playback bookkeeping makes tiny indexed queries (and 5-second progress writes) from the main
                // thread, like the desktop controller. Imports, scans and listing run on background threads.
                .allowMainThreadQueries()
                .build()
        }
    }
}
