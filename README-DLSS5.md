# DLSS 5 Neural Rendering for MPC Video Renderer

A fork of [Aleksoid1978/VideoRenderer](https://github.com/Aleksoid1978/VideoRenderer) that adds
an optional NVIDIA **DLSS 5 Neural Rendering** pass (NGX feature 18) to the Direct3D 11
video pipeline, with a temporal stabilizer made for video, and an experimental option to
enlarge the picture with **DLSS Super Resolution** (NGX feature 1) instead of the resize
shaders.

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
| DLSS Super Resolution | Optional: `nvngx_dlss.dll` 310.5 or later (DLSS 4.5), from the public [NVIDIA/DLSS](https://github.com/NVIDIA/DLSS) repository (`lib/Windows_x86_64/rel`). Developed with 310.9.1 |

### The DLL is not in this repository

`nvngx_dlssnr.dll` is NVIDIA's property and is not redistributable, so it is not here and
will not be. At roughly 159 MB it also exceeds GitHub's per-file limit.

You supply it yourself. The filter looks for it, in order:

1. the path set in the property page, if any
2. next to `MpcVideoRenderer64.ax`
3. `<filter directory>\dlss\`
4. one and two directories above the filter

The **DLSS 5** property page has a **DLL** field and a browse button if you keep it elsewhere.
`nvngx_dlss.dll`, for DLSS Super Resolution, is searched the same way and has its own field;
it must keep its name, because the driver's NGX runtime loads it by name from that folder.

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
the snippet loads, because the value is read once during init and cached. The spoofed answer
goes only to calls made from `nvngx_dlssnr.dll` itself: every NGX snippet asks the same
question, and DLSS Super Resolution told it runs on Blackwell picks kernels an Ampere or Ada
card cannot run, writes nothing and removes the device.

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

## DLSS Super Resolution (experimental)

**Use DLSS SR 4.5 for upscaling**, on the DLSS page, enlarges the picture with DLSS Super
Resolution instead of the **Upscaling** method of the main page, which is then greyed. It is
independent of DLSS 5 NR: another DLL, another session, and it works with NR on or off. When
NR runs before upscaling, SR takes its output.

**How it runs.** Unlike the NR snippet, this feature runs on Direct3D 11 through the display
driver's NGX runtime (`_nvngx.dll`), which loads `nvngx_dlss.dll` itself: no second device,
no shared textures, no waits (`Source/DLSS/DlssSR.cpp`). Every call into NGX runs in a
separate device context state, so NGX's compute bindings never reach the renderer's passes;
the harness checks that the renderer's own resize gives the same picture, bit for bit, before
and after an evaluation. Several things were settled by measurement: the runtime's
`NVSDK_NGX_D3D11_Init_Ext` works (its plain `Init` answers `OutOfDate` once a current snippet
is on the path, and `Init_ProjectID` with the documented argument order crashes), and its
`Shutdown1(device)` crashes where `Shutdown()` ends the session cleanly, leaves an NR session
in the same runtime working, and allows a new one.

**Inputs.** Video has no jitter and no depth: the input is the picture at its own size, the
depth a constant plane, and the motion vectors come from NVIDIA Optical Flow — lent by the NR
stabilizer when it runs on the same picture, otherwise from a motion-only estimator at about
540 lines. A redraw gets no vectors. The quality mode follows the scale (Quality below ×1.6,
Balanced below ×1.85, Performance below ×2.5, Ultra Performance above) and DLSS picks the
model for it unless a preset is set: with 310.9.1, **M** at ×2, **L** at ×3, **K** below.
Past ×4 DLSS stops there and the resize shaders do the rest; where the picture does not grow
on both axes, or no feature can be made for a size, the Upscaling method runs as before.

**Cost** on an RTX 3050 6 GB (`--tsr`, GPU time per frame):

| Source → output | Mode, preset | Time |
|---|---|---|
| 1920×1080 → 3840×2160 | Performance, M (default) | 23.3 ms |
| same | preset J / K / L | 10.1 / 11.2 / 31.6 ms |
| 1280×720 → 3840×2160 | Ultra Performance, L (default) | 17.2 ms |
| 1920×800 → 3840×1600 | Performance, M | 16.3 ms |
| 1920×1080 → 2560×1440 | Quality, K | 4.2 ms |

Add the Optical Flow vectors, 1.6 to 2.2 ms at 1080p, when NR does not lend them.

**Quality — read this before turning it on.** `--tsrq` pans windows of six 4K film frames by
half a source pixel per frame, reduces them to the source, grains or compresses them per
frame, and measures every method against the reference it never saw. Averages over the six
frames (PSNR in dB; *grain* is the fine detail left in flat areas, the source keeps about 2.4):

| | Clean, still | Clean, moving | Grain left | Compressed, still | Compressed, moving |
|---|---|---|---|---|---|
| Catmull-Rom | **55.9** | **55.9** | 2.40 | **46.9** | **46.8** |
| Lanczos3 | 54.7 | 54.7 | 2.55 | 46.5 | 46.4 |
| **DLSS SR, Optical Flow vectors** | 50.6 | 49.7 | **0.73** | 46.2 | 45.5 |
| DLSS SR, no vectors | 51.8 | 49.2 | 0.45 | 46.5 | 45.3 |
| DLSS SR, exact motion (not available in playback) | 51.8 | 54.3 | 0.45 | 46.5 | 49.7 |

With the vectors a player can compute, DLSS SR is **less faithful than Catmull-Rom**: 5 to 6
dB below on clean film frames, about 1 dB below on compressed ones, and it **removes about
70 % of the film grain** over time. Without jitter a still picture gives it nothing new, and
Optical Flow is not precise enough for it to accumulate detail on a pan: finer flow — the
source size, a vector per pixel, the slowest search — measured no better, and worse on still
pictures. Only with exact motion does it beat the filters, on compressed moving pictures. The
option is therefore off by default and marked experimental; your eyes decide whether its
cleaner look is worth it on your films.

---

## Upscaling: FSRCNNX and RAVU-zoom

Three entries at the end of the main page's **Upscaling** list enlarge the luma with a small
network instead of a filter kernel, the way mpv's prescalers do:

| Entry | What it is | Luma time, 1080p→4K, RTX 3050 |
|---|---|---|
| **FSRCNNX 8** | FSRCNNX_x2_8-0-4-1, a 4-layer convolutional network, doubles | 4.6 ms |
| **FSRCNNX 16** | FSRCNNX_x2_16-0-4-1, the same with 16 feature maps, doubles | 16.6 ms |
| **RAVU-zoom** | RAVU-Zoom-AR r3, trained edge-directed weights, to any size at once | 2.8 ms |

The colour comes from Catmull-Rom, which enlarges the picture as usual; the network's luma
then replaces its own, by adding the difference to R, G and B alike, so chroma is untouched.
Where the picture grows by more than the doubling (720p on a 4K screen) the resize shaders
finish the job, and where it grows by less they reduce what the network doubled, as mpv does.
Below 1.3× FSRCNNX does not run at all, and RAVU-zoom needs the picture to grow on both axes;
the statistics then name Catmull-Rom, which is what runs. They need Direct3D 11 at feature
level 11.0 (the passes are shader model 5); on Direct3D 9, on older hardware, and while DLSS
Super Resolution is enlarging the picture, Catmull-Rom stands in.

**Chroma upsampling** has a fourth entry, **RAVU-zoom**, which brings Cb and Cr to the luma
size with the same shader, each plane on its own and placed where the video's chroma siting
puts it (MPEG-2, co-sited or centred). It runs on 4:2:0 through the shader video processor;
elsewhere — 4:2:2, the hardware video processor, Direct3D 9 — Catmull-Rom does it.

What they are worth, on seven 4K film frames reduced and enlarged again (`--tupscale`,
`--tchroma`), as PSNR on the most detailed quarter of the picture, against Catmull-Rom:

| Method | 1080p→4K | 720p→4K | 1080p→4K, compressed |
|---|---|---|---|
| FSRCNNX 16 | **+1.80 dB** | **+2.65 dB** | −0.33 dB |
| FSRCNNX 8 | +1.33 dB | +2.36 dB | −0.42 dB |
| RAVU-zoom | +1.26 dB | +1.68 dB | −0.02 dB |
| Jinc2m | −4.87 dB | −1.65 dB | −2.21 dB |

They restore edges close to the original's sharpness (0.96 to 0.97 where the original is 1.00
and Catmull-Rom 0.90) without the halos and the aliasing Jinc2m adds at 1.13. On compressed
sources every method lands within half a decibel of Catmull-Rom, and on grain RAVU-zoom
matches it while FSRCNNX keeps a little more of it. For chroma none of them beats Catmull-Rom
on film: RAVU-zoom is about a decibel under it overall and a little better only on colour
edges, which is why Catmull-Rom is what the filter defaults to. The **defaults changed**:
Upscaling is Jinc2m and Chroma upsampling Catmull-Rom for a fresh installation; settings
already saved in the registry are left alone.

The statistics show what they cost, next to the DLSS lines:

    Prescale (ms) : FSRCNNX 16 8.4, RAVU-zoom chroma 1.9

Only what runs is listed. When the hardware video processor handles the source format it
converts the picture itself, chroma included, so the **Chroma upsampling** list has nothing to
do and no chroma line appears; the processor line says so:

    VideoProcessor: D3D11 VP, output to R10G10B10A2_UNORM, converts chroma

The main page greys a list that is out of service for that reason, on what the filter is
actually doing: **Chroma upsampling** while the video processor converts, **Upscaling** and
**Downscaling** while it also resizes (*Use for resizing*, which DLSS 5 NR and DLSS SR suspend
on their own), and **Upscaling** while DLSS SR enlarges. A list that a Dolby Vision or YCgCo
picture sends back to the shaders stays available.

Render ahead (below) covers them as well: with FSRCNNX 16 a 4K picture takes more than the 8 ms
the renderer allows itself, and without it the picture would reach the screen late.

The shaders come from mpv's user-shader collections and are translated to HLSL once, offline:
`Shaders/mpv/mpv_shaders.py` runs each pass through glslang and SPIRV-Cross — the route
libplacebo itself takes on Direct3D 11 — and writes `Shaders/mpv/<shader>/passNN.hlsl`, the
lookup tables as half floats, the table `Source/Upscale/MpvShaderTables.h` and the generated
blocks of `compile_shaders.cmd` and `MpcVideoRenderer.rc2`. `Source/Upscale/MpvShader.cpp` runs
them the way libplacebo does: each pass renders into a texture of the size its WIDTH and HEIGHT
give, reading the plane, what earlier passes saved and the tables. `--tmpvport` checks that
this gives the harness's pictures exactly, and that the chroma siting is applied.

---

## Render ahead

The renderer wakes up 8 ms before a picture's time and only then processes it, so whatever
the DLSS passes take beyond those 8 ms used to reach the screen late. The renderer's own *Sync
offset* on an RTX 4060: −7 ms without DLSS, +11 to +17 ms with DLSS 5 NR, +11 to +19 ms with
DLSS 5 NR and DLSS SR — and DLSS SR's GPU time comes on top, since the GPU runs it after
Present has returned. The delay also varied by several ms from picture to picture, enough to
move some pictures to the next refresh of a 60 Hz screen.

**Render ahead to keep audio sync** (Settings page, on by default) measures each picture from
the start of its processing to the GPU being done with it, and starts the next pictures earlier
by the slowest of the last 32 plus 3 ms. Each picture then waits, while the GPU finishes it,
for the moment the renderer presents at without DLSS: half a refresh before its time, on the
reference clock. Presentation stays on the audio clock. It never waits for the GPU itself: a
picture not finished at its present time is presented anyway, reaches the screen once the GPU is
done, as it would have without render ahead, and moves the start earlier. It does nothing while
neither DLSS nor a luma prescaler runs, and a picture cannot start before the previous one has
been presented: about a frame earlier at most, never more than 60 ms.

Measured in a DirectShow graph with the filter itself (`playback_test.exe`): an 800×450 film in
a 1280×720 window on an RTX 3050 and a 60 Hz screen, with DLSS 5 NR and DLSS SR (NR 23 ms,
stabilizer 1.6 ms, SR 3.2 ms), 20 s per run. Sync offset, mean (5th…95th percentile), and
skipped pictures:

| Film | Render ahead off | Render ahead on |
|---|---|---|
| 23.976 fps | +15.1 ms (+14…+16), 0 skipped | **−6.4 ms (−7…−6)**, 0 skipped |
| 29.97 fps | +17.1 ms (+16…+19), 0 skipped | **−6.5 ms (−7…−6)**, 0 skipped |
| 59.94 fps, more than this GPU can do | +20.2 ms, 707 skipped | +19.9 ms, 707 skipped |

Without DLSS the same film measures −7.2 ms (−8…−6). A first version waited for the GPU before
presenting; at 29.97 fps that took the time from the next picture and skipped 25 of them in
20 s. At 29.97 fps most pictures are counted *late* below: the GPU finishes DLSS SR a moment
after the present time, because the source cannot hand over a picture earlier than a frame
minus its own time. Render ahead does not make the chain faster than the video: at 50 or 60 fps
with DLSS 5 NR pictures are still skipped, as they were.

The statistics show where the time goes and what render ahead does:

    DLSS (ms)     : NR 23.0, stabilizer 1.7, SR 3.3
    Render ahead  : 34 ms (ready in 29.8, max 30.6), late 10

*ready in* is the whole picture, from the start of processing to the GPU being done: the mean
and the slowest of the last 32. *late* counts pictures the GPU had not finished at their present
time. A few at start-up are normal; a count that keeps growing means the chain barely fits the
frame rate, and a lighter DLSS SR preset gives it room. DLSS 5 NR is timed where the renderer
waits for it, the other stages with GPU timestamps while the statistics are shown.
*Times(ms): Present* includes the wait.

---

## Settings

Everything lives on the **DLSS** page of the renderer's properties (x64 builds only) and is
stored under `HKCU\Software\MPC-BE Filters\MPC Video Renderer`. Render ahead is the exception:
it serves the luma prescalers as much as DLSS, so it sits on the **Settings** page, where the
32-bit builds reach it too. Its registry key, `DlssRenderAhead`, did not change.

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
| Use DLSS SR 4.5 for upscaling | off | Experimental, see above. Greys the main page's Upscaling list while it is on |
| Preset (DLSS SR) | Automatic | Or J, K, L, M. Applies on the next picture |
| DLL (DLSS SR) | empty | `nvngx_dlss.dll` or its folder. Empty means search next to the filter and up |

On the **Settings** page, bottom right:

| Setting | Default | Notes |
|---|---|---|
| Render ahead to keep audio sync | on | See above. Used while DLSS 5 NR, DLSS SR or a luma prescaler runs. Needs Direct3D 11 |

**Default** on the DLSS page resets the tuning, from Style to Disable temporal history, motion
settings and the DLSS SR preset; it leaves Enable, Use DLSS SR, the key, both DLL paths and
render ahead, which belongs to the Settings page, alone. Applying the page sends only what was
changed on it, so it never undoes the toggle key or the main page, and the main page leaves
these settings alone.

The feature is also reachable programmatically through `IExFilterConfig`:
`Flt_SetBool("dlssNR", true/false)` and `Flt_GetBool("dlssNR", &b)`. `Flt_GetString("statsText")`
returns the statistics as they are drawn on the picture, for tools that log them.

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
itself and the stabilizer. Fine for video framerates; this is not a low-latency design. Render
ahead keeps pictures on time as long as a picture takes less than about a frame.

---

## Diagnostic tools

`tools/dlssnr_probe/` holds four programs; `build.cmd` builds them.

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
dlssnr_harness.exe --tpipeline    the whole DLSS chain as the renderer runs it: CPU and GPU time per stage
dlssnr_harness.exe --tsr          DLSS Super Resolution: bring-up, presets, scales, costs, NR next to it
dlssnr_harness.exe --tsrq         DLSS SR against the resize shaders on moving film frames
dlssnr_harness.exe --tupscale     the resize shaders and the mpv prescalers on film frames
dlssnr_harness.exe --tupscalecost what EfRLFN costs at film sizes
dlssnr_harness.exe --tchroma      the chroma upsamplers on 4:2:0 made from film frames
dlssnr_harness.exe --tmpvport     the filter's prescaler runner against the harness's
```

`--tframes N` sets the frames per run; `--tstrong` uses the strongest network settings;
`--timage <file>` picks the photo for `--teffect`, `--tflow` and `--tstab` (a Windows 11
wallpaper by default). `--nonr` skips the DLSS 5 NR session for the suites that do not need
it, `--srdll <path>` points at `nvngx_dlss.dll`, and `--srrefs N` limits `--tsrq` to the
first N references. `--tupscale`, `--tchroma`, `--tmpvport` and `--tsrq` read 4K film frames
from `tools/dlssnr_probe/upscale_refs/`. `--tupscale` also runs the mpv shaders it finds
translated under `tools/dlssnr_probe/upscalers/hlsl/` (`Shaders/mpv/mpv_shaders.py` puts them
there); the three the filter embeds need nothing but the build.

A neural upscaler, EfRLFN, was measured too and not integrated: 922 ms per 1080p frame on the
RTX 3050 with DirectML (`--tupscalecost`), and no better than Catmull-Rom or Lanczos on clean
film frames (`--tupscale`).

**`vp_rebuild_test.exe`** — checks that rebuilding the hardware video processor keeps the
picture, which is what used to show a green frame when DLSS was toggled while paused.

**`playback_test.exe`** — plays a synthetic film through the built x64 filter
(`_bin\Filter_x64\MpcVideoRenderer64.ax`) in a DirectShow graph, in a window, on the system
clock: DLSS off, DLSS SR, and DLSS 5 NR with SR, render ahead off and on. It reads the filter's
statistics ten times a second and reports the Sync offset, skipped and late pictures, and how
long pause, run and stop take; it fails when a state change takes more than a second. The
settings go to the filter for the run only, nothing is saved.

With `--scalers` it plays a still NV12 picture through the shader video processor with each
Upscaling and Chroma upsampling method in turn, reports what the statistics say they did and
cost, saves each displayed picture as `scalers_<n>.bmp` and compares them with the Catmull-Rom
one — a wrong pass shows up as a large difference. It also saves the same picture with the
statistics drawn over it, as `scalers_<n>_stats.bmp`, which is how the overlay's box is checked.
`--vp` leaves the hardware video processor the formats it is set for, to see it convert.

`--mainpage` and `--dlsspage` show a property page of the built filter for a few seconds and
save it as `proppage.bmp`, without a player; `--click <id>` then clicks one control and saves
the page again as `proppage_clicked.bmp`, which is how the greying is checked. The program
carries a Windows 10 manifest, without which the version helpers answer 6.2 and the pages grey
what the player would not; it turns HDR passthrough off for its runs in exchange, since the
filter may then switch the display's own HDR state, which is not what is being measured.

```
playback_test.exe [--seconds 20] [--size 800x450] [--window 1280x720] [--fps 23.976] [--only N]
playback_test.exe --scalers       each Upscaling and Chroma upsampling method (--vp: hardware)
playback_test.exe --dlsspage 10   shows the filter's DLSS page for 10 s instead
playback_test.exe --mainpage 10   the same for the Settings page, --click <id> clicks one control
```

---

## Licence and credits

MPC Video Renderer is by **Aleksoid1978** and contributors and is licensed **GPLv3**; see
`LICENSE.txt`. This fork is a derivative work and carries the same licence.

The NGX ABI declarations in `Source/DLSS/NGXTypes.h` are hand-written from the publicly
documented shape of the interface. NVIDIA DLSS, NGX and the `nvngx_*` / `_nvngx.dll`
binaries are NVIDIA property under their own licences and are not distributed here;
`nvngx_dlss.dll` comes from NVIDIA's own DLSS repository under its licence.

The prescalers under `Shaders/mpv/` are translated from mpv user shaders and keep their
authors' notices: **FSRCNNX** is Copyright (C) 2017-2021 **igv**
(`github.com/igv/FSRCNN-TensorFlow`) and **RAVU** is by **Bin Jin**
(`github.com/bjin/mpv-prescalers`), both under the **GNU Lesser General Public License 3.0 or
later**, whose text is in `Shaders/mpv/LICENSE.LGPL-3.0.txt`. Only the shaders are taken; the
translation to HLSL and everything that runs them is part of this fork and GPLv3 like the rest.

`Source/DLSS/NvOF/` holds the two interface headers of the NVIDIA Optical Flow SDK 5.0.7,
copied unchanged. Each carries its own permission notice ("This copyright notice applies to
this header file only"), which allows copying and redistribution. Nothing else from the SDK
is used or included; the Optical Flow engine, `nvofapi64.dll`, comes with the NVIDIA driver.
