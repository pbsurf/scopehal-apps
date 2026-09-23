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
	@brief Unit test for SpectrumStitchFilter
 */
#ifdef _CATCH2_V3
#include <catch2/catch_all.hpp>
#else
#include <catch2/catch.hpp>
#endif

#include "../../lib/scopehal/scopehal.h"
#include "../../lib/scopehal/ComplexChannel.h"
#include "../../lib/scopeprotocols/scopeprotocols.h"
#include "Filters.h"
#include "../../lib/scopehal/FilterGraphExecutor.h"
#include "../../lib/scopehal/SCPISDR.h"

using namespace std;

//Synthetic spectra: 100 bins of 10 Hz
static const size_t g_len = 100;
static const int64_t g_bin = 10;

/**
	@brief Makes a spectrum centered on the given frequency, with every bin set to the same value
 */
static UniformAnalogWaveform* MakeSpectrum(int64_t center, float value, int64_t timestamp)
{
	auto ret = new UniformAnalogWaveform;
	ret->m_timescale = g_bin;
	ret->m_triggerPhase = center - static_cast<int64_t>(g_len / 2) * g_bin;
	ret->m_startTimestamp = timestamp;
	ret->m_startFemtoseconds = 0;
	ret->Resize(g_len);
	ret->PrepareForCpuAccess();
	for(size_t i=0; i<g_len; i++)
		ret->m_samples[i] = value;
	ret->MarkSamplesModifiedFromCpu();
	return ret;
}

///@brief Gets the value of the output at a given frequency
static float ValueAt(UniformAnalogWaveform* wfm, int64_t freq)
{
	int64_t i = (freq - wfm->m_triggerPhase) / wfm->m_timescale;
	REQUIRE( (freq - wfm->m_triggerPhase) % wfm->m_timescale == 0);
	REQUIRE(i >= 0);
	REQUIRE(i < static_cast<int64_t>(wfm->size()));
	wfm->PrepareForCpuAccess();
	return wfm->m_samples[i];
}

///@brief Frequency of the first bin past the end of a waveform
static int64_t EndOf(UniformAnalogWaveform* wfm)
{
	return wfm->m_triggerPhase + static_cast<int64_t>(wfm->size()) * wfm->m_timescale;
}

TEST_CASE("Filter_SpectrumStitch")
{
	auto filter = dynamic_cast<SpectrumStitchFilter*>(Filter::CreateFilter("Spectrum Stitch", "#ffffff"));
	REQUIRE(filter != nullptr);
	FilterReferencer ref(filter);

	FilterGraphExecutor exec;
	set<FlowGraphNode*> nodes;
	nodes.emplace(filter);

	//Channel with frequency on the X axis
	auto src = g_scope->GetOscilloscopeChannel(2);
	filter->SetInput("din", StreamDescriptor(src, 0));

	int64_t timestamp = 1;
	auto run = [&](int64_t center, float value) -> UniformAnalogWaveform*
	{
		src->SetData(MakeSpectrum(center, value, timestamp++), 0);
		exec.RunBlocking(nodes);
		auto out = dynamic_cast<UniformAnalogWaveform*>(filter->GetData(0));
		REQUIRE(out != nullptr);
		return out;
	};

	//Default is to use 90% of each capture: 45 bins either side of the center
	const int64_t half = 450;

	SECTION("Not sweeping")
	{
		//Every capture at the same frequency replaces the last, and we get the usable part of it
		for(int i=0; i<3; i++)
		{
			auto out = run(10000, i);
			REQUIRE(out->m_timescale == g_bin);
			REQUIRE(out->m_triggerPhase == 10000 - half);
			REQUIRE(EndOf(out) == 10000 + half + g_bin);
			REQUIRE(ValueAt(out, 10000 - half) == i);
			REQUIRE(ValueAt(out, 10000 + half) == i);
		}
	}

	SECTION("Sweep")
	{
		//Three captures 80 bins apart, so they overlap by 10 bins
		run(10000, 1);
		run(10800, 2);
		auto out = run(11600, 3);
		REQUIRE(out->m_triggerPhase == 10000 - half);
		REQUIRE(EndOf(out) == 11600 + half + g_bin);

		//Where they overlap, the closest capture wins (and the first one if it's a tie)
		REQUIRE(ValueAt(out, 9550) == 1);
		REQUIRE(ValueAt(out, 10390) == 1);
		REQUIRE(ValueAt(out, 10400) == 1);
		REQUIRE(ValueAt(out, 10410) == 2);
		REQUIRE(ValueAt(out, 11190) == 2);
		REQUIRE(ValueAt(out, 11210) == 3);
		REQUIRE(ValueAt(out, 12050) == 3);

		//Refreshing without new data doesn't change anything
		auto rev = out->m_revision;
		exec.RunBlocking(nodes);
		REQUIRE(filter->GetData(0) == out);
		REQUIRE(out->m_revision == rev);

		//Going back to the start is a new sweep. The ends are trimmed to half the step past the outer captures, and
		//the new capture replaces everything from the old sweep that it covers.
		out = run(10000, 4);
		REQUIRE(out->m_triggerPhase == 10000 - 410);
		REQUIRE(EndOf(out) == 11600 + 410 + g_bin);
		REQUIRE(ValueAt(out, 9590) == 4);
		REQUIRE(ValueAt(out, 10450) == 4);
		REQUIRE(ValueAt(out, 10460) == 2);
		REQUIRE(ValueAt(out, 12010) == 3);

		//The next capture of the new sweep is closer to some of those
		out = run(10800, 5);
		REQUIRE(ValueAt(out, 10400) == 4);
		REQUIRE(ValueAt(out, 10410) == 5);
		REQUIRE(ValueAt(out, 11250) == 5);
		REQUIRE(ValueAt(out, 11260) == 3);

		run(11600, 6);

		//Moving the sweep down: the output grows, but only as far past the capture as the last sweep went
		out = run(9200, 7);
		REQUIRE(out->m_triggerPhase == 9200 - 410);
		REQUIRE(EndOf(out) == 11600 + 410 + g_bin);
		REQUIRE(ValueAt(out, 8790) == 7);

		//and once the new sweep is done, what's outside of it goes away
		run(10000, 8);
		run(10800, 9);
		REQUIRE(ValueAt(out, 11600) == 6);
		out = run(9200, 10);
		REQUIRE(out->m_triggerPhase == 9200 - 410);
		REQUIRE(EndOf(out) == 10800 + 410 + g_bin);
		REQUIRE(ValueAt(out, 8790) == 10);
		REQUIRE(ValueAt(out, 11210) == 9);

		//Clearing sweeps starts over
		filter->ClearSweeps();
		out = run(20000, 10);
		REQUIRE(out->m_triggerPhase == 20000 - half);
		REQUIRE(EndOf(out) == 20000 + half + g_bin);
	}

	SECTION("Gaps")
	{
		//Captures too far apart to overlap: the gap is filled in from below
		run(10000, 1);
		auto out = run(11000, 2);
		REQUIRE(ValueAt(out, 10450) == 1);
		REQUIRE(ValueAt(out, 10500) == 1);
		REQUIRE(ValueAt(out, 10550) == 2);
	}

	SECTION("DC notch")
	{
		//Spectrum sloping upwards, with a spike in the middle
		auto wfm = MakeSpectrum(10000, 0, timestamp++);
		for(size_t i=0; i<g_len; i++)
			wfm->m_samples[i] = i;
		wfm->m_samples[g_len/2] = 1000;
		wfm->m_samples[g_len/2 + 1] = 1000;
		src->SetData(wfm, 0);

		//30 units wide is the center bin and one either side
		filter->GetParameter("DC Notch").SetFloatVal(30);
		exec.RunBlocking(nodes);
		auto out = dynamic_cast<UniformAnalogWaveform*>(filter->GetData(0));
		REQUIRE(out != nullptr);
		REQUIRE(fabs(ValueAt(out, 9990) - 49) < 1e-4);
		REQUIRE(fabs(ValueAt(out, 10000) - 50) < 1e-4);
		REQUIRE(fabs(ValueAt(out, 10010) - 51) < 1e-4);
		REQUIRE(ValueAt(out, 10020) == 52);
	}

	SECTION("Bin size change")
	{
		run(10000, 1);
		run(10800, 2);

		//A different FFT size doesn't line up with what we have, so start over
		auto wfm = MakeSpectrum(11600, 3, timestamp++);
		wfm->m_timescale = 20;
		wfm->m_triggerPhase = 11600 - static_cast<int64_t>(g_len / 2) * 20;
		src->SetData(wfm, 0);
		exec.RunBlocking(nodes);
		auto out = dynamic_cast<UniformAnalogWaveform*>(filter->GetData(0));
		REQUIRE(out->m_timescale == 20);
		REQUIRE(out->m_triggerPhase == 11600 - 900);
		REQUIRE(EndOf(out) == 11600 + 900 + 20);
	}

	src->SetData(nullptr, 0);
}

#ifdef HAS_IIO

/**
	@brief Sweeps the simulated PlutoSDR across a span three times wider than it can capture at once
 */
TEST_CASE("Filter_SpectrumStitch_IIOSDR")
{
	auto transport = SCPITransport::CreateTransport("iio", "mock:");
	REQUIRE(transport != nullptr);
	auto sdr = SCPISDR::CreateSDR("iio", transport);
	REQUIRE(sdr != nullptr);

	auto fft = dynamic_cast<ComplexFFTFilter*>(Filter::CreateFilter("Complex FFT", "#ffffff"));
	REQUIRE(fft != nullptr);
	FilterReferencer fftref(fft);
	fft->SetWindowFunction(FFTFilter::WINDOW_BLACKMAN_HARRIS);

	auto stitch = dynamic_cast<SpectrumStitchFilter*>(Filter::CreateFilter("Spectrum Stitch", "#ffffff"));
	REQUIRE(stitch != nullptr);
	FilterReferencer stitchref(stitch);

	auto chan = sdr->GetChannel(0);
	fft->SetInput("I", StreamDescriptor(chan, 0));
	fft->SetInput("Q", StreamDescriptor(chan, 1));
	fft->SetInput("center", StreamDescriptor(chan, 2));
	stitch->SetInput("din", StreamDescriptor(fft, 0));

	FilterGraphExecutor exec;
	set<FlowGraphNode*> nodes;
	nodes.emplace(fft);
	nodes.emplace(stitch);

	//The mock has tones at 2.4005, 2.412, and 2.437 GHz
	const size_t depth = 4096;
	const int64_t center = 2420000000;
	const int64_t span = 60000000;
	sdr->SetSampleDepth(depth);
	sdr->SetSampleRate(20000000);
	sdr->SetCenterFrequency(0, center);
	sdr->SetSpan(span);
	sdr->BackgroundProcessing();

	auto sweep = [&]()
	{
		size_t captures = 0;
		sdr->StartSingleTrigger();
		while(sdr->IsTriggerArmed())
		{
			REQUIRE(sdr->AcquireData());
			REQUIRE(sdr->PopPendingWaveform());
			exec.RunBlocking(nodes);
			captures ++;
			REQUIRE(captures <= 4);
		}
		REQUIRE(captures == 4);

		auto out = dynamic_cast<UniformAnalogWaveform*>(stitch->GetData(0));
		REQUIRE(out != nullptr);
		out->PrepareForCpuAccess();
		return out;
	};

	//Peak level near a frequency, in dBm
	auto peakNear = [](UniformAnalogWaveform* out, double hz)
	{
		float best = -1000;
		for(size_t i=0; i<out->size(); i++)
		{
			double f = (out->m_triggerPhase + static_cast<int64_t>(i) * out->m_timescale) / 1e6;
			if(fabs(f - hz) < 50000)
				best = max(best, (float)out->m_samples[i]);
		}
		return best;
	};

	//Same scaling as the Complex FFT: (2A)^2 / 50 ohms, in dBm, give or take some scalloping loss
	auto expectedDbm = [](double amplitude)
	{ return 10 * log10(4 * amplitude * amplitude / 50) + 30; };

	//Run the sweep twice, the second time the ends are trimmed
	sweep();
	auto out = sweep();
	double startHz = out->m_triggerPhase / 1e6;
	double endHz = EndOf(out) / 1e6;
	REQUIRE(out->m_timescale == fft->GetData(0)->m_timescale);
	REQUIRE(startHz <= center - span/2);
	REQUIRE(endHz >= center + span/2);
	REQUIRE(startHz > center - span/2 - 5000000);
	REQUIRE(endHz < center + span/2 + 5000000);

	//All three tones are there, at the right level
	const double tones[][2] = { { 2400500000, 0.5 }, { 2412000000, 0.4 }, { 2437000000, 0.3 } };
	for(auto& tone : tones)
	{
		float level = peakNear(out, tone[0]);
		REQUIRE(level < expectedDbm(tone[1]) + 0.3);
		REQUIRE(level > expectedDbm(tone[1]) - 1.5);
	}

	//and there's nothing in between
	REQUIRE(peakNear(out, 2425000000) < expectedDbm(0.3) - 40);
	REQUIRE(peakNear(out, 2447000000) < expectedDbm(0.3) - 40);
}

#endif
