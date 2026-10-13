package io.github.victorhmrod.localflix.app

import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import io.github.victorhmrod.localflix.core.Cancelable
import io.github.victorhmrod.localflix.core.Scheduler

/** Timers on the main looper: the playback logic is single-threaded on it. */
class MainScheduler(private val handler: Handler = Handler(Looper.getMainLooper())) : Scheduler {
    override fun uptimeMs(): Long = SystemClock.uptimeMillis()

    override fun postDelayed(delayMs: Long, task: () -> Unit): Cancelable {
        val r = Runnable(task)
        handler.postDelayed(r, delayMs)
        return Cancelable { handler.removeCallbacks(r) }
    }
}
