/*
 * (C) 2026 see Authors.txt
 *
 * This file is part of MPC-BE.
 *
 * MPC-BE is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * MPC-BE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "stdafx.h"
#include "DlssOpticalFlow.h"

namespace {

const wchar_t* StatusName(NV_OF_STATUS status)
{
	switch (status) {
	case NV_OF_SUCCESS:                   return L"success";
	case NV_OF_ERR_OF_NOT_AVAILABLE:      return L"not available";
	case NV_OF_ERR_UNSUPPORTED_DEVICE:    return L"unsupported device";
	case NV_OF_ERR_DEVICE_DOES_NOT_EXIST: return L"device gone";
	case NV_OF_ERR_INVALID_PTR:           return L"invalid pointer";
	case NV_OF_ERR_INVALID_PARAM:         return L"invalid parameter";
	case NV_OF_ERR_INVALID_CALL:          return L"invalid call";
	case NV_OF_ERR_INVALID_VERSION:       return L"invalid version";
	case NV_OF_ERR_OUT_OF_MEMORY:         return L"out of memory";
	case NV_OF_ERR_NOT_INITIALIZED:       return L"not initialised";
	case NV_OF_ERR_UNSUPPORTED_FEATURE:   return L"unsupported feature";
	case NV_OF_ERR_GENERIC:               return L"generic error";
	}
	return L"unknown error";
}

} // namespace

CDlssOpticalFlow::~CDlssOpticalFlow()
{
	Release();
	if (m_hModule) {
		FreeLibrary(m_hModule);
		m_hModule = nullptr;
	}
}

bool CDlssOpticalFlow::LoadApi()
{
	if (m_api.nvCreateOpticalFlowD3D11) {
		return true;
	}
	if (!m_hModule) {
		// The display driver installs it into System32; never take one from
		// the player's directory.
#ifdef _WIN64
		m_hModule = LoadLibraryExW(L"nvofapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
		m_hModule = LoadLibraryExW(L"nvofapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
#endif
	}
	if (!m_hModule) {
		m_status = L"NVIDIA Optical Flow is not installed (no nvofapi64.dll from the driver)";
		return false;
	}

	using PfnGetVersion = NV_OF_STATUS(NVOFAPI*)(uint32_t*);
	using PfnCreate = NV_OF_STATUS(NVOFAPI*)(uint32_t, NV_OF_D3D11_API_FUNCTION_LIST*);
	const auto pfnVersion = (PfnGetVersion)GetProcAddress(m_hModule, "NvOFGetMaxSupportedApiVersion");
	const auto pfnCreate = (PfnCreate)GetProcAddress(m_hModule, "NvOFAPICreateInstanceD3D11");
	if (!pfnCreate) {
		m_status = L"the driver's Optical Flow has no Direct3D 11 interface";
		return false;
	}

	uint32_t version = 0;
	if (pfnVersion && pfnVersion(&version) == NV_OF_SUCCESS) {
		m_driverVersion = version;
		if (version < NV_OF_API_VERSION) {
			m_status = std::format(L"the driver offers Optical Flow API {}.{}; {}.{} is needed",
				version >> 4, version & 0xF, NV_OF_API_MAJOR_VERSION, NV_OF_API_MINOR_VERSION);
			return false;
		}
	}

	NV_OF_D3D11_API_FUNCTION_LIST api = {};
	const NV_OF_STATUS status = pfnCreate(NV_OF_API_VERSION, &api);
	if (status != NV_OF_SUCCESS || !api.nvCreateOpticalFlowD3D11 || !api.nvOFInit || !api.nvOFExecute) {
		m_status = std::format(L"NvOFAPICreateInstanceD3D11 failed: {}", StatusName(status));
		return false;
	}
	m_api = api;
	return true;
}

bool CDlssOpticalFlow::Fail(const std::wstring& what, NV_OF_STATUS status)
{
	std::wstring detail;
	if (m_hOF && m_api.nvOFGetLastError) {
		char text[256] = {};
		uint32_t size = sizeof(text);
		if (m_api.nvOFGetLastError(m_hOF, text, &size) == NV_OF_SUCCESS && text[0]) {
			const size_t length = strnlen(text, sizeof(text));
			detail.assign(text, text + length);
		}
	}
	m_status = std::format(L"{} failed: {}{}{}", what, StatusName(status), detail.empty() ? L"" : L" -- ", detail);
	return false;
}

bool CDlssOpticalFlow::CreateBuffer(ID3D11Device* pDevice, UINT width, UINT height, DXGI_FORMAT format, bool bTarget, Buffer& buffer)
{
	// The bindings the SDK's own buffers carry.
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width            = width;
	desc.Height           = height;
	desc.MipLevels        = 1;
	desc.ArraySize        = 1;
	desc.Format           = format;
	desc.SampleDesc.Count = 1;
	desc.Usage            = D3D11_USAGE_DEFAULT;
	desc.BindFlags        = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	buffer = Buffer{};
	HRESULT hr = pDevice->CreateTexture2D(&desc, nullptr, &buffer.pTexture);
	if (SUCCEEDED(hr)) {
		hr = pDevice->CreateShaderResourceView(buffer.pTexture, nullptr, &buffer.pShaderResource);
	}
	if (SUCCEEDED(hr) && bTarget) {
		hr = pDevice->CreateRenderTargetView(buffer.pTexture, nullptr, &buffer.pRenderTarget);
	}
	if (FAILED(hr)) {
		m_status = std::format(L"cannot create a {}x{} flow texture (0x{:08X})", width, height, (unsigned)hr);
		buffer = Buffer{};
		return false;
	}

	const NV_OF_STATUS status = m_api.nvOFRegisterResourceD3D11(m_hOF, buffer.pTexture, &buffer.hBuffer);
	if (status != NV_OF_SUCCESS || !buffer.hBuffer) {
		buffer.hBuffer = nullptr;
		Fail(L"registering a flow texture", status);
		buffer = Buffer{};
		return false;
	}
	return true;
}

void CDlssOpticalFlow::ReleaseBuffer(Buffer& buffer)
{
	if (buffer.hBuffer && m_api.nvOFUnregisterResourceD3D11) {
		m_api.nvOFUnregisterResourceD3D11(buffer.hBuffer);
	}
	buffer = Buffer{};
}

bool CDlssOpticalFlow::Init(ID3D11Device* pDevice, ID3D11DeviceContext* pContext, UINT width, UINT height, const Options& options)
{
	Release();
	if (!pDevice || !pContext || !width || !height) {
		m_status = L"no device or no size";
		return false;
	}
	if (!LoadApi()) {
		return false;
	}

	NV_OF_STATUS status = m_api.nvCreateOpticalFlowD3D11(pDevice, pContext, &m_hOF);
	if (status != NV_OF_SUCCESS || !m_hOF) {
		m_hOF = nullptr;
		m_status = std::format(L"cannot open Optical Flow on this device: {}", StatusName(status));
		return false;
	}

	auto caps = [&](NV_OF_CAPS cap) {
		std::vector<uint32_t> values;
		uint32_t size = 0;
		if (m_api.nvOFGetCaps(m_hOF, cap, nullptr, &size) == NV_OF_SUCCESS && size) {
			values.resize(size);
			if (m_api.nvOFGetCaps(m_hOF, cap, values.data(), &size) != NV_OF_SUCCESS) {
				values.clear();
			}
		}
		return values;
	};

	// The requested grid if the GPU has it, else the finest coarser one, else the
	// coarsest it has (Turing: 4 only; Ampere and later: 1, 2 and 4).
	UINT grid = 0;
	std::vector<uint32_t> grids = caps(NV_OF_CAPS_SUPPORTED_OUTPUT_GRID_SIZES);
	std::sort(grids.begin(), grids.end());
	for (uint32_t g : grids) {
		if (g >= options.gridSize) {
			grid = g;
			break;
		}
	}
	if (!grid) {
		grid = grids.empty() ? 4 : grids.back();
	}

	const std::vector<uint32_t> minW = caps(NV_OF_CAPS_WIDTH_MIN), maxW = caps(NV_OF_CAPS_WIDTH_MAX);
	const std::vector<uint32_t> minH = caps(NV_OF_CAPS_HEIGHT_MIN), maxH = caps(NV_OF_CAPS_HEIGHT_MAX);
	if ((!minW.empty() && width < minW[0]) || (!maxW.empty() && width > maxW[0])
			|| (!minH.empty() && height < minH[0]) || (!maxH.empty() && height > maxH[0])) {
		m_status = std::format(L"{}x{} is outside what Optical Flow accepts", width, height);
		Release();
		return false;
	}

	NV_OF_INIT_PARAMS init = {};
	init.width               = width;
	init.height              = height;
	init.outGridSize         = (NV_OF_OUTPUT_VECTOR_GRID_SIZE)grid;
	init.hintGridSize        = NV_OF_HINT_VECTOR_GRID_SIZE_UNDEFINED;
	init.mode                = NV_OF_MODE_OPTICALFLOW;
	init.perfLevel           = options.perfLevel;
	init.enableExternalHints = NV_OF_FALSE;
	init.enableOutputCost    = options.cost ? NV_OF_TRUE : NV_OF_FALSE;
	init.hPrivData           = nullptr;
	init.disparityRange      = NV_OF_STEREO_DISPARITY_RANGE_UNDEFINED;
	init.enableRoi           = NV_OF_FALSE;
	init.predDirection       = options.bidirectional ? NV_OF_PRED_DIRECTION_BOTH : NV_OF_PRED_DIRECTION_FORWARD;
	init.enableGlobalFlow    = NV_OF_FALSE;
	init.inputBufferFormat   = NV_OF_BUFFER_FORMAT_GRAYSCALE8;

	status = m_api.nvOFInit(m_hOF, &init);
	bool bidirectional = options.bidirectional;
	if (status != NV_OF_SUCCESS && bidirectional) {
		// Older engines have no backward flow: forward alone still works.
		init.predDirection = NV_OF_PRED_DIRECTION_FORWARD;
		bidirectional = false;
		status = m_api.nvOFInit(m_hOF, &init);
	}
	if (status != NV_OF_SUCCESS) {
		Fail(std::format(L"initialising {}x{} flow", width, height), status);
		const std::wstring why = m_status;
		Release();
		m_status = why;
		return false;
	}

	m_width          = width;
	m_height         = height;
	m_gridSize       = grid;
	m_outWidth       = (width + grid - 1) / grid;
	m_outHeight      = (height + grid - 1) / grid;
	m_bBidirectional = bidirectional;
	m_bCost          = options.cost;
	m_bTemporalHints = options.temporalHints;

	bool ok = CreateBuffer(pDevice, width, height, DXGI_FORMAT_R8_UNORM, true, m_frames[0])
		&& CreateBuffer(pDevice, width, height, DXGI_FORMAT_R8_UNORM, true, m_frames[1])
		&& CreateBuffer(pDevice, m_outWidth, m_outHeight, DXGI_FORMAT_R16G16_SINT, false, m_fwdFlow);
	if (ok && m_bBidirectional) {
		ok = CreateBuffer(pDevice, m_outWidth, m_outHeight, DXGI_FORMAT_R16G16_SINT, false, m_bwdFlow);
	}
	if (ok && m_bCost) {
		ok = CreateBuffer(pDevice, m_outWidth, m_outHeight, DXGI_FORMAT_R8_UINT, false, m_fwdCost);
		if (ok && m_bBidirectional) {
			ok = CreateBuffer(pDevice, m_outWidth, m_outHeight, DXGI_FORMAT_R8_UINT, false, m_bwdCost);
		}
	}
	if (!ok) {
		const std::wstring why = m_status;
		Release();
		m_status = why;
		return false;
	}

	m_current        = 0;
	m_iFramesWritten = 0;
	m_bResetHints    = true;
	m_status = std::format(L"{}x{}, grid {}, {}{}", width, height, grid,
		options.perfLevel == NV_OF_PERF_LEVEL_SLOW ? L"slow" : options.perfLevel == NV_OF_PERF_LEVEL_FAST ? L"fast" : L"medium",
		m_bBidirectional ? L", both directions" : L"");
	return true;
}

void CDlssOpticalFlow::Release()
{
	for (Buffer* buffer : { &m_frames[0], &m_frames[1], &m_fwdFlow, &m_bwdFlow, &m_fwdCost, &m_bwdCost }) {
		ReleaseBuffer(*buffer);
	}
	if (m_hOF) {
		m_api.nvOFDestroy(m_hOF);
		m_hOF = nullptr;
	}
	m_width = m_height = 0;
	m_gridSize = 0;
	m_outWidth = m_outHeight = 0;
	m_bBidirectional = false;
	m_bCost = false;
	m_bTemporalHints = true;
	m_current = 0;
	m_iFramesWritten = 0;
	m_bResetHints = true;
	m_bLastFailed = false;
}

bool CDlssOpticalFlow::Execute()
{
	m_bLastFailed = false;
	if (!m_hOF) {
		return false;
	}
	const int current = m_current;
	const int previous = 1 - m_current;
	m_current = previous;   // the next frame overwrites the older one

	if (m_iFramesWritten < 2) {
		m_iFramesWritten++;
	}
	if (m_iFramesWritten < 2) {
		return false;
	}

	NV_OF_EXECUTE_INPUT_PARAMS in = {};
	in.inputFrame           = m_frames[current].hBuffer;
	in.referenceFrame       = m_frames[previous].hBuffer;
	in.disableTemporalHints = (m_bResetHints || !m_bTemporalHints) ? NV_OF_TRUE : NV_OF_FALSE;

	NV_OF_EXECUTE_OUTPUT_PARAMS out = {};
	out.outputBuffer        = m_fwdFlow.hBuffer;
	out.outputCostBuffer    = m_bCost ? m_fwdCost.hBuffer : nullptr;
	out.bwdOutputBuffer     = m_bBidirectional ? m_bwdFlow.hBuffer : nullptr;
	out.bwdOutputCostBuffer = (m_bBidirectional && m_bCost) ? m_bwdCost.hBuffer : nullptr;

	const NV_OF_STATUS status = m_api.nvOFExecute(m_hOF, &in, &out);
	if (status != NV_OF_SUCCESS) {
		m_bLastFailed = true;
		return Fail(L"nvOFExecute", status);
	}
	m_bResetHints = false;
	return true;
}
