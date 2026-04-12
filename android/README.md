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

`app/build/outputs/apk/debug/LiveVocoder-v7.0-debug.apk`

## Release APK (signed)

Create a keystore (once), then in `app/build.gradle.kts` add a `signingConfigs.release` block and reference it from `buildTypes.release`. Build:

```bash
./gradlew assembleRelease
```

## Version bumps

Edit `app/build.gradle.kts` (`versionCode`, `versionName`) and `app/src/main/res/values/strings.xml` (`version_line`) together with `cpp/src/app_version.hpp` on the desktop side.
