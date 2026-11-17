"""Install the pinned browsers of the HTML browser replay.

The lockfile of test/matchertext/jssrc pins Playwright, and that Playwright
version pins one build each of Chromium, Firefox, and WebKit
(node_modules/playwright-core/browsers.json). These are Playwright's builds,
not branded Chrome, Firefox, or Safari, and Playwright picks the WebKit build
by operating system (macOS 14 gets an older one). This script installs
Playwright from the lockfile and the three browsers into data/build/browsers,
then launches each browser and reports the version that it gives, which the
HTML replay records.

    python3 pipeline/browsers.py

On Linux, the browsers also need their system libraries:
`npx playwright install-deps chromium firefox webkit` (as root).
"""
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
JSSRC = ROOT / "test" / "matchertext" / "jssrc"
BROWSERS = ROOT / "data" / "build" / "browsers"
ENGINES = ("chromium", "firefox", "webkit")

# Prints the version that each engine reports, one JSON object.
VERSIONS = """
import { launch } from './browser.mjs';
const versions = {};
for (const name of process.argv.slice(1)) {
    const { browser, close } = await launch(name);
    versions[name] = browser.version();
    await close();
}
console.log(JSON.stringify(versions));
"""


def env():
    """The environment in which Playwright finds the installed browsers."""
    return {**os.environ, "PLAYWRIGHT_BROWSERS_PATH": str(BROWSERS)}


def _run(cmd, what, **kw):
    r = subprocess.run(cmd, cwd=JSSRC, env=env(), capture_output=True, text=True, **kw)
    if r.returncode:
        sys.exit(f"{what} failed (exit {r.returncode}):\n{r.stderr[-4000:]}")
    return r.stdout


def install():
    """Install Playwright and the pinned browsers when missing; return the version that each engine reports."""
    lock = JSSRC / "package-lock.json"
    stamp = JSSRC / "node_modules" / ".package-lock.json"
    if not stamp.exists() or stamp.stat().st_mtime < lock.stat().st_mtime:
        print("installing Playwright ...", flush=True)
        _run(["npm", "ci", "--no-audit", "--no-fund"], "npm ci")
    print("installing the pinned browsers ...", flush=True)
    _run(["npx", "--no-install", "playwright", "install", *ENGINES], "playwright install")
    return json.loads(_run(["node", "--input-type=module", "-e", VERSIONS, *ENGINES], "browser launch"))


if __name__ == "__main__":
    for name, version in install().items():
        print(f"{name:9} {version}")
