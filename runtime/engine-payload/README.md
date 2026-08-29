# engine-payload

Laid out the way a CrossOver engine is, so a patcher can copy this tree over the
engine it is building without a mapping table:

    wine/x86_64-windows/winegstreamer.dll   ->  <CX>/lib/wine/x86_64-windows/winegstreamer.dll
    wine/x86_64-unix/winegstreamer.so       ->  <CX>/lib/wine/x86_64-unix/winegstreamer.so

`built-for.json` says what these were compiled against and is not copied into the
engine. **Check it before replacing anything.** The two halves speak an interface
that changes between wine revisions, and installing them onto a different wine
is not a degraded experience — it is an engine that may stop loading media at
all, failing in the shape of the fault they were built to repair.

The version alone does not identify an engine: the patched fork and a stock
CrossOver both report `26.3.0.39832`. The app name is the part that tells them
apart, which is why `built-for.json` records it and why
`install-engine-media.sh` refuses on either check failing.

Why these exist: codecs that were present would not show. Built by
`scripts/build-winegstreamer.sh` from a wine tree; see the wiki page
*What ships, and where it goes*.

The `d3d9.dll` this project also built is deliberately not here. It did not fix
the title it was made for, the bridge that does ships in the game folder, and
installing it displaces the d9vk a patcher puts there.
