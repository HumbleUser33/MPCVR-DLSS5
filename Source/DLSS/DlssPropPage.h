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

#include "IVideoRenderer.h"

// CVRDlssPPage
//
// The NVIDIA DLSS 5 Neural Rendering settings, on a page of their own (x64 only).
// Applying sends only what was changed on this page since it was opened: the
// main page owns every other field, and the toggle key can switch DLSS while
// this page is open.

class __declspec(uuid("E3A1C5D7-6B2F-4F19-A8D4-5C0B9E7F2136"))
	CVRDlssPPage : public CBasePropertyPage, public CWindow
{
	CComQIPtr<IVideoRenderer> m_pVideoRenderer;

	Settings_t m_SetsPP;       // as edited on the page
	Settings_t m_SetsOpened;   // as the renderer held them when the page was opened or last applied

	HWND m_hHint = nullptr;

public:
	CVRDlssPPage(LPUNKNOWN lpunk, HRESULT* phr);
	~CVRDlssPPage();

private:
	void SetControls();
	void EnableControls();

	HRESULT OnConnect(IUnknown* pUnknown) override;
	HRESULT OnDisconnect() override;
	HRESULT OnActivate() override;
	void SetDirty()
	{
		m_bDirty = TRUE;
		if (m_pPageSite) {
			m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
		}
	}
	INT_PTR OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) override;
	HRESULT OnApplyChanges() override;

	void AddHint(int id, const LPCWSTR text);
};
