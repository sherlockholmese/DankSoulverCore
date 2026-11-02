"""Runs the plugin's QML under Quickshell against the real helper.

Usage: test_launcher.py HELPER

Each scenario assembles a throwaway shell around the plugin: the driver in tests/qml, a stub for
the one DMS service the plugin imports, and a fake `dms` that records clipboard writes.
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PLUGIN_FILES = ("SoulverLauncher.qml", "SoulverService.qml")
SKIP = 77
RATES = {
    "fiat": {"rates": {"USD": 1, "SGD": 2}},
    "crypto": {"prices": {"BTC": 50000}},
}


def run(helper, scenario, environment=None):
    directory = Path(tempfile.mkdtemp(prefix="dank-soulver-qml-"))
    try:
        for name in PLUGIN_FILES:
            shutil.copy(ROOT / name, directory)
        shutil.copy(ROOT / "tests/qml/shell.qml", directory)
        (directory / "Services").mkdir()
        shutil.copy(ROOT / "tests/qml/ToastService.qml", directory / "Services")

        rates = directory / "cache/dank-soulver-core/numen-rates.json"
        rates.parent.mkdir(parents=True)
        rates.write_text(json.dumps(RATES))

        clipboard = directory / "clipboard"
        fake_dms = directory / "path/dms"
        fake_dms.parent.mkdir()
        fake_dms.write_text(f'#!/bin/sh\nprintf %s "$3" > "{clipboard}"\n')
        fake_dms.chmod(0o755)

        process = subprocess.run(
            ["qs", "--no-color", "-p", str(directory)],
            env={
                **os.environ,
                **(environment or {}),
                "PATH": f"{fake_dms.parent}:{os.environ['PATH']}",
                "QT_QPA_PLATFORM": "offscreen",
                "XDG_CACHE_HOME": str(directory / "cache"),
                "XDG_RUNTIME_DIR": str(directory),
                "DANK_SOULVER_RATES_URL": "http://127.0.0.1:9/unreachable",
                "LC_ALL": "C",
                "TEST_HELPER": helper,
                "TEST_SCENARIO": scenario,
                "TEST_CLIPBOARD": str(clipboard),
            },
            capture_output=True,
            text=True,
            timeout=90,
        )
    finally:
        shutil.rmtree(directory, True)

    output = process.stdout + process.stderr
    lines = [line for line in output.splitlines() if "TEST:" in line]
    for line in lines:
        print(f"  [{scenario}] {line[line.index('TEST:') + 5:].strip()}")

    passed = any("TEST: scenario passed" in line for line in lines)
    if not passed:
        print(output)
    return passed


def main():
    helper = sys.argv[1]
    if not shutil.which("qs"):
        print("Quickshell is not installed")
        return SKIP

    missing_soulver = {"SOULVER_WRAPPER_LIBRARY": "/nonexistent/libSoulverWrapper.so"}
    scenarios = [
        ("numen", None),
        ("switching", None),
        ("trigger", None),
        ("unknownBackend", None),
        ("missingSoulver", missing_soulver),
    ]
    if not all(run(helper, name, environment) for name, environment in scenarios):
        return 1
    print("All launcher scenarios passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
