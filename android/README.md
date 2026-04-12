# Live Vocoder — Android (release **7.0**)

This module builds a **versioned 7.0** Android package (`versionName` / UI match the desktop C++ line). It is a **companion app**: it does **not** embed the desktop vocoder engine (PortAudio, FFTW, full SDL UI). Those pieces need a separate NDK port (e.g. Oboe + native DSP).

## Requirements

- JDK **17+**
- Android SDK with **Platform 35** (or adjust `compileSdk` / `targetSdk` in `app/build.gradle.kts`) and **Build-Tools 34+**
- Accept licenses:  
  `sdkmanager --licenses`

## Build a debug APK

```bash
cd android
export ANDROID_SDK_ROOT="$HOME/Android/Sdk"   # or your SDK path
./gradlew assembleDebug
```

Output:

`app/build/outputs/apk/debug/app-debug.apk` (Gradle’s default name; the app is still version **7.0** in the manifest.)

## Release APK (signed)

Create a keystore (once), then in `app/build.gradle.kts` add a `signingConfigs.release` block and reference it from `buildTypes.release`. Build:

```bash
./gradlew assembleRelease
```

## Version bumps

Edit `app/build.gradle.kts` (`versionCode`, `versionName`) and `app/src/main/res/values/strings.xml` (`version_line`) together with `cpp/src/app_version.hpp` on the desktop side.

## Virtual audio

The APK surfaces the same ideas as the desktop README: Android has no system-wide virtual cable like VB‑Audio. `RECORD_AUDIO` is declared for a future on-device vocoder; see `strings.xml` (`virtual_audio_body`) and the **Virtual audio on Android** section in the UI.
