# DLSS 5 Neural Rendering for MPC Video Renderer

A fork of [Aleksoid1978/VideoRenderer](https://github.com/Aleksoid1978/VideoRenderer) that adds
an optional NVIDIA **DLSS 5 Neural Rendering** pass (NGX feature 18) to the Direct3D 11
video pipeline, with a temporal stabilizer made for video.

The renderer stays a Direct3D 11 filter. Nothing about its existing pipeline changes when
the feature is off, and the rendering is bit-identical to upstream in that state.

---

## What you need

| | |
|---|---|
| GPU | NVIDIA RTX. Developed and tested on an RTX 3050 (Ampere), driver r596 |
| OS | Windows 10 1703 or later (shared fences), x64 only |
| Player | MPC-BE or any DirectShow player that lets you force a renderer |
| DLL | `nvngx_dlssnr.dll` — **not included, see below** |
| Optical Flow | `nvofapi64.dll`, installed with the NVIDIA driver (RTX 20 series and later). Optional: without it the stabilizer uses its shader detector |

### The DLL is not in this repository

`nvngx_dlssnr.dll` is NVIDIA's property and is not redistributable, so it is not here and
will not be. At roughly 159 MB it also exceeds GitHub's per-file limit.

You supply it yourself. The filter looks for it, in order:

1. the path set in the property page, if any
2. next to `MpcVideoRenderer64.ax`
3. `<filter directory>\dlss\`
4. one and two directories above the filter

The **DLSS 5** property page has a **DLL** field and a browse button if you keep it elsewhere.

---

## Building

```
git clone --recursive https://github.com/<you>/<repo>.git
```

If you forgot `--recursive`: `git submodule update --init --recursive`.

Requirements: **Visual Studio 2022** with the **C++ MFC for v143 build tools (x86 & x64)**
component. Upstream declares `UseOfMfc`, and MSBuild enforces it even though no source
actually includes MFC.

```
build_mpcvr.cmd NoWait
```

Outputs `_bin\Filter_x64\MpcVideoRenderer64.ax` and, beside it,
`_bin\Filter_x64\dlss\nvngx.dll` — a small shim of ours, described below. Register the
filter with `distrib\Install_MPCVR_64.cmd`.

---

## How it works

Four things had to be solved. All of them were settled by measurement, not documentation —
there is none for this feature.

**The D3D11 backend of the snippet is inert.** It exports a complete D3D11 surface —
`Init_Ext`, `CreateFeature`, `EvaluateFeature`, an `NGXCubinD3D11` class, NVAPI cubin calls —
and every one of them returns `FAIL_FeatureNotSupported`. During nine init attempts with
different argument shapes the snippet issued *zero* NVAPI queries, so it is not a driver
gap: it refuses before asking the driver anything, and before initialising its own logging.
The D3D12 path, on the same device in the same process, works.

So `CDlssNR` builds a **private D3D12 device** on the same adapter (matched by LUID). The
renderer keeps allocating its textures on D3D11; they carry an NT share handle and are
opened a second time on the D3D12 side. Synchronisation is CPU-side in both directions —
see `Source/DLSS/DlssNR.h` for why that is deliberate.

**The snippet refuses unknown callers.** It walks back to the calling module and rejects
anything whose path does not contain `nvngx.dll`, returning `0xBAD00002` and logging
"Not called from NGX runtime". `NgxShim/` builds a small `nvngx.dll` of forwarders that the
guarded entry points are called through. It must not be tail-call optimised, or the return
address is the caller's again and the check fails from inside the shim.

**The architecture check.** The snippet asks NVAPI for the GPU architecture in
`NGXCubinGeneric::SetGPUArch` and refuses anything below its own declared minimum
(`0x1B0`, Blackwell). `CDlssNR` hooks `NvAPI_GPU_GetArchInfo` with MinHook and reports the
minimum the snippet itself declares — nothing is invented. It has to be installed before
the snippet loads, because the value is read once during init and cached.

**Video has no motion vectors and no depth.** The network runs colour-only at
`ScalingRatio 1.0`: `DLSSNR.Depth` is never set, and `DLSSNR.MVec` only when the stabilizer is
asked to pass its Optical Flow vectors on. Left unset, the DLL's own log confirms it runs
that way (`EvaluateFeature Color=... MVec=0000000000000000 Depth=0000000000000000`). Vectors
alone do not buy temporal stability — see the next section.

Other findings worth knowing: the SDK version must be **0x13** (the driver core rejects
anything newer with `FAIL_OutOfDate`), the driver's NGX core does not know feature 18 at all
so the snippet must be loaded directly, and the working set is about **500 MB of video
memory at 1080p**, growing with resolution.

---

## Temporal stabilizer

The network blends each frame with its own previous output (`CC_Control_History_Blend`,
`dlssnr_prev_output`). A game keeps that history aligned with motion vectors; video has
none, and the result is a luminance shimmer: on real pictures, plain DLSS shows about 1.5×
the low-frequency flicker of its own input. The temporal suites of the harness (below)
measured what this DLL does with its inputs:

- **Motion vectors do not realign the history.** Their convention could be found (pixels,
  current to previous, scale 1), but once the history is in use, correct, inverted and absent
  vectors measure the same. Given as `DLSSNR.MVec`, Optical Flow vectors cut the flicker only
  part of the way and change the rendering around moving objects. Depth changes nothing.
- **Left alone, the history does nothing measurable**: it measures the same as no history.
- **`DLSSNR.ControlMask` steadies, but it strips the effect.** It weighs the history per
  pixel, and with any mask bound only 10 % (mask at 1) to 36 % (mask at 0, a frozen picture)
  of the network's change to the picture is left (`--teffect`, on a photo). The first version
  of this stabilizer drove that mask from a motion detector; in the player it looked exactly
  like DLSS turned off. DLSS5-Reshade-AIO leaves the parameter unused as well.

So the stabilizer works **after** the network and never touches its inputs
(`Source/DLSS/DlssStabilizer.cpp`, `Shaders/d3d11/ps_dlss_stabilize.hlsl`):

1. **Only the effect is steadied.** The network's change to the picture, E = output − input,
   is filtered over time and added back to the current input. The video itself is never
   delayed, so a cut or fast motion cannot leave a ghost of the picture — at most of the
   enhancement.
2. **The history is clamped** to the range of E in the current 3×3 neighbourhood, ± 0.02, so a
   wrong motion estimate cannot drag old values far.
3. **Motion** comes from one of two sources, chosen on the page:
   - **NVIDIA Optical Flow** (default): forward and backward flow from the driver's
     `nvofapi64.dll`, computed at about 540 lines whatever the video (4×4 grid, medium
     quality). The history is moved along the vectors, so moving areas are steadied too. A
     vector is trusted according to its forward/backward consistency, its matching cost, and
     how well the previous input, moved along it, matches the current one; where trust falls,
     the network's output for that picture is used as it is.
   - **Shader detector**: the four passes of the first version (`ps_dlss_motion.hlsl`) at a
     quarter of the resolution, with a noise floor measured on the video, a dilation and an
     8-picture hold. It has no vectors, so it steadies only what stands still.

   If Optical Flow cannot start (a GPU older than RTX 20, an old driver) or keeps failing,
   the detector takes over; the statistics and the page's status line say which one runs.
4. **Optionally**, the Optical Flow vectors also go to the network as `DLSSNR.MVec`.

Only a new picture moves the history on. A redraw — paused video, the OSD, a change that
brings no new picture — is steadied again against the same history as its picture, so it
shows what changed without taking a second step.

Measured on a photo with grain and 8×8 block noise, 1920×1080 windows moving with known
motion (`--tstab`, default network settings, RTX 3050). Flicker is the frame-to-frame change
of blurred luma, compensated by the true motion, as a ratio to the input's; *effect* is how
much of plain DLSS's change to the picture is left:

| | Still | Slow pan | Pan | Moving object | Cut | Effect |
|---|---|---|---|---|---|---|
| Plain DLSS | 1.44× | 1.59× | 1.56× | 1.49× | 1.47× | 100 % |
| **Stabilizer, Optical Flow** | 1.06× | 1.11× | 1.14× | 1.09× | 1.06× | 100 % |
| Stabilizer, shader detector | 1.02× | 1.55× | 1.56× | 1.12× | 1.13× | 100 % |
| Stabilizer, true motion (the ceiling) | 1.02× | 1.04× | 1.04× | 1.04× | 1.02× | 100 % |
| Vectors to DLSS, no stabilizer | 1.22× | 1.29× | 1.29× | 1.22× | 1.23× | 97–100 % |
| Vectors to DLSS + stabilizer | 1.00× | 1.03× | 1.05× | 1.02× | 1.01× | 96–100 % |

With Optical Flow the stabilizer removes about 80 % of the flicker DLSS adds, moving content
included, and stays within 0.0005 of plain DLSS on average (0.0013 on edges and where the
object moved). Passing the vectors to the network as well steadies a little more, but the
network then renders differently: 0.0032 away from plain DLSS where the object moved, 2.5
times the stabilizer alone — which is why that box is off by default. A variant filtering the
whole output, TAA-style, was measured too and dropped: it steadies more by also smoothing the
video itself, its grain and its edges.

Checked in the harness (`--tstabport`): the filter's own class, with its shaders embedded as
in the filter, measures exactly what the reference passes measure — every sequence, every
motion source, vectors to DLSS included — and each of 285 redraws shows, bit for bit, what
its picture showed.

Cost on an RTX 3050 (`--tstabbench`, GPU time per picture):

| Working size | Optical Flow | of which flow and vectors | Shader detector |
|---|---|---|---|
| 1920×1080 | 2.8 ms | 2.2 ms | 0.8 ms |
| 3840×2160 | 5.5 ms | 3.0 ms | 2.9 ms |

The CPU side stays under half a millisecond: nothing waits for the GPU. Memory: about 95 MB
at 1080p — the history, two input copies and the result at the working size — and 375 MB at
2160p, plus the Optical Flow engine's own buffers. With the stabilizer at 0, nothing is
allocated and nothing runs.

The detector itself was validated against a perfect motion map on synthetic sequences
(`--tdetect`): no lag on fades and slow pans, no added smear around moving objects, and
repeated pictures (variable frame rates, 25p stored as 50p) handled like the perfect map.

---

## Settings

Everything lives on the **DLSS 5** page of the renderer's properties (x64 builds only) and is
stored under `HKCU\Software\MPC-BE Filters\MPC Video Renderer`.

| Setting | Default | Notes |
|---|---|---|
| Enable | off | Forces 16-bit float internal textures |
| Apply after upscaling | off | Runs at display resolution instead of source. Much heavier at 4K |
| Toggle key | F12 | Toggles during playback. The filter swallows this key |
| DLL | empty | Empty means search the locations listed above |
| Style | Default | Default / Natural / Cinematic. Applies live |
| Preset | 0 | **No effect with the DLL builds seen so far** — they ship a single network and every preset falls back to it |
| Auto mask | on | |
| Intensity | 1.50 | |
| Local tone | 0.30 | Low on purpose: the local terms amplify frame-to-frame variation |
| Local struct. | 0.50 | |
| Skin struct. | 0.90 | |
| Stabilizer | 100 | 0–100. Where the history is trusted, the current picture weighs from 1 down to 0.25 at 100, the measured setting. 0 runs nothing. Applies live |
| Motion | NVIDIA Optical Flow | Or *Shader detector (still areas)*. Applies on the next picture |
| Send the motion vectors to DLSS | off | Optical Flow only. Steadier, but the network renders differently around moving objects |
| Disable temporal history | off | Forces `DLSSNR.Reset` every frame. The stabilizer is not affected |

**Default** on the page resets the tuning, from Style to Disable temporal history, motion
settings included; it leaves Enable, the key and the DLL path alone. Applying the page sends
only what was changed on it, so it never undoes the toggle key or the main page, and the main
page leaves these settings alone.

The feature is also reachable programmatically through `IExFilterConfig`:
`Flt_SetBool("dlssNR", true/false)` and `Flt_GetBool("dlssNR", &b)`.

---

## Known limitations

**Some shimmer remains.** With Optical Flow about 1.1× the input's flicker is left on moving
content, and the shader detector leaves moving areas as the network rendered them. Lowering
**Local tone** and **Local struct.** still helps, which is why they default low.

**Where Optical Flow is unsure** — large flat or dark areas, occlusions, transparency — trust
falls and those pixels get the network's output for that picture alone. The clamp keeps any
mistake within the local range of the effect.

**Motion the detector misses.** In detector mode, motion finer than the noise over nearly
flat areas can go undetected, and the steadied effect then lags behind it. Use Optical Flow,
or lower the stabilizer.

**Memory.** ~500 MB of VRAM at 1080p for DLSS, more at higher resolutions, plus a 159 MB
module resident while the session is up. The stabilizer adds about 95 MB at 1080p and 375 MB
at 2160p, plus what the Optical Flow engine allocates.

**Cost.** Two CPU stalls per frame for the cross-device synchronisation, plus the network
itself and the stabilizer. Fine for video framerates; this is not a low-latency design.

---

## Diagnostic tools

`tools/dlssnr_probe/` builds three programs; `build.cmd` builds them.

**`dlssnr_probe.exe`** — talks to the snippet directly and reports what it says about
itself: caller validation, the parameter vtable layout, `GetFeatureRequirements`, which
NVAPI entry points it resolves and which the driver refuses, and a full D3D12
create/evaluate cycle. Useful on a new GPU, a new driver or a different DLL build.

```
dlssnr_probe.exe                 report only, nothing altered
dlssnr_probe.exe --spoof-auto    report the architecture the DLL asks for
dlssnr_probe.exe --spoof 1B0     report a specific architecture id
```

**`dlssnr_harness.exe`** — compiles `Source/DLSS/DlssNR.cpp` and the renderer's own
`Tex2D_t` against a real D3D11 device and runs 300 frames with toggles, resolution changes
and preset changes, then a teardown with work in flight. Not a mock: it is the filter's
code. Run it before putting a build in a player.

```
dlssnr_harness.exe --frames 300
```

It should end with `0 check(s) failed`.

Its temporal suites measure the network on sequences whose true motion is known, and write
their tables next to the program. Run them from `tools/dlssnr_probe`: the reference passes
compile the shaders from `Shaders/d3d11`, and `build.cmd` also embeds them the way the filter
does, so the filter's own classes (`DlssMotionMask.cpp`, `DlssOpticalFlow.cpp`,
`DlssStabilizer.cpp`) run unchanged.

```
dlssnr_harness.exe --temporal     motion vector conventions, depth, history
dlssnr_harness.exe --tmask        ControlMask values on still and moving content
dlssnr_harness.exe --toracle      a perfect motion map as the mask
dlssnr_harness.exe --tdetect      the shader detector against that map
dlssnr_harness.exe --tport        the filter's detector class against the harness detector
dlssnr_harness.exe --tbench       what the detector costs at 1080p and 2160p
dlssnr_harness.exe --teffect      how much of the effect each ControlMask value leaves
dlssnr_harness.exe --tflow        NVIDIA Optical Flow: direction, units, accuracy, timings
dlssnr_harness.exe --tstab        the post-DLSS stabilizer variants on real pictures
dlssnr_harness.exe --tstabport    the filter's stabilizer class against those passes, redraws included
dlssnr_harness.exe --tstabbench   what the stabilizer costs at 1080p and 2160p
```

`--tframes N` sets the frames per run; `--tstrong` uses the strongest network settings;
`--timage <file>` picks the photo for `--teffect`, `--tflow` and `--tstab` (a Windows 11
wallpaper by default).

**`vp_rebuild_test.exe`** — checks that rebuilding the hardware video processor keeps the
picture, which is what used to show a green frame when DLSS was toggled while paused.

---

## Licence and credits

MPC Video Renderer is by **Aleksoid1978** and contributors and is licensed **GPLv3**; see
`LICENSE.txt`. This fork is a derivative work and carries the same licence.

The NGX ABI declarations in `Source/DLSS/NGXTypes.h` are hand-written from the publicly
documented shape of the interface. NVIDIA DLSS, NGX and the `nvngx_*` / `_nvngx.dll`
binaries are NVIDIA property under their own licences and are not distributed here.

`Source/DLSS/NvOF/` holds the two interface headers of the NVIDIA Optical Flow SDK 5.0.7,
copied unchanged. Each carries its own permission notice ("This copyright notice applies to
this header file only"), which allows copying and redistribution. Nothing else from the SDK
is used or included; the Optical Flow engine, `nvofapi64.dll`, comes with the NVIDIA driver.
