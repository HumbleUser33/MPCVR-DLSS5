# MPC Video Renderer + DLSS 5 Neural Rendering

A fork of [Aleksoid1978/VideoRenderer](https://github.com/Aleksoid1978/VideoRenderer)
that adds an optional NVIDIA **DLSS 5 Neural Rendering** pass to the Direct3D 11
pipeline. Nothing else changes: with the option off, the renderer behaves exactly
as upstream does.

**→ [What it does, what it needs, how to build it](README-DLSS5.md)**

Two things to know before you start: the feature needs NVIDIA's `nvngx_dlssnr.dll`,
which is **not included here** and which you have to supply yourself, and it is x64
only.

For the unmodified renderer, go to [upstream](https://github.com/Aleksoid1978/VideoRenderer) —
that is where the releases, the issue tracker and the actual development are.

---

## About MPC Video Renderer

MPC Video Renderer is a free and open-source video renderer for DirectShow. The renderer can potentially work with any DirectShow player, but full support is available only in the MPC-BE. Recommended MPC-BE 1.8.9.106 or newer.

## Key features

* Can work with DXVA2 and Direct3D 11 hardware decoder.
* DVXA2 and Direct3D11 Video Processor with hardware de-interlacing for NV12, YUY2, P010 formats.
* Shader video processor for various YUV, RGB and grayscale formats.
* Various frame resizing algorithms, including Super Resolution.
* Subtitle and OSD display.
* Rotation and flip of the video frame.
* Dithering when the final color depth is reduced from 10/16 bits to 8 bits.
* HDR video support (HDR10, HLG and partially Dolby Vision).
* Automatic HDR to SDR conversion.
* Transferring HDR10 data to the display.

## Minimum system requirements

* An SSE2-capable CPU
* Windows 7¹ or newer
* DirectX 9.0c (PS 3.0) video card

¹For Windows 7, you must have D3DCompiler_47.dll file. It can be installed via update KB4019990.

## Recommended system requirements

* An SSE2-capable CPU
* Windows 10 or newer
* DirectX 10/11 video card

## License

MPC Video Renderer's code is licensed under [GPL v3]. This fork is a derivative work
and carries the same licence.

## Download

[Releases](https://github.com/Aleksoid1978/VideoRenderer/releases)

[Nightly builds](https://github.com/Aleksoid1978/VideoRenderer/wiki/Nightly-builds)

## Links

[Topic in MPC-BE forum (Russian)](https://mpc-be.org/forum/index.php?topic=381)

[MPC-BE](https://github.com/Aleksoid1978/MPC-BE)

## Donate

<https://mpc-be.org/forum/index.php?topic=240>
