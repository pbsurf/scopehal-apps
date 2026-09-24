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
	@brief Unit test for peak detection on the FFT filter
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

TEST_CASE("Filter_FFT_Peaks")
{
	auto filter = dynamic_cast<FFTFilter*>(Filter::CreateFilter("FFT", "#ffffff"));
	REQUIRE(filter != nullptr);
	FilterReferencer ref(filter);

	FilterGraphExecutor exec;
	set<FlowGraphNode*> nodes;
	nodes.emplace(filter);

	//Tones at 10 MHz and 30 MHz, plus a smaller one 200 kHz from the first, which is inside the default
	//500 kHz peak window so must not be reported
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
	filter->SetInput("din", g_scope->GetOscilloscopeChannel(0));
	filter->GetParameter("Number of Peaks").SetIntVal(10);

	exec.RunBlocking(nodes);

	auto out = dynamic_cast<UniformAnalogWaveform*>(filter->GetData(0));
	REQUIRE(out != nullptr);
	int64_t bin = out->m_timescale;

	auto& peaks = filter->GetPeaks();
	REQUIRE(peaks.size() == 10);
	LogVerbose("FFT peaks (bin = %s):\n", Unit(Unit::UNIT_MICROHZ).PrettyPrint(bin).c_str());
	for(auto& p : peaks)
		LogVerbose("    %s: %.2f dBm\n", Unit(Unit::UNIT_MICROHZ).PrettyPrint(p.m_x).c_str(), p.m_y);

	//Two largest are the main tones (X axis is in uHz)
	REQUIRE(llabs(peaks[0].m_x - (int64_t)10e12) <= bin);
	REQUIRE(llabs(peaks[1].m_x - (int64_t)30e12) <= bin);

	//No two peaks closer than the search radius (half the window)
	for(size_t i=0; i<peaks.size(); i++)
	{
		for(size_t j=i+1; j<peaks.size(); j++)
			REQUIRE(llabs(peaks[i].m_x - peaks[j].m_x) > (int64_t)250e9);
	}

	g_scope->GetOscilloscopeChannel(0)->Detach(0);
}
