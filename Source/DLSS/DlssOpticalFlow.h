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

#pragma once

#include <string>
#include "NvOF/nvOpticalFlowD3D11.h"

// CDlssOpticalFlow
//
// NVIDIA Optical Flow -- the NVOFA engine of Turing and later GPUs -- on the
// renderer's own Direct3D 11 device, through the display driver's nvofapi64.dll.
// Flow runs between two R8 luma frames at a size the caller picks, usually well
// below the video's.
//
// Forward flow goes from the frame just written to the one before it: for every
// block, where its content was one frame earlier. That is the direction DLSS
// motion vectors use. Backward flow and matching costs are there to reject bad
// vectors.

class CDlssOpticalFlow
{
public:
	struct Options {
		UINT             gridSize      = 4;                        // pixels per vector: 1, 2 or 4
		NV_OF_PERF_LEVEL perfLevel     = NV_OF_PERF_LEVEL_MEDIUM;
		bool             bidirectional = true;                     // backward flow as well
		bool             cost          = true;                     // matching cost per vector
	};

	CDlssOpticalFlow() = default;
	~CDlssOpticalFlow();
	CDlssOpticalFlow(const CDlssOpticalFlow&) = delete;
	CDlssOpticalFlow& operator=(const CDlssOpticalFlow&) = delete;

	// A session for frames of width x height; the driver's API is loaded on first
	// use. On failure nothing is left allocated and GetStatusLine() says why. A
	// grid size the GPU lacks is replaced by the nearest one it has.
	bool Init(ID3D11Device* pDevice, ID3D11DeviceContext* pContext, UINT width, UINT height, const Options& options);
	void Release();
	bool IsReady() const { return m_hOF != nullptr; }

	UINT Width() const { return m_width; }
	UINT Height() const { return m_height; }
	UINT GridSize() const { return m_gridSize; }
	UINT OutputWidth() const { return m_outWidth; }
	UINT OutputHeight() const { return m_outHeight; }
	bool Bidirectional() const { return m_bBidirectional; }
	bool HasCost() const { return m_bCost; }

	// Where the next frame goes: R8_UNORM, Width x Height.
	ID3D11Texture2D*        FrameTexture() const { return m_frames[m_current].pTexture; }
	ID3D11RenderTargetView* FrameTarget() const { return m_frames[m_current].pRenderTarget; }

	// Flow between the frame just written and the one written before it; then the
	// two swap roles. False until two frames have been written since Init or Reset,
	// and on failure. The first flow after a Reset ignores the engine's temporal
	// hints, which would still follow the old motion.
	bool Execute();
	void Reset() { m_iFramesWritten = 0; m_bResetHints = true; }
	// The last Execute returned false because the engine failed, not because there
	// was no previous frame yet.
	bool LastExecuteFailed() const { return m_bLastFailed; }

	// Results of the last successful Execute, OutputWidth x OutputHeight. Flow is
	// R16G16_SINT in 1/32 pixel of the flow frame; cost is R8_UINT, higher is worse.
	ID3D11Texture2D*          ForwardFlowTexture() const { return m_fwdFlow.pTexture; }
	ID3D11Texture2D*          BackwardFlowTexture() const { return m_bwdFlow.pTexture; }
	ID3D11Texture2D*          ForwardCostTexture() const { return m_fwdCost.pTexture; }
	ID3D11ShaderResourceView* ForwardFlow() const { return m_fwdFlow.pShaderResource; }
	ID3D11ShaderResourceView* BackwardFlow() const { return m_bwdFlow.pShaderResource; }
	ID3D11ShaderResourceView* ForwardCost() const { return m_fwdCost.pShaderResource; }
	ID3D11ShaderResourceView* BackwardCost() const { return m_bwdCost.pShaderResource; }

	const std::wstring& GetStatusLine() const { return m_status; }
	UINT DriverApiVersion() const { return m_driverVersion; }   // major << 4 | minor

private:
	struct Buffer {
		CComPtr<ID3D11Texture2D>          pTexture;
		CComPtr<ID3D11ShaderResourceView> pShaderResource;
		CComPtr<ID3D11RenderTargetView>   pRenderTarget;
		NvOFGPUBufferHandle               hBuffer = nullptr;
	};

	bool LoadApi();
	bool CreateBuffer(ID3D11Device* pDevice, UINT width, UINT height, DXGI_FORMAT format, bool bTarget, Buffer& buffer);
	void ReleaseBuffer(Buffer& buffer);
	bool Fail(const std::wstring& what, NV_OF_STATUS status);

	HMODULE                       m_hModule = nullptr;
	NV_OF_D3D11_API_FUNCTION_LIST m_api = {};
	NvOFHandle                    m_hOF = nullptr;
	UINT                          m_driverVersion = 0;

	Buffer m_frames[2];
	Buffer m_fwdFlow;
	Buffer m_bwdFlow;
	Buffer m_fwdCost;
	Buffer m_bwdCost;

	UINT m_width = 0, m_height = 0;
	UINT m_gridSize = 0;
	UINT m_outWidth = 0, m_outHeight = 0;
	bool m_bBidirectional = false;
	bool m_bCost = false;
	int  m_current = 0;          // m_frames slot the next frame goes into
	int  m_iFramesWritten = 0;
	bool m_bResetHints = true;
	bool m_bLastFailed = false;

	std::wstring m_status;
};
