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

#include <d3d11.h>
#include <atlbase.h>
#include <atomic>

// Mean and maximum of the last few values, in ms.
class CRollingMs
{
public:
	void Reset() { m_count = 0; m_next = 0; }
	void Add(double ms);
	int Count() const { return m_count; }
	double Mean() const; // negative when empty
	double Max() const;  // negative when empty

private:
	static constexpr int kSize = 32; // about 1.3 s of film

	float m_values[kSize] = {};
	int m_count = 0;
	int m_next = 0;
};

// CRenderAhead
//
// The DirectShow renderer wakes up 8 ms before a picture's time and only then
// processes it, so whatever the DLSS passes take beyond those 8 ms reaches the
// screen late: 11 to 19 ms of Sync offset with DLSS 5 NR and SR on an RTX 4060,
// plus DLSS SR's own GPU time, which runs after Present has returned. The delay
// also varies from picture to picture, enough to move some of them to the next
// refresh of a 60 Hz screen.
//
// Render ahead measures each picture from the start of its processing to the GPU
// being done with it, starts the next ones that much earlier, and holds each
// picture until its time while the GPU finishes it: presentation stays on the
// reference clock, where it was before DLSS. A picture the GPU has not finished by
// then counts as late and moves the start earlier. The measure rises at once and
// falls slowly, so one slow picture moves the start forward straight away and a
// quiet stretch does not pull it back too eagerly.

class CRenderAhead
{
public:
	// CBaseVideoRenderer2 already schedules every picture 8 ms early.
	static constexpr double kRendererAllowanceMs = 8.0;
	// Spare time on top of the slowest recent picture.
	static constexpr double kMarginMs = 3.0;
	// How fast the measure falls back when pictures get quicker.
	static constexpr double kFallMsPerPicture = 0.25;
	// Never earlier than this: the renderer keeps its locks while a picture waits.
	static constexpr double kMaxAheadMs = 60.0;

	void Reset();

	// One picture: latencyMs from the start of its processing to the GPU being done
	// with it, or to its present time when the GPU was not done by then.
	// presentLeadMs is how long before its time the renderer presents a picture
	// (half a refresh), frameMs the duration of a frame.
	void AddLatency(double latencyMs, double presentLeadMs, double frameMs);
	// The GPU was not done with a picture at its present time.
	void CountLate() { m_late++; }

	// How much earlier than usual processing starts, in 100 ns units.
	int GetAhead() const { return m_ahead.load(std::memory_order_relaxed); }

	// For the statistics.
	double MeanLatencyMs() const { return m_latency.Mean(); }
	double MaxLatencyMs() const { return m_latency.Max(); }
	int LateCount() const { return m_late; }

private:
	CRollingMs m_latency;
	double m_aheadMs = 0;
	std::atomic<int> m_ahead = 0;
	int m_late = 0;
};

// Sleeps to about a millisecond whatever the system timer resolution, through a
// high-resolution waitable timer (Windows 10 1803 and later), Sleep() otherwise.
class CPreciseSleep
{
public:
	CPreciseSleep() = default;
	~CPreciseSleep();
	CPreciseSleep(const CPreciseSleep&) = delete;
	CPreciseSleep& operator=(const CPreciseSleep&) = delete;

	void Sleep(double ms);

private:
	HANDLE m_hTimer = nullptr;
	bool m_bTried = false;
};

// CGpuStageTimes
//
// GPU time of the DLSS stages, for the statistics: timestamp queries read back a
// few pictures later, never waited for. A picture for which every query set is
// still with the GPU goes untimed. DLSS 5 NR runs on a Direct3D 12 device of its
// own and is timed on the CPU instead, where the renderer waits for it.

class CGpuStageTimes
{
public:
	enum Stage { NRMotion, NRStabilize, SRMotion, SR, StageCount };

	void Release();

	void BeginFrame(ID3D11Device* pDevice, ID3D11DeviceContext* pContext);
	void Begin(ID3D11DeviceContext* pContext, Stage stage);
	void End(ID3D11DeviceContext* pContext, Stage stage);
	void EndFrame(ID3D11DeviceContext* pContext);

	// Reads back what the GPU has finished.
	void Collect(ID3D11DeviceContext* pContext);

	// Mean time of a stage over its recent pictures; negative when it has not run lately.
	double MeanMs(Stage stage) const;

private:
	static constexpr int kSets = 8;
	static constexpr uint64_t kStalePictures = 48;

	struct QuerySet {
		CComPtr<ID3D11Query> pDisjoint;
		CComPtr<ID3D11Query> pBegin[StageCount];
		CComPtr<ID3D11Query> pEnd[StageCount];
		bool bOpen[StageCount] = {};
		bool bUsed[StageCount] = {};
		bool bPending = false;
		uint64_t picture = 0;
	};

	QuerySet m_sets[kSets];
	int m_iRecording = -1;
	uint64_t m_pictures = 0;
	bool m_bFailed = false;

	CRollingMs m_times[StageCount];
	uint64_t m_lastSeen[StageCount] = {};
};
