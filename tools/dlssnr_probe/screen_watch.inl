// What the screen really shows.
//
// A swap chain can present picture after picture, without error and in time, into a
// window the desktop no longer updates -- which is what a frozen picture with sound
// running looks like. Nothing inside the filter can see that, and neither can a GDI
// screen copy, which does not get the content of a flip model swap chain. The
// desktop duplication does: it hands out the desktop exactly as Windows composes it.

#include <dxgi1_2.h>

class CScreenWatch
{
	CComPtr<ID3D11Device> m_pDevice;
	CComPtr<ID3D11DeviceContext> m_pContext;
	CComPtr<IDXGIOutputDuplication> m_pDuplication;
	CComPtr<ID3D11Texture2D> m_pStaging;
	UINT m_width = 0;
	UINT m_height = 0;
	int m_left = 0;   // where the duplicated output starts on the desktop
	int m_top = 0;

public:
	// The output the window sits on, duplicated. Returns what went wrong.
	std::string Start(HWND hwnd)
	{
		const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
		D3D_FEATURE_LEVEL level = {};
		HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
			levels, (UINT)std::size(levels), D3D11_SDK_VERSION, &m_pDevice, &level, &m_pContext);
		if (FAILED(hr)) {
			return "no device for the screen watch";
		}
		CComQIPtr<IDXGIDevice> pDXGIDevice(m_pDevice.p);
		CComPtr<IDXGIAdapter> pAdapter;
		if (!pDXGIDevice || FAILED(pDXGIDevice->GetAdapter(&pAdapter))) {
			return "no adapter for the screen watch";
		}
		const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
		for (UINT i = 0; ; i++) {
			CComPtr<IDXGIOutput> pOutput;
			if (FAILED(pAdapter->EnumOutputs(i, &pOutput))) {
				break;
			}
			DXGI_OUTPUT_DESC desc = {};
			pOutput->GetDesc(&desc);
			if (desc.Monitor != monitor) {
				continue;
			}
			CComQIPtr<IDXGIOutput1> pOutput1(pOutput.p);
			if (!pOutput1 || FAILED(pOutput1->DuplicateOutput(m_pDevice, &m_pDuplication))) {
				return "the desktop cannot be duplicated (another program may hold it)";
			}
			m_left = desc.DesktopCoordinates.left;
			m_top = desc.DesktopCoordinates.top;
			m_width = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
			m_height = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
			break;
		}
		if (!m_pDuplication) {
			return "the window is on no duplicated output";
		}

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = m_width;
		desc.Height = m_height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.SampleDesc = { 1, 0 };
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		if (FAILED(m_pDevice->CreateTexture2D(&desc, nullptr, &m_pStaging))) {
			return "no staging picture for the screen watch";
		}
		return {};
	}

	bool Ready() const { return m_pDuplication != nullptr; }

	// The window's own area of the desktop, reduced to a number. Two readings that
	// match mean the screen did not move, whatever the renderer believes it drew.
	uint64_t Signature(HWND hwnd)
	{
		if (!m_pDuplication) {
			return 0;
		}
		// Take whatever the desktop has moved on to, newest first.
		for (int i = 0; i < 8; i++) {
			DXGI_OUTDUPL_FRAME_INFO info = {};
			CComPtr<IDXGIResource> pResource;
			const HRESULT hr = m_pDuplication->AcquireNextFrame(120, &info, &pResource);
			if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
				break;
			}
			if (FAILED(hr)) {
				return 0;
			}
			if (CComQIPtr<ID3D11Texture2D> pDesktop = pResource.p) {
				m_pContext->CopyResource(m_pStaging, pDesktop);
			}
			m_pDuplication->ReleaseFrame();
		}

		RECT rc = {};
		GetClientRect(hwnd, &rc);
		POINT topLeft = { 0, 0 };
		ClientToScreen(hwnd, &topLeft);
		const int x0 = std::clamp<int>(topLeft.x - m_left, 0, (int)m_width - 1);
		const int y0 = std::clamp<int>(topLeft.y - m_top, 0, (int)m_height - 1);
		const int x1 = std::clamp<int>(x0 + rc.right, 0, (int)m_width);
		const int y1 = std::clamp<int>(y0 + rc.bottom, 0, (int)m_height);

		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(m_pContext->Map(m_pStaging, 0, D3D11_MAP_READ, 0, &mapped))) {
			return 0;
		}
		uint64_t signature = 0;
		for (int y = y0; y < y1; y += 7) {
			const BYTE* row = (const BYTE*)mapped.pData + (size_t)y * mapped.RowPitch;
			for (int x = x0; x < x1; x += 11) {
				signature = signature * 131 + row[(size_t)x * 4] + row[(size_t)x * 4 + 1];
			}
		}
		m_pContext->Unmap(m_pStaging, 0);
		return signature;
	}
};
