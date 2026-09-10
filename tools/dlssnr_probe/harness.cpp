// Exercises the actual filter code -- Source/DLSS/DlssNR.cpp and the real
// Tex2D_t from Source/DX11Helper.h -- against a real D3D11 device, outside
// MPC-BE.
//
// The point is that every bug that reached the player so far would have been
// caught here in seconds: the command-allocator reset that removed the device,
// the texture type that CheckCreate ignored, the fence deadlock on teardown.
// Nothing in this file is a mock. If a sequence survives here it should survive
// in the renderer.

#include "stdafx.h"
#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <cstdio>
#include <string>
#include "Helper.h"        // DX11Helper.h needs the plane-config types
#include "DX11Helper.h"
#include "DLSS/DlssNR.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

// DX11Helper.cpp carries a debug dump helper we never call. Stubbing it beats
// compiling Helper.cpp, which would drag in DirectShow, WIC and the CPU feature
// detection for no benefit here.
HRESULT SaveToBMP(BYTE* src, UINT src_pitch, UINT width, UINT height, UINT bitdepth, const wchar_t* filename)
{
	UNREFERENCED_PARAMETER(src); UNREFERENCED_PARAMETER(src_pitch);
	UNREFERENCED_PARAMETER(width); UNREFERENCED_PARAMETER(height);
	UNREFERENCED_PARAMETER(bitdepth); UNREFERENCED_PARAMETER(filename);
	return E_NOTIMPL;
}

static int g_failures = 0;

static void Head(const char* s) { printf("\n=== %s ===\n", s); }

static void Check(bool ok, const char* what)
{
	printf("  [%s] %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok) {
		g_failures++;
	}
}

static void Dump(const CDlssNR& dlss)
{
	const std::wstring info = dlss.GetInfoBlock();
	printf("%S", info.c_str());
}

// A D3D11 write into the shared input, the way the renderer's blit would be.
static void FillInput(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                      ID3D11Texture2D* tex, float phase)
{
	CComPtr<ID3D11RenderTargetView> rtv;
	if (SUCCEEDED(dev->CreateRenderTargetView(tex, nullptr, &rtv))) {
		const FLOAT c[4] = { 0.25f + 0.5f * phase, 0.5f, 0.75f - 0.5f * phase, 1.0f };
		ctx->ClearRenderTargetView(rtv, c);
	}
}

// Did the network actually write something?
static bool OutputIsNonZero(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* tex)
{
	D3D11_TEXTURE2D_DESC d = {};
	tex->GetDesc(&d);
	d.Usage = D3D11_USAGE_STAGING;
	d.BindFlags = 0;
	d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	d.MiscFlags = 0;

	CComPtr<ID3D11Texture2D> stage;
	if (FAILED(dev->CreateTexture2D(&d, nullptr, &stage))) {
		return false;
	}
	ctx->CopyResource(stage, tex);

	D3D11_MAPPED_SUBRESOURCE mr = {};
	if (FAILED(ctx->Map(stage, 0, D3D11_MAP_READ, 0, &mr))) {
		return false;
	}
	const uint16_t* px = (const uint16_t*)mr.pData;
	bool nonZero = false;
	for (int i = 0; i < 256 && !nonZero; i++) {
		if (px[i]) nonZero = true;
	}
	ctx->Unmap(stage, 0);
	return nonZero;
}

// Exactly the sizing and texture type UpdateTexures() uses in the filter.
static bool MakeSharedPair(ID3D11Device* dev, Tex2D_t& in, Tex2D_t& out, UINT w, UINT h)
{
	HRESULT hr = in.CheckCreate(dev, DXGI_FORMAT_R16G16B16A16_FLOAT, w, h, Tex2D_DefaultShaderRTargetUAVShared);
	if (SUCCEEDED(hr)) {
		hr = out.CheckCreate(dev, DXGI_FORMAT_R16G16B16A16_FLOAT, w, h, Tex2D_DefaultShaderRTargetUAVShared);
	}
	return SUCCEEDED(hr) && in.pTexture && out.pTexture;
}

int wmain(int argc, wchar_t** argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);
	printf("CDlssNR harness -- the filter's own code, outside the player\n");
	printf("===========================================================\n");

	int frames = 300;
	const wchar_t* dllPath = L"";
	for (int i = 1; i < argc; i++) {
		if (!wcscmp(argv[i], L"--frames") && i + 1 < argc) frames = _wtoi(argv[++i]);
		else if (!wcscmp(argv[i], L"--dll") && i + 1 < argc) dllPath = argv[++i];
	}

	// ---- device, shaped like CDX11VideoProcessor::Init --------------------
	Head("D3D11 device");
	D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
	CComPtr<ID3D11Device> dev0;
	CComPtr<ID3D11DeviceContext> ctx0;
	D3D_FEATURE_LEVEL fl = {};
	HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
	                               levels, 2, D3D11_SDK_VERSION, &dev0, &fl, &ctx0);
	Check(SUCCEEDED(hr), "D3D11CreateDevice");
	if (FAILED(hr)) return 1;

	CComPtr<ID3D11Device> dev = dev0;
	CComPtr<ID3D11DeviceContext> ctx = ctx0;
	{
		CComPtr<ID3D10Multithread> mt;
		if (SUCCEEDED(ctx->QueryInterface(IID_PPV_ARGS(&mt))) && mt) {
			mt->SetMultithreadProtected(TRUE);
		}
	}

	// ---- session ----------------------------------------------------------
	Head("CDlssNR::Init");
	CDlssNR dlss;
	const bool inited = dlss.Init(dev, dllPath, true);
	Check(inited, "Init");
	Dump(dlss);
	if (!inited) {
		printf("\n  Cannot continue without a session.\n");
		return 2;
	}

	CDlssNR::Params params;
	params.bNoHistory = true;

	Tex2D_t texIn, texOut;
	UINT w = 1920, h = 1080;

	// ---- feature ----------------------------------------------------------
	Head("First feature");
	Check(MakeSharedPair(dev, texIn, texOut, w, h), "shared texture pair");
	Check(dlss.CreateFeature(texIn.pTexture, texOut.pTexture, w, h, params), "CreateFeature");
	Dump(dlss);
	if (!dlss.IsFeatureReady()) return 3;

	// ---- the loop that matters -------------------------------------------
	// Steady playback with the things that broke the player mixed in: toggling
	// the feature, changing resolution the way a fullscreen switch does, and
	// changing the preset, which forces a rebuild.
	Head("Playback loop");
	printf("  %d frames, with toggles, resolution changes and preset changes\n", frames);

	int evaluated = 0, evalFailed = 0, blank = 0, rebuilds = 0;

	for (int f = 0; f < frames; f++) {
		if (f && f % 60 == 0) {
			// A fullscreen switch, as far as this code is concerned.
			const bool big = ((f / 60) & 1) != 0;
			const UINT nw = big ? 2560u : 1920u;
			const UINT nh = big ? 1440u : 1080u;
			if (nw != w || nh != h) {
				w = nw; h = nh;
				if (!MakeSharedPair(dev, texIn, texOut, w, h)
						|| !dlss.CreateFeature(texIn.pTexture, texOut.pTexture, w, h, params)) {
					printf("  frame %d: rebuild at %ux%u FAILED -- %S\n", f, w, h, dlss.GetStatusLine().c_str());
					g_failures++;
					break;
				}
				rebuilds++;
			}
		}

		if (f && f % 90 == 0) {
			// Toggle off and straight back on, as the checkbox does.
			dlss.ReleaseFeature();
			if (!dlss.CreateFeature(texIn.pTexture, texOut.pTexture, w, h, params)) {
				printf("  frame %d: toggle rebuild FAILED -- %S\n", f, dlss.GetStatusLine().c_str());
				g_failures++;
				break;
			}
			rebuilds++;
		}

		if (f && f % 120 == 0) {
			// Preset change: baked into the feature, so this recreates it.
			params.iPreset = (params.iPreset + 1) % 3;
			dlss.ReleaseFeature();
			if (!dlss.CreateFeature(texIn.pTexture, texOut.pTexture, w, h, params)) {
				printf("  frame %d: preset rebuild FAILED -- %S\n", f, dlss.GetStatusLine().c_str());
				g_failures++;
				break;
			}
			rebuilds++;
		}

		// Sliders move without touching the feature.
		params.fIntensity = 0.5f + 1.0f * ((f % 50) / 50.0f);

		FillInput(dev, ctx, texIn.pTexture, (f % 30) / 30.0f);

		if (dlss.Evaluate(params)) {
			evaluated++;
			if (f % 50 == 0 && !OutputIsNonZero(dev, ctx, texOut.pTexture)) {
				blank++;
			}
		} else {
			evalFailed++;
			if (evalFailed <= 3) {
				printf("  frame %d: Evaluate failed -- %S\n", f, dlss.GetStatusLine().c_str());
			}
			if (evalFailed > 10) {
				printf("  giving up after 10 failures\n");
				break;
			}
		}

		// The renderer would be doing plenty of its own D3D11 work here.
		if (const HRESULT rr = dev->GetDeviceRemovedReason(); FAILED(rr)) {
			printf("  frame %d: D3D11 DEVICE REMOVED 0x%08X\n", f, (unsigned)rr);
			g_failures++;
			break;
		}
	}

	printf("  evaluated %d, failed %d, blank readbacks %d, rebuilds %d\n",
	       evaluated, evalFailed, blank, rebuilds);
	Check(evalFailed == 0, "no Evaluate failures");
	Check(blank == 0, "output never blank");
	Check(SUCCEEDED(dev->GetDeviceRemovedReason()), "D3D11 device survived");

	// ---- teardown while work is in flight ---------------------------------
	Head("Teardown");
	FillInput(dev, ctx, texIn.pTexture, 0.5f);
	dlss.Evaluate(params);
	dlss.Shutdown();          // the sequence that used to freeze the picture
	Check(SUCCEEDED(dev->GetDeviceRemovedReason()), "device survived Shutdown");

	// The renderer keeps drawing afterwards -- make sure its timeline is alive.
	FillInput(dev, ctx, texIn.pTexture, 0.25f);
	ctx->Flush();
	Check(SUCCEEDED(dev->GetDeviceRemovedReason()), "D3D11 still usable after Shutdown");

	// ---- a second session on the same device ------------------------------
	Head("Re-init");
	const bool reinit = dlss.Init(dev, dllPath, true);
	Check(reinit, "Init again");
	if (reinit) {
		texIn.Release();
		texOut.Release();
		Check(MakeSharedPair(dev, texIn, texOut, 1280, 720), "shared pair at 1280x720");
		Check(dlss.CreateFeature(texIn.pTexture, texOut.pTexture, 1280, 720, params), "CreateFeature again");
		FillInput(dev, ctx, texIn.pTexture, 0.5f);
		Check(dlss.Evaluate(params), "Evaluate again");
		dlss.Shutdown();
	}

	Head("Result");
	printf("  %d check(s) failed\n", g_failures);
	if (g_failures) {
		Dump(dlss);
	}
	printf("\n%s\n", g_failures ? "NOT SAFE to put in the player yet."
	                            : "Clean. This is the same code the filter runs.");
	return g_failures ? 1 : 0;
}
