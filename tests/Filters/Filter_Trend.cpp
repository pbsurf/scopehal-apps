/***********************************************************************************************************************
*                                                                                                                      *
* ngscopeclient                                                                                                        *
*                                                                                                                      *
* Copyright (c) 2012-2026 Andrew D. Zonenberg and contributors                                                         *
* All rights reserved.                                                                                                 *
*                                                                                                                      *
* Redistribution and use in source and binary forms, with or without modification, are permitted provided that the     *
* following conditions are met:                                                                                        *
*                                                                                                                      *
*    * Redistributions of source code must retain the above copyright notice, this list of conditions, and the         *
*      following disclaimer.                                                                                           *
*                                                                                                                      *
*    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the       *
*      following disclaimer in the documentation and/or other materials provided with the distribution.                *
*                                                                                                                      *
*    * Neither the name of the author nor the names of any contributors may be used to endorse or promote products     *
*      derived from this software without specific prior written permission.                                           *
*                                                                                                                      *
* THIS SOFTWARE IS PROVIDED BY THE AUTHORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED   *
* TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL *
* THE AUTHORS BE HELD LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES        *
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR       *
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE       *
* POSSIBILITY OF SUCH DAMAGE.                                                                                          *
*                                                                                                                      *
***********************************************************************************************************************/

/**
	@file
	@author ngscopeclient contributors
	@brief Unit tests for TrendFilter, and looking up values in waveforms spanning more than fits in an int64_t of fs
 */
#ifdef _CATCH2_V3
#include <catch2/catch_all.hpp>
#else
#include <catch2/catch.hpp>
#endif

#include "../../lib/scopehal/scopehal.h"
#include "../../lib/scopeprotocols/scopeprotocols.h"
#include "Filters.h"
#include "../../lib/scopehal/FilterGraphExecutor.h"

#include <thread>

using namespace std;

///@brief Timescale of trend waveforms (1 ns)
static const int64_t TREND_TIMESCALE = 1000000;

/**
	@brief Trend output uses 1 ns ticks, with the newest sample at offset zero
 */
TEST_CASE("Filter_Trend_Timescale")
{
	auto constant = Filter::CreateFilter("Constant", "#ffffff");
	auto trend = Filter::CreateFilter("Trend", "#ffffff");
	REQUIRE(constant != nullptr);
	REQUIRE(trend != nullptr);
	FilterReferencer cref(constant);
	FilterReferencer tref(trend);
	constant->GetParameter("Value").SetFloatVal(1.5);
	trend->SetInput("din", StreamDescriptor(constant, 0));

	FilterGraphExecutor exec;
	set<FlowGraphNode*> nodes;
	nodes.emplace(constant);
	nodes.emplace(trend);

	for(int i=0; i<3; i++)
	{
		if(i > 0)
			this_thread::sleep_for(chrono::milliseconds(20));
		exec.RunBlocking(nodes);
	}

	auto wfm = dynamic_cast<SparseAnalogWaveform*>(trend->GetData(0));
	REQUIRE(wfm != nullptr);
	wfm->PrepareForCpuAccess();
	REQUIRE(wfm->m_timescale == TREND_TIMESCALE);
	REQUIRE(wfm->size() == 3);
	REQUIRE(wfm->m_offsets[2] == 0);

	//Two steps of at least 20 ms (but not absurdly long) in ns
	REQUIRE(wfm->m_offsets[0] <= -40000000);
	REQUIRE(wfm->m_offsets[0] > -2000000000LL);
	REQUIRE(wfm->m_offsets[1] - wfm->m_offsets[0] == wfm->m_durations[0]);
	REQUIRE(wfm->m_samples[0] == 1.5f);
}

/**
	@brief A trend waveform from an older session (femtosecond ticks) is converted to 1 ns ticks, and the time of its
	last sample is taken from the waveform's start timestamp rather than measuring from zero
 */
TEST_CASE("Filter_Trend_LoadedFromOldSession")
{
	auto constant = Filter::CreateFilter("Constant", "#ffffff");
	auto trend = Filter::CreateFilter("Trend", "#ffffff");
	REQUIRE(constant != nullptr);
	REQUIRE(trend != nullptr);
	FilterReferencer cref(constant);
	FilterReferencer tref(trend);
	constant->GetParameter("Value").SetFloatVal(2);
	trend->SetInput("din", StreamDescriptor(constant, 0));

	//Two samples one second apart, the last one half a second ago, in fs ticks
	double last = GetTime() - 0.5;
	auto old = new SparseAnalogWaveform;
	old->m_timescale = 1;
	old->m_triggerPhase = 0;
	old->m_startTimestamp = floor(last);
	old->m_startFemtoseconds = (last - floor(last)) * FS_PER_SECOND;
	old->Resize(2);
	old->m_offsets[0] = -FS_PER_SECOND;
	old->m_offsets[1] = 0;
	old->m_durations[0] = FS_PER_SECOND;
	old->m_durations[1] = 0;
	old->m_samples[0] = 0;
	old->m_samples[1] = 1;
	old->MarkModifiedFromCpu();
	trend->SetData(old, 0);

	FilterGraphExecutor exec;
	set<FlowGraphNode*> nodes;
	nodes.emplace(constant);
	nodes.emplace(trend);
	exec.RunBlocking(nodes);

	auto wfm = dynamic_cast<SparseAnalogWaveform*>(trend->GetData(0));
	REQUIRE(wfm != nullptr);
	wfm->PrepareForCpuAccess();
	REQUIRE(wfm->m_timescale == TREND_TIMESCALE);
	REQUIRE(wfm->size() == 3);

	//Old samples are 1.5 s and 0.5 s back (allowing for the time the test takes)
	const int64_t ns = 1000000000LL;
	REQUIRE(wfm->m_offsets[2] == 0);
	REQUIRE(wfm->m_offsets[1] <= -ns/2);
	REQUIRE(wfm->m_offsets[1] > -ns);
	REQUIRE(wfm->m_offsets[0] == wfm->m_offsets[1] - ns);
	REQUIRE(wfm->m_durations[0] == ns);
	REQUIRE(wfm->m_samples[2] == 2);
}

/**
	@brief Cursor lookups work on a waveform hours long, where times in fs don't fit in an int64_t
 */
TEST_CASE("Waveform_ValueAtTime_Long")
{
	//Six hours of samples ten seconds apart, newest at zero, in 1 ns ticks
	const int64_t step = 10LL * 1000000000LL;
	const size_t len = 6 * 360 + 1;
	SparseAnalogWaveform wfm;
	wfm.m_timescale = TREND_TIMESCALE;
	wfm.m_triggerPhase = 0;
	wfm.Resize(len);
	for(size_t i=0; i<len; i++)
	{
		wfm.m_offsets[i] = (static_cast<int64_t>(i) - static_cast<int64_t>(len - 1)) * step;
		wfm.m_durations[i] = step;
		wfm.m_samples[i] = i;
	}
	wfm.MarkModifiedFromCpu();

	//Three hours back is sample 1080, at -1.08e19 fs
	double t = -3.0 * 3600 * FS_PER_SECOND;
	REQUIRE(t < -9.3e18);

	bool oob = true;
	REQUIRE(GetIndexNearestAtOrBeforeTimestamp(&wfm, t, oob) == 1080);
	REQUIRE(!oob);

	//Interpolated a quarter of the way to the next sample, or held
	auto v = GetValueAtTime(&wfm, t + 2.5 * FS_PER_SECOND, false);
	REQUIRE(v.has_value());
	REQUIRE(fabs(v.value() - 1080.25) < 1e-3);
	auto held = GetValueAtTime(&wfm, t + 2.5 * FS_PER_SECOND, true);
	REQUIRE(held.has_value());
	REQUIRE(held.value() == 1080);

	//Far outside the waveform (beyond what fits in an int64_t number of ticks) is out of range, not a crash
	REQUIRE(!GetValueAtTime(&wfm, -1e30, false).has_value());
	REQUIRE(!GetValueAtTime(&wfm, 1e30, false).has_value());
}

/**
	@brief Lookups in a waveform with fs ticks are exact far from zero (they were rounded to a float)
 */
TEST_CASE("Waveform_ValueAtTime_Exact")
{
	//Samples 1 fs apart, starting at 1 ms
	const int64_t base = 1000000000000LL;
	SparseAnalogWaveform wfm;
	wfm.m_timescale = 1;
	wfm.m_triggerPhase = 0;
	wfm.Resize(100);
	for(size_t i=0; i<100; i++)
	{
		wfm.m_offsets[i] = base + i;
		wfm.m_durations[i] = 1;
		wfm.m_samples[i] = i;
	}
	wfm.MarkModifiedFromCpu();

	auto v = GetValueAtTime(&wfm, base + 37, true);
	REQUIRE(v.has_value());
	REQUIRE(v.value() == 37);
}
