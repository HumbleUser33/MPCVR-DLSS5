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
#include <DirectXPackedVector.h>
#include "resource.h"
#include "Helper.h"
#include "DlssMotionMask.h"

namespace {

// Tuned in tools/dlssnr_probe (--tdetect) against a perfect motion map.
constexpr float kThreshold     = 2.5f;       // motion starts at this many noise floors
constexpr float kSoftness      = 1.0f;       // and is complete one noise floor further
constexpr float kDecay         = 1.0f / 8;   // protection lost per picture: an 8-picture trail
constexpr float kMinFloor      = 0.0015f;    // frame difference of clean content, luma units
constexpr float kMaxFloor      = 0.02f;
constexpr float kFloorRise     = 0.005f;     // share of a higher floor accepted per picture
constexpr float kRelearnRise   = 0.2f;       // the same, shortly after a fade or a cut
constexpr int   kRelearnFrames = 12;
constexpr float kFadeThreshold = 0.0015f;    // change of mean luma per picture that counts as one

// The layout of the renderer's VERTEX.
struct QuadVertex {
	float x, y, z;
	float u, v;
};

} // namespace

HRESULT CDlssMotionMask::CreateTarget(ID3D11Device* pDevice, UINT width, UINT height, DXGI_FORMAT format, bool bMips, Target& target)
{
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width            = width;
	desc.Height           = height;
	desc.MipLevels        = bMips ? 0 : 1;
	desc.ArraySize        = 1;
	desc.Format           = format;
	desc.SampleDesc.Count = 1;
	desc.Usage            = D3D11_USAGE_DEFAULT;
	desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	desc.MiscFlags        = bMips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;

	target = Target{};
	HRESULT hr = pDevice->CreateTexture2D(&desc, nullptr, &target.pTexture);
	if (SUCCEEDED(hr)) {
		hr = pDevice->CreateShaderResourceView(target.pTexture, nullptr, &target.pShaderResource);
	}
	if (SUCCEEDED(hr)) {
		// Mip 0: the passes write the full size, GenerateMips fills the rest.
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format        = format;
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		hr = pDevice->CreateRenderTargetView(target.pTexture, &rtvDesc, &target.pRenderTarget);
	}
	if (SUCCEEDED(hr)) {
		target.pTexture->GetDesc(&desc);
		target.width  = width;
		target.height = height;
		target.levels = desc.MipLevels;
	}
	return hr;
}

bool CDlssMotionMask::Matches(UINT width, UINT height) const
{
	return m_width == width && m_height == height && m_TexMask.pTexture && m_pPSMask;
}

HRESULT CDlssMotionMask::Create(ID3D11Device* pDevice, UINT width, UINT height)
{
	CheckPointer(pDevice, E_POINTER);
	if (Matches(width, height)) {
		return S_OK;
	}
	// A size that failed once fails again: no new attempt on every picture until
	// the size changes or the owner calls Release.
	if (width == m_failedWidth && height == m_failedHeight) {
		return E_FAIL;
	}
	const HRESULT hr = CreateResources(pDevice, width, height);
	if (FAILED(hr)) {
		m_failedWidth  = width;
		m_failedHeight = height;
	}
	return hr;
}

HRESULT CDlssMotionMask::CreateResources(ID3D11Device* pDevice, UINT width, UINT height)
{
	Release();
	if (width < 16 || height < 16) {
		return E_INVALIDARG;
	}

	HRESULT hr = S_OK;
	const struct { UINT resid; ID3D11PixelShader** ppShader; } shaders[] = {
		{ IDF_PS_11_DLSS_MOTION_LUMA, &m_pPSLuma },
		{ IDF_PS_11_DLSS_MOTION_DIFF, &m_pPSDiff },
		{ IDF_PS_11_DLSS_MOTION_AGE,  &m_pPSAge  },
		{ IDF_PS_11_DLSS_MOTION_MASK, &m_pPSMask },
	};
	for (const auto& s : shaders) {
		LPVOID data = nullptr;
		DWORD size = 0;
		hr = GetDataFromResource(data, size, s.resid);
		if (SUCCEEDED(hr)) {
			hr = pDevice->CreatePixelShader(data, size, nullptr, s.ppShader);
		}
		if (FAILED(hr)) {
			DLog(L"CDlssMotionMask::Create() : shader {} failed with error {}", s.resid, HR2Str(hr));
			Release();
			return hr;
		}
	}

	D3D11_BUFFER_DESC bufferDesc = { 8 * sizeof(float), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
	hr = pDevice->CreateBuffer(&bufferDesc, nullptr, &m_pConstants);
	if (SUCCEEDED(hr)) {
		// FillVertices for an unrotated, unflipped blit of a whole texture: the
		// winding the renderer draws with, which survives back-face culling.
		static const QuadVertex quad[4] = {
			{ -1, -1, 0,  0, 1 },
			{ -1, +1, 0,  0, 0 },
			{ +1, -1, 0,  1, 1 },
			{ +1, +1, 0,  1, 0 },
		};
		bufferDesc = { sizeof(quad), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
		const D3D11_SUBRESOURCE_DATA init = { quad, 0, 0 };
		hr = pDevice->CreateBuffer(&bufferDesc, &init, &m_pQuad);
	}

	const UINT w4 = std::max(width / 4, 1u);
	const UINT h4 = std::max(height / 4, 1u);
	for (int i = 0; i < kHistory && SUCCEEDED(hr); i++) {
		hr = CreateTarget(pDevice, w4, h4, DXGI_FORMAT_R16_FLOAT, true, m_Luma[i]);
	}
	for (int i = 0; i < 2 && SUCCEEDED(hr); i++) {
		hr = CreateTarget(pDevice, w4, h4, DXGI_FORMAT_R16_FLOAT, false, m_Age[i]);
	}
	if (SUCCEEDED(hr)) {
		hr = CreateTarget(pDevice, w4, h4, DXGI_FORMAT_R16G16_FLOAT, true, m_Diff);
	}

	if (SUCCEEDED(hr)) {
		D3D11_TEXTURE2D_DESC stageDesc = {};
		stageDesc.Width            = std::max(w4 >> kStatsMip, 1u);
		stageDesc.Height           = std::max(h4 >> kStatsMip, 1u);
		stageDesc.MipLevels        = 1;
		stageDesc.ArraySize        = 1;
		stageDesc.Format           = DXGI_FORMAT_R16G16_FLOAT;
		stageDesc.SampleDesc.Count = 1;
		stageDesc.Usage            = D3D11_USAGE_STAGING;
		stageDesc.CPUAccessFlags   = D3D11_CPU_ACCESS_READ;
		hr = pDevice->CreateTexture2D(&stageDesc, nullptr, &m_pStageDiff);
		if (SUCCEEDED(hr)) {
			stageDesc.Width  = 1;
			stageDesc.Height = 1;
			stageDesc.Format = DXGI_FORMAT_R16_FLOAT;
			hr = pDevice->CreateTexture2D(&stageDesc, nullptr, &m_pStageMean);
		}
	}

	if (SUCCEEDED(hr)) {
		hr = m_TexMask.CheckCreate(pDevice, DXGI_FORMAT_R8_UNORM, width, height, Tex2D_DefaultShaderRTargetUAVShared);
	}
	if (SUCCEEDED(hr)) {
		hr = pDevice->CreateRenderTargetView(m_TexMask.pTexture, nullptr, &m_pMaskTarget);
	}

	if (FAILED(hr)) {
		DLog(L"CDlssMotionMask::Create() : {}x{} failed with error {}", width, height, HR2Str(hr));
		Release();
		return hr;
	}

	// A mask the network reads before the first picture must mean "no history".
	CComPtr<ID3D11DeviceContext> pContext;
	pDevice->GetImmediateContext(&pContext);
	const FLOAT one[4] = { 1, 1, 1, 1 };
	pContext->ClearRenderTargetView(m_pMaskTarget, one);

	m_width  = width;
	m_height = height;
	m_bResetPending = true;

	return S_OK;
}

void CDlssMotionMask::Release()
{
	m_pPSLuma.Release();
	m_pPSDiff.Release();
	m_pPSAge.Release();
	m_pPSMask.Release();
	m_pConstants.Release();
	m_pQuad.Release();
	for (auto& target : m_Luma) {
		target = Target{};
	}
	m_Age[0] = Target{};
	m_Age[1] = Target{};
	m_Diff = Target{};
	m_pStageDiff.Release();
	m_pStageMean.Release();
	m_pMaskTarget.Release();
	m_TexMask.Release();
	m_width  = 0;
	m_height = 0;
	m_failedWidth  = 0;
	m_failedHeight = 0;
	m_bResetPending = true;
}

void CDlssMotionMask::Draw(ID3D11DeviceContext* pContext, ID3D11PixelShader* pShader, ID3D11RenderTargetView* pTarget,
	UINT width, UINT height, std::initializer_list<ID3D11ShaderResourceView*> inputs)
{
	ID3D11ShaderResourceView* views[4] = {};
	std::copy_n(inputs.begin(), std::min<size_t>(inputs.size(), std::size(views)), views);

	const D3D11_VIEWPORT viewport = { 0, 0, (FLOAT)width, (FLOAT)height, 0, 1 };
	const UINT stride = sizeof(QuadVertex);
	const UINT offset = 0;
	ID3D11SamplerState* samplers[2] = { m_pSamplerPoint, m_pSamplerLinear };
	ID3D11Buffer* pConstants = m_pConstants;
	ID3D11Buffer* pQuad = m_pQuad;

	pContext->IASetInputLayout(m_pInputLayout);
	pContext->OMSetRenderTargets(1, &pTarget, nullptr);
	pContext->RSSetViewports(1, &viewport);
	pContext->OMSetBlendState(nullptr, nullptr, D3D11_DEFAULT_SAMPLE_MASK);
	pContext->VSSetShader(m_pVertexShader, nullptr, 0);
	pContext->PSSetShader(pShader, nullptr, 0);
	pContext->PSSetShaderResources(0, (UINT)std::size(views), views);
	pContext->PSSetSamplers(0, (UINT)std::size(samplers), samplers);
	pContext->PSSetConstantBuffers(0, 1, &pConstants);
	pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	pContext->IASetVertexBuffers(0, 1, &pQuad, &stride, &offset);
	pContext->Draw(4, 0);

	// Unbound, so the next pass can write what this one read.
	ID3D11ShaderResourceView* noViews[4] = {};
	pContext->PSSetShaderResources(0, (UINT)std::size(noViews), noViews);
	ID3D11RenderTargetView* noTarget = nullptr;
	pContext->OMSetRenderTargets(1, &noTarget, nullptr);
}

void CDlssMotionMask::Process(ID3D11DeviceContext* pContext, ID3D11ShaderResourceView* pColour, float strength,
	ID3D11InputLayout* pInputLayout, ID3D11VertexShader* pVertexShader,
	ID3D11SamplerState* pSamplerPoint, ID3D11SamplerState* pSamplerLinear, bool bWriteMask)
{
	if (!m_width || !pContext || !pColour) {
		return;
	}
	m_pInputLayout   = pInputLayout;
	m_pVertexShader  = pVertexShader;
	m_pSamplerPoint  = pSamplerPoint;
	m_pSamplerLinear = pSamplerLinear;

	if (m_bResetPending) {
		// Everything counts as moving until the detector has seen a few pictures.
		const FLOAT one[4] = { 1, 1, 1, 1 };
		pContext->ClearRenderTargetView(m_Age[0].pRenderTarget, one);
		pContext->ClearRenderTargetView(m_Age[1].pRenderTarget, one);
		m_iHead      = 0;
		m_iAge       = 0;
		m_iFrames    = 0;
		m_iRelearn   = 0;
		m_bHaveStats = false;
		m_fNoise     = 0;
		m_fProtect   = 1;
		m_fPrevMean  = -1;
		m_bResetPending = false;
	}

	const Target& lumaCur  = m_Luma[m_iHead];
	const Target& lumaPrev = m_Luma[(m_iHead + kHistory - 1) % kHistory];
	const Target& luma4    = m_Luma[(m_iHead + kHistory - 4) % kHistory];
	const Target& luma8    = m_Luma[(m_iHead + kHistory - 8) % kHistory];
	const Target& ageCur   = m_Age[m_iAge];
	const Target& agePrev  = m_Age[1 - m_iAge];

	Draw(pContext, m_pPSLuma, lumaCur.pRenderTarget, lumaCur.width, lumaCur.height, { pColour });
	if (m_iFrames == 0) {
		// Nothing to compare with yet: every earlier slot holds this picture.
		for (int i = 0; i < kHistory; i++) {
			if (i != m_iHead) {
				pContext->CopyResource(m_Luma[i].pTexture, lumaCur.pTexture);
			}
		}
	}
	m_iFrames++;

	Draw(pContext, m_pPSDiff, m_Diff.pRenderTarget, m_Diff.width, m_Diff.height,
		{ lumaCur.pShaderResource, lumaPrev.pShaderResource, luma4.pShaderResource, luma8.pShaderResource });

	// The previous picture's statistics, then this picture's copies for the next.
	ReadStats(pContext);
	pContext->GenerateMips(m_Diff.pShaderResource);
	pContext->GenerateMips(lumaCur.pShaderResource);
	pContext->CopySubresourceRegion(m_pStageDiff, 0, 0, 0, 0, m_Diff.pTexture, kStatsMip, nullptr);
	pContext->CopySubresourceRegion(m_pStageMean, 0, 0, 0, 0, lumaCur.pTexture, lumaCur.levels - 1, nullptr);
	m_bHaveStats = true;

	D3D11_MAPPED_SUBRESOURCE mr = {};
	if (SUCCEEDED(pContext->Map(m_pConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mr))) {
		const float constants[8] = {
			m_fNoise > 0 ? m_fNoise : kMaxFloor, kThreshold, kSoftness, kDecay,
			m_fProtect, std::clamp(strength, 0.0f, 1.0f), 0, 0
		};
		memcpy(mr.pData, constants, sizeof(constants));
		pContext->Unmap(m_pConstants, 0);
	}

	Draw(pContext, m_pPSAge, ageCur.pRenderTarget, ageCur.width, ageCur.height,
		{ m_Diff.pShaderResource, agePrev.pShaderResource });
	if (bWriteMask) {
		Draw(pContext, m_pPSMask, m_pMaskTarget, m_width, m_height, { ageCur.pShaderResource });
	}

	m_iHead = (m_iHead + 1) % kHistory;
	m_iAge  = 1 - m_iAge;
}

void CDlssMotionMask::ReadStats(ID3D11DeviceContext* pContext)
{
	if (!m_bHaveStats) {
		m_fProtect = 1;
		return;
	}

	// Never wait for the GPU. The copies were queued a picture ago and the DLSS
	// pass synchronises with the GPU on every picture, so they are almost always
	// ready; when they are not, what was learned so far stands.
	D3D11_MAPPED_SUBRESOURCE mr = {};
	if (FAILED(pContext->Map(m_pStageDiff, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mr))) {
		return;
	}
	D3D11_TEXTURE2D_DESC desc = {};
	m_pStageDiff->GetDesc(&desc);
	// Large pictures give far more samples than a percentile needs: every other
	// one in each direction keeps the distribution for a quarter of the work.
	const UINT step = (desc.Width > 160) ? 2 : 1;
	m_Row.resize(2 * (size_t)desc.Width);
	m_Values.clear();
	for (UINT y = 0; y < desc.Height; y += step) {
		DirectX::PackedVector::XMConvertHalfToFloatStream(m_Row.data(), sizeof(float),
			(const DirectX::PackedVector::HALF*)((const BYTE*)mr.pData + (size_t)mr.RowPitch * y),
			sizeof(DirectX::PackedVector::HALF), m_Row.size());
		for (UINT x = 0; x < desc.Width; x += step) {
			m_Values.push_back(m_Row[2 * (size_t)x + 1]);   // green: consecutive pictures only
		}
	}
	pContext->Unmap(m_pStageDiff, 0);

	float mean = m_fPrevMean;
	if (SUCCEEDED(pContext->Map(m_pStageMean, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mr))) {
		mean = DirectX::PackedVector::XMConvertHalfToFloat(*(const DirectX::PackedVector::HALF*)mr.pData);
		pContext->Unmap(m_pStageMean, 0);
	}

	// A change of the whole picture -- a fade, a cut -- protects everything, and
	// can bring a different noise level, which the floor may then follow quickly.
	const float change = (m_fPrevMean >= 0) ? std::abs(mean - m_fPrevMean) : 1.0f;
	m_fProtect = std::clamp((change - kFadeThreshold) / kFadeThreshold, 0.0f, 1.0f);
	m_fPrevMean = mean;
	if (m_fProtect > 0.5f) {
		m_iRelearn = kRelearnFrames;
	}

	if (m_iFrames <= 2) {
		// The first difference compares the picture with copies of itself.
		m_fProtect = 1;
	} else if (!m_Values.empty()) {
		// A repeated picture -- a stream repeating frames, or the renderer feeding
		// the last one again -- differs by nothing even where things move. Taken
		// as the noise level it would flag the grain of every following picture
		// as motion, so such a picture teaches nothing.
		const size_t k95 = m_Values.size() * 19 / 20;
		std::nth_element(m_Values.begin(), m_Values.begin() + k95, m_Values.end());
		if (m_Values[k95] >= kMinFloor) {
			// A low percentile is the typical difference where nothing moves, as
			// long as a fifth of the picture is still.
			const size_t k = m_Values.size() / 5;
			std::nth_element(m_Values.begin(), m_Values.begin() + k, m_Values.end());
			const float current = std::clamp(m_Values[k], kMinFloor, kMaxFloor);
			// Down at once, up slowly: sustained motion must not teach the
			// detector that motion is noise.
			const float rise = (m_iRelearn > 0) ? kRelearnRise : kFloorRise;
			m_fNoise = (m_fNoise <= 0 || current < m_fNoise) ? current : m_fNoise + (current - m_fNoise) * rise;
		}
	}
	if (m_iRelearn > 0) {
		m_iRelearn--;
	}
}
