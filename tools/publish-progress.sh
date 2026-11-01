#!/usr/bin/env bash
# Publish one workflow's build progress to the build-status branch.
#
# The status page fetches <name>.json from the raw branch URL, so each workflow
# owns its own file and two jobs never overwrite each other.
#
# Usage: tools/publish-progress.sh NAME LOGFILE WORKFILE
#
# Needs GH_TOKEN with contents:write (the workflow token is enough) and
# GITHUB_REPOSITORY in the form owner/repo.
set -euo pipefail

name=${1:?usage: publish-progress.sh NAME LOGFILE WORKFILE}
log=${2:?usage: publish-progress.sh NAME LOGFILE WORKFILE}
file=${3:?usage: publish-progress.sh NAME LOGFILE WORKFILE}

: "${GITHUB_REPOSITORY:?GITHUB_REPOSITORY is not set}"
: "${GH_TOKEN:?GH_TOKEN is not set}"

branch=build-status
path="$name.json"
api="https://api.github.com/repos/$GITHUB_REPOSITORY/contents/$path"
auth="Authorization: Bearer $GH_TOKEN"

bash "$(dirname "$0")/build-progress.sh" "$log" "$file" "$name"

# The file may not exist yet; then the PUT has no sha and creates it.
sha=$(curl -fsSL -H "$auth" "$api?ref=$branch" 2>/dev/null |
      python3 -c 'import json,sys; print(json.load(sys.stdin).get("sha",""))' 2>/dev/null || true)

content=$(base64 -w0 < "$file")
body=$(python3 - "$content" "$branch" "$sha" <<'PY'
import json, sys
content, branch, sha = sys.argv[1:4]
document = {"message": "build progress", "content": content, "branch": branch}
if sha:
    document["sha"] = sha
print(json.dumps(document))
PY
)

status=$(curl -sS -o /tmp/publish-response.json -w '%{http_code}' \
    -X PUT -H "$auth" -H "Accept: application/vnd.github+json" \
    -d "$body" "$api")

if [[ $status != 200 && $status != 201 ]]; then
    echo "publish $path failed with HTTP $status" >&2
    cat /tmp/publish-response.json >&2
    exit 1
fi
