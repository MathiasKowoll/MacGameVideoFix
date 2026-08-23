# Watch Dogs 2 — measured, and not ours to fix

Internal notes. This game gets no entry in the wiki and no fix in the app,
because what stops it is not a defect in the translation layer. Written down so
the next person who looks at it does not spend the afternoon we spent.

**Symptom.** Launches from Steam. No error, no dialog, and the game window never
appears.

## What actually happens

`bin/WatchDogs2.exe` is not the game. It is a 537 KB launcher stub whose `.text`
section is 2939 bytes; the engine is `bin/Disrupt_64.dll`, 138 MB. Getting that
the wrong way round costs a detour, because a 138 MB executable importing three
DLLs invites a theory about packers that does not apply.

Launched directly, bypassing the EasyAntiCheat launcher at the game root, the
stub lives 13–20 seconds and exits silently. It spawns `bin/SplashScreen.exe`,
which outlives it and sits idle forever at ~0% CPU. That orphan is why nothing
appears — the splash is still there, waiting for a process that is gone.

`lsof` on the live stub shows 28 mapped modules, all Wine or system. Notably
absent: `Disrupt_64.dll`, `uplay_r1_loader64.dll`, `steam_api64.dll`, and even
`MSVCR110.dll` which the stub imports statically. It dies long before the
engine. What it *does* map is `crypt32`, `bcrypt`, `ncrypt`, `cryptbase`,
`libgnutls`, `libgmp` — a licence round trip and nothing else.

Ubisoft Connect's own log names the other end of it:

    GameStartPipe.cpp (83)  Accepted connection.
    GameStartPipe.cpp (84)  Starting game session.
    <nothing follows>

On every run that worked (3 Jul, 5 Jul, 6 Aug, 7 Aug) those two lines were
followed within ten seconds by

    AccountOnlineLogin.cpp (333)   Login Type: ticket, platform: Steam.
    AccountStartupLogin.cpp (321)  User: <omitted>
    ApiProcessConnection.cpp       Game with process id N connected

The chain is therefore: the game asks Ubisoft Connect for a session over a named
pipe, Connect has no authenticated account to answer with, the answer never
comes, and the engine terminates its own process on a timeout. Zero bytes on
stderr, no Wine backtrace and no crash report is the signature of a deliberate
exit, not a fault.

## Why Ubisoft Connect cannot answer

Its session expired. `user.dat` was last written 2026-08-07 11:04:23, which is
the timestamp of the last successful login, and every start since then ends at

    ConnectView.cpp (204)  Using CEF with native rendering
    ...  No visitor metrics url present in Space Parameters.

with no `StartView.cpp (1052)` after it. `StartView` is the client's main view;
the login, the account and the play session all hang off it being drawn. The
embedded Chromium creates its profile and never navigates — its `Cache` stays at
0 bytes.

## What was ruled out, by measurement

Each of these cost a run and none of them is the cause. They are listed so they
are not tried again.

- **A missing dependency of the engine.** Every DLL `Disrupt_64.dll` imports is
  present, including `MSVCP110.dll` and `MSVCR110.dll` as genuine 64-bit PEs in
  `system32` with the 32-bit pair in `syswow64`.
- **The EasyAntiCheat launcher.** Bypassing it changes nothing; the game gets
  exactly as far either way.
- **A graphics fault.** The engine never loads. Nothing graphical has run.
- **A corrupt Ubisoft Connect install.** The whole install was moved aside and
  reinstalled clean, same version, identical failure.
- **A corrupt CEF cache.** Cleared; it rebuilt and failed the same way.
- **CEF rendering mode.** Both the working and the failing bottle log
  `Using CEF with native rendering`, with byte-identical command lines
  (`--no-sandbox --in-process-gpu --disable-gpu`).
- **`GST_PLUGIN_PATH`, which this project sets.** The failing bottle has it and
  the working one does not, so it looked like ours. It was removed, the bottle
  restarted, and the failure was identical. Restored afterwards. Worth recording
  precisely because the shape of the coincidence was so persuasive.
- **Steam running or not.** Fails both ways.
- **Network and TLS.** The launcher fetches its Space Parameters config from
  Ubisoft successfully in both bottles.
- **Chromium in general.** Steam's own embedded browser renders its store in the
  same bottle where Ubisoft Connect will not draw.

## What is still open

Two things, and neither is a translation-layer defect.

**Why the client draws its view in one bottle and not another.** Same client
build (13247), same CrossOver (27.0.0.40921), same graphics backend, same
Windows version, no DLL overrides in either, no virtual desktop in either. The
difference was not found. The bottle was going to be rebuilt, so the search
stopped there.

**A product id that changed under the game.** Every working run passed
`-upc_uplay_id 920`; every run after the 2026-08-23 03:09 Steam re-download
passes `-upc_uplay_id 3619`, and the bottle's registry knows `Installs\920` and
`GameStarter\920` and nothing about 3619. That it changed is measured. That it
also causes a failure is not — the expired session explains the symptom on its
own, and this cannot be separated from it until a client can log in.

Also unresolved and independent of all the above: Ubisoft's login page serves a
bot-detection challenge in this environment, in the bottle where rendering
works. One of the reasons it lists is "JavaScript disabled or not working",
which an embedded Chromium under Wine may well fail on its own merits. Nothing
in this project attempts to get around that, and nothing should.

## The lesson worth keeping

Three separate times here, a component was blamed on the strength of a
coincidence that was real but not causal: the 138 MB file that was not the
executable, the environment variable this project sets, and the `1_0_CORE` retry
in the sibling investigation. The measurement that settles a cause is the one
that makes the fault appear and disappear — not the one that finds something
unusual nearby.
