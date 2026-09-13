// What happens to the paused picture when a settings change rebuilds the D3D11
// video processor -- measured with the renderer's own CD3D11VP on the real
// driver, not a mock.
//
// While paused, the renderer has usually already let go of the sample it drew:
// CBaseRenderer::Receive clears it right after rendering, and the next one only
// exists if the decoder already delivered it. So the only copy of the picture
// on screen lives inside CD3D11VP. Toggling DLSS rebuilds the processor. This
// answers, for both ways a frame reaches the processor and at 1080p and 4K:
// does the next Blt still see that picture?
//
// A first version of this test rebuilt the processor and saw the picture
// survive. It did not: the driver handed the new input textures the memory the
// old ones had just freed. Every rebuild below therefore controls what that
// memory is, instead of trusting whatever the allocator happens to do.

#include "stdafx.h"
#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <cstdio>
#include <string>
#include "Helper.h"
#include "DX11Helper.h"
#include "IVideoRenderer.h"
#include "D3D11VP.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

// Pulled in by DX11Helper.cpp, never called here (see harness.cpp).
HRESULT SaveToBMP(BYTE*, UINT, UINT, UINT, UINT, const wchar_t*) { return E_NOTIMPL; }

static int g_failures = 0;

static void Head(const char* s) { printf("\n=== %s ===\n", s); }

static void Check(bool ok, const char* what)
{
	printf("  [%s] %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok) {
		g_failures++;
	}
}

// Stands in for a decoder sample. CD3D11VP only keeps a reference to it.
class CStubSample : public IMediaSample
{
	LONG m_ref = 1;
public:
	STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
		if (riid == IID_IUnknown || riid == IID_IMediaSample) {
			*ppv = static_cast<IMediaSample*>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}
	STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }
	STDMETHODIMP_(ULONG) Release() override { return InterlockedDecrement(&m_ref); } // lives on the stack
	STDMETHODIMP GetPointer(BYTE**) override { return E_NOTIMPL; }
	STDMETHODIMP_(long) GetSize() override { return 0; }
	STDMETHODIMP GetTime(REFERENCE_TIME*, REFERENCE_TIME*) override { return E_NOTIMPL; }
	STDMETHODIMP SetTime(REFERENCE_TIME*, REFERENCE_TIME*) override { return E_NOTIMPL; }
	STDMETHODIMP IsSyncPoint() override { return S_OK; }
	STDMETHODIMP SetSyncPoint(BOOL) override { return E_NOTIMPL; }
	STDMETHODIMP IsPreroll() override { return S_FALSE; }
	STDMETHODIMP SetPreroll(BOOL) override { return E_NOTIMPL; }
	STDMETHODIMP_(long) GetActualDataLength() override { return 0; }
	STDMETHODIMP SetActualDataLength(long) override { return E_NOTIMPL; }
	STDMETHODIMP GetMediaType(AM_MEDIA_TYPE**) override { return S_FALSE; }
	STDMETHODIMP SetMediaType(AM_MEDIA_TYPE*) override { return E_NOTIMPL; }
	STDMETHODIMP IsDiscontinuity() override { return S_FALSE; }
	STDMETHODIMP SetDiscontinuity(BOOL) override { return E_NOTIMPL; }
	STDMETHODIMP GetMediaTime(LONGLONG*, LONGLONG*) override { return E_NOTIMPL; }
	STDMETHODIMP SetMediaTime(LONGLONG*, LONGLONG*) override { return E_NOTIMPL; }
	LONG Refs() const { return m_ref; }
};

struct Gpu {
	CComPtr<ID3D11Device> dev;
	CComPtr<ID3D11DeviceContext> ctx;
	UINT vendor = 0;
	std::wstring name;
};

// The NVIDIA adapter when there is one -- the one the DLSS pass runs on.
static bool CreateDevice(Gpu& g)
{
	CComPtr<IDXGIFactory1> factory;
	if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
		return false;
	}
	CComPtr<IDXGIAdapter1> chosen;
	CComPtr<IDXGIAdapter1> adapter;
	for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++, adapter.Release()) {
		DXGI_ADAPTER_DESC1 d = {};
		adapter->GetDesc1(&d);
		if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
			continue;
		}
		if (!chosen || d.VendorId == 0x10DE) {
			chosen = adapter;
			g.vendor = d.VendorId;
			g.name = d.Description;
			if (d.VendorId == 0x10DE) {
				break;
			}
		}
	}
	if (!chosen) {
		return false;
	}
	const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
	return SUCCEEDED(D3D11CreateDevice(chosen, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
		D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
		levels, (UINT)std::size(levels), D3D11_SDK_VERSION, &g.dev, nullptr, &g.ctx));
}

static CComPtr<ID3D11Texture2D> MakeStagingNV12(Gpu& g, UINT w, UINT h)
{
	D3D11_TEXTURE2D_DESC desc = CreateTex2DDesc(DXGI_FORMAT_NV12, w, h, Tex2D_DynamicShaderWriteNoSRV);
	CComPtr<ID3D11Texture2D> tex;
	g.dev->CreateTexture2D(&desc, nullptr, &tex);
	return tex;
}

// A flat NV12 picture written through Map -- how the renderer stages a
// software-decoded frame (MemCopyToTexSrcVideo) before copying it onward.
static bool FillNV12(Gpu& g, ID3D11Texture2D* tex, UINT w, UINT h, BYTE y, BYTE u, BYTE v)
{
	D3D11_MAPPED_SUBRESOURCE m = {};
	if (!tex || FAILED(g.ctx->Map(tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
		return false;
	}
	BYTE* p = (BYTE*)m.pData;
	for (UINT row = 0; row < h; row++) {
		memset(p + row * m.RowPitch, y, w);
	}
	for (UINT row = 0; row < h / 2; row++) {
		BYTE* uv = p + (h + row) * m.RowPitch;
		for (UINT x = 0; x + 1 < w; x += 2) {
			uv[x] = u;
			uv[x + 1] = v;
		}
	}
	g.ctx->Unmap(tex, 0);
	return true;
}

static DXVA2_ExtendedFormat Bt709()
{
	DXVA2_ExtendedFormat fmt = {};
	fmt.SampleFormat           = DXVA2_SampleProgressiveFrame;
	fmt.VideoChromaSubsampling = DXVA2_VideoChromaSubsampling_MPEG2;
	fmt.NominalRange           = DXVA2_NominalRange_16_235;
	fmt.VideoTransferMatrix    = DXVA2_VideoTransferMatrix_BT709;
	fmt.VideoLighting          = DXVA2_VideoLighting_dim;
	fmt.VideoPrimaries         = DXVA2_VideoPrimaries_BT709;
	fmt.VideoTransferFunction  = DXVA2_VideoTransFunc_709;
	return fmt;
}

enum class Between {
	Nothing,   // release and recreate back to back
	Occupy,    // something takes the memory the old input textures freed
};

// The processor part of CDX11VideoProcessor::InitializeD3D11VP, followed by the
// rectangles D3D11VPPass sets before every Blt. With Between::Occupy, blocks of
// the same size are allocated -- and kept -- between the release of the old
// input textures and the creation of the new ones, so the new ones cannot
// inherit the old picture by accident.
static HRESULT InitVP(CD3D11VP& vp, Gpu& g, UINT w, UINT h, Between between,
                      std::vector<CComPtr<ID3D11Texture2D>>* pOccupied = nullptr)
{
	DXGI_FORMAT out = DXGI_FORMAT_B8G8R8A8_UNORM;
	HRESULT hr = vp.InitVideoProcessor(DXGI_FORMAT_NV12, w, h, Bt709(), DEINT_Disable, false, out);
	if (SUCCEEDED(hr) && between == Between::Occupy && pOccupied) {
		for (int i = 0; i < 6; i++) {
			// A distinct, recognisable colour: pure blue in BT.709 studio range.
			CComPtr<ID3D11Texture2D> t = MakeStagingNV12(g, w, h);
			FillNV12(g, t, w, h, 32, 240, 118);
			D3D11_TEXTURE2D_DESC d = CreateTex2DDesc(DXGI_FORMAT_NV12, w, h, Tex2D_Default);
			CComPtr<ID3D11Texture2D> block;
			if (SUCCEEDED(g.dev->CreateTexture2D(&d, nullptr, &block))) {
				g.ctx->CopyResource(block, t);
				pOccupied->push_back(block);
			}
		}
	}
	if (SUCCEEDED(hr)) {
		hr = vp.InitInputTextures(g.dev);
	}
	if (SUCCEEDED(hr)) {
		const RECT r = { 0, 0, (LONG)w, (LONG)h };
		hr = vp.SetRectangles(&r, &r);
	}
	return hr;
}

// The decoder hands out slices of one texture array bound for decoding.
static CComPtr<ID3D11Texture2D> MakeDecoderArray(Gpu& g, ID3D11Texture2D* picture, UINT w, UINT h, UINT slice)
{
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = w;
	desc.Height = h;
	desc.MipLevels = 1;
	desc.ArraySize = 8;
	desc.Format = DXGI_FORMAT_NV12;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_DECODER;
	CComPtr<ID3D11Texture2D> arr;
	if (FAILED(g.dev->CreateTexture2D(&desc, nullptr, &arr))) {
		return nullptr;
	}

	// Go through a plain default texture: a dynamic one is not a valid source
	// for a subresource copy into an array slice on every driver.
	D3D11_TEXTURE2D_DESC plain = desc;
	plain.ArraySize = 1;
	plain.BindFlags = 0;
	CComPtr<ID3D11Texture2D> mid;
	if (FAILED(g.dev->CreateTexture2D(&plain, nullptr, &mid))) {
		return nullptr;
	}
	g.ctx->CopyResource(mid, picture);
	g.ctx->CopySubresourceRegion(arr, D3D11CalcSubresource(0, slice, 1), 0, 0, 0, mid, 0, nullptr);
	return arr;
}

enum class Seen { Grey, Green, Blue, Black, Untouched, Other, BltFailed };

static const char* Name(Seen s)
{
	switch (s) {
	case Seen::Grey:      return "the picture (grey)";
	case Seen::Green:     return "GREEN";
	case Seen::Blue:      return "blue (memory taken in between)";
	case Seen::Black:     return "black";
	case Seen::Untouched: return "nothing written";
	case Seen::BltFailed: return "Blt failed";
	default:              return "other";
	}
}

// Blt into a target pre-filled with magenta, so that "the processor wrote
// nothing" cannot pass for black, then classify the centre pixel.
static Seen Blt(CD3D11VP& vp, Gpu& g, UINT w, UINT h)
{
	D3D11_TEXTURE2D_DESC desc = CreateTex2DDesc(DXGI_FORMAT_B8G8R8A8_UNORM, w, h, Tex2D_DefaultShaderRTarget);
	CComPtr<ID3D11Texture2D> out;
	if (FAILED(g.dev->CreateTexture2D(&desc, nullptr, &out))) {
		return Seen::BltFailed;
	}
	{
		CComPtr<ID3D11RenderTargetView> rtv;
		if (SUCCEEDED(g.dev->CreateRenderTargetView(out, nullptr, &rtv))) {
			const FLOAT magenta[4] = { 1, 0, 1, 1 };
			g.ctx->ClearRenderTargetView(rtv, magenta);
		}
	}

	if (FAILED(vp.Process(out, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE, false))) {
		return Seen::BltFailed;
	}

	desc.Usage = D3D11_USAGE_STAGING;
	desc.BindFlags = 0;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	CComPtr<ID3D11Texture2D> staging;
	if (FAILED(g.dev->CreateTexture2D(&desc, nullptr, &staging))) {
		return Seen::BltFailed;
	}
	g.ctx->CopyResource(staging, out);
	D3D11_MAPPED_SUBRESOURCE m = {};
	if (FAILED(g.ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m))) {
		return Seen::BltFailed;
	}
	const BYTE* px = (const BYTE*)m.pData + m.RowPitch * (h / 2) + 4 * (w / 2);
	const int b = px[0], gr = px[1], r = px[2];
	g.ctx->Unmap(staging, 0);

	printf("      centre pixel R=%3d G=%3d B=%3d  ", r, gr, b);
	if (r > 200 && gr < 50 && b > 200)                   return Seen::Untouched;
	if (abs(r - gr) < 16 && abs(gr - b) < 16 && gr > 60) return Seen::Grey;
	if (gr > r + 40 && gr > b + 40)                      return Seen::Green;
	if (b > r + 100 && b > gr + 100)                     return Seen::Blue;
	if (r < 20 && gr < 20 && b < 20)                     return Seen::Black;
	return Seen::Other;
}

static Seen Report(const char* step, Seen s)
{
	printf("-> %s\n    %s\n", Name(s), step);
	return s;
}

static void RunSize(Gpu& g, UINT w, UINT h)
{
	char title[64];
	sprintf_s(title, "%ux%u", w, h);
	Head(title);

	const auto PT = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;

	// ---- Frame uploaded from system memory (software decoding, copy-back) ----
	// m_TexSrcVideo is the upload staging texture, so it holds the last picture.
	{
		CComPtr<ID3D11Texture2D> srcVideo = MakeStagingNV12(g, w, h);
		Check(FillNV12(g, srcVideo, w, h, 126, 128, 128), "upload path: grey picture staged in m_TexSrcVideo");

		CD3D11VP vp;
		Check(SUCCEEDED(vp.InitVideoDevice(g.dev, g.ctx, g.vendor)), "upload path: video device");
		Check(SUCCEEDED(InitVP(vp, g, w, h, Between::Nothing)), "upload path: processor created");
		g.ctx->CopyResource(vp.GetNextInputTexture(PT), srcVideo);
		Check(Report("upload path: frame fed, first Blt", Blt(vp, g, w, h)) == Seen::Grey,
			"upload path: the picture shows before any rebuild");

		// InitializeD3D11VP releases m_TexSrcVideo first, rebuilds, recreates it.
		srcVideo.Release();
		Check(SUCCEEDED(InitVP(vp, g, w, h, Between::Nothing)), "upload path: rebuilt in InitializeD3D11VP order");
		srcVideo = MakeStagingNV12(g, w, h);
		Report("upload path: renderer order, nothing fed again  [measurement]", Blt(vp, g, w, h));

		std::vector<CComPtr<ID3D11Texture2D>> occupied;
		Check(SUCCEEDED(InitVP(vp, g, w, h, Between::Occupy, &occupied)), "upload path: rebuilt with the freed memory taken");
		Report("upload path: freed memory taken, nothing fed again  [measurement]", Blt(vp, g, w, h));
	}

	// ---- Decoder output used in place (native D3D11 decoding) ----
	// Here m_TexSrcVideo is created but never written.
	{
		CComPtr<ID3D11Texture2D> picture = MakeStagingNV12(g, w, h);
		FillNV12(g, picture, w, h, 126, 128, 128);
		const UINT slice = 3;
		CComPtr<ID3D11Texture2D> arr = MakeDecoderArray(g, picture, w, h, slice);
		Check(arr != nullptr, "decoder path: texture array with the grey picture in one slice");
		if (!arr) {
			return;
		}

		CComPtr<ID3D11Texture2D> srcVideo = MakeStagingNV12(g, w, h); // never written
		CStubSample sample;
		CD3D11VP vp;
		Check(SUCCEEDED(vp.InitVideoDevice(g.dev, g.ctx, g.vendor)), "decoder path: video device");
		Check(SUCCEEDED(InitVP(vp, g, w, h, Between::Nothing)), "decoder path: processor created");
		vp.SetInputVideoData(arr, &sample, slice, PT);
		Check(Report("decoder path: sample fed, first Blt", Blt(vp, g, w, h)) == Seen::Grey,
			"decoder path: the picture shows before any rebuild");

		srcVideo.Release();
		Check(SUCCEEDED(InitVP(vp, g, w, h, Between::Nothing)), "decoder path: rebuilt in InitializeD3D11VP order");
		srcVideo = MakeStagingNV12(g, w, h);
		printf("      sample references held after rebuild: %ld (1 = released)\n", sample.Refs());
		Report("decoder path: renderer order, nothing fed again  [measurement]", Blt(vp, g, w, h));

		std::vector<CComPtr<ID3D11Texture2D>> occupied;
		Check(SUCCEEDED(InitVP(vp, g, w, h, Between::Occupy, &occupied)), "decoder path: rebuilt with the freed memory taken");
		Report("decoder path: freed memory taken, nothing fed again  [measurement]", Blt(vp, g, w, h));

		// What the SetSettings re-feed does when a sample is still pending. The
		// input view cached from the previous processor is reused here.
		std::vector<CComPtr<ID3D11Texture2D>> occupied2;
		Check(SUCCEEDED(InitVP(vp, g, w, h, Between::Occupy, &occupied2)), "decoder path: rebuilt again");
		vp.SetInputVideoData(arr, &sample, slice, PT);
		Check(Report("decoder path: same sample fed again after the rebuild", Blt(vp, g, w, h)) == Seen::Grey,
			"decoder path: a re-fed sample shows through the cached input view");
	}
}

// The fix: what Configure() does around the rebuild. Every rebuild here takes
// the freed memory in between -- the case that showed green above.
static void RunHoldRestore(Gpu& g, UINT w, UINT h)
{
	char title[64];
	sprintf_s(title, "%ux%u, picture held across the rebuild", w, h);
	Head(title);

	const auto PT = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;

	{
		CD3D11VP vp;
		vp.InitVideoDevice(g.dev, g.ctx, g.vendor);
		Check(SUCCEEDED(InitVP(vp, g, w, h, Between::Nothing)), "nothing fed yet: processor created");
		const CD3D11VP::HeldFrame held = vp.HoldFrame();
		Check(!held.pTexture && !held.pSample, "nothing fed yet: nothing held");
		std::vector<CComPtr<ID3D11Texture2D>> occupied;
		InitVP(vp, g, w, h, Between::Occupy, &occupied);
		vp.RestoreFrame(held, g.ctx, PT); // must be a no-op
	}

	CComPtr<ID3D11Texture2D> picture = MakeStagingNV12(g, w, h);
	Check(FillNV12(g, picture, w, h, 126, 128, 128), "grey picture staged");

	{
		CD3D11VP vp;
		vp.InitVideoDevice(g.dev, g.ctx, g.vendor);
		Check(SUCCEEDED(InitVP(vp, g, w, h, Between::Nothing)), "upload path: processor created");
		g.ctx->CopyResource(vp.GetNextInputTexture(PT), picture);

		for (int round = 1; round <= 3; round++) {
			const CD3D11VP::HeldFrame held = vp.HoldFrame();
			std::vector<CComPtr<ID3D11Texture2D>> occupied;
			Check(SUCCEEDED(InitVP(vp, g, w, h, Between::Occupy, &occupied)), "upload path: rebuilt, freed memory taken");
			vp.RestoreFrame(held, g.ctx, PT);
			char what[96];
			sprintf_s(what, "upload path: picture kept across rebuild %d", round);
			Check(Report(what, Blt(vp, g, w, h)) == Seen::Grey, what);
		}
	}

	const UINT slice = 5;
	CComPtr<ID3D11Texture2D> arr = MakeDecoderArray(g, picture, w, h, slice);
	Check(arr != nullptr, "decoder path: texture array with the grey picture in one slice");
	if (!arr) {
		return;
	}
	{
		CStubSample sample; // declared first, so it outlives the processor
		CD3D11VP vp;
		vp.InitVideoDevice(g.dev, g.ctx, g.vendor);
		Check(SUCCEEDED(InitVP(vp, g, w, h, Between::Nothing)), "decoder path: processor created");
		vp.SetInputVideoData(arr, &sample, slice, PT);

		for (int round = 1; round <= 3; round++) {
			const CD3D11VP::HeldFrame held = vp.HoldFrame();
			std::vector<CComPtr<ID3D11Texture2D>> occupied;
			Check(SUCCEEDED(InitVP(vp, g, w, h, Between::Occupy, &occupied)), "decoder path: rebuilt, freed memory taken");
			vp.RestoreFrame(held, g.ctx, PT);
			char what[96];
			sprintf_s(what, "decoder path: picture kept across rebuild %d", round);
			Check(Report(what, Blt(vp, g, w, h)) == Seen::Grey, what);
		}

		// Our own reference plus the processor's. A leak here would pin a decoder
		// surface for as long as the processor lives.
		printf("      sample references: %ld\n", sample.Refs());
		Check(sample.Refs() == 2, "decoder path: only the processor still holds the sample");
	}
}

int wmain()
{
	Gpu g;
	if (!CreateDevice(g)) {
		printf("No usable D3D11 hardware device.\n");
		return 1;
	}
	printf("Adapter: %S (vendor 0x%04X)\n", g.name.c_str(), g.vendor);

	RunSize(g, 1920, 1080);
	RunSize(g, 3840, 2160);
	RunHoldRestore(g, 1920, 1080);
	RunHoldRestore(g, 3840, 2160);

	printf("\n%d check(s) failed\n", g_failures);
	return g_failures ? 1 : 0;
}
