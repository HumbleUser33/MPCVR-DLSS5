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
#include <vector>
#include "DX11Helper.h"

// CDlssMotionMask
//
// The history mask of the DLSS 5 NR stabilizer. DLSSNR.ControlMask weighs, per
// pixel, how far the network leans on its own previous output. That output is
// not moved along with the video, so leaning on it steadies what is still and
// smears what moves. The passes of Shaders/d3d11/ps_dlss_motion.hlsl tell the
// two apart, and the mask gets the stabilizer strength where the picture is
// still and 1 wherever it moves or changes.
//
// The detector was tuned in tools/dlssnr_probe (--tdetect) against a perfect
// motion map; the reasoning behind its constants is at the top of
// detect_suite.inl. On an RTX 3050 it costs 0.3 ms of GPU time at 1080p and
// 1.1 ms at 2160p (--tbench).

class CDlssMotionMask
{
public:
	// Everything for a working size: shaders, quarter-resolution textures and
	// the full-size mask, which is shared with the DLSS device. Does nothing if
	// the size already matches. Clear the DLSS guides before a call that can
	// recreate the mask, and before Release, so no handle outlives its texture.
	HRESULT Create(ID3D11Device* pDevice, UINT width, UINT height);
	void Release();

	bool Matches(UINT width, UINT height) const;
	ID3D11Texture2D* GetMask() const { return m_TexMask.pTexture; }

	// The next picture starts over: a seek, a new session, a reset of the
	// network's history.
	void Reset() { m_bResetPending = true; }

	// One new picture. pColour is the DLSS input at the working size; strength
	// is the mask value where nothing moves, 0..1. Draws with the renderer's
	// vertex shader, input layout and samplers, unbinds what it bound, and
	// leaves the rasterizer state alone. Without bWriteMask only the motion
	// analysis runs, for GetAge().
	void Process(ID3D11DeviceContext* pContext, ID3D11ShaderResourceView* pColour, float strength,
		ID3D11InputLayout* pInputLayout, ID3D11VertexShader* pVertexShader,
		ID3D11SamplerState* pSamplerPoint, ID3D11SamplerState* pSamplerLinear, bool bWriteMask = true);

	// Frame difference of static content, in luma units; 0 until measured.
	float NoiseFloor() const { return m_fNoise; }

	// The protection age the last Process wrote, a quarter of the working size:
	// 0 where nothing moved lately, 1 where something just did. R16F.
	ID3D11ShaderResourceView* GetAge() const { return m_Age[1 - m_iAge].pShaderResource; }

private:
	struct Target {
		CComPtr<ID3D11Texture2D>          pTexture;
		CComPtr<ID3D11ShaderResourceView> pShaderResource;
		CComPtr<ID3D11RenderTargetView>   pRenderTarget;
		UINT width = 0, height = 0, levels = 1;
	};

	static constexpr int  kHistory  = 9;   // this picture and the eight before it
	static constexpr UINT kStatsMip = 2;   // of the quarter-resolution differences: 16x16 source pixels

	static HRESULT CreateTarget(ID3D11Device* pDevice, UINT width, UINT height, DXGI_FORMAT format, bool bMips, Target& target);
	void Draw(ID3D11DeviceContext* pContext, ID3D11PixelShader* pShader, ID3D11RenderTargetView* pTarget,
		UINT width, UINT height, std::initializer_list<ID3D11ShaderResourceView*> inputs);
	void ReadStats(ID3D11DeviceContext* pContext);
	HRESULT CreateResources(ID3D11Device* pDevice, UINT width, UINT height);

	CComPtr<ID3D11PixelShader> m_pPSLuma;
	CComPtr<ID3D11PixelShader> m_pPSDiff;
	CComPtr<ID3D11PixelShader> m_pPSAge;
	CComPtr<ID3D11PixelShader> m_pPSMask;
	CComPtr<ID3D11Buffer>      m_pConstants;
	CComPtr<ID3D11Buffer>      m_pQuad;

	Target m_Luma[kHistory];
	Target m_Age[2];
	Target m_Diff;                            // red: motion, green: consecutive pictures only
	CComPtr<ID3D11Texture2D> m_pStageDiff;    // m_Diff at kStatsMip
	CComPtr<ID3D11Texture2D> m_pStageMean;    // mean luma, last mip
	Tex2D_t m_TexMask;
	CComPtr<ID3D11RenderTargetView> m_pMaskTarget;
	UINT m_width  = 0;
	UINT m_height = 0;
	UINT m_failedWidth  = 0;   // the last size Create could not make
	UINT m_failedHeight = 0;

	// The renderer's objects, for the duration of Process.
	ID3D11InputLayout*  m_pInputLayout   = nullptr;
	ID3D11VertexShader* m_pVertexShader  = nullptr;
	ID3D11SamplerState* m_pSamplerPoint  = nullptr;
	ID3D11SamplerState* m_pSamplerLinear = nullptr;

	// Per sequence.
	bool  m_bResetPending = true;
	int   m_iHead      = 0;    // m_Luma slot this picture goes into
	int   m_iAge       = 0;    // m_Age slot this picture writes
	int   m_iFrames    = 0;    // pictures since the reset
	int   m_iRelearn   = 0;    // pictures left of fast noise-floor learning
	bool  m_bHaveStats = false;
	float m_fNoise     = 0;
	float m_fProtect   = 1;    // the whole picture changed: fade, cut
	float m_fPrevMean  = -1;

	std::vector<float> m_Row;
	std::vector<float> m_Values;
};
