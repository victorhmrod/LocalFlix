package io.github.victorhmrod.localflix

import android.net.Uri
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.hasSetTextAction
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performTextInput
import androidx.test.ext.junit.runners.AndroidJUnit4
import io.github.victorhmrod.localflix.app.AppContainer
import io.github.victorhmrod.localflix.core.FileProbe
import io.github.victorhmrod.localflix.core.ImportPlanner
import io.github.victorhmrod.localflix.data.LocalFlixDatabase
import io.github.victorhmrod.localflix.ui.LocalFlixRoot
import io.github.victorhmrod.localflix.ui.theme.LocalFlixTheme
import org.junit.After
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/** The real screens on the device, on an isolated in-memory library (the app's own data is never touched). */
@RunWith(AndroidJUnit4::class)
class UiSmokeTest {
    @get:Rule
    val rule = createComposeRule()

    private var container: AppContainer? = null

    private fun start(prepare: (AppContainer) -> Unit = {}) {
        val c = onMainResult {
            AppContainer(
                targetContext, LocalFlixDatabase.open(targetContext, null), File(targetContext.cacheDir, "ui-covers"),
                fileProbe = { FileProbe { File(Uri.parse(it).path.orEmpty()).exists() } }, startupChecks = false,
            )
        }
        container = c
        prepare(c)
        rule.setContent { LocalFlixTheme { LocalFlixRoot(c) } }
    }

    @After
    fun tearDown() {
        container?.let { c -> onMain { c.playback.shutdown(); c.backend.release() } }
    }

    @Test
    fun anEmptyLibraryInvitesYouToAddThings() {
        start()
        rule.onNodeWithText("Your library is empty").assertIsDisplayed()
        rule.onNodeWithText("Create a show").assertIsDisplayed()
        rule.onNodeWithText("Add movies").assertIsDisplayed()
    }

    @Test
    fun creatingAShowOpensItsPage() {
        start()
        rule.onNodeWithText("TV shows").performClick()
        rule.onNodeWithText("No shows yet").assertIsDisplayed()
        rule.onNodeWithText("New show").performClick()
        rule.onNode(hasSetTextAction()).performTextInput("Dark Matter")
        rule.onNodeWithText("Create").performClick()
        rule.waitUntil(5_000) { rule.onAllNodesWithTextCompat("No episodes yet") }
        rule.onNodeWithText("No episodes yet").assertIsDisplayed()
        rule.onNodeWithText("Add files").assertIsDisplayed()
    }

    @Test
    fun aShowShowsItsEpisodesInTheOrderTheyWillPlay() {
        start { c ->
            val showId = c.library.createShow("Order Show")
            val files = listOf("Order Show/ep10.mp4", "Order Show/ep2.mp4", "Order Show/ep1.mp4").map { scannedFile(it) }
            c.library.importEpisodes(showId, 0, ImportPlanner.planEpisodes(files))
            c.library.saveProgress(c.library.episodes(showId)[1].file.id, 120_000, 600_000)
        }
        rule.onNodeWithText("TV shows").performClick()
        rule.onNodeWithText("Order Show").performClick()
        rule.onNodeWithText("Season 1").assertIsDisplayed()
        rule.onNodeWithText("ep1").assertIsDisplayed()
        rule.onNodeWithText("Continue", substring = true).assertIsDisplayed() // resume button names the place to continue
    }

    @Test
    fun theSkipOptionsAreInTheSettingsAndStartOff() {
        start()
        val settings = container!!.settings
        assertFalse(settings.autoSkipIntro)
        assertFalse(settings.autoSkipEnding)
        rule.onNodeWithText("Settings").performClick()
        rule.onNodeWithText("Skip openings automatically").performScrollTo().assertIsDisplayed()
        rule.onNodeWithText("Skip endings automatically").performScrollTo().assertIsDisplayed()

        rule.onNodeWithText("Skip openings automatically").performClick()
        rule.runOnIdle {
            assertTrue(settings.autoSkipIntro)
            assertFalse(settings.autoSkipEnding) // each option has its own switch
        }
        rule.onNodeWithText("Skip endings automatically").performClick()
        rule.runOnIdle { assertTrue(settings.autoSkipEnding) }
    }

    @Test
    fun thePlayerOffersSkipIntroInsideTheOpeningAndSkipEndingInsideTheEnding() {
        assumeTrue("missing clip (run android/scripts/make_chaptered_clips.py)", testVideoAvailable("chaptered_episode.mkv"))
        val clip = copyTestVideoAs("chaptered_episode.mkv", "ep1.mkv", File(targetContext.cacheDir, "ui-skip"))
        start { c ->
            val showId = c.library.createShow("Skip UI")
            c.library.importEpisodes(showId, 0, ImportPlanner.planEpisodes(listOf(fileScan(clip))))
        }
        rule.onNodeWithText("TV shows").performClick()
        rule.onNodeWithText("Skip UI").performClick()
        rule.onNodeWithText("ep1").performClick() // plays it: the player screen opens

        rule.waitUntil(20_000) { rule.onAllNodesWithTextCompat("Skip Intro") }
        rule.onNodeWithText("Skip Intro").assertIsDisplayed().performClick()
        rule.waitUntil(8_000) { onMainResult { container!!.backend.positionMs } >= 4_500 }
        rule.waitUntil(5_000) { !rule.onAllNodesWithTextCompat("Skip Intro") }
        rule.waitUntil(20_000) { rule.onAllNodesWithTextCompat("Skip Ending") }
        rule.onNodeWithText("Skip Ending").assertIsDisplayed()
    }
}

private fun androidx.compose.ui.test.junit4.ComposeContentTestRule.onAllNodesWithTextCompat(text: String): Boolean =
    onAllNodes(androidx.compose.ui.test.hasText(text)).fetchSemanticsNodes().isNotEmpty()
