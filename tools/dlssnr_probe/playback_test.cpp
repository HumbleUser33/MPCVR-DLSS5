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
//   playback_test.exe [--seconds 20] [--size 800x450] [--window 1280x720] [--fps 23.976] [--only N]
//   playback_test.exe --dlsspage N    shows the filter's DLSS 5 page for N seconds instead

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

// The film: soft gradients, a textured band and discs moving at different speeds,
// so Optical Flow and the networks have real work.
class CFilmStream : public CSourceStream
{
	const int m_width;
	const int m_height;
	int m_frame = 0;

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
		vih->bmiHeader.biBitCount = 32;
		vih->bmiHeader.biCompression = BI_RGB;
		vih->bmiHeader.biSizeImage = m_width * m_height * 4;
		SetRect(&vih->rcSource, 0, 0, m_width, m_height);
		SetRect(&vih->rcTarget, 0, 0, m_width, m_height);
		pmt->SetType(&MEDIATYPE_Video);
		pmt->SetSubtype(&MEDIASUBTYPE_RGB32);
		pmt->SetFormatType(&FORMAT_VideoInfo);
		pmt->SetTemporalCompression(FALSE);
		pmt->SetSampleSize(vih->bmiHeader.biSizeImage);
		return S_OK;
	}

	HRESULT DecideBufferSize(IMemAllocator* pAlloc, ALLOCATOR_PROPERTIES* pProps) override
	{
		CAutoLock lock(m_pFilter->pStateLock());
		pProps->cBuffers = std::max(pProps->cBuffers, 1L);
		pProps->cbBuffer = std::max(pProps->cbBuffer, (long)(m_width * m_height * 4));
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
		if (FAILED(pSample->GetPointer(&p)) || pSample->GetSize() < m_width * m_height * 4) {
			return E_FAIL;
		}
		const double t = m_frame;
		const double cx1 = m_width * (0.5 + 0.35 * std::sin(t * 0.05)), cy1 = m_height * (0.5 + 0.3 * std::cos(t * 0.04));
		const double cx2 = std::fmod(t * 6.0, m_width + 200.0) - 100.0, cy2 = m_height * 0.7;
		const double r1 = m_height * 0.12, r2 = m_height * 0.08;
		for (int y = 0; y < m_height; y++) {
			BYTE* row = p + (size_t)(m_height - 1 - y) * m_width * 4; // bottom-up
			for (int x = 0; x < m_width; x++) {
				double r = 60 + 80.0 * x / m_width;
				double g = 50 + 90.0 * y / m_height;
				double b = 90 + 40.0 * std::sin((x + 2.0 * t) * 0.02);
				if (y > m_height / 3 && y < m_height / 2) { // a textured band panning left
					const int u = x + 3 * m_frame;
					const double tex = ((u * 37 ^ y * 91) & 63) - 32;
					r += tex; g += tex; b += tex;
				}
				if ((x - cx1) * (x - cx1) + (y - cy1) * (y - cy1) < r1 * r1) {
					r = 220; g = 180; b = 140;
				}
				if ((x - cx2) * (x - cx2) + (y - cy2) * (y - cy2) < r2 * r2) {
					r = 40; g = 70; b = 200;
				}
				row[x * 4 + 0] = (BYTE)std::clamp(b, 0.0, 255.0);
				row[x * 4 + 1] = (BYTE)std::clamp(g, 0.0, 255.0);
				row[x * 4 + 2] = (BYTE)std::clamp(r, 0.0, 255.0);
				row[x * 4 + 3] = 255;
			}
		}
		REFERENCE_TIME start = m_frame * g_frameDuration;
		REFERENCE_TIME end = start + g_frameDuration;
		pSample->SetTime(&start, &end);
		pSample->SetSyncPoint(TRUE);
		pSample->SetActualDataLength(m_width * m_height * 4);
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
	double stopSeconds[3] = {};
	std::string error;
};

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
	sets.bDlssNR = config.bNR;
	sets.bDlssSR = config.bSR;
	sets.bDlssRenderAhead = config.bAhead;
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

// --dlsspage N: the filter's DLSS 5 property page in the window for N seconds, to look at.
static int ShowDlssPage(HMODULE hFilter, HWND hwnd, int seconds)
{
	static const CLSID CLSID_DlssPage = { 0xE3A1C5D7, 0x6B2F, 0x4F19, { 0xA8, 0xD4, 0x5C, 0x0B, 0x9E, 0x7F, 0x21, 0x36 } };
	using PFN_DllGetClassObject = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, LPVOID*);
	const auto pfnGetClassObject = (PFN_DllGetClassObject)GetProcAddress(hFilter, "DllGetClassObject");
	CComPtr<IClassFactory> pRendererFactory, pPageFactory;
	CComPtr<IBaseFilter> pRenderer;
	CComPtr<IPropertyPage> pPage;
	if (!pfnGetClassObject
			|| FAILED(pfnGetClassObject(CLSID_MpcVideoRenderer, IID_IClassFactory, (LPVOID*)&pRendererFactory))
			|| FAILED(pRendererFactory->CreateInstance(nullptr, IID_IBaseFilter, (void**)&pRenderer))
			|| FAILED(pfnGetClassObject(CLSID_DlssPage, IID_IClassFactory, (LPVOID*)&pPageFactory))
			|| FAILED(pPageFactory->CreateInstance(nullptr, IID_IPropertyPage, (void**)&pPage))) {
		printf("the DLSS page could not be created\n");
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
	printf("DLSS page %ldx%ld shown for %d s\n", info.size.cx, info.size.cy, seconds);
	fflush(stdout);
	Pump(seconds * 1000);

	pPage->Deactivate();
	pPage->SetObjects(0, nullptr);
	pPage->SetPageSite(nullptr);
	return 0;
}

int wmain(int argc, wchar_t* argv[])
{
	int seconds = 20;
	int only = -1;
	int pageSeconds = 0;
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
		const int rc = ShowDlssPage(hFilter, hwnd, pageSeconds);
		DestroyWindow(hwnd);
		CoUninitialize();
		return rc;
	}

	static const Config configs[] = {
		{ "DLSS off",                       false, false, true  },
		{ "DLSS SR, render ahead off",      false, true,  false },
		{ "DLSS SR, render ahead on",       false, true,  true  },
		{ "NR + SR, render ahead off",      true,  true,  false },
		{ "NR + SR, render ahead on",       true,  true,  true  },
	};

	printf("Film %ldx%ld at %.3f fps in a %ldx%ld window, %d s per configuration after 5 s to settle.\n",
		source.cx, source.cy, 10000000.0 / g_frameDuration, window.cx, window.cy, seconds);
	printf("Sync offset in ms: when Present returned, against the picture's time.\n\n");

	int failures = 0;
	for (int c = 0; c < (int)std::size(configs); c++) {
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
