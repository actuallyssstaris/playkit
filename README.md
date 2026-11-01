# PlayKit

A WebKit browser shell for PlayStation 5, built as a native Prospero application.

PlayKit draws its own browser chrome: a Firefox-style tab strip with tab groups,
a home screen with site tiles and fetched favicons, a dark/light theme, an
on-screen keyboard, bookmarks, and downloads. The WebKit engine is wired in
through a diagnostic renderer while the full WPE backend is ported.

## Layout

| Path | Purpose |
| --- | --- |
| `src/` | Application sources: shell UI, favicons, input, heap, WebKit glue |
| `platform/` | PS5 SDK compatibility headers and platform shims |
| `runtime/` | `libc.prx` runtime module required by the loader |
| `tooling/native/` | Host-side ELF converter, SELF signer, and startup objects |
| `assets/` | Baked UI fonts |
| `sce_sys/` | Application metadata and icon |
| `docs/` | Architecture, launch process, and porting notes |
| `status/` | Development status page and current renders |

## Build

Requirements: Linux, `clang-18`, `lld-18`, `make`, `python3`, `wget`, `tar`,
`pkg-config`, and the PS5 payload SDK.

```sh
wget https://github.com/ps5-payload-dev/sdk/releases/latest/download/ps5-payload-sdk.zip
sudo unzip -d /opt ps5-payload-sdk.zip
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
make app
```

`make app` compiles, links, signs, and writes the installable folder to
`dist/PPSA90002/`. PacBrew ports (libcurl, libpng) are downloaded into
`.deps/pacbrew` on first use.

## Deploy

```sh
make deploy PS5_HOST=<console-ip>
```

FTP defaults to port `2121`, user `anonymous`, password `codex`. The deploy
script uploads under temporary names and promotes `eboot.bin` and `param.json`
last, so an interrupted upload cannot leave a half-updated title.

## Status

`status/index.html` records what works, what is in progress, and the latest
renders. `HANDOVER.md` carries the current development state.

## License

GPL-3.0-or-later. Third-party components keep their own licenses; see
`THIRD_PARTY_NOTICES.md` where present.
