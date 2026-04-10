#!/usr/bin/env python3
"""
Portable launcher (same idea as run.bat): chdir to app folder, create .venv if needed,
then run live_vocoder.py --web-gui.

When frozen as RunLiveVocoder.exe, the project is found automatically if live_vocoder.py sits
in the same folder as the .exe **or** in a parent folder (e.g. exe in dist/ and sources in
the repo root). requirements.txt must live next to live_vocoder.py. System Python is needed
once to create .venv; after that only .venv is used.

Works on native Windows and when the same .exe is started under Wine (install a Windows
Python build inside your Wine prefix for first-time venv creation, or use ./run-wine.sh on
Linux to skip Wine for the venv step).

For a single file with no separate Python install, use the full LiveVocoder.exe build instead.
"""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path


def _bundle_dir() -> Path:
    """Directory containing this script, or the .exe when frozen."""
    if getattr(sys, "frozen", False) and getattr(sys, "executable", None):
        return Path(sys.executable).resolve().parent
    return Path(__file__).resolve().parent


def _walk_up_for_project(start: Path) -> Path | None:
    """Return first ancestor of start (including start) that contains live_vocoder.py."""
    try:
        p = start.resolve()
    except OSError:
        return None
    for _ in range(12):
        if (p / "live_vocoder.py").is_file():
            return p
        if p.parent == p:
            break
        p = p.parent
    return None


def _find_project_root() -> Path | None:
    """Resolve folder with live_vocoder.py: prefer cwd chain, then exe/script directory chain."""
    try:
        hit = _walk_up_for_project(Path.cwd())
        if hit is not None:
            return hit
    except OSError:
        pass
    return _walk_up_for_project(_bundle_dir())


def _venv_python(root: Path) -> Path:
    if sys.platform == "win32":
        return root / ".venv" / "Scripts" / "python.exe"
    return root / ".venv" / "bin" / "python"


def _running_under_wine() -> bool:
    """True when this Windows process is running under Wine (not native Windows)."""
    if sys.platform != "win32":
        return False
    windir = Path(os.environ.get("WINDIR", r"C:\Windows"))
    for sub in ("System32", "SysWOW64", "system32"):
        if (windir / sub / "wineboot.exe").is_file():
            return True
    return False


def _python_cmd_candidates_win32() -> tuple[list[str], ...]:
    """Order matters: Wine bottles rarely have the 'py' launcher; native Windows often does."""
    if _running_under_wine():
        return (["python"], ["python3"], ["py", "-3"])
    return (["py", "-3"], ["python"], ["python3"])


def _python_for_venv_create() -> list[str] | None:
    """Command prefix to invoke Python (e.g. ['py', '-3'] or ['/usr/bin/python3'])."""
    if sys.platform == "win32":
        for cmd in _python_cmd_candidates_win32():
            try:
                r = subprocess.run(
                    [*cmd, "-c", "import sys; print(sys.version_info >= (3, 10))"],
                    capture_output=True,
                    text=True,
                    timeout=30,
                    cwd=_bundle_dir(),
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
                )
            except (OSError, subprocess.TimeoutExpired):
                continue
            if r.returncode == 0 and r.stdout.strip() == "True":
                return cmd
        return None
    # Unfrozen dev run: use current interpreter
    if not getattr(sys, "frozen", False):
        return [sys.executable]
    for cand in ("python3", "python"):
        p = shutil_which(cand)
        if p:
            return [p]
    return None


def shutil_which(name: str) -> str | None:
    from shutil import which

    return which(name)


def _ensure_venv(root: Path) -> Path:
    vp = _venv_python(root)
    if vp.is_file():
        return vp
    py = _python_for_venv_create()
    if not py:
        msg = (
            "No Python 3.10+ found on PATH. Install Python from python.org (enable 'py launcher' on Windows) "
            "or use the all-in-one LiveVocoder.exe from packaging/build-exe.bat."
        )
        if _running_under_wine():
            msg += (
                "\n\n(Under Wine: install Windows Python inside this Wine prefix, **or** on Linux run "
                "`./run-wine.sh` / `python3 run_portable.py` from the project folder so the venv uses "
                "host Python instead.)"
            )
        print(msg, file=sys.stderr)
        sys.exit(1)
    print("Creating .venv and installing dependencies (first run only)…", file=sys.stderr)
    subprocess.check_call([*py, "-m", "venv", ".venv"], cwd=root)
    subprocess.check_call([str(vp), "-m", "pip", "install", "-U", "pip"], cwd=root)
    subprocess.check_call(
        [str(vp), "-m", "pip", "install", "-r", "requirements.txt"],
        cwd=root,
    )
    return vp


def main() -> int:
    root = _find_project_root()
    if root is None:
        bd = _bundle_dir()
        try:
            cwd = str(Path.cwd().resolve())
        except OSError:
            cwd = "?"
        print(
            "Could not find live_vocoder.py. Searched upward from:\n"
            f"  current directory: {cwd}\n"
            f"  launcher location: {bd}\n"
            "Put the launcher next to the project files, run from inside the project folder, "
            "or use dist/RunLiveVocoder.exe with live_vocoder.py in the parent directory.",
            file=sys.stderr,
        )
        return 1
    os.chdir(root)
    vp = _ensure_venv(root)
    script = root / "live_vocoder.py"
    args = sys.argv[1:]
    if not args:
        args = ["--web-gui"]
    return subprocess.call([str(vp), str(script), *args], cwd=root)


if __name__ == "__main__":
    raise SystemExit(main())
