# Control Resonant: the window-interior shaders

Found 2026-09-26 by hiding pixel shaders on D3DMetal 4.0b2 (Metal 4 OFF, no
D3DM_* variables) through diagnostics/ctl-shader-dump.c, which mutes a pipeline
by setting every render target's write mask to 0. Every round below was checked
in pipelines.log: the listed pipelines were muted, none left unmuted.

| Round | Hidden | Windows | Rest of the frame |
|---|---|---|---|
| 1 | 18 PS that bind a TextureCube | unchanged | unchanged |
| 2 | every pipeline with RT0 blending on | gone | lighting, UI gone |
| 3 | 13 blending PS writing format 10 (RGBA16F) | unchanged | UI gone, lighting fine |
| 4 | 15 blending PS writing format 26 (R11G11B10) | gone | some ambient light missing |
| 5 | group A (8 of the 15) | correct from one angle (frosted glass), still wrong from another, less | UI and the rest fine |
| 6 | A1 = 04915701, 0a3c2391, 0e5b057b, 2b377228 | wrong from every angle again | fine |
| 7 | 46cad82e, 4ce74ec0 | modern glass-facade building correct; brick building (Bruno's) windows blown white with red rims, as they already were -- a separate effect | fine |
| 8 | 46cad82e alone | modern building: one window correct, its neighbour still wrong -- 46cad82e and 4ce74ec0 both draw these windows (variants) | fine |
| 9 | 46cad82e, 4ce74ec0 + all of B | both buildings' windows black, nothing blown out -- the brick building's effect is in B, and B also draws the modern building's frosted-glass layer | fine |
| 10 | 46cad82e, 4ce74ec0 + B1 (7123e119, 77d242cc, a4bc361e, a6adb879) | brick building mostly correct, one pane with a colour smear; modern building blown out again -- view- and time-dependent, too noisy to bisect further | fine |
| 11 | **interiorPS (cc6f0f9f) alone** | **no colour anywhere, on either building, from any angle; windows dark, no room interiors** | fine |

## Entry points (llvm-dis reads the DXIL bitcode)

04915701 emissionPS, 0a3c2391 emissionPS, 0e5b057b fillPS, 2b377228 mainColor,
46cad82e meshParticleEmissionPS (pass-through, cannot be mistranslated),
4ce74ec0 mainPS, 51149093 flareMainPS, 691f2281 / 7123e119 / a6adb879
character_lightPS, 77d242cc drawPlanePS, a4bc361e pmediaDensityPS,
**cc6f0f9f interiorPS**, eede4b32 emissionPS, f56b748d addReflectionWithBRDF_PS.

interiorPS turns a direction into equirectangular UVs (atan, acos) and samples
an unbounded `Texture2D[]` (space 1) at LOD 0 and LOD 5, with the index
`umin(a, 15) + (b << 4)` and NonUniformResourceIndex set. Flat blue/violet is
what a normal map looks like, yellow/white/magenta what masks look like: the
hypothesis is that the index lands on the wrong texture of the array.

So at least one culprit is in group A and at least one in group B.

| Pixel shader | Group | Pipelines (render targets, blend) | Vertex shaders |
|---|---|---|---|
| `04915701348db2b9` | A | 26,26 blend off; 26,26 blend on | `69323657fed7c85b` |
| `0a3c239124c7c99e` | A | 26,26 blend off; 26,26 blend on | `69323657fed7c85b` |
| `0e5b057b2925f069` | A | 26 blend off; 26 blend on | `1dd3001a4f43fb74` |
| `2b377228c42b74c7` | A | 10 blend off; 26 blend on | `3acf15f33d2fad11` |
| `46cad82e7acd1d75` | A | 26,26 blend off; 26,26 blend on | `86bc52c7305bd3d9` |
| `4ce74ec0a6f8d03e` | A | 10 blend off; 26 blend on | `c22c2b18badd853b` |
| `5114909385ec3fc4` | A | 10 blend off; 26 blend on | `a7eb7f12a9452ef9` |
| `691f228129040d34` | A | 26,26 blend off; 26,26 blend on | `f7dd77294215ba97` |
| `7123e119a107f77a` | B | 26,26 blend off; 26,26 blend on | `f7dd77294215ba97` |
| `77d242ccc25894e9` | B | 10 blend off; 26 blend on | `0158c926a8d81db9` |
| `a4bc361ee8c3e983` | B | 26 blend off; 26 blend on | `7e029387a6111adf` |
| `a6adb8796770a6fb` | B | 26,26 blend off; 26,26 blend on | `f7dd77294215ba97` |
| `cc6f0f9fb056f7f5` | B | 26,26 blend off; 26,26 blend on | `08d62aaa8a6c01c2` |
| `eede4b32e51b995a` | B | 26,26 blend off; 26,26 blend on | `b839e4612030503b` |
| `f56b748d39b6b392` | B | 26 blend off; 26 blend on | `9961c0f6509e132d` |

The .dxbc of each is beside this file, named ps_<hash>.dxbc.

**Confirmed (round 11): interiorPS is the shader.** The user also saw the fault
"clone" onto windows that were correct a moment before -- what a
NonUniformResourceIndex treated as uniform would do: one lane's index used for a
whole SIMD group, so every window sharing that group reads the same wrong
texture. Not yet proven; it is the working hypothesis for a real fix.

## The fix (2026-09-26)

interiorPS was rebuilt -- dxil-spirv -> spirv-cross -> HLSL with the buffers'
real types and the original signature -> dxc (run under wine) -- with each of
its three texture reads wrapped in a WaveReadLaneFirst loop, so every distinct
index in a SIMD group is served on its own. The probe substitutes it at
pipeline creation (replace_<hash>.dxbc). ISG1, OSG1 and the PSV0 resources are
byte-identical to the original.

* First version: the game froze while loading. An unbounded loop never ends
  for a helper lane, which is not active for wave operations and so may never
  see its own index come first.
* Bounded to 64 iterations: **the window interiors render as on GeForce NOW
  (red-lit rooms)**, near buildings clean; some tinted specks remain on far
  buildings. This confirms that D3DMetal executes a NonUniformResourceIndex
  texture read as uniform.

65 captured pixel shaders index textures non-uniformly; only those whose index
varies inside a group show it. Next candidates for the far windows: the three
emissionPS (04915701, 0a3c2391, eede4b32), rewritten by
nonuniform-fix/fixshader.py (Sample -> SampleGrad with derivatives taken before
the loop).

## Where it ended (2026-09-26)

* 50 pixel shaders rebuilt the same way (every one that indexes textures
  non-uniformly and that the tool handles; 15 are refused: UAV writes,
  GetDimensions on a non-uniform texture). All 47 that the title created were
  substituted in 97 pipelines, none refused -- and the windows still turned to
  flat colours after about a minute of play, lower texture quality too.
* So a second fault sits outside the shaders. Working hypothesis, not measured:
  texture streaming rewrites descriptors in place, and D3DMetal does not make
  the new texture resident, so the argument-buffer read lands on another
  texture's memory. Nothing a shader can do about that.
* The user chose the practical fix: build this probe with -DCTL_FIX, which hides
  interiorPS only (windows dark, nothing else changes), no dumping, no
  replacements, a short log. Carried on PxFoundation_x64.dll.
