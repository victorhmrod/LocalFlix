package io.github.victorhmrod.localflix.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class AppSettingsTest {
    private class MapStore : SettingsStore {
        val map = HashMap<String, String>()
        var writes = 0

        override fun allSettings(): Map<String, String> = HashMap(map)

        override fun setSetting(key: String, value: String) {
            map[key] = value
            writes++
        }
    }

    @Test
    fun defaultsMatchTheDesktopApp() {
        val s = AppSettings(MapStore())
        assertTrue(s.autoPlayNext)
        assertEquals(3, s.autoPlayCountdownSeconds)
        assertTrue(s.resumeAutomatically)
        assertFalse(s.skipCompleted)
        assertFalse(s.autoSkipIntro)
        assertFalse(s.autoSkipEnding)
        assertEquals("", s.preferredAudioLanguage)
        assertEquals("", s.preferredSubtitleLanguage)
        assertEquals(100, s.defaultVolume)
        assertEquals(1.0, s.playbackSpeed, 0.0)
        assertEquals(PlayerOrientation.LANDSCAPE, s.playerOrientation)
    }

    @Test
    fun valuesAreClampedAndPersisted() {
        val store = MapStore()
        val s = AppSettings(store)
        s.setAutoPlayCountdownSeconds(99)
        assertEquals(30, s.autoPlayCountdownSeconds)
        s.setDefaultVolume(-4)
        assertEquals(0, s.defaultVolume)
        s.setPlaybackSpeed(9.0)
        assertEquals(4.0, s.playbackSpeed, 0.0)
        s.setPreferredAudioLanguage("  por ")
        assertEquals("por", s.preferredAudioLanguage)
        s.setSkipCompleted(true)
        s.setAutoSkipEnding(true)
        s.setPlayerOrientation(PlayerOrientation.FOLLOW_DEVICE)

        val reopened = AppSettings(store) // a restart reads the same values back
        assertEquals(30, reopened.autoPlayCountdownSeconds)
        assertEquals("por", reopened.preferredAudioLanguage)
        assertTrue(reopened.skipCompleted)
        assertTrue(reopened.autoSkipEnding)
        assertFalse(reopened.autoSkipIntro) // each option is stored on its own
        assertEquals(PlayerOrientation.FOLLOW_DEVICE, reopened.playerOrientation)
    }

    @Test
    fun unchangedValuesAreNotWrittenAndChangesAreAnnounced() {
        val store = MapStore()
        val s = AppSettings(store)
        val changed = ArrayList<String>()
        s.addChangeListener { changed += it }
        s.setAutoPlayNext(true) // still the default
        assertEquals(0, store.writes)
        assertTrue(changed.isEmpty())
        s.setAutoPlayNext(false)
        s.setAutoPlayNext(false)
        assertEquals(1, store.writes)
        assertEquals(listOf(SettingKey.AUTO_PLAY_NEXT), changed)
    }

    @Test
    fun garbageInTheStoreFallsBackToDefaults() {
        val store = MapStore().also {
            it.map[SettingKey.AUTO_PLAY_COUNTDOWN] = "soon"
            it.map[SettingKey.DEFAULT_VOLUME] = ""
            it.map[SettingKey.PLAYBACK_SPEED] = "fast"
        }
        val s = AppSettings(store)
        assertEquals(3, s.autoPlayCountdownSeconds)
        assertEquals(100, s.defaultVolume)
        assertEquals(1.0, s.playbackSpeed, 0.0)
    }
}
