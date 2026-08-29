# Third-party binaries shipped for NINJA GAIDEN 3: Razor's Edge

Three of the four DLLs installed by `install-ng3-fix.sh` are other people's
work. They are redistributed here because a fix that only works for people who
already installed something else is not a fix. Both licences permit that, and
both require this notice to travel with the binaries.

## ng3-d3d9.dll — d9vk

- Upstream: <https://github.com/Sikarugir-App/d9vk>
- Release: `v1.10.3-20250511`, archive
  `d9vk-macOS-async-v1.10.3-20250511.tar.gz`
- Archive sha256:
  `13a088e96c90501705c26326044ccc57e2b03adda3ed0dd7061e89249d4c176e`
- Binary sha256:
  `26ffa15d6b59a712cf4b73496205239870b322e6ae915ecf05f1b6e86fca6a20`
- Licence: **zlib**, as DXVK. See the upstream `LICENSE`.

d9vk is DXVK's Direct3D 9 front end. This is the same DXVK *version* CrossOver
ships, built differently for macOS: CrossOver's cannot create a Vulkan device
on Apple silicon for this title and this one can.

## ng3-qasf.dll, ng3-quartz.dll, ng3-winegstreamer.dll — Wine, patched

- Upstream project: Wine — <https://www.winehq.org/>
- Patched builds by the author of **winevideo**, release 0.5
- Licence: **LGPL-2.1-or-later**, as Wine
- Binary sha256, as shipped here:
  - `ng3-qasf.dll` — `712c0d7924f1a5200abcb3ebe63c1343704b9549bd956a0860871b39662f3ca5`
  - `ng3-quartz.dll` — `d938b12c1affb545c57b4f9a4bb0608d3b8dfdea999f4e8bfdb4d6c89fd8ca6e`
  - `ng3-winegstreamer.dll` — `253d2eb299fac32f73ce8c3fe7528b8ae1e7b569e01c5a49b5775892cbac8f90`

The substantial change is in `qasf`. Wine's DirectShow ASF Reader delivered a
video sample on the WMReader callback; the downstream video pin blocked in
`Receive()`; audio needed the same callback to preroll and never got it; the
graph sat in `VFW_S_STATE_INTERMEDIATE` instead of reaching Running. The
patched build moves blocking video delivery onto a single serialized worker so
audio can preroll, preserving video sample order and putting end-of-stream
behind accepted samples.

That diagnosis and that implementation are winevideo's author's, not this
project's. This project measured the codec (ASF/WMV3/WMAv2, with ffprobe), the
renderer (D3D9 only, unlike SIGMA and SIGMA 2), and why CrossOver's DXVK fails
— and spent an afternoon instrumenting Media Foundation before being told the
game uses DirectShow.

**LGPL source availability:** anyone redistributing these binaries must be able
to supply the corresponding source. Wine's is public; the patches are
winevideo's to publish. Ask before shipping these further afield.

## Checksums of the binaries as shipped

`check-builds.sh` reads this table. One line per file, name then sha256, so a
redistributed binary that has been swapped is caught here rather than on
somebody's machine.

| file | sha256 |
| --- | --- |
| `ng3-d3d9.dll` | `26ffa15d6b59a712cf4b73496205239870b322e6ae915ecf05f1b6e86fca6a20` |
| `ng3-qasf.dll` | `712c0d7924f1a5200abcb3ebe63c1343704b9549bd956a0860871b39662f3ca5` |
| `ng3-quartz.dll` | `d938b12c1affb545c57b4f9a4bb0608d3b8dfdea999f4e8bfdb4d6c89fd8ca6e` |
| `ng3-winegstreamer.dll` | `253d2eb299fac32f73ce8c3fe7528b8ae1e7b569e01c5a49b5775892cbac8f90` |
