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

#include <initializer_list>
#include <string>
#include "DX11Helper.h"
#include "DlssMotionMask.h"
#include "DlssOpticalFlow.h"

// CDlssStabilizer
//
// Steadies DLSS 5 NR over time without touching the network. Only its change to
// the picture, output - input, is filtered over time and then added to the current
// frame, so the video itself is never delayed -- the EFFECT variant of
// Shaders/d3d11/ps_dlss_stabilize.hlsl. The history is clamped to the current
// neighbourhood, so a wrong motion estimate cannot drag old content far.
//
// Motion comes from NVIDIA Optical Flow, run at about 540 lines whatever the video:
// the history follows the picture, so moving areas are steadied too. Or from the
// shader detector (CDlssMotionMask), which steadies only what stands still.
//
// Measured in tools/dlssnr_probe --tstab on real pictures with known motion: plain
// DLSS shows 1.44x to 1.59x the low-frequency flicker of its input; with Optical
// Flow 1.06x to 1.14x, with the whole effect kept.

class CDlssStabilizer
{
public:
	enum class Motion { Detector, OpticalFlow };

	// How Optical Flow runs. The defaults are what the stabilizer was measured
	// with; DLSS Super Resolution needs finer vectors (--tsrq).
	struct FlowSettings {
		UINT flowFactor = 0;                                  // working pixels per flow pixel; 0: about 540 lines
		UINT gridSize = 4;                                    // flow pixels per vector
		NV_OF_PERF_LEVEL perfLevel = NV_OF_PERF_LEVEL_MEDIUM;
		bool bidirectional = true;                            // backward flow and cost feed the confidence map
		bool cost = true;
		bool operator==(const FlowSettings&) const = default;
	};

	// Everything for a working size and motion source; nothing if both already
	// match. Optical Flow that cannot start falls back to the detector, and
	// GetStatusLine() says why. The renderer's input layout, vertex shader and
	// samplers are used for every pass and must outlive this object's resources.
	//
	// bMotionOnly builds the Optical Flow vectors and nothing else, for DLSS Super
	// Resolution: no history, no result, no detector to fall back on. Stabilize
	// then does nothing, and GetMotionVectors() is null while Optical Flow is down.
	HRESULT Create(ID3D11Device* pDevice, ID3D11DeviceContext* pContext, UINT width, UINT height, Motion motion,
		ID3D11InputLayout* pInputLayout, ID3D11VertexShader* pVertexShader,
		ID3D11SamplerState* pSamplerPoint, ID3D11SamplerState* pSamplerLinear,
		bool bMotionOnly = false, const FlowSettings& flow = FlowSettings());
	void Release();

	bool Matches(UINT width, UINT height, Motion motion, bool bMotionOnly = false, const FlowSettings& flow = FlowSettings()) const;
	bool IsCreated() const { return m_width != 0; }

	// The next picture starts over: a seek, a new session.
	void Reset();

	// A new picture: its motion against the previous one. Call before DLSS, so the
	// vectors can reach the network as well.
	void PrepareMotion(ID3D11DeviceContext* pContext, ID3D11ShaderResourceView* pInput);

	// DLSSNR.MVec: RG16F, working-size pixels, current -> previous, shared with the
	// DLSS device. Null unless Optical Flow is the active source. Zero where no flow
	// exists yet.
	ID3D11Texture2D* GetMotionVectors() const;

	// After DLSS: the steadied picture into GetResult(). strength 0..1, 1 being the
	// measured setting. Only a new picture moves the history on. A redraw
	// (bNewPicture false) is steadied against the same history as its picture, so
	// it shows what changed meanwhile -- another setting, a zoom after upscaling --
	// without taking a second step.
	void Stabilize(ID3D11DeviceContext* pContext, ID3D11ShaderResourceView* pInput,
		ID3D11ShaderResourceView* pNetwork, float strength, bool bNewPicture);

	Tex2D_t* GetResult() { return &m_TexResult; }

	Motion ActiveMotion() const { return m_ActiveMotion; }
	const std::wstring& GetStatusLine() const { return m_status; }

private:
	struct Target {
		CComPtr<ID3D11Texture2D>          pTexture;
		CComPtr<ID3D11ShaderResourceView> pShaderResource;
		CComPtr<ID3D11RenderTargetView>   pRenderTarget;
	};

	static HRESULT CreateTarget(ID3D11Device* pDevice, UINT width, UINT height, DXGI_FORMAT format, Target& target);
	HRESULT CreateResources(ID3D11Device* pDevice, UINT width, UINT height, bool bMotionOnly);
	HRESULT StartDetector();
	void Draw(ID3D11DeviceContext* pContext, ID3D11PixelShader* pShader,
		std::initializer_list<ID3D11RenderTargetView*> targets, UINT width, UINT height,
		std::initializer_list<ID3D11ShaderResourceView*> inputs, const float* constants, size_t count);

	CComPtr<ID3D11Device>      m_pDevice;
	CComPtr<ID3D11PixelShader> m_pPSFlowFrame;
	CComPtr<ID3D11PixelShader> m_pPSFlowMotion;
	CComPtr<ID3D11PixelShader> m_pPSStabilize;
	CComPtr<ID3D11Buffer>      m_pConstants;
	CComPtr<ID3D11Buffer>      m_pQuad;

	Tex2D_t m_TexMotion;                       // RG16F, shared with the DLSS device
	CComPtr<ID3D11RenderTargetView> m_pMotionTarget;
	Target  m_Confidence;                      // R8
	Target  m_History[2];                      // RGBA16F: the steadied effect
	Target  m_Input[2];                        // RGBA16F: network inputs, for the reprojection check
	Tex2D_t m_TexResult;                       // RGBA16F: what the renderer shows
	CComPtr<ID3D11RenderTargetView> m_pResultTarget;

	CDlssOpticalFlow m_Flow;
	CDlssMotionMask  m_Detector;

	// The renderer's objects.
	ID3D11InputLayout*  m_pInputLayout   = nullptr;
	ID3D11VertexShader* m_pVertexShader  = nullptr;
	ID3D11SamplerState* m_pSamplerPoint  = nullptr;
	ID3D11SamplerState* m_pSamplerLinear = nullptr;

	UINT   m_width  = 0;
	UINT   m_height = 0;
	UINT   m_failedWidth  = 0;   // the last size and source Create could not make
	UINT   m_failedHeight = 0;
	Motion m_failedMotion = Motion::OpticalFlow;
	UINT   m_flowFactor = 1;
	Motion m_Requested    = Motion::OpticalFlow;
	Motion m_ActiveMotion = Motion::OpticalFlow;
	bool   m_bMotionOnly  = false;
	FlowSettings m_FlowSettings;

	int  m_iHistory      = 0;       // m_History slot the last new picture wrote
	int  m_iInput        = 0;       // m_Input slot holding the last new picture's input
	int  m_iFlowFailures = 0;
	bool m_bHistoryValid = false;
	bool m_bHaveMotion   = false;
	bool m_bHaveResult   = false;   // a new picture was steadied since the reset
	bool m_bLastReset    = true;    // and it started the history over

	std::wstring m_status;
};
