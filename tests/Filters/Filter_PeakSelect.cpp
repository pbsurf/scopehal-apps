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
	@brief Unit test for the Peak Select filter
 */
#ifdef _CATCH2_V3
#include <catch2/catch_all.hpp>
#else
#include <catch2/catch.hpp>
#endif

#include "../../lib/scopehal/scopehal.h"
#include "../../lib/scopehal/PeakDetectionFilter.h"
#include "../../lib/scopehal/FilterGraphExecutor.h"
#include "../../lib/scopeprotocols/scopeprotocols.h"
#include "Filters.h"

using namespace std;

TEST_CASE("Filter_PeakSelect")
{
	auto fft = dynamic_cast<FFTFilter*>(Filter::CreateFilter("FFT", "#ffffff"));
	REQUIRE(fft != nullptr);
	FilterReferencer fftref(fft);

	auto sel = dynamic_cast<PeakSelectFilter*>(Filter::CreateFilter("Peak Select", "#ffffff"));
	REQUIRE(sel != nullptr);
	FilterReferencer selref(sel);

	FilterGraphExecutor exec;
	set<FlowGraphNode*> nodes;
	nodes.emplace(fft);
	nodes.emplace(sel);

	//Same signal as Filter_FFT_Peaks: tones at 10 MHz and 30 MHz, plus a smaller one at 10.2 MHz
	//which is inside the default 500 kHz peak window so is never reported
	const size_t depth = 65536;
	UniformAnalogWaveform ua;
	ua.m_timescale = 1000000;	//1 Gsps
	ua.m_triggerPhase = 0;
	ua.Resize(depth);
	ua.PrepareForCpuAccess();
	for(size_t i=0; i<depth; i++)
	{
		double t = i * 1e-9;
		ua.m_samples[i] =
			1.0 * sin(2 * M_PI * 10e6 * t) +
			0.5 * sin(2 * M_PI * 10.2e6 * t) +
			0.8 * sin(2 * M_PI * 30e6 * t);
	}
	ua.MarkModifiedFromCpu();

	g_scope->GetOscilloscopeChannel(0)->SetData(&ua, 0);
	fft->SetInput("din", g_scope->GetOscilloscopeChannel(0));

	//The FFT doesn't display any peaks, but must still search because Peak Select reads them
	REQUIRE(fft->GetParameter("Number of Peaks").GetIntVal() == 0);
	sel->SetInput("din", StreamDescriptor(fft, 0));

	//X axis is in uHz. One FFT bin, from a first run
	exec.RunBlocking(nodes);
	auto out = dynamic_cast<UniformAnalogWaveform*>(fft->GetData(0));
	REQUIRE(out != nullptr);
	const int64_t bin = out->m_timescale;
	auto& mode = sel->GetParameter("Mode");

	SECTION("Rank")
	{
		mode.ParseString("Rank");
		sel->GetParameter("Rank").SetIntVal(2);
		exec.RunBlocking(nodes);

		REQUIRE(fft->GetDisplayedPeakCount() == 0);
		REQUIRE(fft->GetPeaks().size() > 10);

		//Second highest is the 30 MHz tone, reported to full double precision
		REQUIRE(fabs(sel->GetScalarValue(0) - 30e12) <= bin);
		REQUIRE(isfinite(sel->GetScalarValue(1)));
		REQUIRE(sel->GetScalarValue(2) > 0);
		REQUIRE(sel->GetYAxisUnits(0).GetType() == Unit::UNIT_MICROHZ);
		REQUIRE(sel->GetYAxisUnits(2).GetType() == Unit::UNIT_MICROHZ);

		//Out of range rank gives NaN
		sel->GetParameter("Rank").SetIntVal(5000);
		exec.RunBlocking(nodes);
		REQUIRE(isnan(sel->GetScalarValue(0)));
		REQUIRE(isnan(sel->GetScalarValue(1)));
		REQUIRE(isnan(sel->GetScalarValue(2)));
	}

	SECTION("Highest in range")
	{
		mode.ParseString("Highest in range");
		sel->GetParameter("Target").SetIntVal((int64_t)30e12);
		sel->GetParameter("Range").SetIntVal((int64_t)1e12);
		exec.RunBlocking(nodes);
		REQUIRE(fabs(sel->GetScalarValue(0) - 30e12) <= bin);

		//Off-center target: the 10 MHz tone is the highest within 10.4 MHz +/- 500 kHz, even though
		//smaller peaks may be closer to 10.4 MHz
		sel->GetParameter("Target").SetIntVal((int64_t)10.4e12);
		sel->GetParameter("Range").SetIntVal((int64_t)500e9);
		exec.RunBlocking(nodes);
		REQUIRE(fabs(sel->GetScalarValue(0) - 10e12) <= bin);
	}

	SECTION("Nearest to target")
	{
		//10 MHz is 100 kHz away. Any other peak is at least 250 kHz from it, so further from 10.1 MHz.
		mode.ParseString("Nearest to target");
		sel->GetParameter("Target").SetIntVal((int64_t)10.1e12);
		sel->GetParameter("Range").SetIntVal(0);
		exec.RunBlocking(nodes);
		REQUIRE(fabs(sel->GetScalarValue(0) - 10e12) <= bin);

		//Range too small to reach it
		sel->GetParameter("Range").SetIntVal((int64_t)50e9);
		exec.RunBlocking(nodes);
		REQUIRE(isnan(sel->GetScalarValue(0)));
	}

	SECTION("Search stops without a consumer")
	{
		//While searching, the FFT syncs with the GPU itself instead of leaving its command buffer to the executor
		auto tailCall = (uint32_t)FlowGraphNode::ExecutionCapabilities::CommandBufferTailCall;
		REQUIRE((fft->GetExecutionCapabilitiesMask() & tailCall) == 0);

		sel->SetInput("din", StreamDescriptor(nullptr, 0));
		REQUIRE((fft->GetExecutionCapabilitiesMask() & tailCall) != 0);
	}

	sel->SetInput("din", StreamDescriptor(nullptr, 0));
	g_scope->GetOscilloscopeChannel(0)->Detach(0);
}
