# DLSS 5 Neural Rendering for MPC Video Renderer

A fork of [Aleksoid1978/VideoRenderer](https://github.com/Aleksoid1978/VideoRenderer) that adds
an optional NVIDIA **DLSS 5 Neural Rendering** pass (NGX feature 18) to the Direct3D 11
video pipeline.

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

### The DLL is not in this repository

`nvngx_dlssnr.dll` is NVIDIA's property and is not redistributable, so it is not here and
will not be. At roughly 159 MB it also exceeds GitHub's per-file limit.

You supply it yourself. The filter looks for it, in order:

1. the path set in the property page, if any
2. next to `MpcVideoRenderer64.ax`
3. `<filter directory>\dlss\`
4. one and two directories above the filter

The property page has a **DLL** field and a browse button if you keep it elsewhere.

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

**Video has no motion vectors and no depth.** It turns out none are needed:
`DLSSNR.MVec` and `DLSSNR.Depth` are left unset and the DLL's own log confirms it runs that
way (`EvaluateFeature Color=... MVec=0000000000000000 Depth=0000000000000000`). The network
runs colour-only at `ScalingRatio 1.0`.

Other findings worth knowing: the SDK version must be **0x13** (the driver core rejects
anything newer with `FAIL_OutOfDate`), the driver's NGX core does not know feature 18 at all
so the snippet must be loaded directly, and the working set is about **500 MB of video
memory at 1080p**, growing with resolution.

---

## Settings

Everything lives in the renderer's property page, in a **NVIDIA DLSS 5 Neural Rendering**
group, and is stored under
`HKCU\Software\MPC-BE Filters\MPC Video Renderer`.

| Setting | Default | Notes |
|---|---|---|
| Enable | off | Forces 16-bit float internal textures |
| Style | Default | Default / Natural / Cinematic. Applies live |
| Preset | 0 | **No effect with the DLL builds seen so far** — they ship a single network and every preset falls back to it |
| Auto mask | on | |
| Intensity | 1.50 | |
| Local tone | 0.30 | Low on purpose: the local terms amplify frame-to-frame variation |
| Local struct. | 0.50 | |
| Skin struct. | 0.90 | |
| Disable temporal history | off | Forces `DLSSNR.Reset` every frame. See the shimmer note |
| Apply after upscaling | off | Runs at display resolution instead of source. Much heavier at 4K |
| Toggle key | F12 | Toggles during playback. The filter swallows this key |
| DLL | empty | Empty means search the locations listed above |

The feature is also reachable programmatically through `IExFilterConfig`:
`Flt_SetBool("dlssNR", true/false)` and `Flt_GetBool("dlssNR", &b)`.

---

## Known limitations

**Shimmering.** A light but visible luminance flicker on moving content. The cause is
structural: the network is named `CC_Control_History_Blend_...`, allocates a
`dlssnr_prev_output` texture and blends each frame with the previous one. In a game that
blend is guided by motion vectors, and the renderer additionally jitters the camera by a
sub-pixel Halton offset each frame and hands the offset to the network — that is what
produces temporal stability. Video has neither, and `DLSSNR` exposes no jitter parameter at
all, so the mechanism that protects games is not available here.

Lowering **Local tone** and **Local struct.** helps, which is why they default low.
**Disable temporal history** trades the misaligned blend for no blend at all; which of the
two looks worse depends on the content. The real fix would be feeding computed optical flow
as `DLSSNR.MVec` — `NvOFAPICreateInstanceD3D11` is available — but the convention the
network expects is undocumented and unverified.

**Memory.** ~500 MB of VRAM at 1080p, more at higher resolutions, plus a 159 MB module
resident while the session is up.

**Cost.** Two CPU stalls per frame for the cross-device synchronisation, plus the network
itself. Fine for video framerates; this is not a low-latency design.

---

## Diagnostic tools

`tools/dlssnr_probe/` builds two programs. `build.cmd` builds both.

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

---

## Licence and credits

MPC Video Renderer is by **Aleksoid1978** and contributors and is licensed **GPLv3**; see
`LICENSE.txt`. This fork is a derivative work and carries the same licence.

The NGX ABI declarations in `Source/DLSS/NGXTypes.h` are hand-written from the publicly
documented shape of the interface. NVIDIA DLSS, NGX and the `nvngx_*` / `_nvngx.dll`
binaries are NVIDIA property under their own licences and are not distributed here.
