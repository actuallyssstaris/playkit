#!/bin/sh
# Serve the PlayKit checklist on 127.0.0.1:8000.
cd "$(dirname "$0")" || exit 1
exec python3 -m http.server 8000 --bind 127.0.0.1
