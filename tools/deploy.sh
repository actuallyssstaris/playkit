#!/usr/bin/env bash
# PlayKit - FTP development deployment and cleanup.
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds the app folder and publishes it below /data/homebrew, or removes
# only the current title's staged files. Folder files are uploaded under
# ignored temporary names and promoted individually.
#
# Adapted from ps5-native-app-boilerplate (BlackBearReloaded, GPL-3.0-or-later).

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
action=${1:-deploy}
host=${PS5_HOST:-}
port=${FTP_PORT:-2121}
user=${PS5_FTP_USER:-anonymous}
password=${PS5_FTP_PASSWORD:-codex}

[[ $# -le 1 && ($action == deploy || $action == undeploy) ]] || {
    echo "usage: tools/deploy.sh [undeploy]" >&2
    exit 2
}
[[ $host =~ ^[A-Za-z0-9][A-Za-z0-9.-]*$ ]] || {
    echo "PS5_HOST must be an IPv4 address or hostname" >&2
    exit 2
}
[[ $port =~ ^[0-9]+$ ]] && (( 10#$port >= 1 && 10#$port <= 65535 )) || {
    echo "FTP_PORT must be between 1 and 65535" >&2
    exit 2
}
command -v make >/dev/null || { echo "missing required command: make" >&2; exit 2; }
command -v python3 >/dev/null || { echo "missing required command: python3" >&2; exit 2; }

param_path=${APP_PARAM:-sce_sys/param.json}
[[ $param_path == /* ]] || param_path="$root/$param_path"
title_id=$(python3 - "$param_path" <<'PY'
import json, re, sys
with open(sys.argv[1], encoding="utf-8") as source:
    title_id = json.load(source)["titleId"]
if not re.fullmatch(r"PPSA\d{5}", title_id):
    raise SystemExit("param.json contains an invalid titleId")
print(title_id)
PY
)
base_url="ftp://$host:$port/data/homebrew"

if [[ $action == undeploy ]]; then
    printf '==> [undeploy] Title: %s\n' "$title_id"
    printf '==> [undeploy] Folder: %s/%s/\n' "$base_url" "$title_id"

    if [[ ${DEPLOY_DRY_RUN:-0} == 1 ]]; then
        echo "==> [undeploy] Dry run complete; no network request was sent"
        exit 0
    fi

    python3 - "$host" "$port" "$user" "$password" "$title_id" <<'PY'
from ftplib import FTP, error_perm
from posixpath import join
import sys

host, port, user, password, title_id = sys.argv[1:]
homebrew_root = "/data/homebrew"


def reply_code(error):
    return str(error).split(maxsplit=1)[0]


def list_names(ftp, path):
    previous = ftp.pwd()
    ftp.cwd(path)
    try:
        return [name for name, _ in ftp.mlsd() if name not in {".", ".."}]
    finally:
        ftp.cwd(previous)


def validate_name(name):
    if not name or name in {".", ".."} or "/" in name or "\r" in name or "\n" in name:
        raise RuntimeError(f"unsafe FTP entry name: {name!r}")


def remove_entry(ftp, path):
    try:
        # ftpsrv implements DELE with POSIX remove(), which safely unlinks files,
        # symlinks, and empty directories before recursion is considered.
        ftp.sendcmd(f"DELE {path}")
        print(f"Removed: {path}")
        return True
    except error_perm as error:
        message = str(error)
        if reply_code(error) == "550" and "No such file or directory" in message:
            return False
        if reply_code(error) != "550" or "Directory not empty" not in message:
            raise

    for name in list_names(ftp, path):
        validate_name(name)
        remove_entry(ftp, join(path, name))
    ftp.rmd(path)
    print(f"Removed directory: {path}")
    return True


with FTP() as ftp:
    ftp.connect(host, int(port), timeout=15)
    ftp.login(user, password)
    removed = remove_entry(ftp, join(homebrew_root, title_id))
    for suffix in ("ffpkg", "ffpfsc"):
        removed |= remove_entry(ftp, join(homebrew_root, f"{title_id}.{suffix}"))
        removed |= remove_entry(ftp, join(homebrew_root, f".{title_id}.{suffix}.upload"))
    try:
        ftp.quit()
    except (EOFError, OSError):
        pass

if removed:
    print(f"Undeployment complete: {title_id}")
else:
    print(f"Nothing staged for {title_id}")
PY
    exit 0
fi

echo "==> [deploy] Building the app folder"
make -C "$root" --no-print-directory app

artifact="$root/dist/$title_id"
[[ -d $artifact ]] || {
    echo "missing deployment folder: $artifact" >&2
    exit 2
}
[[ -s $artifact/eboot.bin && -s $artifact/sce_sys/param.json ]] || {
    echo "deployment folder is missing eboot.bin or sce_sys/param.json" >&2
    exit 2
}
printf '==> [deploy] Target: %s/%s/\n' "$base_url" "$title_id"

if [[ ${DEPLOY_DRY_RUN:-0} == 1 ]]; then
    echo "==> [deploy] Dry run complete; no network request was sent"
    exit 0
fi

python3 - "$host" "$port" "$user" "$password" "$title_id" "$artifact" <<'PY'
from ftplib import FTP, error_perm
from pathlib import Path
from posixpath import dirname, join
import hashlib
import sys

host, port, user, password, title_id, artifact_name = sys.argv[1:]
homebrew_root = "/data/homebrew"


def reply_code(error):
    return str(error).split(maxsplit=1)[0]


def list_names(ftp, path):
    previous = ftp.pwd()
    ftp.cwd(path)
    try:
        return {name for name, _ in ftp.mlsd() if name not in {".", ".."}}
    finally:
        ftp.cwd(previous)


def ensure_directory(ftp, path):
    current = ""
    for component in path.strip("/").split("/"):
        current += f"/{component}"
        try:
            ftp.mkd(current)
        except error_perm as error:
            if reply_code(error) != "550":
                raise
            previous = ftp.pwd()
            try:
                ftp.cwd(current)
            finally:
                ftp.cwd(previous)


def remove_if_present(ftp, path):
    try:
        # ftpsrv returns 226 for a successful DELE. FTP.sendcmd accepts every
        # valid 2xx completion while ftplib.FTP.delete only permits 200/250.
        ftp.sendcmd(f"DELE {path}")
        return True
    except error_perm as error:
        text = str(error).lower()
        if reply_code(error) == "550" and ("no such" in text or "not found" in text):
            return False
        raise


def upload_atomic(ftp, local, remote):
    directory = dirname(remote)
    temporary = join(directory, f".{remote.rsplit('/', 1)[-1]}.upload")
    ensure_directory(ftp, directory)
    remove_if_present(ftp, temporary)
    with local.open("rb") as source:
        ftp.storbinary(f"STOR {temporary}", source, blocksize=256 * 1024)
    remove_if_present(ftp, remote)
    ftp.rename(temporary, remote)


with FTP() as ftp:
    ftp.connect(host, int(port), timeout=15)
    ftp.login(user, password)
    # ftpsrv has a SELF transfer mode: on download it returns the ELF embedded
    # in a SELF instead of the SELF itself. With it on, a deployed signed
    # eboot.bin reads back as an unsigned ELF and looks broken. Turn it off so
    # the verification below compares the stored bytes.
    try:
        for _ in range(4):
            if "disabled" in ftp.sendcmd("SELF"):
                break
    except error_perm:
        pass
    artifact = Path(artifact_name)
    critical = [artifact / "eboot.bin", artifact / "sce_sys/param.json"]
    files = sorted(
        path for path in artifact.rglob("*")
        if path.is_file() and path not in critical
    ) + critical
    print(f"==> [deploy] Publishing {len(files)} files; eboot.bin and param.json are last")
    remote_root = join(homebrew_root, title_id)
    for index, local in enumerate(files, 1):
        relative = local.relative_to(artifact).as_posix()
        if "\n" in relative or "\r" in relative:
            raise RuntimeError(f"deployment paths cannot contain newlines: {relative!r}")
        print(f"==> [deploy] [{index}/{len(files)}] {relative}")
        upload_atomic(ftp, local, join(remote_root, relative))
    if "eboot.bin" not in list_names(ftp, remote_root):
        raise RuntimeError("FTP upload completed but eboot.bin is not listed")
    if "param.json" not in list_names(ftp, join(remote_root, "sce_sys")):
        raise RuntimeError("FTP upload completed but param.json is not listed")
    # Read the stored eboot back and compare hashes. A truncated or converted
    # file in this position fails to launch, and the presence check above
    # cannot tell the difference.
    local_hash = hashlib.sha256((artifact / "eboot.bin").read_bytes()).hexdigest()
    remote_hash = hashlib.sha256()

    def digest_chunk(chunk):
        remote_hash.update(chunk)

    ftp.retrbinary(f"RETR {join(remote_root, 'eboot.bin')}", digest_chunk,
                   blocksize=256 * 1024)
    if remote_hash.hexdigest() != local_hash:
        raise RuntimeError(
            "eboot.bin on the console does not match the signed local file")
    print("==> [deploy] eboot.bin verified byte for byte")
    print(f"Deployment complete: ftp://{host}:{port}{remote_root}/")
    try:
        ftp.quit()
    except (EOFError, OSError):
        pass
PY
