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
#include <commdlg.h>
#include "resource.h"
#include "Helper.h"
#include "../../Include/FilterInterfaces.h"
#include "DlssPropPage.h"

// Combo box helpers as PropPage.cpp has them. Its own are inline there, so this
// page keeps private copies rather than sharing them across translation units.

static void Combo_AddStringData(HWND hWnd, int id, LPCWSTR str, LONG_PTR data)
{
	const LRESULT index = SendDlgItemMessageW(hWnd, id, CB_ADDSTRING, 0, (LPARAM)str);
	if (index != CB_ERR) {
		SendDlgItemMessageW(hWnd, id, CB_SETITEMDATA, index, data);
	}
}

static LONG_PTR Combo_GetCurItemData(HWND hWnd, int id)
{
	LRESULT value = SendDlgItemMessageW(hWnd, id, CB_GETCURSEL, 0, 0);
	if (value != CB_ERR) {
		value = SendDlgItemMessageW(hWnd, id, CB_GETITEMDATA, value, 0);
	}
	return value;
}

static void Combo_SelectByItemData(HWND hWnd, int id, LONG_PTR data)
{
	const LRESULT count = SendDlgItemMessageW(hWnd, id, CB_GETCOUNT, 0, 0);
	for (LRESULT i = 0; count != CB_ERR && i < count; i++) {
		if (SendDlgItemMessageW(hWnd, id, CB_GETITEMDATA, i, 0) == data) {
			SendDlgItemMessageW(hWnd, id, CB_SETCURSEL, i, 0);
			break;
		}
	}
}

// The network strengths are stored x100 and shown as 1.00.
static std::wstring StrengthText(int value)
{
	return std::format(L"{:.2f}", (float)value / DLSSNR_STR_SCALE);
}

// The stabilizer is a plain 0..100, where 0 turns it off.
static std::wstring StabilizerText(int value)
{
	return value ? std::to_wstring(value) : std::wstring(L"off");
}

static HWND CreateHintWindow(HWND parent, int timePop)
{
	HWND hhint = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr,
		WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, parent, nullptr, nullptr, nullptr);

	::SetWindowPos(hhint, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	SendMessageW(hhint, TTM_SETDELAYTIME, TTDT_AUTOPOP, MAKELONG(timePop, 0));
	SendMessageW(hhint, TTM_SETDELAYTIME, TTDT_INITIAL, MAKELONG(70, 0));
	SendMessageW(hhint, TTM_SETDELAYTIME, TTDT_RESHOW, MAKELONG(7, 0));
	SendMessageW(hhint, TTM_SETMAXTIPWIDTH, 0, 470);
	return hhint;
}

// CVRDlssPPage

CVRDlssPPage::CVRDlssPPage(LPUNKNOWN lpunk, HRESULT* phr) :
	CBasePropertyPage(L"DlssProp", lpunk, IDD_DLSSPROPPAGE, IDS_DLSSPROPPAGE_TITLE)
{
	DLog(L"CVRDlssPPage()");
}

CVRDlssPPage::~CVRDlssPPage()
{
	DLog(L"~CVRDlssPPage()");
}

void CVRDlssPPage::SetControls()
{
	CheckDlgButton(IDC_CHECK20, m_SetsPP.bDlssNR              ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK21, m_SetsPP.bDlssNRAutoMask      ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK22, m_SetsPP.bDlssNRNoHistory     ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK23, m_SetsPP.bDlssNRAfterUpscale  ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK24, m_SetsPP.bDlssNRMotionVectors ? BST_CHECKED : BST_UNCHECKED);
	Combo_SelectByItemData(m_hWnd, IDC_COMBO11, m_SetsPP.iDlssNRStyle);
	Combo_SelectByItemData(m_hWnd, IDC_COMBO12, m_SetsPP.iDlssNRPreset);
	Combo_SelectByItemData(m_hWnd, IDC_COMBO13, m_SetsPP.iDlssNRToggleKey);
	Combo_SelectByItemData(m_hWnd, IDC_COMBO14, m_SetsPP.iDlssNRMotion);

	const struct { int idSlider; int idEdit; int value; bool bStrength; } sliders[] = {
		{ IDC_SLIDER3, IDC_EDIT3, m_SetsPP.iDlssNRIntensity,      true  },
		{ IDC_SLIDER4, IDC_EDIT4, m_SetsPP.iDlssNRLocalTone,      true  },
		{ IDC_SLIDER5, IDC_EDIT5, m_SetsPP.iDlssNRLocalStructure, true  },
		{ IDC_SLIDER6, IDC_EDIT6, m_SetsPP.iDlssNRSkinStructure,  true  },
		{ IDC_SLIDER7, IDC_EDIT8, m_SetsPP.iDlssNRStabilizer,     false },
	};
	for (const auto& s : sliders) {
		SendDlgItemMessageW(s.idSlider, TBM_SETPOS, TRUE, (LPARAM)s.value);
		SetDlgItemTextW(s.idEdit, (s.bStrength ? StrengthText(s.value) : StabilizerText(s.value)).c_str());
	}

	SetDlgItemTextW(IDC_EDIT7, m_SetsPP.szDlssNRDllPath);

	CheckDlgButton(IDC_CHECK25, m_SetsPP.bDlssSR ? BST_CHECKED : BST_UNCHECKED);
	Combo_SelectByItemData(m_hWnd, IDC_COMBO15, m_SetsPP.iDlssSRPreset);
	SetDlgItemTextW(IDC_EDIT9, m_SetsPP.szDlssSRDllPath);

}

void CVRDlssPPage::EnableControls()
{
	// The path box stays live even when the feature is off, so a wrong path
	// can be fixed without enabling it first.
	const BOOL bD3D11 = m_SetsPP.bUseD3D11;
	const BOOL bOn    = bD3D11 && m_SetsPP.bDlssNR;
	for (const int id : { IDC_CHECK20, IDC_STATIC27, IDC_EDIT7, IDC_BUTTON2, IDC_STATIC29, IDC_COMBO13 }) {
		GetDlgItem(id).EnableWindow(bD3D11);
	}
	for (const int id : { IDC_CHECK21, IDC_CHECK22, IDC_CHECK23, IDC_COMBO11, IDC_COMBO12,
			IDC_SLIDER3, IDC_SLIDER4, IDC_SLIDER5, IDC_SLIDER6,
			IDC_EDIT3, IDC_EDIT4, IDC_EDIT5, IDC_EDIT6,
			IDC_STATIC21, IDC_STATIC22, IDC_STATIC23, IDC_STATIC24, IDC_STATIC25, IDC_STATIC26 }) {
		GetDlgItem(id).EnableWindow(bOn);
	}
	// The stabilizer works after the network, whatever its own history does.
	for (const int id : { IDC_STATIC32, IDC_SLIDER7, IDC_EDIT8, IDC_STATIC33, IDC_STATIC34, IDC_COMBO14 }) {
		GetDlgItem(id).EnableWindow(bOn);
	}
	// Vectors exist only with Optical Flow, and only while the stabilizer runs.
	GetDlgItem(IDC_CHECK24).EnableWindow(bOn && m_SetsPP.iDlssNRStabilizer > 0
		&& m_SetsPP.iDlssNRMotion == DLSSNR_MOTION_OPTICALFLOW);

	// DLSS Super Resolution does not depend on DLSS 5 NR: another DLL, another session.
	for (const int id : { IDC_CHECK25, IDC_STATIC37, IDC_EDIT9, IDC_BUTTON4 }) {
		GetDlgItem(id).EnableWindow(bD3D11);
	}
	for (const int id : { IDC_STATIC36, IDC_COMBO15 }) {
		GetDlgItem(id).EnableWindow(bD3D11 && m_SetsPP.bDlssSR);
	}
}

HRESULT CVRDlssPPage::OnConnect(IUnknown* pUnk)
{
	if (pUnk == nullptr) return E_POINTER;

	m_pVideoRenderer = pUnk;
	if (!m_pVideoRenderer) {
		return E_NOINTERFACE;
	}

	return S_OK;
}

HRESULT CVRDlssPPage::OnDisconnect()
{
	if (m_pVideoRenderer == nullptr) {
		return E_UNEXPECTED;
	}

	m_pVideoRenderer.Release();

	return S_OK;
}

HRESULT CVRDlssPPage::OnActivate()
{
	// set m_hWnd for CWindow
	m_hWnd = m_hwnd;

	m_pVideoRenderer->GetSettings(m_SetsPP);
	m_SetsOpened = m_SetsPP;

	Combo_AddStringData(m_hWnd, IDC_COMBO11, L"Default",   DLSSNR_STYLE_Default);
	Combo_AddStringData(m_hWnd, IDC_COMBO11, L"Natural",   DLSSNR_STYLE_Natural);
	Combo_AddStringData(m_hWnd, IDC_COMBO11, L"Cinematic", DLSSNR_STYLE_Cinematic);

	for (int i = 0; i < DLSSNR_PRESET_COUNT; i++) {
		Combo_AddStringData(m_hWnd, IDC_COMBO12, std::format(L"Preset {}", i).c_str(), i);
	}

	Combo_AddStringData(m_hWnd, IDC_COMBO14, L"NVIDIA Optical Flow", DLSSNR_MOTION_OPTICALFLOW);
	Combo_AddStringData(m_hWnd, IDC_COMBO14, L"Shader detector (still areas)", DLSSNR_MOTION_DETECTOR);

	Combo_AddStringData(m_hWnd, IDC_COMBO15, L"Automatic", DLSSSR_PRESET_DEF);
	for (int preset = DLSSSR_PRESET_J; preset <= DLSSSR_PRESET_M; preset++) {
		Combo_AddStringData(m_hWnd, IDC_COMBO15, std::format(L"Preset {}", (wchar_t)(L'J' + preset - DLSSSR_PRESET_J)).c_str(), preset);
	}

	// Keys that players rarely bind to anything destructive. The hook swallows
	// whichever one is chosen, so it must not be something the player needs.
	static const struct { const wchar_t* name; int vk; } dlssKeys[] = {
		{ L"None", 0 }, { L"Home", VK_HOME }, { L"End", VK_END },
		{ L"Insert", VK_INSERT }, { L"Delete", VK_DELETE },
		{ L"Page Up", VK_PRIOR }, { L"Page Down", VK_NEXT },
		{ L"Pause", VK_PAUSE }, { L"Scroll Lock", VK_SCROLL },
		{ L"F9", VK_F9 }, { L"F10", VK_F10 }, { L"F11", VK_F11 }, { L"F12", VK_F12 },
	};
	for (const auto& k : dlssKeys) {
		Combo_AddStringData(m_hWnd, IDC_COMBO13, k.name, k.vk);
	}

	for (const int id : { IDC_SLIDER3, IDC_SLIDER4, IDC_SLIDER5, IDC_SLIDER6 }) {
		const int minimum = (id == IDC_SLIDER6) ? DLSSNR_SKIN_MIN : DLSSNR_STR_MIN;
		SendDlgItemMessageW(id, TBM_SETRANGE, 0, MAKELONG(minimum, DLSSNR_STR_MAX));
		SendDlgItemMessageW(id, TBM_SETTIC, 0, DLSSNR_STR_DEF);
		SendDlgItemMessageW(id, TBM_SETLINESIZE, 0, 1);
		SendDlgItemMessageW(id, TBM_SETPAGESIZE, 0, 10);
	}
	SendDlgItemMessageW(IDC_SLIDER7, TBM_SETRANGE, 0, MAKELONG(DLSSNR_STAB_MIN, DLSSNR_STAB_MAX));
	SendDlgItemMessageW(IDC_SLIDER7, TBM_SETLINESIZE, 0, 1);
	SendDlgItemMessageW(IDC_SLIDER7, TBM_SETPAGESIZE, 0, 10);

	{
		// Show whether each session actually came up, and why not if it did not.
		const struct { const char* field; int id; } statuses[] = {
			{ "dlssStatus",   IDC_STATIC28 },
			{ "dlssSRStatus", IDC_STATIC38 },
		};
		for (const auto& s : statuses) {
			std::wstring status;
			if (CComQIPtr<IExFilterConfig> pIExFilterConfig = m_pVideoRenderer.p) {
				LPWSTR pstr = nullptr;
				if (S_OK == pIExFilterConfig->Flt_GetString(s.field, &pstr, nullptr) && pstr) {
					status = pstr;
					CoTaskMemFree(pstr);
				}
			}
			SetDlgItemTextW(s.id, status.empty() ? L"" : (L"Status: " + status).c_str());
		}
	}

	SetControls();
	EnableControls();

	AddHint(IDC_CHECK20,
		L"Available for Direct3D 11, x64, NVIDIA only.\n"
		"Requires nvngx_dlssnr.dll. Runs the network on a private\n"
		"Direct3D 12 device; the renderer itself stays Direct3D 11.\n"
		"Forces 16-bit float internal textures and uses about\n"
		"500 MB of video memory at 1080p.");
	AddHint(IDC_CHECK23,
		L"Run the network on the scaled image instead of the source.\n"
		"Much heavier: at 4K output it works on roughly four times the\n"
		"pixels, and its working set grows with them.");
	AddHint(IDC_COMBO13,
		L"Toggles DLSS during playback without opening this page.\n"
		"The filter swallows this key, so pick one the player does\n"
		"not need. Set to None to disable the shortcut.");
	AddHint(IDC_EDIT7,
		L"Path to nvngx_dlssnr.dll.\n"
		"Leave empty to look next to the filter, then one and two\n"
		"directories up.");
	AddHint(IDC_COMBO12,
		L"This DLL build ships a single network, so every preset\n"
		"falls back to the same one. Kept for other builds.");
	AddHint(IDC_SLIDER7,
		L"Steadies the network's effect over time, after it runs: only the\n"
		"change it makes to the picture is filtered, then added to the current\n"
		"frame, so the video itself is never delayed. Removes most of the\n"
		"shimmer DLSS adds. 100 is the measured setting; 0 runs nothing.");
	AddHint(IDC_COMBO14,
		L"Where the stabilizer takes motion from.\n"
		"NVIDIA Optical Flow follows the picture, so moving areas are\n"
		"steadied too. Where it cannot run, the shader detector takes over.\n"
		"The shader detector only steadies what stands still.\n"
		"GPU time per picture on an RTX 3050 at 1080p: 2.8 ms with\n"
		"Optical Flow, 0.8 ms with the shader detector.");
	AddHint(IDC_CHECK24,
		L"Also gives the Optical Flow vectors to the network, which then uses\n"
		"them for its own history. Steadier still, but the network renders\n"
		"differently, most visibly around moving objects.");
	AddHint(IDC_CHECK22,
		L"Makes the network ignore its previous output on every frame.\n"
		"The stabilizer is not affected: it works after the network.");
	AddHint(IDC_CHECK25,
		L"Enlarges the picture with NVIDIA DLSS Super Resolution instead of\n"
		"the Upscaling method of the main page, which is then greyed.\n"
		"Works with or without DLSS 5 NR. Requires nvngx_dlss.dll (DLSS 4.5,\n"
		"310.5 or later) and an RTX GPU. Motion comes from NVIDIA Optical Flow;\n"
		"where DLSS cannot run, the Upscaling method takes over.\n"
		"Experimental: on grainy film, moving subjects can still shimmer a\n"
		"little, as estimated motion is never exact, and DLSS removes film\n"
		"grain along with compression noise.");
	AddHint(IDC_COMBO15,
		L"Automatic lets DLSS pick the model for the scale: with 310.9,\n"
		"M for x2 (1080p on a 4K screen), L for x3 (720p), K below x1.85.\n"
		"J and K are the first transformer models, L and M the second.\n"
		"GPU time per frame at 1080p to 4K on an RTX 3050: J or K 11 ms,\n"
		"M 24 ms, L 31 ms.");
	AddHint(IDC_EDIT9,
		L"Path to nvngx_dlss.dll, or its folder; the file must keep that name.\n"
		"Leave empty to look next to the filter, then one and two\n"
		"directories up.");

	// Everything here needs Direct3D 11, which is the other page's checkbox; the frame
	// says nothing when it is applied there.
	SetTimer(kRefreshTimer, 500);

	return S_OK;
}

HRESULT CVRDlssPPage::OnDeactivate()
{
	KillTimer(kRefreshTimer);

	return S_OK;
}

INT_PTR CVRDlssPPage::OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_TIMER && wParam == kRefreshTimer && m_pVideoRenderer) {
		Settings_t current;
		m_pVideoRenderer->GetSettings(current);
		if (current.bUseD3D11 != m_SetsPP.bUseD3D11) {
			// Not an edit made here, so the page's own reference moves with it and
			// applying this page never writes it back.
			m_SetsPP.bUseD3D11 = current.bUseD3D11;
			m_SetsOpened.bUseD3D11 = current.bUseD3D11;
			EnableControls();
		}
	}

	if (uMsg == WM_COMMAND) {
		const int nID = LOWORD(wParam);
		const int action = HIWORD(wParam);

		if (action == BN_CLICKED) {
			const struct { int id; bool* pValue; bool bEnables; } checks[] = {
				{ IDC_CHECK20, &m_SetsPP.bDlssNR,              true  },
				{ IDC_CHECK21, &m_SetsPP.bDlssNRAutoMask,      false },
				{ IDC_CHECK22, &m_SetsPP.bDlssNRNoHistory,     false },
				{ IDC_CHECK23, &m_SetsPP.bDlssNRAfterUpscale,  false },
				{ IDC_CHECK24, &m_SetsPP.bDlssNRMotionVectors, false },
				{ IDC_CHECK25, &m_SetsPP.bDlssSR,              true  },
			};
			for (const auto& c : checks) {
				if (nID == c.id) {
					*c.pValue = IsDlgButtonChecked(c.id) == BST_CHECKED;
					if (c.bEnables) {
						EnableControls();
					}
					SetDirty();
					return (LRESULT)1;
				}
			}

			if (nID == IDC_BUTTON2 || nID == IDC_BUTTON4) {
				const bool bSR = (nID == IDC_BUTTON4);
				wchar_t* target = bSR ? m_SetsPP.szDlssSRDllPath : m_SetsPP.szDlssNRDllPath;
				wchar_t path[MAX_PATH] = {};
				wcscpy_s(path, target);

				OPENFILENAMEW ofn = {};
				ofn.lStructSize = sizeof(ofn);
				ofn.hwndOwner   = m_hWnd;
				ofn.lpstrFilter = bSR
					? L"DLSS Super Resolution\0nvngx_dlss.dll\0DLL files (*.dll)\0*.dll\0All files (*.*)\0*.*\0\0"
					: L"NGX snippet\0nvngx_dlssnr.dll;nvngx*.dll\0DLL files (*.dll)\0*.dll\0All files (*.*)\0*.*\0\0";
				ofn.lpstrFile   = path;
				ofn.nMaxFile    = std::size(path);
				ofn.lpstrTitle  = bSR ? L"Select nvngx_dlss.dll" : L"Select nvngx_dlssnr.dll";
				ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

				if (GetOpenFileNameW(&ofn)) {
					wcscpy_s(target, MAX_PATH, path);
					SetDlgItemTextW(bSR ? IDC_EDIT9 : IDC_EDIT7, target);
					SetDirty();
				}
				return (LRESULT)1;
			}

			if (nID == IDC_BUTTON3) {
				// Back to the default tuning. Whether DLSS is on, its key and where
				// the DLL lives are not tuning, so they stay as they are.
				Settings_t defaults;
				defaults.bDlssNR = m_SetsPP.bDlssNR;
				defaults.iDlssNRToggleKey = m_SetsPP.iDlssNRToggleKey;
				wcscpy_s(defaults.szDlssNRDllPath, m_SetsPP.szDlssNRDllPath);
				defaults.bDlssSR = m_SetsPP.bDlssSR;
				wcscpy_s(defaults.szDlssSRDllPath, m_SetsPP.szDlssSRDllPath);
				CopyDlssSettings(m_SetsPP, defaults);
				SetControls();
				EnableControls();
				SetDirty();
				return (LRESULT)1;
			}
		}

		if (action == EN_CHANGE && (nID == IDC_EDIT7 || nID == IDC_EDIT9)) {
			// SetControls fills the box too; only a real edit makes the page dirty.
			wchar_t* target = (nID == IDC_EDIT9) ? m_SetsPP.szDlssSRDllPath : m_SetsPP.szDlssNRDllPath;
			wchar_t path[MAX_PATH] = {};
			GetDlgItemTextW(nID, path, (int)std::size(path));
			if (wcscmp(path, target)) {
				wcscpy_s(target, MAX_PATH, path);
				SetDirty();
			}
			return (LRESULT)1;
		}

		if (action == CBN_SELCHANGE) {
			const struct { int id; int* pValue; } combos[] = {
				{ IDC_COMBO11, &m_SetsPP.iDlssNRStyle },
				{ IDC_COMBO12, &m_SetsPP.iDlssNRPreset },
				{ IDC_COMBO13, &m_SetsPP.iDlssNRToggleKey },
				{ IDC_COMBO14, &m_SetsPP.iDlssNRMotion },
				{ IDC_COMBO15, &m_SetsPP.iDlssSRPreset },
			};
			for (const auto& c : combos) {
				if (nID == c.id) {
					*c.pValue = (int)Combo_GetCurItemData(m_hWnd, c.id);
					EnableControls();   // the vectors box follows the motion source
					SetDirty();
					return (LRESULT)1;
				}
			}
		}
	}
	else if (uMsg == WM_HSCROLL) {
		// Unlike the nits slider on the main page these are not applied on drag:
		// a settings round-trip per pixel of travel would recreate state on every
		// mouse move.
		const struct { int idSlider; int idEdit; int* pValue; bool bStrength; } sliders[] = {
			{ IDC_SLIDER3, IDC_EDIT3, &m_SetsPP.iDlssNRIntensity,      true  },
			{ IDC_SLIDER4, IDC_EDIT4, &m_SetsPP.iDlssNRLocalTone,      true  },
			{ IDC_SLIDER5, IDC_EDIT5, &m_SetsPP.iDlssNRLocalStructure, true  },
			{ IDC_SLIDER6, IDC_EDIT6, &m_SetsPP.iDlssNRSkinStructure,  true  },
			{ IDC_SLIDER7, IDC_EDIT8, &m_SetsPP.iDlssNRStabilizer,     false },
		};
		for (const auto& s : sliders) {
			if ((HWND)lParam == GetDlgItem(s.idSlider)) {
				const int value = (int)SendDlgItemMessageW(s.idSlider, TBM_GETPOS, 0, 0);
				if (value != *s.pValue) {
					*s.pValue = value;
					SetDlgItemTextW(s.idEdit, (s.bStrength ? StrengthText(value) : StabilizerText(value)).c_str());
					if (!s.bStrength) {
						EnableControls();   // the vectors box needs a running stabilizer
					}
					SetDirty();
				}
				return (LRESULT)1;
			}
		}
	}

	// Let the parent class handle the message.
	return CBasePropertyPage::OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}

HRESULT CVRDlssPPage::OnApplyChanges()
{
	// The DLL path is free text; an empty box means "locate it automatically".
	GetDlgItemTextW(IDC_EDIT7, m_SetsPP.szDlssNRDllPath, (int)std::size(m_SetsPP.szDlssNRDllPath));
	m_SetsPP.szDlssNRDllPath[std::size(m_SetsPP.szDlssNRDllPath) - 1] = L'\0';
	GetDlgItemTextW(IDC_EDIT9, m_SetsPP.szDlssSRDllPath, (int)std::size(m_SetsPP.szDlssSRDllPath));
	m_SetsPP.szDlssSRDllPath[std::size(m_SetsPP.szDlssSRDllPath) - 1] = L'\0';

	// Start from what the renderer holds now and take only what was changed
	// here, so neither the main page nor the toggle key gets overwritten.
	Settings_t current;
	m_pVideoRenderer->GetSettings(current);

#define TAKE_IF_CHANGED(field) \
	if (m_SetsPP.field != m_SetsOpened.field) { current.field = m_SetsPP.field; }
	TAKE_IF_CHANGED(bDlssNR)
	TAKE_IF_CHANGED(iDlssNRStyle)
	TAKE_IF_CHANGED(iDlssNRPreset)
	TAKE_IF_CHANGED(iDlssNRIntensity)
	TAKE_IF_CHANGED(iDlssNRLocalTone)
	TAKE_IF_CHANGED(iDlssNRLocalStructure)
	TAKE_IF_CHANGED(iDlssNRSkinStructure)
	TAKE_IF_CHANGED(bDlssNRAutoMask)
	TAKE_IF_CHANGED(bDlssNRNoHistory)
	TAKE_IF_CHANGED(bDlssNRAfterUpscale)
	TAKE_IF_CHANGED(iDlssNRStabilizer)
	TAKE_IF_CHANGED(iDlssNRMotion)
	TAKE_IF_CHANGED(bDlssNRMotionVectors)
	TAKE_IF_CHANGED(iDlssNRToggleKey)
	TAKE_IF_CHANGED(bDlssSR)
	TAKE_IF_CHANGED(iDlssSRPreset)
#undef TAKE_IF_CHANGED
	if (wcscmp(m_SetsPP.szDlssNRDllPath, m_SetsOpened.szDlssNRDllPath)) {
		wcscpy_s(current.szDlssNRDllPath, m_SetsPP.szDlssNRDllPath);
	}
	if (wcscmp(m_SetsPP.szDlssSRDllPath, m_SetsOpened.szDlssSRDllPath)) {
		wcscpy_s(current.szDlssSRDllPath, m_SetsPP.szDlssSRDllPath);
	}

	m_pVideoRenderer->SetSettings(current);
	m_pVideoRenderer->SaveSettings();

	// The page now shows what was applied, including a toggle made meanwhile.
	m_SetsPP = current;
	m_SetsOpened = current;
	SetControls();
	EnableControls();

	// And whether DLSS Super Resolution came up with it.
	if (CComQIPtr<IExFilterConfig> pIExFilterConfig = m_pVideoRenderer.p) {
		LPWSTR pstr = nullptr;
		std::wstring status;
		if (S_OK == pIExFilterConfig->Flt_GetString("dlssSRStatus", &pstr, nullptr) && pstr) {
			status = pstr;
			CoTaskMemFree(pstr);
		}
		SetDlgItemTextW(IDC_STATIC38, status.empty() ? L"" : (L"Status: " + status).c_str());
	}

	return S_OK;
}

void CVRDlssPPage::AddHint(int id, const LPCWSTR text)
{
	if (!m_hHint) {
		m_hHint = CreateHintWindow(m_Dlg, 15000);
	}
	TOOLINFOW ti = { sizeof(TOOLINFOW) };
	ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
	ti.hwnd = m_Dlg;
	ti.uId = (UINT_PTR)GetDlgItem(id).m_hWnd;
	ti.lpszText = const_cast<LPWSTR>(text);
	SendMessageW(m_hHint, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}
