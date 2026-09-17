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
#include <algorithm>
#include <iterator>
#include "DlssTiming.h"

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002 // Windows 10 1803; the SDK hides it below that target
#endif

// CRollingMs

void CRollingMs::Add(double ms)
{
	m_values[m_next] = (float)ms;
	m_next = (m_next + 1) % kSize;
	if (m_count < kSize) {
		m_count++;
	}
}

double CRollingMs::Mean() const
{
	if (!m_count) {
		return -1.0;
	}
	double sum = 0;
	for (int i = 0; i < m_count; i++) {
		sum += m_values[i];
	}
	return sum / m_count;
}

double CRollingMs::Max() const
{
	if (!m_count) {
		return -1.0;
	}
	return *std::max_element(m_values, m_values + m_count);
}

// CRenderAhead

void CRenderAhead::Reset()
{
	m_latency.Reset();
	m_aheadMs = 0;
	m_ahead.store(0, std::memory_order_relaxed);
	m_late = 0;
}

void CRenderAhead::AddLatency(double latencyMs, double presentLeadMs, double frameMs)
{
	m_latency.Add(latencyMs);

	// Processing starts kRendererAllowanceMs + ahead before the picture's time and
	// the picture is presented presentLeadMs before it, so it is ready in time when
	// ahead >= latency + margin + presentLead - allowance, for the slowest recent picture.
	double needMs = m_latency.Max() + kMarginMs + presentLeadMs - kRendererAllowanceMs;
	// A picture cannot start before the previous one has left, about a frame earlier.
	const double limitMs = std::min(kMaxAheadMs, frameMs > 0 ? frameMs : kMaxAheadMs);
	needMs = std::clamp(needMs, 0.0, limitMs);

	if (needMs >= m_aheadMs) {
		m_aheadMs = needMs;
	} else {
		m_aheadMs = std::max(needMs, m_aheadMs - kFallMsPerPicture);
	}
	m_ahead.store((int)(m_aheadMs * 10000.0), std::memory_order_relaxed);
}

// CPreciseSleep

CPreciseSleep::~CPreciseSleep()
{
	if (m_hTimer) {
		CloseHandle(m_hTimer);
	}
}

void CPreciseSleep::Sleep(double ms)
{
	if (ms <= 0) {
		return;
	}
	if (!m_bTried) {
		m_bTried = true;
		m_hTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	}
	if (m_hTimer) {
		LARGE_INTEGER due;
		due.QuadPart = -std::max<LONGLONG>(1, (LONGLONG)(ms * 10000.0)); // relative, in 100 ns
		if (SetWaitableTimer(m_hTimer, &due, 0, nullptr, nullptr, FALSE)) {
			WaitForSingleObject(m_hTimer, INFINITE);
			return;
		}
	}
	::Sleep((DWORD)std::max(1.0, ms + 0.5));
}

// CGpuStageTimes

void CGpuStageTimes::Release()
{
	for (auto& set : m_sets) {
		set = QuerySet{};
	}
	m_iRecording = -1;
	m_bFailed = false;
	for (auto& times : m_times) {
		times.Reset();
	}
}

void CGpuStageTimes::BeginFrame(ID3D11Device* pDevice, ID3D11DeviceContext* pContext)
{
	if (m_iRecording >= 0) {
		EndFrame(pContext);
	}
	if (m_bFailed) {
		return;
	}
	m_pictures++;

	for (int i = 0; i < kSets; i++) {
		QuerySet& set = m_sets[i];
		if (set.bPending) {
			continue;
		}
		if (!set.pDisjoint) {
			D3D11_QUERY_DESC desc = { D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
			bool bOk = SUCCEEDED(pDevice->CreateQuery(&desc, &set.pDisjoint));
			desc.Query = D3D11_QUERY_TIMESTAMP;
			for (int s = 0; s < StageCount && bOk; s++) {
				bOk = SUCCEEDED(pDevice->CreateQuery(&desc, &set.pBegin[s]))
					&& SUCCEEDED(pDevice->CreateQuery(&desc, &set.pEnd[s]));
			}
			if (!bOk) {
				set = QuerySet{};
				m_bFailed = true;
				return;
			}
		}
		std::fill(std::begin(set.bOpen), std::end(set.bOpen), false);
		std::fill(std::begin(set.bUsed), std::end(set.bUsed), false);
		set.picture = m_pictures;
		pContext->Begin(set.pDisjoint);
		m_iRecording = i;
		return;
	}
	// Every set is still with the GPU: this picture goes untimed.
}

void CGpuStageTimes::Begin(ID3D11DeviceContext* pContext, Stage stage)
{
	if (m_iRecording >= 0) {
		QuerySet& set = m_sets[m_iRecording];
		pContext->End(set.pBegin[stage]);
		set.bOpen[stage] = true;
	}
}

void CGpuStageTimes::End(ID3D11DeviceContext* pContext, Stage stage)
{
	if (m_iRecording >= 0 && m_sets[m_iRecording].bOpen[stage]) {
		QuerySet& set = m_sets[m_iRecording];
		pContext->End(set.pEnd[stage]);
		set.bOpen[stage] = false;
		set.bUsed[stage] = true;
	}
}

void CGpuStageTimes::EndFrame(ID3D11DeviceContext* pContext)
{
	if (m_iRecording < 0) {
		return;
	}
	QuerySet& set = m_sets[m_iRecording];
	pContext->End(set.pDisjoint);
	set.bPending = true;
	m_iRecording = -1;
}

void CGpuStageTimes::Collect(ID3D11DeviceContext* pContext)
{
	for (;;) {
		// Oldest first: a later set is never ready before an earlier one.
		QuerySet* pSet = nullptr;
		for (auto& set : m_sets) {
			if (set.bPending && (!pSet || set.picture < pSet->picture)) {
				pSet = &set;
			}
		}
		if (!pSet) {
			return;
		}

		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint = {};
		const HRESULT hr = pContext->GetData(pSet->pDisjoint, &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH);
		if (hr == S_FALSE) {
			return;
		}
		if (hr == S_OK && !disjoint.Disjoint && disjoint.Frequency) {
			for (int s = 0; s < StageCount; s++) {
				UINT64 t0 = 0, t1 = 0;
				if (pSet->bUsed[s]
						&& S_OK == pContext->GetData(pSet->pBegin[s], &t0, sizeof(t0), D3D11_ASYNC_GETDATA_DONOTFLUSH)
						&& S_OK == pContext->GetData(pSet->pEnd[s], &t1, sizeof(t1), D3D11_ASYNC_GETDATA_DONOTFLUSH)
						&& t1 >= t0) {
					m_times[s].Add((double)(t1 - t0) * 1000.0 / (double)disjoint.Frequency);
					m_lastSeen[s] = pSet->picture;
				}
			}
		}
		pSet->bPending = false;
	}
}

double CGpuStageTimes::MeanMs(Stage stage) const
{
	if (!m_times[stage].Count() || m_pictures - m_lastSeen[stage] > kStalePictures) {
		return -1.0;
	}
	return m_times[stage].Mean();
}
