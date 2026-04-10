# Build LiveVocoder.exe with PyInstaller (run from repo root: live_vocoder\)
Set-Location (Split-Path -Parent $PSScriptRoot)
if (Get-Command py -ErrorAction SilentlyContinue) {
    & py -3 -m pip install -q -r requirements.txt pyinstaller
    & py -3 -m PyInstaller packaging\LiveVocoder.spec --clean --noconfirm
} else {
    & python -m pip install -q -r requirements.txt pyinstaller
    & python -m PyInstaller packaging\LiveVocoder.spec --clean --noconfirm
}
Write-Host "Output: dist\LiveVocoder.exe"
