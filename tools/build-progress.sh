#!/usr/bin/env bash
# Write the live build state for status/index.html.
#
# Reads a ninja log and writes a small JSON file the page polls. Usage:
#   tools/build-progress.sh LOGFILE OUTFILE [SOURCE]
#
# ninja prints "[123/1091] ..." for every step, so the last such line is the
# current position. The file is replaced atomically so the page never reads a
# half-written JSON document.
set -euo pipefail

log=${1:?usage: build-progress.sh LOGFILE OUTFILE [SOURCE]}
out=${2:?usage: build-progress.sh LOGFILE OUTFILE [SOURCE]}
source_name=${3:-local}

python3 - "$log" "$out" "$source_name" <<'PY'
import json
import os
import re
import sys
import tempfile
from datetime import datetime, timezone

log_path, out_path, source = sys.argv[1:4]

current = total = 0
step = ""
status = "running"
phase = "Configure"

try:
    with open(log_path, "r", errors="replace") as log:
        lines = log.read().splitlines()
except OSError:
    lines = []

progress = re.compile(r"^\[(\d+)/(\d+)\] ?(.*)$")
marker = re.compile(r"^==> \[([a-z0-9]+)\] ?(.*)$")
for line in lines:
    match = progress.match(line)
    if match:
        current, total = int(match.group(1)), int(match.group(2))
        step = match.group(3).strip()
        phase = "Compile"
        continue
    match = marker.match(line)
    if match:
        phase = match.group(1)
        step = match.group(2).strip() or step

# A build spends a while in ICU and in CMake before ninja prints anything, so
# report the last thing the log said rather than an empty step.
if not step:
    for line in reversed(lines):
        if line.strip():
            step = line.strip()
            break

# ninja stops after printing FAILED, so a failure wins over the step count.
if any(line.startswith("FAILED:") for line in lines):
    status = "failed"
elif any("done:" in line or "Build complete" in line for line in lines):
    status = "done"
    current = total = max(total, current, 1)
elif not lines:
    status = "waiting"

if status == "waiting":
    percent = 0
else:
    percent = round(100 * current / total) if total else 0

document = {
    "source": source,
    "phase": phase,
    "status": status,
    "current": current,
    "total": total,
    "percent": percent,
    "step": step[:120],
    "updated": datetime.now(timezone.utc).isoformat(timespec="seconds"),
}

directory = os.path.dirname(os.path.abspath(out_path)) or "."
os.makedirs(directory, exist_ok=True)
handle, temporary = tempfile.mkstemp(dir=directory, prefix=".build-", suffix=".json")
with os.fdopen(handle, "w") as target:
    json.dump(document, target)
os.replace(temporary, out_path)
PY
