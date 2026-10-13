package io.github.victorhmrod.localflix

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import io.github.victorhmrod.localflix.app.LocalFlixApp
import io.github.victorhmrod.localflix.ui.LocalFlixRoot
import io.github.victorhmrod.localflix.ui.theme.LocalFlixTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        val container = (application as LocalFlixApp).container
        setContent {
            LocalFlixTheme {
                LocalFlixRoot(container)
            }
        }
    }
}
