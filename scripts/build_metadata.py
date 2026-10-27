"""Generate firmware identity locally; never write into the source tree or contact Git remote."""
Import("env")

import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
import subprocess
from SCons.Script import COMMAND_LINE_TARGETS

project = Path(env.subst("$PROJECT_DIR"))


def git(*args):
    try:
        return subprocess.check_output(
            ["git", "-C", str(project), *args], stderr=subprocess.DEVNULL
        ).decode().strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def generate_metadata():
    commit = git("rev-parse", "--short=12", "HEAD") or "unknown"
    status = git("status", "--porcelain", "--untracked-files=normal")
    tree_state = ("dirty" if status else "clean") if commit != "unknown" else "unknown"
    source_hash = hashlib.sha256()
    inputs = [project / "platformio.ini"]
    for directory in ("src", "include", "lib", "data", "scripts"):
        inputs.extend(p for p in (project / directory).rglob("*") if p.is_file())
    for path in sorted(inputs):
        source_hash.update(path.relative_to(project).as_posix().encode() + b"\0")
        source_hash.update(path.read_bytes() + b"\0")

    values = {
        "BALANCEBOT_VERSION": "1.4.7-network-open",
        "BALANCEBOT_COMMIT": commit,
        "BALANCEBOT_TREE_STATE": tree_state,
        "BALANCEBOT_SOURCE_SHA256": source_hash.hexdigest(),
        "BALANCEBOT_BUILD_UTC": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
    }
    header = Path(env.subst("$BUILD_DIR")) / "generated" / "balancebot_build.hpp"
    header.parent.mkdir(parents=True, exist_ok=True)
    content = "#pragma once\n" + "".join(
        f"#define {key} {json.dumps(value)}\n" for key, value in values.items()
    )
    if not header.exists() or header.read_text() != content:
        header.write_text(content)
    env.Append(CPPPATH=[str(header.parent)])
    print(f"BalanceBot {values['BALANCEBOT_VERSION']} {commit} ({tree_state}) "
          f"build={values['BALANCEBOT_BUILD_UTC']} source={values['BALANCEBOT_SOURCE_SHA256']}")


# Filesystem actions must not change the identity of an already-built firmware.
filesystem_only = bool(COMMAND_LINE_TARGETS) and set(COMMAND_LINE_TARGETS).issubset(
    {"buildfs", "uploadfs", "clean"}
)
if not filesystem_only:
    generate_metadata()
