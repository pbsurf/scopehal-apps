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
	@brief Unit test for ComplexFFTFilter
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

using namespace std;

///@brief Sample rate of the test signals: 2.5 MSPS
static const int64_t g_timescale = 400000000;
static const double g_sampleRate = 2500000;

/**
	@brief Makes a complex tone of the given amplitude that is exactly on FFT bin k (negative k for negative frequencies)
 */
static UniformAnalogWaveform* MakeToneI(size_t depth, int k, double amp, int64_t timescale = g_timescale)
{
	auto ret = new UniformAnalogWaveform;
	ret->m_timescale = timescale;
	ret->m_triggerPhase = 0;
	ret->Resize(depth);
	ret->PrepareForCpuAccess();
	for(size_t n=0; n<depth; n++)
		ret->m_samples[n] = amp * cos(2 * M_PI * k * n / depth);
	ret->MarkSamplesModifiedFromCpu();
	return ret;
}

static UniformAnalogWaveform* MakeToneQ(size_t depth, int k, double amp, int64_t timescale = g_timescale)
{
	auto ret = new UniformAnalogWaveform;
	ret->m_timescale = timescale;
	ret->m_triggerPhase = 0;
	ret->Resize(depth);
	ret->PrepareForCpuAccess();
	for(size_t n=0; n<depth; n++)
		ret->m_samples[n] = amp * sin(2 * M_PI * k * n / depth);
	ret->MarkSamplesModifiedFromCpu();
	return ret;
}

/**
	@brief Finds the largest value in a spectrum and returns the index of it
 */
static size_t FindPeakBin(UniformAnalogWaveform* wfm)
{
	wfm->PrepareForCpuAccess();
	size_t best = 0;
	for(size_t i=0; i<wfm->size(); i++)
	{
		if(wfm->m_samples[i] > wfm->m_samples[best])
			best = i;
	}
	return best;
}

TEST_CASE("Filter_ComplexFFT")
{
	auto filter = dynamic_cast<ComplexFFTFilter*>(Filter::CreateFilter("Complex FFT", "#ffffff"));
	REQUIRE(filter != nullptr);
	FilterReferencer ref(filter);

	//Set up executor for the graph
	FilterGraphExecutor exec;
	set<FlowGraphNode*> nodes;
	nodes.emplace(filter);

	//Source channel with I, Q, and the center frequency. It owns the waveforms we give it.
	auto src = new ComplexChannel(g_scope, "IQ", "#ffffff", Unit(Unit::UNIT_FS), Unit(Unit::UNIT_VOLTS));
	filter->SetInput("I", StreamDescriptor(src, 0));
	filter->SetInput("Q", StreamDescriptor(src, 1));
	filter->SetInput("center", StreamDescriptor(src, 2));

	const size_t depth = 4096;
	const double binHz = g_sampleRate / depth;
	const int64_t binUhz = llround(binHz * 1e6);
	const int k = 819;
	const double amp = 0.5;

	//A complex tone of amplitude A has |X| = A*N, so with the 2/N scaling used by the complex spectrogram the power
	//is (2A)^2 / 50 ohms, in dBm
	const double expectedDbm = 10 * log10(4 * amp * amp / 50) + 30;

	auto run = [&](int tone, double center) -> UniformAnalogWaveform*
	{
		src->SetData(MakeToneI(depth, tone, amp), 0);
		src->SetData(MakeToneQ(depth, tone, amp), 1);
		src->UpdateCenterFrequency(center);
		exec.RunBlocking(nodes);
		return dynamic_cast<UniformAnalogWaveform*>(filter->GetData(0));
	};

	SECTION("Positive tone")
	{
		filter->SetWindowFunction(FFTFilter::WINDOW_RECTANGULAR);
		auto out = run(k, 2400000000);
		REQUIRE(out != nullptr);

		//Two sided, so as many outputs as inputs
		REQUIRE(out->size() == depth);
		REQUIRE(filter->test_GetNumOuts() == depth);
		REQUIRE(out->m_timescale == binUhz);

		//Peak is k bins above the center, which is in the middle of the output
		size_t peak = FindPeakBin(out);
		REQUIRE(peak == depth/2 + k);
		REQUIRE(fabs(out->m_samples[peak] - expectedDbm) < 0.1);

		//The mirror image (k bins below center) must be way down, or the spectrum is backwards
		REQUIRE(out->m_samples[depth/2 - k] < out->m_samples[peak] - 60);

		//X axis is absolute frequency in uHz: check the peak is at center + k bins
		double peakHz = (out->m_triggerPhase + static_cast<int64_t>(peak) * out->m_timescale) / 1e6;
		REQUIRE(fabs(peakHz - (2400000000.0 + k * binHz)) < binHz);
	}

	SECTION("Negative tone")
	{
		filter->SetWindowFunction(FFTFilter::WINDOW_RECTANGULAR);
		auto out = run(-k, 2400000000);
		REQUIRE(out != nullptr);

		size_t peak = FindPeakBin(out);
		REQUIRE(peak == depth/2 - k);
		REQUIRE(fabs(out->m_samples[peak] - expectedDbm) < 0.1);
		REQUIRE(out->m_samples[depth/2 + k] < out->m_samples[peak] - 60);

		double peakHz = (out->m_triggerPhase + static_cast<int64_t>(peak) * out->m_timescale) / 1e6;
		REQUIRE(fabs(peakHz - (2400000000.0 - k * binHz)) < binHz);
	}

	SECTION("DC is in the middle")
	{
		filter->SetWindowFunction(FFTFilter::WINDOW_RECTANGULAR);
		auto out = run(0, 2400000000);
		REQUIRE(out != nullptr);

		size_t peak = FindPeakBin(out);
		REQUIRE(peak == depth/2);

		//The middle of the X axis is the center frequency
		double peakHz = (out->m_triggerPhase + static_cast<int64_t>(peak) * out->m_timescale) / 1e6;
		REQUIRE(fabs(peakHz - 2400000000.0) < 1);
	}

	SECTION("Follows the center frequency")
	{
		filter->SetWindowFunction(FFTFilter::WINDOW_RECTANGULAR);
		auto out = run(k, 915000000);
		REQUIRE(out != nullptr);

		size_t peak = FindPeakBin(out);
		REQUIRE(peak == depth/2 + k);
		double peakHz = (out->m_triggerPhase + static_cast<int64_t>(peak) * out->m_timescale) / 1e6;
		REQUIRE(fabs(peakHz - (915000000.0 + k * binHz)) < binHz);
	}

	SECTION("Window functions")
	{
		//The scaling corrects for the coherent gain of each window, so a tone on a bin should come out at the same
		//level for all of them
		for(auto window : { FFTFilter::WINDOW_RECTANGULAR, FFTFilter::WINDOW_HANN, FFTFilter::WINDOW_HAMMING,
			FFTFilter::WINDOW_BLACKMAN_HARRIS })
		{
			INFO("window " << window);
			filter->SetWindowFunction(window);
			auto out = run(k, 2400000000);
			REQUIRE(out != nullptr);

			size_t peak = FindPeakBin(out);
			REQUIRE(peak == depth/2 + k);
			REQUIRE(fabs(out->m_samples[peak] - expectedDbm) < 0.3);
		}
	}

	SECTION("Two tones")
	{
		//Add a second, weaker tone at the opposite frequency to make sure both come through in the right places
		filter->SetWindowFunction(FFTFilter::WINDOW_RECTANGULAR);
		auto ui = MakeToneI(depth, k, amp);
		auto uq = MakeToneQ(depth, k, amp);
		auto ui2 = MakeToneI(depth, -100, amp / 10);
		auto uq2 = MakeToneQ(depth, -100, amp / 10);
		ui->PrepareForCpuAccess();
		uq->PrepareForCpuAccess();
		ui2->PrepareForCpuAccess();
		uq2->PrepareForCpuAccess();
		for(size_t n=0; n<depth; n++)
		{
			ui->m_samples[n] += ui2->m_samples[n];
			uq->m_samples[n] += uq2->m_samples[n];
		}
		ui->MarkSamplesModifiedFromCpu();
		uq->MarkSamplesModifiedFromCpu();
		delete ui2;
		delete uq2;
		src->SetData(ui, 0);
		src->SetData(uq, 1);
		src->UpdateCenterFrequency(2400000000);
		exec.RunBlocking(nodes);

		auto out = dynamic_cast<UniformAnalogWaveform*>(filter->GetData(0));
		REQUIRE(out != nullptr);
		out->PrepareForCpuAccess();
		REQUIRE(fabs(out->m_samples[depth/2 + k] - expectedDbm) < 0.1);

		//A tenth of the amplitude is 20 dB down
		REQUIRE(fabs(out->m_samples[depth/2 - 100] - (expectedDbm - 20)) < 0.1);
	}

	SECTION("Mismatched I and Q")
	{
		//I and Q at different sample rates can't be combined
		src->SetData(MakeToneI(depth, k, amp, g_timescale), 0);
		src->SetData(MakeToneQ(depth, k, amp, g_timescale/2), 1);
		src->UpdateCenterFrequency(2400000000);
		exec.RunBlocking(nodes);
		REQUIRE(filter->GetData(0) == nullptr);
	}

	//Detach the filter from the source before the channel and its waveforms go away
	filter->SetInput("I", StreamDescriptor(nullptr, 0), true);
	filter->SetInput("Q", StreamDescriptor(nullptr, 0), true);
	filter->SetInput("center", StreamDescriptor(nullptr, 0), true);
	delete src;
}

#ifdef HAS_IIO

/**
	@brief Runs the filter on the simulated PlutoSDR, which has a tone at 2.4005 GHz that is in band by default
 */
TEST_CASE("Filter_ComplexFFT_IIOSDR")
{
	auto transport = SCPITransport::CreateTransport("iio", "mock:");
	REQUIRE(transport != nullptr);
	auto sdr = SCPISDR::CreateSDR("iio", transport);
	REQUIRE(sdr != nullptr);

	auto filter = dynamic_cast<ComplexFFTFilter*>(Filter::CreateFilter("Complex FFT", "#ffffff"));
	REQUIRE(filter != nullptr);
	FilterReferencer ref(filter);
	filter->SetWindowFunction(FFTFilter::WINDOW_BLACKMAN_HARRIS);

	//The stream indexes are I, Q, and the center frequency
	auto chan = sdr->GetChannel(0);
	filter->SetInput("I", StreamDescriptor(chan, 0));
	filter->SetInput("Q", StreamDescriptor(chan, 1));
	filter->SetInput("center", StreamDescriptor(chan, 2));

	//Capture some data (2.4 GHz LO, 2.5 MSPS, 2 MHz bandwidth)
	const size_t depth = 16384;
	sdr->SetSampleDepth(depth);
	sdr->StartSingleTrigger();
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->PopPendingWaveform());

	FilterGraphExecutor exec;
	set<FlowGraphNode*> nodes;
	nodes.emplace(filter);
	exec.RunBlocking(nodes);

	auto out = dynamic_cast<UniformAnalogWaveform*>(filter->GetData(0));
	REQUIRE(out != nullptr);
	REQUIRE(out->size() == depth);

	//The strongest thing in the band is the 2.4005 GHz tone, at 0.5 of full scale
	size_t peak = FindPeakBin(out);
	double peakHz = (out->m_triggerPhase + static_cast<int64_t>(peak) * out->m_timescale) / 1e6;
	double binHz = g_sampleRate / depth;
	REQUIRE(fabs(peakHz - 2400500000.0) < 2 * binHz);

	//Same power scaling as the spectrogram: (2A)^2 / 50 ohms, in dBm, give or take some scalloping loss
	const double expectedDbm = 10 * log10(4 * 0.5 * 0.5 / 50) + 30;
	REQUIRE(out->m_samples[peak] < expectedDbm + 0.3);
	REQUIRE(out->m_samples[peak] > expectedDbm - 1.5);

	//And there's nothing in the mirror image position
	size_t mirror = depth - peak;
	REQUIRE(out->m_samples[mirror] < out->m_samples[peak] - 40);
}

#endif
