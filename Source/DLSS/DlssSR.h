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

#include <d3d11_4.h>
#include <deque>
#include <string>
#include <vector>
#include "DX11Helper.h"
#include "NGXTypes.h"

// CDlssSR
//
// DLSS Super Resolution (NGX feature 1) as the renderer's upscaler, on the
// renderer's own Direct3D 11 device. Unlike the DLSS 5 NR snippet, the display
// driver's NGX runtime (_nvngx.dll) runs this feature on Direct3D 11 itself: no
// second device, no shared textures, no waits. The runtime loads nvngx_dlss.dll
// from the folder it is given.
//
// A video has no jitter and no depth. The input is the picture at its own size,
// the depth a constant, and the motion vectors come from NVIDIA Optical Flow when
// the caller has them. DLSS then works as a temporal upscaler on the video's own
// sub-pixel motion.
//
// NGX binds its own shaders and views on the immediate context. Every call into
// it runs in a separate device context state, so none of that reaches the
// renderer's passes, and none of theirs reaches NGX.

class CDlssSR
{
public:
	enum class State {
		Off, CoreNotFound, DllNotFound, NoContextState, InitFailed, NotAvailable,
		TexturesFailed, FeatureCreateFailed, EvaluateFailed, Ready
	};

	CDlssSR() = default;
	~CDlssSR();
	CDlssSR(const CDlssSR&) = delete;
	CDlssSR& operator=(const CDlssSR&) = delete;

	// The NGX session on the renderer's device. pConfiguredDllPath is
	// nvngx_dlss.dll or its folder; empty looks next to the filter. False on any
	// failure, with the reason in GetStatusLine().
	bool Init(ID3D11Device* pDevice, const wchar_t* pConfiguredDllPath);
	void Shutdown();
	bool IsInitialised() const { return m_bInitialised; }

	// The feature and its textures for a picture of inWidth x inHeight shown at
	// outWidth x outHeight. preset is NGX_DLSS_PRESET_*. Nothing is rebuilt when
	// all of it already matches.
	bool CreateFeature(UINT inWidth, UINT inHeight, UINT outWidth, UINT outHeight, unsigned preset);
	void ReleaseFeature();
	bool IsFeatureReady() const { return m_hFeature != nullptr; }
	bool MatchesFeature(UINT inWidth, UINT inHeight, UINT outWidth, UINT outHeight, unsigned preset) const {
		return m_hFeature && m_inWidth == inWidth && m_inHeight == inHeight
			&& m_outWidth == outWidth && m_outHeight == outHeight && m_preset == preset;
	}
	// CreateFeature already failed for exactly these, and will not try again.
	bool IsRefused(UINT inWidth, UINT inHeight, UINT outWidth, UINT outHeight, unsigned preset) const {
		return inWidth == m_failedIn[0] && inHeight == m_failedIn[1] && outWidth == m_failedOut[0]
			&& outHeight == m_failedOut[1] && preset == m_failedPreset;
	}

	// Where the picture goes before Evaluate: RGBA16F, input size.
	Tex2D_t* GetInput() { return m_hFeature ? &m_TexIn : nullptr; }
	// What Evaluate wrote: RGBA16F, output size.
	Tex2D_t* GetOutput() { return m_hFeature ? &m_TexOut : nullptr; }

	// One frame. pMotion: RG16F at the input size, input pixels from the current
	// picture to the previous one, or null for none. frameTimeMs: the time since
	// the previous picture.
	bool Evaluate(ID3D11Texture2D* pMotion, float frameTimeMs);

	// The next Evaluate starts over: a seek, a new stream.
	void RequestReset() { m_bResetPending = true; }

	State GetState() const { return m_State; }
	std::wstring GetStatusLine() const;   // one line, for the property page
	std::wstring GetStatsLine() const;    // the same, short enough for the statistics
	std::wstring GetInfoBlock() const;    // several lines, for the statistics and the harness
	const std::wstring& GetDllPath() const { return m_DllPath; }

	// The scale the feature was tuned for, as a name, and the mode behind it.
	static int QualityModeFor(UINT inWidth, UINT inHeight, UINT outWidth, UINT outHeight);
	static const wchar_t* QualityModeName(int mode);

	static std::vector<std::wstring> CandidateDllPaths(const wchar_t* pConfigured);

	// Every log line, and every NGX log line, as it happens: the harness prints
	// them, so a crash inside NGX still shows how far it got.
	using TraceFn = void (*)(const wchar_t* line);
	static void SetTrace(TraceFn fn);

private:
	bool LoadCore();
	bool FindSnippet(const wchar_t* pConfiguredDllPath);
	bool Fail(State state, const std::wstring& why);
	void Log(const std::wstring& line);

	HMODULE m_hCore = nullptr;   // _nvngx.dll, the driver's NGX runtime

	PFN_NGX_D3D11_Init_Ext                m_pfnInitExt     = nullptr;
	PFN_NGX_D3D11_GetCapabilityParameters m_pfnGetCaps     = nullptr;
	PFN_NGX_D3D11_DestroyParameters       m_pfnDestroy     = nullptr;
	PFN_NGX_D3D11_CreateFeature           m_pfnCreate      = nullptr;
	PFN_NGX_D3D11_EvaluateFeature         m_pfnEvaluate    = nullptr;
	PFN_NGX_D3D11_ReleaseFeature          m_pfnRelease     = nullptr;
	PFN_NGX_D3D11_Shutdown                m_pfnShutdown    = nullptr;

	CComPtr<ID3D11Device>         m_pDevice;
	CComPtr<ID3D11DeviceContext1> m_pContext;
	CComPtr<ID3DDeviceContextState> m_pNgxState;   // the pipeline state NGX works in

	NVSDK_NGX_Parameter* m_pParams  = nullptr;     // owned by the runtime
	NVSDK_NGX_Handle*    m_hFeature = nullptr;

	Tex2D_t m_TexIn;           // RGBA16F, input size
	Tex2D_t m_TexOut;          // RGBA16F with a UAV, output size
	Tex2D_t m_TexDepth;        // R32F, input size, constant
	Tex2D_t m_TexNoMotion;     // RG16F, input size, zero

	UINT     m_inWidth = 0, m_inHeight = 0;
	UINT     m_outWidth = 0, m_outHeight = 0;
	unsigned m_preset = 0;
	int      m_mode = NGX_PERFQUALITY_MaxPerf;
	wchar_t  m_presetUsed = 0;     // the model DLSS picked, when its log said so

	// The last feature that could not be made, not retried on every picture:
	// a window too small for DLSS, a size it refuses.
	UINT     m_failedIn[2] = {}, m_failedOut[2] = {};
	unsigned m_failedPreset = 0;

	std::wstring m_DllPath;       // the nvngx_dlss.dll that was found
	std::wstring m_SnippetDir;    // its folder, given to the runtime
	std::wstring m_DataPath;
	uint32_t     m_SdkVersion = 0;

	bool m_bInitialised  = false;
	bool m_bResetPending = true;

	State            m_State = State::Off;
	NVSDK_NGX_Result m_LastResult = 0;
	std::wstring     m_Detail;
	std::deque<std::wstring> m_Log;
};
