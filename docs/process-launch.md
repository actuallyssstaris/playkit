# Process launch on the PS5

WebKit2 runs the browser UI and the web content in separate processes. Sony's
PlayStation port calls a private function for this:

```cpp
PlayStation::launchProcess(path, argv, envp, socket, userId);
```

That comes from `WebKitRequirements` / `ProcessLauncher`, which are not part of
the public payload SDK. Without a replacement, `WebKitWebProcess` cannot start
and the browser only shows an error page.

## What does work on a homebrew console

Homebrew payloads cannot call `fork`/`execve` to start an unsigned ELF directly.
Every public loader uses the same sequence instead:

1. Create a child process from a signed system eboot with `rfork_thread`.
2. `execve` that eboot in the child, then stop it with `ptrace`.
3. Replace the mapped image with the target ELF and resume.

This is what `ps5-payload-dev/elfldr` does in `elfldr_spawn`, and what
`ps5-payload-dev/shsrv` and `soniciso/sonicloader` do in their loaders. Some
builds use `sceKernelSpawn(&pid, 1, eboot, 0, argv)` instead of the rfork path.
That call exists in `libkernel_sys`, so it works for payloads loaded by etaHEN
or another syscore payload, not for payloads loaded through `libkernel_web`.

Template eboot used by the public loaders:

```
/system/vsh/app/NPXS40112/eboot.bin
```

## How this fits WebKit

`Source/WebKit/UIProcess/Launcher/playstation/ProcessLauncherPlayStation.cpp`
already builds the argv array, the socket pair, and the launch options. Only the
call to the private launcher needs replacing. The replacement has to:

1. Spawn `WebKitWebProcess` or `WebKitNetworkProcess` with the inherited IPC
   socket.
2. Pass the socket number in the environment or argv, as the Sony port does.
3. Return the new pid to WebKit.

The child still needs a normal `main` that creates a `WebProcess` and connects
to the socket. That part is unchanged from the upstream WPE port.

## Licences

`ps5-payload-dev/elfldr` and `ps5-payload-dev/shsrv` are GPL-3.0. Linking their
loader into PlayKit makes the combined binary GPL-3.0. The technique itself can
be reimplemented from the documented behaviour, but copying the loader code
means adopting that licence. Decide this before shipping.
