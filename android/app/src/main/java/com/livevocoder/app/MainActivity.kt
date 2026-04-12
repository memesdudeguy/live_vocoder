package com.livevocoder.app

import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.FileProvider
import androidx.lifecycle.lifecycleScope
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.livevocoder.app.databinding.ActivityMainBinding
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.buttonReleases.setOnClickListener {
            startActivity(
                Intent(
                    Intent.ACTION_VIEW,
                    Uri.parse("https://github.com/${GithubApkUpdate.REPO}/releases"),
                ),
            )
        }

        binding.buttonCheckApkUpdate.setOnClickListener {
            checkForApkUpdate()
        }
    }

    private fun ensureInstallFromUnknownSources(): Boolean {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            if (!packageManager.canRequestPackageInstalls()) {
                MaterialAlertDialogBuilder(this)
                    .setTitle(R.string.check_apk_update)
                    .setMessage(R.string.update_need_install_permission)
                    .setPositiveButton(R.string.ok) { _, _ ->
                        startActivity(
                            Intent(
                                Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                                Uri.parse("package:$packageName"),
                            ),
                        )
                    }
                    .setNegativeButton(R.string.no, null)
                    .show()
                return false
            }
        }
        return true
    }

    private fun checkForApkUpdate() {
        val checking = MaterialAlertDialogBuilder(this)
            .setTitle(R.string.check_apk_update)
            .setMessage(R.string.update_checking)
            .setCancelable(false)
            .create()
        checking.show()

        lifecycleScope.launch {
            val result = withContext(Dispatchers.IO) {
                runCatching { GithubApkUpdate.fetchLatestApkRelease() }
            }
            checking.dismiss()

            result.exceptionOrNull()?.let { e ->
                MaterialAlertDialogBuilder(this@MainActivity)
                    .setMessage(getString(R.string.update_api_failed, e.message ?: "unknown"))
                    .setPositiveButton(R.string.ok, null)
                    .show()
                return@launch
            }

            val release = result.getOrNull()
            if (release == null) {
                MaterialAlertDialogBuilder(this@MainActivity)
                    .setMessage(R.string.update_no_apk)
                    .setPositiveButton(R.string.yes) { _, _ ->
                        startActivity(
                            Intent(
                                Intent.ACTION_VIEW,
                                Uri.parse("https://github.com/${GithubApkUpdate.REPO}/releases"),
                            ),
                        )
                    }
                    .setNegativeButton(R.string.no, null)
                    .show()
                return@launch
            }

            val localVersion = BuildConfig.VERSION_NAME
            val cmp = compareSemver(release.version, localVersion)
            if (cmp <= 0) {
                MaterialAlertDialogBuilder(this@MainActivity)
                    .setMessage(getString(R.string.update_up_to_date, localVersion))
                    .setPositiveButton(R.string.ok, null)
                    .show()
                return@launch
            }

            MaterialAlertDialogBuilder(this@MainActivity)
                .setTitle(R.string.update_available_title)
                .setMessage(
                    getString(
                        R.string.update_available_message,
                        release.version,
                        localVersion,
                    ),
                )
                .setPositiveButton(R.string.yes) { _, _ ->
                    if (!ensureInstallFromUnknownSources()) return@setPositiveButton
                    downloadAndInstall(release)
                }
                .setNegativeButton(R.string.no, null)
                .show()
        }
    }

    private fun downloadAndInstall(release: GithubApkUpdate.ReleaseApk) {
        val progress = MaterialAlertDialogBuilder(this)
            .setTitle(R.string.check_apk_update)
            .setMessage(R.string.update_downloading)
            .setCancelable(false)
            .create()
        progress.show()

        lifecycleScope.launch {
            val dest = File(cacheDir, "livevocoder_update.apk")
            val download = withContext(Dispatchers.IO) {
                runCatching {
                    if (dest.exists()) dest.delete()
                    GithubApkUpdate.downloadToFile(release.downloadUrl, dest)
                    dest
                }
            }
            progress.dismiss()

            download.onFailure { e ->
                MaterialAlertDialogBuilder(this@MainActivity)
                    .setMessage(getString(R.string.update_download_failed, e.message ?: "error"))
                    .setPositiveButton(R.string.ok, null)
                    .show()
            }
            download.onSuccess { file ->
                val uri = FileProvider.getUriForFile(
                    this@MainActivity,
                    "${BuildConfig.APPLICATION_ID}.fileprovider",
                    file,
                )
                val intent = Intent(Intent.ACTION_VIEW).apply {
                    setDataAndType(uri, "application/vnd.android.package-archive")
                    addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                    addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                }
                try {
                    startActivity(intent)
                } catch (e: Exception) {
                    Toast.makeText(this@MainActivity, e.message, Toast.LENGTH_LONG).show()
                }
            }
        }
    }
}
