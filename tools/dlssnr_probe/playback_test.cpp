// Render ahead, measured in a real DirectShow graph: a synthetic film at 23.976 fps
// (or --fps) played by the renderer's own filter (_bin\Filter_x64\MpcVideoRenderer64.ax) in a
// window, on the system clock. For each configuration -- DLSS off, DLSS SR, DLSS 5
// NR with SR, render ahead off and on -- it reads the statistics the renderer
// draws (IExFilterConfig "statsText") ten times a second and reports:
//
//   sync offset  when Present returned, against the picture's time (the renderer's
//                own Sync offset); without DLSS it sits about half a refresh early
//   skipped      pictures the renderer dropped while measuring
//   late         pictures render ahead held that were ready only after their time
//
// Then it pauses, runs and stops the graph, and fails if a state change takes more
// than a second: a held picture must never block the player.
//
// The settings are handed to the filter for this run only; nothing is saved.
//
// --scalers plays a still NV12 picture through the shader video processor with each
// Upscaling and Chroma upsampling method in turn instead (Catmull-Rom, Jinc2m, the
// mpv prescalers), reports what the statistics say they did and cost, saves the
// displayed picture as scalers_<n>.bmp next to the program and compares each with
// the Catmull-Rom one: a wrong pass shows as a large difference.
//
//   playback_test.exe [--seconds 20] [--size 800x450] [--window 1280x720] [--fps 23.976] [--only N] [--scalers]
//   playback_test.exe --dlsspage N    shows the filter's DLSS page for N seconds instead
//   playback_test.exe --mainpage N    the same for the Settings page

#include <windows.h>
#include <VersionHelpers.h>
#include <streams.h>
#include <dvdmedia.h>
#include <atlbase.h>
#include <d3d9.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "IVideoRenderer.h"
#include "../../Include/FilterInterfaces.h"

#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")

// BaseClasses' DLL entry code comes along with the library; an executable has no
// class factories to list.
CFactoryTemplate g_Templates[] = { { L"", nullptr, nullptr, nullptr, nullptr } };
int g_cTemplates = 0;

static const CLSID CLSID_MpcVideoRenderer = { 0x71F080AA, 0x8661, 0x4093, { 0xB1, 0x5E, 0x4F, 0x69, 0x03, 0xE7, 0x7D, 0x0A } };

static REFERENCE_TIME g_frameDuration = 417083; // 23.976 fps unless --fps says otherwise
static bool g_bScalers = false; // --scalers: a still picture, in NV12
static bool g_bHardwareVP = false; // --vp: leave the video processor the formats it is set for

// The film: soft gradients, a textured band and discs moving at different speeds,
// so Optical Flow and the networks have real work. For --scalers it stands still, in
// NV12, with fine lines of colour and of luma for the scalers to show on.
class CFilmStream : public CSourceStream
{
	const int m_width;
	const int m_height;
	int m_frame = 0;
	std::vector<BYTE> m_rgb;

public:
	CFilmStream(HRESULT* phr, CSource* pParent, int width, int height)
		: CSourceStream(L"Film", phr, pParent, L"Out")
		, m_width(width), m_height(height)
	{}

	HRESULT OnThreadCreate() override
	{
		m_frame = 0; // a run after a stop starts again at stream time 0
		return S_OK;
	}

	HRESULT GetMediaType(CMediaType* pmt) override
	{
		auto vih = (VIDEOINFOHEADER*)pmt->AllocFormatBuffer(sizeof(VIDEOINFOHEADER));
		if (!vih) {
			return E_OUTOFMEMORY;
		}
		ZeroMemory(vih, sizeof(VIDEOINFOHEADER));
		vih->AvgTimePerFrame = g_frameDuration;
		vih->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		vih->bmiHeader.biWidth = m_width;
		vih->bmiHeader.biHeight = m_height;
		vih->bmiHeader.biPlanes = 1;
		vih->bmiHeader.biBitCount = g_bScalers ? 12 : 32;
		vih->bmiHeader.biCompression = g_bScalers ? MAKEFOURCC('N', 'V', '1', '2') : BI_RGB;
		vih->bmiHeader.biSizeImage = SampleSize();
		SetRect(&vih->rcSource, 0, 0, m_width, m_height);
		SetRect(&vih->rcTarget, 0, 0, m_width, m_height);
		pmt->SetType(&MEDIATYPE_Video);
		pmt->SetSubtype(g_bScalers ? &MEDIASUBTYPE_NV12 : &MEDIASUBTYPE_RGB32);
		pmt->SetFormatType(&FORMAT_VideoInfo);
		pmt->SetTemporalCompression(FALSE);
		pmt->SetSampleSize(vih->bmiHeader.biSizeImage);
		return S_OK;
	}

	long SampleSize() const
	{
		return g_bScalers ? m_width * m_height * 3 / 2 : m_width * m_height * 4;
	}

	HRESULT DecideBufferSize(IMemAllocator* pAlloc, ALLOCATOR_PROPERTIES* pProps) override
	{
		CAutoLock lock(m_pFilter->pStateLock());
		pProps->cBuffers = std::max(pProps->cBuffers, 1L);
		pProps->cbBuffer = std::max(pProps->cbBuffer, SampleSize());
		ALLOCATOR_PROPERTIES actual = {};
		const HRESULT hr = pAlloc->SetProperties(pProps, &actual);
		if (FAILED(hr)) {
			return hr;
		}
		return actual.cbBuffer < pProps->cbBuffer ? E_FAIL : S_OK;
	}

	HRESULT FillBuffer(IMediaSample* pSample) override
	{
		BYTE* p = nullptr;
		if (FAILED(pSample->GetPointer(&p)) || pSample->GetSize() < SampleSize()) {
			return E_FAIL;
		}
		const int still = g_bScalers ? 0 : m_frame;
		const double t = still;
		const double cx1 = m_width * (0.5 + 0.35 * std::sin(t * 0.05)), cy1 = m_height * (0.5 + 0.3 * std::cos(t * 0.04));
		const double cx2 = std::fmod(t * 6.0, m_width + 200.0) - 100.0, cy2 = m_height * 0.7;
		const double r1 = m_height * 0.12, r2 = m_height * 0.08;
		m_rgb.resize((size_t)m_width * m_height * 4);
		for (int y = 0; y < m_height; y++) {
			BYTE* row = m_rgb.data() + (size_t)y * m_width * 4; // top-down
			for (int x = 0; x < m_width; x++) {
				double r = 60 + 80.0 * x / m_width;
				double g = 50 + 90.0 * y / m_height;
				double b = 90 + 40.0 * std::sin((x + 2.0 * t) * 0.02);
				if (y > m_height / 3 && y < m_height / 2) { // a textured band panning left
					const int u = x + 3 * still;
					const double tex = ((u * 37 ^ y * 91) & 63) - 32;
					r += tex; g += tex; b += tex;
				}
				if ((x - cx1) * (x - cx1) + (y - cy1) * (y - cy1) < r1 * r1) {
					r = 220; g = 180; b = 140;
				}
				if ((x - cx2) * (x - cx2) + (y - cy2) * (y - cy2) < r2 * r2) {
					r = 40; g = 70; b = 200;
				}
				if (g_bScalers && y > m_height * 3 / 4) {
					// Lines 1 to 4 pixels wide: red on blue on the left, then grey
					// diagonals, for chroma and luma edges.
					if (x < m_width / 2) {
						const int width = 1 + x * 8 / m_width;
						const bool on = (x % (2 * width)) < width;
						r = on ? 210 : 30; g = on ? 40 : 50; b = on ? 40 : 200;
					} else {
						const bool on = ((x + y) % 7) < 2 || ((x - y + 7000) % 11) < 1;
						r = g = b = on ? 230 : 25;
					}
				}
				row[x * 4 + 0] = (BYTE)std::clamp(b, 0.0, 255.0);
				row[x * 4 + 1] = (BYTE)std::clamp(g, 0.0, 255.0);
				row[x * 4 + 2] = (BYTE)std::clamp(r, 0.0, 255.0);
				row[x * 4 + 3] = 255;
			}
		}
		if (g_bScalers) {
			// BT.601 limited range, what the renderer assumes for a picture of this
			// size, chroma averaged over 2x2 blocks.
			for (int y = 0; y < m_height; y++) {
				for (int x = 0; x < m_width; x++) {
					const BYTE* s = &m_rgb[((size_t)y * m_width + x) * 4];
					p[(size_t)y * m_width + x] = (BYTE)std::lround(16.0 + (65.481 * s[2] + 128.553 * s[1] + 24.966 * s[0]) / 255.0);
				}
			}
			BYTE* uv = p + (size_t)m_width * m_height;
			for (int y = 0; y < m_height / 2; y++) {
				for (int x = 0; x < m_width / 2; x++) {
					double cb = 0, cr = 0;
					for (int j = 0; j < 2; j++) {
						for (int i = 0; i < 2; i++) {
							const BYTE* s = &m_rgb[((size_t)(2 * y + j) * m_width + 2 * x + i) * 4];
							cb += 128.0 + (-37.797 * s[2] - 74.203 * s[1] + 112.0 * s[0]) / 255.0;
							cr += 128.0 + (112.0 * s[2] - 93.786 * s[1] - 18.214 * s[0]) / 255.0;
						}
					}
					uv[(size_t)y * m_width + 2 * x] = (BYTE)std::lround(cb / 4);
					uv[(size_t)y * m_width + 2 * x + 1] = (BYTE)std::lround(cr / 4);
				}
			}
		} else {
			for (int y = 0; y < m_height; y++) {
				memcpy(p + (size_t)(m_height - 1 - y) * m_width * 4, &m_rgb[(size_t)y * m_width * 4], (size_t)m_width * 4); // bottom-up
			}
		}
		REFERENCE_TIME start = m_frame * g_frameDuration;
		REFERENCE_TIME end = start + g_frameDuration;
		pSample->SetTime(&start, &end);
		pSample->SetSyncPoint(TRUE);
		pSample->SetActualDataLength(SampleSize());
		m_frame++;
		return S_OK;
	}
};

class CFilmSource : public CSource
{
public:
	CFilmSource(HRESULT* phr, int width, int height)
		: CSource(L"Film source", nullptr, GUID_NULL, phr)
	{
		new CFilmStream(phr, this, width, height); // the pin registers itself with the filter
	}
};

static CComPtr<IPin> GetPin(IBaseFilter* pFilter, PIN_DIRECTION dir)
{
	CComPtr<IEnumPins> pEnum;
	if (SUCCEEDED(pFilter->EnumPins(&pEnum))) {
		for (CComPtr<IPin> pPin; pEnum->Next(1, &pPin, nullptr) == S_OK; pPin.Release()) {
			PIN_DIRECTION d;
			if (SUCCEEDED(pPin->QueryDirection(&d)) && d == dir) {
				return pPin;
			}
		}
	}
	return nullptr;
}

static void Pump(DWORD ms)
{
	const ULONGLONG end = GetTickCount64() + ms;
	for (;;) {
		MSG msg;
		while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
		const ULONGLONG now = GetTickCount64();
		if (now >= end) {
			return;
		}
		MsgWaitForMultipleObjects(0, nullptr, FALSE, (DWORD)std::min<ULONGLONG>(end - now, 10), QS_ALLINPUT);
	}
}

static std::wstring StatsText(IBaseFilter* pRenderer)
{
	std::wstring text;
	if (CComQIPtr<IExFilterConfig> pConfig{ pRenderer }) {
		LPWSTR pstr = nullptr;
		if (S_OK == pConfig->Flt_GetString("statsText", &pstr, nullptr) && pstr) {
			text = pstr;
			CoTaskMemFree(pstr);
		}
	}
	return text;
}

// The rest of the line after a label such as L"Sync offset   : ".
static std::wstring StatsLine(const std::wstring& text, const wchar_t* label)
{
	const size_t pos = text.find(label);
	if (pos == std::wstring::npos) {
		return {};
	}
	const size_t start = pos + wcslen(label);
	const size_t end = text.find(L'\n', start);
	return text.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
}

struct Config {
	const char* name;
	bool bNR;
	bool bSR;
	bool bAhead;
	int iUpscaling = -1;     // UPSCALE_*, -1: the saved setting
	int iChromaScaling = -1; // CHROMA_*, -1: the saved setting
};

struct Result {
	bool bRan = false;
	std::vector<int> syncs;
	int skippedAtStart = -1;
	int skippedAtEnd = -1;
	int framesAtStart = 0;
	int framesAtEnd = 0;
	int lateAtStart = 0;
	int lateAtEnd = 0;
	std::wstring nrLine, srLine, dlssTimes, aheadLine;
	std::wstring vprocLine, scalingLine, prescaleTimes;
	std::vector<BYTE> picture;      // BGRA, top-down, as displayed
	std::vector<BYTE> statsPicture; // the same with the statistics drawn over it
	int pictureW = 0, pictureH = 0;
	double stopSeconds[3] = {};
	std::string error;
};

// The back buffer as the renderer hands it out (IExFilterConfig "displayedImage").
static bool GrabDisplayed(IBaseFilter* pRenderer, std::vector<BYTE>& bgra, int& w, int& h)
{
	CComQIPtr<IExFilterConfig> pConfig{ pRenderer };
	LPVOID data = nullptr;
	unsigned size = 0;
	if (!pConfig || S_OK != pConfig->Flt_GetBin("displayedImage", &data, &size) || !data) {
		return false;
	}
	const auto bih = (const BITMAPINFOHEADER*)data;
	const bool ok = bih->biBitCount == 32 && size >= sizeof(BITMAPINFOHEADER) + (size_t)bih->biWidth * std::abs(bih->biHeight) * 4;
	if (ok) {
		w = bih->biWidth;
		h = std::abs(bih->biHeight);
		const BYTE* bits = (const BYTE*)(bih + 1);
		bgra.assign(bits, bits + (size_t)w * h * 4);
	}
	LocalFree(data);
	return ok;
}

static bool SaveBmp(const std::wstring& path, const std::vector<BYTE>& bgra, int w, int h)
{
	FILE* f = nullptr;
	if (_wfopen_s(&f, path.c_str(), L"wb") || !f) {
		return false;
	}
	BITMAPFILEHEADER bfh = {};
	BITMAPINFOHEADER bih = {};
	bih.biSize = sizeof(bih);
	bih.biWidth = w;
	bih.biHeight = -h; // top-down
	bih.biPlanes = 1;
	bih.biBitCount = 32;
	bih.biSizeImage = (DWORD)bgra.size();
	bfh.bfType = 0x4D42;
	bfh.bfOffBits = sizeof(bfh) + sizeof(bih);
	bfh.bfSize = bfh.bfOffBits + bih.biSizeImage;
	fwrite(&bfh, sizeof(bfh), 1, f);
	fwrite(&bih, sizeof(bih), 1, f);
	fwrite(bgra.data(), 1, bgra.size(), f);
	fclose(f);
	return true;
}

static bool ParseSkipped(const std::wstring& text, int& frames, int& skipped)
{
	const std::wstring line = StatsLine(text, L"Frames        : ");
	int dropped2 = 0, failed = 0;
	return !line.empty() && swscanf_s(line.c_str(), L"%d, skipped: %d/%d, failed: %d", &frames, &skipped, &dropped2, &failed) >= 2;
}

// The late count at the end of the Render ahead line; 0 when there is none.
static int ParseLate(const std::wstring& text)
{
	const std::wstring line = StatsLine(text, L"Render ahead  : ");
	const size_t pos = line.rfind(L"late ");
	return pos == std::wstring::npos ? 0 : _wtoi(line.c_str() + pos + 5);
}

static Result RunConfig(HMODULE hFilter, HWND hwnd, const Config& config, SIZE source, SIZE window, int seconds)
{
	Result result;
	HRESULT hr = S_OK;

	CComPtr<IFilterGraph2> pGraph;
	if (FAILED(pGraph.CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER))) {
		result.error = "no filter graph";
		return result;
	}

	auto pSourceFilter = new CFilmSource(&hr, source.cx, source.cy);
	CComPtr<IBaseFilter> pSource = pSourceFilter;

	using PFN_DllGetClassObject = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, LPVOID*);
	const auto pfnGetClassObject = (PFN_DllGetClassObject)GetProcAddress(hFilter, "DllGetClassObject");
	CComPtr<IClassFactory> pFactory;
	CComPtr<IBaseFilter> pRenderer;
	if (!pfnGetClassObject || FAILED(pfnGetClassObject(CLSID_MpcVideoRenderer, IID_IClassFactory, (LPVOID*)&pFactory))
			|| FAILED(pFactory->CreateInstance(nullptr, IID_IBaseFilter, (void**)&pRenderer))) {
		result.error = "the renderer could not be created";
		return result;
	}

	// This run's settings, handed over without saving them.
	CComQIPtr<IVideoRenderer> pVR(pRenderer.p);
	Settings_t sets;
	pVR->GetSettings(sets);
	sets.bUseD3D11 = true;
	sets.bShowStats = true;
	sets.bExclusiveFS = false;
	// The test never touches the screen: with a Windows 10 manifest the filter is
	// allowed to switch the display's HDR state itself, which is not what is measured
	// here and leaves the desktop changed behind it.
	sets.bHdrPassthrough = false;
	sets.iHdrToggleDisplay = HDRTD_Disabled;
	sets.bDlssNR = config.bNR;
	sets.bDlssSR = config.bSR;
	sets.bDlssRenderAhead = config.bAhead;
	if (config.iUpscaling >= 0) {
		sets.iUpscaling = config.iUpscaling;
	}
	if (config.iChromaScaling >= 0) {
		sets.iChromaScaling = config.iChromaScaling;
	}
	if (g_bScalers && !g_bHardwareVP) {
		// The shader video processor converts and scales, not the hardware one.
		sets.VPFmts = { false, false, false, false };
		sets.bVPScaling = false;
		sets.iTexFormat = TEXFMT_AUTOINT;
	}
	pVR->SetSettings(sets);

	pGraph->AddFilter(pSource, L"Film source");
	pGraph->AddFilter(pRenderer, L"MPC Video Renderer");
	hr = pGraph->ConnectDirect(GetPin(pSource, PINDIR_OUTPUT), GetPin(pRenderer, PINDIR_INPUT), nullptr);
	if (FAILED(hr)) {
		result.error = "connection failed";
		return result;
	}

	CComQIPtr<IVideoWindow> pVW(pRenderer.p);
	CComQIPtr<IBasicVideo> pBV(pRenderer.p);
	pVW->put_Owner((OAHWND)hwnd);
	pVW->put_WindowStyle(WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
	pVW->SetWindowPosition(0, 0, window.cx, window.cy);
	pBV->SetDestinationPosition(0, 0, window.cx, window.cy);

	CComQIPtr<IMediaControl> pMC(pGraph.p);
	pMC->Run();
	result.bRan = true;

	// Sessions, features and Optical Flow settle first.
	Pump(5000);
	std::wstring text = StatsText(pRenderer);
	ParseSkipped(text, result.framesAtStart, result.skippedAtStart);
	result.lateAtStart = ParseLate(text);

	for (int i = 0; i < seconds * 10; i++) {
		Pump(100);
		text = StatsText(pRenderer);
		const std::wstring sync = StatsLine(text, L"Sync offset   : ");
		int value = 0;
		if (!sync.empty() && swscanf_s(sync.c_str(), L"%d", &value) == 1) {
			result.syncs.push_back(value);
		}
	}
	ParseSkipped(text, result.framesAtEnd, result.skippedAtEnd);
	result.lateAtEnd = ParseLate(text);
	result.nrLine = StatsLine(text, L"DLSS 5 NR     : ");
	result.srLine = StatsLine(text, L"DLSS SR       : ");
	result.dlssTimes = StatsLine(text, L"DLSS (ms)     : ");
	result.aheadLine = StatsLine(text, L"Render ahead  : ");
	result.vprocLine = StatsLine(text, L"VideoProcessor: ");
	result.scalingLine = StatsLine(text, L"Scaling       : ");
	result.prescaleTimes = StatsLine(text, L"Prescale (ms) : ");
	if (g_bScalers) {
		// One picture with the statistics over it, for the look of the box.
		GrabDisplayed(pRenderer, result.statsPicture, result.pictureW, result.pictureH);
		// Then one without, which is what the configurations are compared on.
		sets.bShowStats = false;
		pVR->SetSettings(sets);
		Pump(800);
		GrabDisplayed(pRenderer, result.picture, result.pictureW, result.pictureH);
	}

	// State changes while pictures are being held.
	const auto TimeCall = [&](auto&& call) {
		const ULONGLONG t0 = GetTickCount64();
		call();
		return (GetTickCount64() - t0) / 1000.0;
	};
	result.stopSeconds[0] = TimeCall([&] { pMC->Pause(); });
	Pump(700);
	result.stopSeconds[1] = TimeCall([&] { pMC->Run(); });
	Pump(1500);
	result.stopSeconds[2] = TimeCall([&] { pMC->Stop(); });
	Pump(200);

	pVW->put_Visible(OAFALSE);
	pVW->put_Owner(0);
	pGraph->RemoveFilter(pRenderer);
	pGraph->RemoveFilter(pSource);
	return result;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// The smallest site a property page accepts, to show one without registering the filter.
class CPageSite : public IPropertyPageSite
{
public:
	STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
	{
		if (riid == IID_IUnknown || riid == IID_IPropertyPageSite) {
			*ppv = static_cast<IPropertyPageSite*>(this);
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}
	STDMETHODIMP_(ULONG) AddRef() override { return 2; }  // lives on the stack
	STDMETHODIMP_(ULONG) Release() override { return 1; }
	STDMETHODIMP OnStatusChange(DWORD) override { return S_OK; }
	STDMETHODIMP GetLocaleID(LCID* pLocaleID) override { *pLocaleID = LOCALE_USER_DEFAULT; return S_OK; }
	STDMETHODIMP GetPageContainer(IUnknown** ppUnk) override { *ppUnk = nullptr; return E_NOTIMPL; }
	STDMETHODIMP TranslateAccelerator(MSG*) override { return S_FALSE; }
};

// What a window looks like, through PrintWindow: a property page is then checked from
// its picture rather than by eye.
static bool CaptureWindow(HWND hwnd, std::vector<BYTE>& bgra, int& w, int& h)
{
	RECT rc = {};
	if (!GetClientRect(hwnd, &rc) || rc.right <= 0 || rc.bottom <= 0) {
		return false;
	}
	w = rc.right;
	h = rc.bottom;

	const HDC hdcWindow = GetDC(hwnd);
	const HDC hdc = CreateCompatibleDC(hdcWindow);
	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h; // top-down
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	void* bits = nullptr;
	const HBITMAP hbm = CreateDIBSection(hdcWindow, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
	bool ok = false;
	if (hbm && bits) {
		const HGDIOBJ prev = SelectObject(hdc, hbm);
		ok = PrintWindow(hwnd, hdc, PW_CLIENTONLY) != FALSE;
		GdiFlush();
		if (ok) {
			bgra.assign((const BYTE*)bits, (const BYTE*)bits + (size_t)w * h * 4);
		}
		SelectObject(hdc, prev);
	}
	if (hbm) {
		DeleteObject(hbm);
	}
	DeleteDC(hdc);
	ReleaseDC(hwnd, hdcWindow);
	return ok;
}

// --dlsspage N, --mainpage N: one of the filter's property pages in the window for N
// seconds, to look at -- the greying and the layout without going through a player.
// Its picture is saved next to the program as proppage.bmp.
static int ShowPropertyPage(HMODULE hFilter, HWND hwnd, int seconds, REFCLSID clsidPage, int clickId = 0)
{
	using PFN_DllGetClassObject = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, LPVOID*);
	const auto pfnGetClassObject = (PFN_DllGetClassObject)GetProcAddress(hFilter, "DllGetClassObject");
	CComPtr<IClassFactory> pRendererFactory, pPageFactory;
	CComPtr<IBaseFilter> pRenderer;
	CComPtr<IPropertyPage> pPage;
	if (!pfnGetClassObject
			|| FAILED(pfnGetClassObject(CLSID_MpcVideoRenderer, IID_IClassFactory, (LPVOID*)&pRendererFactory))
			|| FAILED(pRendererFactory->CreateInstance(nullptr, IID_IBaseFilter, (void**)&pRenderer))
			|| FAILED(pfnGetClassObject(clsidPage, IID_IClassFactory, (LPVOID*)&pPageFactory))
			|| FAILED(pPageFactory->CreateInstance(nullptr, IID_IPropertyPage, (void**)&pPage))) {
		printf("the property page could not be created\n");
		return 1;
	}
	CPageSite site;
	pPage->SetPageSite(&site);
	IUnknown* objects[] = { pRenderer.p };
	pPage->SetObjects(1, objects);
	PROPPAGEINFO info = { sizeof(info) };
	pPage->GetPageInfo(&info);
	CoTaskMemFree(info.pszTitle);
	CoTaskMemFree(info.pszDocString);
	CoTaskMemFree(info.pszHelpFile);

	RECT rc = { 0, 0, info.size.cx, info.size.cy };
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
	SetWindowPos(hwnd, nullptr, 20, 20, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER);
	RECT page = { 0, 0, info.size.cx, info.size.cy };
	pPage->Activate(hwnd, &page, FALSE);
	pPage->Show(SW_SHOW);
	printf("property page %ldx%ld shown for %d s\n", info.size.cx, info.size.cy, seconds);
	fflush(stdout);
	Pump(700);
	wchar_t exe[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
	const std::wstring dir = std::wstring(exe).substr(0, std::wstring(exe).find_last_of(L'\\'));
	const auto Shoot = [&](const wchar_t* name) {
		std::vector<BYTE> shot;
		int shotW = 0, shotH = 0;
		if (CaptureWindow(hwnd, shot, shotW, shotH)) {
			const std::wstring path = dir + L"\\" + name;
			SaveBmp(path, shot, shotW, shotH);
			wprintf(L"picture: %s\n", path.c_str());
		}
	};
	Shoot(L"proppage.bmp");

	// Tick a box and take the page again: how the greying answers, without a player.
	if (clickId) {
		const HWND hDlg = GetWindow(hwnd, GW_CHILD);
		const HWND hControl = hDlg ? GetDlgItem(hDlg, clickId) : nullptr;
		if (hControl) {
			SendMessageW(hControl, BM_SETCHECK,
				IsDlgButtonChecked(hDlg, clickId) == BST_CHECKED ? BST_UNCHECKED : BST_CHECKED, 0);
			SendMessageW(hDlg, WM_COMMAND, MAKEWPARAM(clickId, BN_CLICKED), (LPARAM)hControl);
			Pump(300);
			Shoot(L"proppage_clicked.bmp");
		} else {
			printf("control %d not found\n", clickId);
		}
	}
	Pump(std::max(0, seconds * 1000 - 1000));

	pPage->Deactivate();
	pPage->SetObjects(0, nullptr);
	pPage->SetPageSite(nullptr);
	return 0;
}

int wmain(int argc, wchar_t* argv[])
{
	setvbuf(stdout, nullptr, _IONBF, 0); // so a crash keeps what was already reported

	int seconds = 20;
	int only = -1;
	int pageSeconds = 0;
	bool bMainPage = false;
	int clickControl = 0; // --click N: a control to tick once the page is up
	SIZE source = { 800, 450 };
	SIZE window = { 1280, 720 };
	for (int i = 1; i + 1 < argc; i++) {
		if (!wcscmp(argv[i], L"--seconds")) {
			seconds = _wtoi(argv[i + 1]);
		} else if (!wcscmp(argv[i], L"--size")) {
			swscanf_s(argv[i + 1], L"%dx%d", &source.cx, &source.cy);
		} else if (!wcscmp(argv[i], L"--window")) {
			swscanf_s(argv[i + 1], L"%dx%d", &window.cx, &window.cy);
		} else if (!wcscmp(argv[i], L"--fps")) {
			g_frameDuration = (REFERENCE_TIME)std::llround(10000000.0 / _wtof(argv[i + 1]));
		} else if (!wcscmp(argv[i], L"--only")) {
			only = _wtoi(argv[i + 1]);
		} else if (!wcscmp(argv[i], L"--dlsspage")) {
			pageSeconds = _wtoi(argv[i + 1]);
		} else if (!wcscmp(argv[i], L"--click")) {
			clickControl = _wtoi(argv[i + 1]);
		} else if (!wcscmp(argv[i], L"--mainpage")) {
			pageSeconds = _wtoi(argv[i + 1]);
			bMainPage = true;
		}
	}
	for (int i = 1; i < argc; i++) {
		if (!wcscmp(argv[i], L"--scalers")) {
			g_bScalers = true;
			source.cx &= ~1; // NV12
			source.cy &= ~1;
		} else if (!wcscmp(argv[i], L"--vp")) {
			g_bHardwareVP = true;
		}
	}

	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

	wchar_t exe[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
	std::wstring filterPath = exe;
	filterPath = filterPath.substr(0, filterPath.find_last_of(L'\\')) + L"\\..\\..\\_bin\\Filter_x64\\MpcVideoRenderer64.ax";
	HMODULE hFilter = LoadLibraryExW(filterPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!hFilter) {
		wprintf(L"cannot load %s\n", filterPath.c_str());
		return 1;
	}

	WNDCLASSW wc = {};
	wc.lpfnWndProc = WndProc;
	wc.hInstance = GetModuleHandleW(nullptr);
	wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	wc.lpszClassName = L"RenderAheadTest";
	RegisterClassW(&wc);
	RECT rc = { 0, 0, window.cx, window.cy };
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
	HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"MPC Video Renderer -- render ahead test",
		WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN, 20, 20, rc.right - rc.left, rc.bottom - rc.top,
		nullptr, nullptr, wc.hInstance, nullptr);
	// The first ShowWindow of a process takes the show state it was started with,
	// hidden when a script starts it hidden, and a hidden window is never composed.
	ShowWindow(hwnd, SW_SHOWNORMAL);
	ShowWindow(hwnd, SW_SHOWNORMAL);
	SetForegroundWindow(hwnd);
	Pump(300);
	if (!IsWindowVisible(hwnd)) {
		printf("the test window could not be shown\n");
		return 1;
	}

	if (pageSeconds > 0) {
		static const CLSID CLSID_DlssPage = { 0xE3A1C5D7, 0x6B2F, 0x4F19, { 0xA8, 0xD4, 0x5C, 0x0B, 0x9E, 0x7F, 0x21, 0x36 } };
		static const CLSID CLSID_MainPage = { 0xDA46D181, 0x07D6, 0x441D, { 0xB3, 0x14, 0x01, 0x9A, 0xEB, 0x10, 0x14, 0x8A } };
		const int rc = ShowPropertyPage(hFilter, hwnd, pageSeconds, bMainPage ? CLSID_MainPage : CLSID_DlssPage, clickControl);
		DestroyWindow(hwnd);
		CoUninitialize();
		return rc;
	}

	static const Config dlssConfigs[] = {
		{ "DLSS off",                       false, false, true  },
		{ "DLSS SR, render ahead off",      false, true,  false },
		{ "DLSS SR, render ahead on",       false, true,  true  },
		{ "NR + SR, render ahead off",      true,  true,  false },
		{ "NR + SR, render ahead on",       true,  true,  true  },
	};
	// The first is the one the others are compared with.
	static const Config scalerConfigs[] = {
		{ "Catmull-Rom, chroma Catmull-Rom", false, false, true, UPSCALE_CatmullRom, CHROMA_CatmullRom },
		{ "Jinc2m, chroma Catmull-Rom",      false, false, true, UPSCALE_Jinc2,      CHROMA_CatmullRom },
		{ "FSRCNNX 8, chroma Catmull-Rom",   false, false, true, UPSCALE_FSRCNNX8,   CHROMA_CatmullRom },
		{ "FSRCNNX 16, chroma Catmull-Rom",  false, false, true, UPSCALE_FSRCNNX16,  CHROMA_CatmullRom },
		{ "RAVU-zoom, chroma Catmull-Rom",   false, false, true, UPSCALE_RAVUZoom,   CHROMA_CatmullRom },
		{ "Catmull-Rom, chroma RAVU-zoom",   false, false, true, UPSCALE_CatmullRom, CHROMA_RAVU       },
		{ "RAVU-zoom, chroma RAVU-zoom",     false, false, true, UPSCALE_RAVUZoom,   CHROMA_RAVU       },
	};
	const Config* configs = g_bScalers ? scalerConfigs : dlssConfigs;
	const int configCount = g_bScalers ? (int)std::size(scalerConfigs) : (int)std::size(dlssConfigs);

	printf("Film %ldx%ld at %.3f fps in a %ldx%ld window, %d s per configuration after 5 s to settle.\n",
		source.cx, source.cy, 10000000.0 / g_frameDuration, window.cx, window.cy, seconds);
	printf("Sync offset in ms: when Present returned, against the picture's time.\n\n");

	const std::wstring exeDir = std::wstring(exe).substr(0, std::wstring(exe).find_last_of(L'\\'));
	std::vector<BYTE> firstPicture;
	int firstW = 0, firstH = 0;
	int failures = 0;
	for (int c = 0; c < configCount; c++) {
		if (only >= 0 && c != only) {
			continue;
		}
		const Config& config = configs[c];
		const Result r = RunConfig(hFilter, hwnd, config, source, window, seconds);
		printf("== %s\n", config.name);
		if (!r.error.empty()) {
			printf("   FAILED: %s\n", r.error.c_str());
			failures++;
			continue;
		}
		if (g_bScalers) {
			wprintf(L"   processor    : %s\n", r.vprocLine.c_str());
			wprintf(L"   scaling      : %s\n", r.scalingLine.c_str());
			if (!r.prescaleTimes.empty()) {
				wprintf(L"   prescale (ms): %s\n", r.prescaleTimes.c_str());
			}
			if (r.picture.empty()) {
				printf("   FAIL: no displayed picture\n");
				failures++;
			} else {
				const std::wstring path = exeDir + L"\\scalers_" + std::to_wstring(c) + L".bmp";
				SaveBmp(path, r.picture, r.pictureW, r.pictureH);
				if (!r.statsPicture.empty()) {
					SaveBmp(exeDir + L"\\scalers_" + std::to_wstring(c) + L"_stats.bmp", r.statsPicture, r.pictureW, r.pictureH);
				}
				if (firstPicture.empty()) {
					firstPicture = r.picture;
					firstW = r.pictureW;
					firstH = r.pictureH;
				} else if (r.pictureW == firstW && r.pictureH == firstH) {
					// Mean absolute difference over RGB, and the share of values more
					// than 24 levels apart: scalers differ a little, a broken pass a lot.
					double sum = 0;
					size_t beyond = 0;
					for (size_t i = 0; i < r.picture.size(); i += 4) {
						for (int k = 0; k < 3; k++) {
							const int d = std::abs((int)r.picture[i + k] - (int)firstPicture[i + k]);
							sum += d;
							beyond += d > 24;
						}
					}
					const double values = 3.0 * r.picture.size() / 4;
					printf("   against the first: mean difference %.2f levels, %.3f%% of values beyond 24\n",
						sum / values, 100.0 * beyond / values);
				}
				wprintf(L"   picture      : %s\n", path.c_str());
			}
		}
		if (config.bNR) {
			wprintf(L"   DLSS 5 NR    : %s\n", r.nrLine.c_str());
		}
		if (config.bSR) {
			wprintf(L"   DLSS SR      : %s\n", r.srLine.c_str());
		}
		if (!r.dlssTimes.empty()) {
			wprintf(L"   DLSS (ms)    : %s\n", r.dlssTimes.c_str());
		}
		if (!r.aheadLine.empty()) {
			wprintf(L"   Render ahead : %s\n", r.aheadLine.c_str());
		}
		if (!r.syncs.empty()) {
			double sum = 0, squares = 0;
			for (int v : r.syncs) {
				sum += v;
				squares += (double)v * v;
			}
			const double mean = sum / r.syncs.size();
			const double sd = std::sqrt(std::max(0.0, squares / r.syncs.size() - mean * mean));
			std::vector<int> sorted = r.syncs;
			std::sort(sorted.begin(), sorted.end());
			printf("   sync offset  : mean %+.1f, sd %.1f, min %+d, p5 %+d, p95 %+d, max %+d  (%zu samples)\n",
				mean, sd, sorted.front(), sorted[sorted.size() * 5 / 100], sorted[sorted.size() * 95 / 100], sorted.back(), sorted.size());
		} else {
			printf("   no Sync offset read\n");
			failures++;
		}
		printf("   frames       : %d shown while measuring, %d skipped, %d late (%d while settling)\n",
			r.framesAtEnd - r.framesAtStart, r.skippedAtEnd - r.skippedAtStart, r.lateAtEnd - r.lateAtStart, r.lateAtStart);
		printf("   state changes: pause %.2f s, run %.2f s, stop %.2f s\n", r.stopSeconds[0], r.stopSeconds[1], r.stopSeconds[2]);
		for (double s : r.stopSeconds) {
			if (s > 1.0) {
				printf("   FAIL: a state change took more than a second\n");
				failures++;
				break;
			}
		}
		printf("\n");
	}

	DestroyWindow(hwnd);
	CoUninitialize();
	printf("%d failure(s)\n", failures);
	return failures ? 1 : 0;
}
