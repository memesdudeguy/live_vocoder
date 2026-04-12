package com.livevocoder.app

import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.util.concurrent.TimeUnit
import okhttp3.OkHttpClient
import okhttp3.Request

/**
 * Latest GitHub release + .apk asset (same idea as desktop WinHTTP update check).
 * Repo default: memesdudeguy/live_vocoder — change [REPO] for forks.
 */
object GithubApkUpdate {

    const val REPO = "memesdudeguy/live_vocoder"

    private val client = OkHttpClient.Builder()
        .connectTimeout(30, TimeUnit.SECONDS)
        .readTimeout(120, TimeUnit.SECONDS)
        .writeTimeout(120, TimeUnit.SECONDS)
        .build()

    data class ReleaseApk(
        val tagName: String,
        val version: String,
        val downloadUrl: String,
        val fileName: String,
    )

    /** GET /releases/latest; first .apk asset preferring a LiveVocoder* name. */
    @Throws(IOException::class)
    fun fetchLatestApkRelease(): ReleaseApk? {
        val req = Request.Builder()
            .url("https://api.github.com/repos/$REPO/releases/latest")
            .header("Accept", "application/vnd.github+json")
            .header("User-Agent", "LiveVocoder-Android")
            .header("X-GitHub-Api-Version", "2022-11-28")
            .build()
        client.newCall(req).execute().use { resp ->
            if (!resp.isSuccessful) {
                throw IOException("GitHub API HTTP ${resp.code}")
            }
            val body = resp.body?.string() ?: return null
            val o = JSONObject(body)
            val tag = o.optString("tag_name", "").ifEmpty { return null }
            val ver = tag.removePrefix("v").trim()
            val assets = o.optJSONArray("assets") ?: return null
            var fallbackUrl: String? = null
            var fallbackName: String? = null
            for (i in 0 until assets.length()) {
                val a = assets.getJSONObject(i)
                val name = a.optString("name", "")
                if (!name.endsWith(".apk", ignoreCase = true)) continue
                val url = a.optString("browser_download_url", "")
                if (url.isEmpty()) continue
                if (name.contains("livevocoder", ignoreCase = true)) {
                    return ReleaseApk(tag, ver, url, name)
                }
                if (fallbackUrl == null) {
                    fallbackUrl = url
                    fallbackName = name
                }
            }
            return if (fallbackUrl != null && fallbackName != null) {
                ReleaseApk(tag, ver, fallbackUrl, fallbackName)
            } else {
                null
            }
        }
    }

    @Throws(IOException::class)
    fun downloadToFile(url: String, dest: File) {
        val req = Request.Builder()
            .url(url)
            .header("User-Agent", "LiveVocoder-Android")
            .header("Accept", "application/octet-stream")
            .build()
        client.newCall(req).execute().use { resp ->
            if (!resp.isSuccessful) throw IOException("Download HTTP ${resp.code}")
            val b = resp.body ?: throw IOException("Empty body")
            b.byteStream().use { input ->
                dest.outputStream().use { output -> input.copyTo(output) }
            }
        }
    }
}

/** &gt; 0 if [remote] is newer than [local] (e.g. "7.1" vs "7.0"). */
fun compareSemver(remote: String, local: String): Int {
    fun parts(s: String) = s.trim().removePrefix("v").split('.')
        .map { p -> p.takeWhile { it.isDigit() }.toIntOrNull() ?: 0 }
    val r = parts(remote)
    val l = parts(local)
    val n = maxOf(r.size, l.size)
    for (i in 0 until n) {
        val a = r.getOrElse(i) { 0 }
        val b = l.getOrElse(i) { 0 }
        if (a != b) return a.compareTo(b)
    }
    return 0
}
