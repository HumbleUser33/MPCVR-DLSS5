// A real film in the graph: LAV Splitter and LAV Video, the ones the installed
// player carries, feeding the built renderer. Only then does the bench see what the
// player sees -- a real decoder, real media types, and pictures that stay on the GPU
// where the decoder puts them there.

static const CLSID CLSID_LAVSplitterSource = { 0xB98D13E7, 0x55DB, 0x4385, { 0xA3, 0x3D, 0x09, 0xFD, 0x1B, 0xA2, 0x63, 0x38 } };
static const CLSID CLSID_LAVVideoDecoder   = { 0xEE30215D, 0x164F, 0x4A92, { 0xA4, 0xEB, 0x9D, 0x4C, 0x13, 0x39, 0x0F, 0x9F } };
static const CLSID CLSID_LAVAudioDecoder   = { 0xE8E73B6B, 0x4CB3, 0x44A4, { 0xBE, 0x99, 0x4F, 0x7B, 0xCB, 0x96, 0xE4, 0x91 } };

static const wchar_t* g_lavPaths[] = {
	L"C:\\Program Files\\MPC-HC\\LAVFilters64\\",
	L"C:\\Program Files\\MPC-BE x64\\LAVFilters64\\",
	L"C:\\Program Files (x86)\\LAV Filters\\x64\\",
};

// One filter out of one of those .ax files, created the way the renderer is.
static CComPtr<IBaseFilter> CreateFilterFromAx(const wchar_t* fileName, REFCLSID clsid, std::string& error)
{
	CComPtr<IBaseFilter> filter;
	for (const wchar_t* dir : g_lavPaths) {
		const std::wstring path = std::wstring(dir) + fileName;
		const HMODULE module = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
		if (!module) {
			continue;
		}
		using PFN_DllGetClassObject = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, LPVOID*);
		const auto pfn = (PFN_DllGetClassObject)GetProcAddress(module, "DllGetClassObject");
		CComPtr<IClassFactory> factory;
		if (pfn && SUCCEEDED(pfn(clsid, IID_IClassFactory, (LPVOID*)&factory))
				&& SUCCEEDED(factory->CreateInstance(nullptr, IID_IBaseFilter, (void**)&filter))) {
			return filter;
		}
	}
	error = "cannot load ";
	char name[MAX_PATH] = {};
	WideCharToMultiByte(CP_ACP, 0, fileName, -1, name, sizeof(name), nullptr, nullptr);
	error += name;
	error += " (LAV Filters, as the installed player carries them)";
	return filter;
}

// Every output pin of a filter, so the splitter's video one can be found by trying.
static std::vector<CComPtr<IPin>> OutputPins(IBaseFilter* filter)
{
	std::vector<CComPtr<IPin>> pins;
	CComPtr<IEnumPins> enumPins;
	if (FAILED(filter->EnumPins(&enumPins))) {
		return pins;
	}
	CComPtr<IPin> pin;
	while (enumPins->Next(1, &pin, nullptr) == S_OK) {
		PIN_DIRECTION dir;
		if (SUCCEEDED(pin->QueryDirection(&dir)) && dir == PINDIR_OUTPUT) {
			pins.push_back(pin);
		}
		pin.Release();
	}
	return pins;
}

// The file, split, decoded and handed to the renderer. Returns what went wrong.
static std::string BuildFileGraph(IFilterGraph2* pGraph, const wchar_t* file, IBaseFilter* pRenderer)
{
	std::string error;
	CComPtr<IBaseFilter> pSource = CreateFilterFromAx(L"LAVSplitter.ax", CLSID_LAVSplitterSource, error);
	if (!pSource) {
		return error;
	}
	CComQIPtr<IFileSourceFilter> pFileSource(pSource.p);
	if (!pFileSource || FAILED(pFileSource->Load(file, nullptr))) {
		return "the splitter cannot open the file";
	}
	CComPtr<IBaseFilter> pDecoder = CreateFilterFromAx(L"LAVVideo.ax", CLSID_LAVVideoDecoder, error);
	if (!pDecoder) {
		return error;
	}

	pGraph->AddFilter(pSource, L"LAV Splitter Source");
	pGraph->AddFilter(pDecoder, L"LAV Video Decoder");
	pGraph->AddFilter(pRenderer, L"MPC Video Renderer");

	CComPtr<IPin> pDecoderIn = GetPin(pDecoder, PINDIR_INPUT);
	CComPtr<IPin> pDecoderOut = GetPin(pDecoder, PINDIR_OUTPUT);
	CComPtr<IPin> pRendererIn = GetPin(pRenderer, PINDIR_INPUT);
	if (!pDecoderIn || !pDecoderOut || !pRendererIn) {
		return "a pin is missing";
	}

	CComPtr<IPin> pVideoPin;
	for (const auto& pin : OutputPins(pSource)) {
		if (SUCCEEDED(pGraph->ConnectDirect(pin, pDecoderIn, nullptr))) {
			pVideoPin = pin;   // the video pin is the one the video decoder takes
			break;
		}
	}
	if (!pVideoPin) {
		return "no video stream the decoder takes";
	}
	if (FAILED(pGraph->ConnectDirect(pDecoderOut, pRendererIn, nullptr))) {
		return "the decoder and the renderer did not agree on a format";
	}

	// The sound is rendered too, silently: it is what gives the graph its reference
	// clock, and the video renderer waits on that clock for each picture's turn. A
	// graph without it renders as fast as the pictures come, which is not playback.
	CComPtr<IBaseFilter> pAudioDecoder = CreateFilterFromAx(L"LAVAudio.ax", CLSID_LAVAudioDecoder, error);
	CComPtr<IBaseFilter> pAudioRenderer;
	if (pAudioDecoder && SUCCEEDED(pAudioRenderer.CoCreateInstance(CLSID_DSoundRender, nullptr, CLSCTX_INPROC_SERVER))) {
		pGraph->AddFilter(pAudioDecoder, L"LAV Audio Decoder");
		pGraph->AddFilter(pAudioRenderer, L"Audio renderer");
		CComPtr<IPin> pAudioIn = GetPin(pAudioDecoder, PINDIR_INPUT);
		CComPtr<IPin> pAudioOut = GetPin(pAudioDecoder, PINDIR_OUTPUT);
		for (const auto& pin : OutputPins(pSource)) {
			if (pin.p == pVideoPin.p) {
				continue;
			}
			if (SUCCEEDED(pGraph->ConnectDirect(pin, pAudioIn, nullptr))) {
				pGraph->ConnectDirect(pAudioOut, GetPin(pAudioRenderer, PINDIR_INPUT), nullptr);
				break;
			}
		}
		if (CComQIPtr<IBasicAudio> pBasicAudio = pGraph) {
			pBasicAudio->put_Volume(-10000);   // the bench says nothing out loud
		}
	}
	return {};
}

// Where to start playing, in seconds from the beginning.
static void SeekTo(IFilterGraph2* pGraph, double seconds)
{
	CComQIPtr<IMediaSeeking> pSeeking(pGraph);
	if (pSeeking) {
		LONGLONG position = (LONGLONG)(seconds * 10000000.0);
		pSeeking->SetPositions(&position, AM_SEEKING_AbsolutePositioning, nullptr, AM_SEEKING_NoPositioning);
	}
}
