// A source that hands the renderer D3D11 textures, the way a hardware decoder does.
//
// It creates the device, gives it to the renderer through ID3D11DecoderConfiguration
// (which is what makes the renderer run on the decoder's device), keeps a texture
// array of its own and delivers samples that carry one slice of it. Without this the
// bench only ever sees pictures that come from memory, and the statistics say
// "P010" where the player says "D3D11_P010".

class CD3D11Sample : public CMediaSample, public IMediaSampleD3D11
{
	ID3D11Texture2D* m_pTexture;   // the allocator owns it
	const UINT m_slice;

public:
	CD3D11Sample(CBaseAllocator* pAllocator, HRESULT* phr, ID3D11Texture2D* pTexture, UINT slice)
		: CMediaSample(const_cast<LPCTSTR>(L"D3D11 sample"), pAllocator, phr, nullptr, 0)
		, m_pTexture(pTexture)
		, m_slice(slice)
	{
	}

	STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
	{
		if (riid == __uuidof(IMediaSampleD3D11)) {
			*ppv = static_cast<IMediaSampleD3D11*>(this);
			AddRef();
			return S_OK;
		}
		return CMediaSample::QueryInterface(riid, ppv);
	}
	STDMETHODIMP_(ULONG) AddRef() override { return CMediaSample::AddRef(); }
	STDMETHODIMP_(ULONG) Release() override { return CMediaSample::Release(); }

	STDMETHODIMP GetD3D11Texture(int nView, ID3D11Texture2D** ppTexture, UINT* pArraySlice) override
	{
		if (nView != 0 || !ppTexture || !pArraySlice) {
			return E_INVALIDARG;
		}
		m_pTexture->AddRef();
		*ppTexture = m_pTexture;
		*pArraySlice = m_slice;
		return S_OK;
	}
};

// One sample per slice of the array, as a decoder's pool works.
class CD3D11Allocator : public CBaseAllocator
{
	CComPtr<ID3D11Texture2D> m_pArray;
	std::vector<CD3D11Sample*> m_samples;

public:
	CD3D11Allocator(HRESULT* phr, ID3D11Texture2D* pArray, long count)
		: CBaseAllocator(const_cast<LPCTSTR>(L"D3D11 allocator"), nullptr, phr)
		, m_pArray(pArray)
	{
		m_lCount = count;
		m_lSize = 1;
		m_lAlignment = 1;
		m_lPrefix = 0;
	}
	~CD3D11Allocator() { Decommit(); }

	HRESULT Alloc() override
	{
		CAutoLock lck(this);
		HRESULT hr = CBaseAllocator::Alloc();
		if (FAILED(hr) || hr == S_FALSE) {
			return SUCCEEDED(hr) ? S_OK : hr;
		}
		for (long i = 0; i < m_lCount; i++) {
			HRESULT hrSample = S_OK;
			auto* pSample = new CD3D11Sample(this, &hrSample, m_pArray, (UINT)i);
			if (!pSample || FAILED(hrSample)) {
				delete pSample;
				return E_OUTOFMEMORY;
			}
			m_samples.push_back(pSample);
			m_lFree.Add(pSample);
			m_lAllocated++;
		}
		return S_OK;
	}

	void Free() override
	{
		CAutoLock lck(this);
		for (auto* pSample : m_samples) {
			delete pSample;
		}
		m_samples.clear();
		m_lAllocated = 0;
	}

	// The renderer asks for its own properties; the sizes mean nothing here since the
	// picture lives in the texture, not in the buffer.
	STDMETHODIMP SetProperties(ALLOCATOR_PROPERTIES* pRequest, ALLOCATOR_PROPERTIES* pActual) override
	{
		CheckPointer(pActual, E_POINTER);
		CAutoLock lck(this);
		if (m_bCommitted) {
			return VFW_E_ALREADY_COMMITTED;
		}
		pActual->cBuffers = m_lCount;
		pActual->cbBuffer = m_lSize;
		pActual->cbAlign = m_lAlignment;
		pActual->cbPrefix = m_lPrefix;
		m_bChanged = TRUE;   // without this the base class allocates nothing
		return S_OK;
	}
};

// The film, in a texture array the renderer reads from, as a decoder would deliver it.
class CD3D11FilmSource : public CSource
{
	class CStream : public CSourceStream
	{
		CD3D11FilmSource* m_pParent;
		int m_frame = 0;

	public:
		CStream(HRESULT* phr, CD3D11FilmSource* pParent)
			: CSourceStream(const_cast<LPCTSTR>(L"D3D11 film"), phr, pParent, const_cast<LPCWSTR>(L"Output"))
			, m_pParent(pParent)
		{
		}

		HRESULT GetMediaType(CMediaType* pmt) override
		{
			auto vih = (VIDEOINFOHEADER*)pmt->AllocFormatBuffer(sizeof(VIDEOINFOHEADER));
			ZeroMemory(vih, sizeof(VIDEOINFOHEADER));
			vih->AvgTimePerFrame = g_frameDuration;
			vih->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			vih->bmiHeader.biWidth = m_pParent->m_width;
			vih->bmiHeader.biHeight = m_pParent->m_height;
			vih->bmiHeader.biPlanes = 1;
			vih->bmiHeader.biBitCount = 24;
			vih->bmiHeader.biCompression = MAKEFOURCC('P', '0', '1', '0');
			vih->bmiHeader.biSizeImage = m_pParent->m_width * m_pParent->m_height * 3;
			SetRect(&vih->rcSource, 0, 0, m_pParent->m_width, m_pParent->m_height);
			SetRect(&vih->rcTarget, 0, 0, m_pParent->m_width, m_pParent->m_height);
			pmt->SetType(&MEDIATYPE_Video);
			pmt->SetSubtype(&MEDIASUBTYPE_P010);
			pmt->SetFormatType(&FORMAT_VideoInfo);
			pmt->SetTemporalCompression(FALSE);
			pmt->SetSampleSize(vih->bmiHeader.biSizeImage);
			return S_OK;
		}

		HRESULT DecideBufferSize(IMemAllocator*, ALLOCATOR_PROPERTIES*) override { return S_OK; }

		HRESULT Active() override
		{
			const HRESULT hr = CSourceStream::Active();
			printf("D3D11 source: streaming %s\n", SUCCEEDED(hr) ? "started" : "could not start");
			return hr;
		}

		// The renderer must take our allocator: its own cannot hold textures.
		HRESULT DecideAllocator(IMemInputPin* pPin, IMemAllocator** ppAlloc) override
		{
			HRESULT hr = S_OK;
			auto* pAllocator = new CD3D11Allocator(&hr, m_pParent->m_pTextures, CD3D11FilmSource::kSlices);
			if (!pAllocator || FAILED(hr)) {
				delete pAllocator;
				return E_OUTOFMEMORY;
			}
			pAllocator->AddRef();
			ALLOCATOR_PROPERTIES props = { CD3D11FilmSource::kSlices, 1, 1, 0 }, actual = {};
			pAllocator->SetProperties(&props, &actual);
			hr = pPin->NotifyAllocator(pAllocator, FALSE);
			if (FAILED(hr)) {
				pAllocator->Release();
				return hr;
			}
			*ppAlloc = pAllocator;
			return S_OK;
		}

		// The renderer takes the device from us before anything is connected, which is
		// what makes it run on the decoder's device.
		HRESULT CompleteConnect(IPin* pReceivePin) override
		{
			if (CComQIPtr<ID3D11DecoderConfiguration> pConfig = pReceivePin) {
				const HRESULT hr = pConfig->ActivateD3D11Decoding(m_pParent->m_pDevice, m_pParent->m_pContext, m_pParent->m_hMutex, 0);
				printf("D3D11 decoding: %s\n", SUCCEEDED(hr) ? "the renderer took our device" : "refused by the renderer");
				if (FAILED(hr)) {
					return hr;
				}
			} else {
				printf("D3D11 decoding: the renderer does not offer ID3D11DecoderConfiguration\n");
				return E_FAIL;
			}
			return CSourceStream::CompleteConnect(pReceivePin);
		}

		HRESULT FillBuffer(IMediaSample* pSample) override
		{
			// The pictures are already in the array; the slice is the sample's own.
			if (m_frame == 0) {
				printf("D3D11 source: first picture delivered\n");
			}
			REFERENCE_TIME start = m_frame * g_frameDuration;
			REFERENCE_TIME end = start + g_frameDuration;
			pSample->SetTime(&start, &end);
			pSample->SetSyncPoint(TRUE);
			pSample->SetActualDataLength(1);
			m_frame++;
			return S_OK;
		}
	};

public:
	static const long kSlices = 8;

	CComPtr<ID3D11Device> m_pDevice;
	CComPtr<ID3D11DeviceContext> m_pContext;
	CComPtr<ID3D11Texture2D> m_pTextures;
	HANDLE m_hMutex = nullptr;
	const int m_width;
	const int m_height;

	CD3D11FilmSource(HRESULT* phr, int width, int height)
		: CSource(const_cast<LPCTSTR>(L"D3D11 film source"), nullptr, CLSID_NULL)
		, m_width(width)
		, m_height(height)
	{
		m_hMutex = CreateMutexW(nullptr, FALSE, nullptr);
		*phr = CreateDevice();
		if (SUCCEEDED(*phr)) {
			*phr = CreateTextures();
		}
		if (SUCCEEDED(*phr)) {
			new CStream(phr, this);   // the source owns its pins
		}
	}
	~CD3D11FilmSource()
	{
		if (m_hMutex) {
			CloseHandle(m_hMutex);
		}
	}

private:
	HRESULT CreateDevice()
	{
		const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
		D3D_FEATURE_LEVEL level = {};
		const HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
			D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
			levels, (UINT)std::size(levels), D3D11_SDK_VERSION, &m_pDevice, &level, &m_pContext);
		if (SUCCEEDED(hr)) {
			// A decoder's device is shared with the renderer, so it is protected.
			if (CComQIPtr<ID3D10Multithread> pMultithread = m_pContext.p) {
				pMultithread->SetMultithreadProtected(TRUE);
			}
		}
		return hr;
	}

	// One array, one picture a slice: a still film, each slice a little brighter, so
	// a picture that stops moving is visible.
	HRESULT CreateTextures()
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = m_width;
		desc.Height = m_height;
		desc.MipLevels = 1;
		desc.ArraySize = kSlices;
		desc.Format = DXGI_FORMAT_P010;
		desc.SampleDesc = { 1, 0 };
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_DECODER;
		HRESULT hr = m_pDevice->CreateTexture2D(&desc, nullptr, &m_pTextures);
		if (FAILED(hr)) {
			return hr;
		}

		D3D11_TEXTURE2D_DESC staging = desc;
		staging.ArraySize = 1;
		staging.BindFlags = 0;
		staging.Usage = D3D11_USAGE_STAGING;
		staging.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		CComPtr<ID3D11Texture2D> pStaging;
		hr = m_pDevice->CreateTexture2D(&staging, nullptr, &pStaging);
		if (FAILED(hr)) {
			return hr;
		}

		for (long slice = 0; slice < kSlices; slice++) {
			D3D11_MAPPED_SUBRESOURCE mapped = {};
			hr = m_pContext->Map(pStaging, 0, D3D11_MAP_WRITE, 0, &mapped);
			if (FAILED(hr)) {
				return hr;
			}
			for (int y = 0; y < m_height; y++) {
				auto* luma = (uint16_t*)((BYTE*)mapped.pData + (size_t)y * mapped.RowPitch);
				for (int x = 0; x < m_width; x++) {
					const int band = (x + slice * 32) % 256;
					luma[x] = (uint16_t)(((64 + band * 3) & 0x3FF) << 6);
				}
			}
			for (int y = 0; y < m_height / 2; y++) {
				auto* chroma = (uint16_t*)((BYTE*)mapped.pData + (size_t)(m_height + y) * mapped.RowPitch);
				for (int x = 0; x < m_width / 2; x++) {
					chroma[2 * x + 0] = (uint16_t)(((512 + ((x + slice * 8) % 128)) & 0x3FF) << 6);
					chroma[2 * x + 1] = (uint16_t)(((512 - ((y + slice * 8) % 128)) & 0x3FF) << 6);
				}
			}
			m_pContext->Unmap(pStaging, 0);
			m_pContext->CopySubresourceRegion(m_pTextures, D3D11CalcSubresource(0, slice, 1), 0, 0, 0, pStaging, 0, nullptr);
		}
		m_pContext->Flush();
		return S_OK;
	}
};
