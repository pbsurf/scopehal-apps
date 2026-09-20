/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
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
	@brief Unit tests for the IIO SDR driver, using the simulated (mock) IIO device
 */
#include "../../lib/scopehal/scopehal.h"

#ifdef HAS_IIO

#ifdef _CATCH2_V3
#include <catch2/catch_all.hpp>
#else
#include <catch2/catch.hpp>
#endif

#include "Primitives.h"
#include "../../lib/scopehal/ComplexChannel.h"

using namespace std;

static const char* phy = "ad9361-phy";

/**
	@brief Creates an IIO SDR on a mock device

	@param uri	Mock URI
	@param ctx	Receives the IIO context so tests can look at what's really in the (simulated) hardware
 */
static shared_ptr<SCPISDR> MakeSDR(const string& uri, IIOContext*& ctx)
{
	auto transport = dynamic_cast<SCPIIIOTransport*>(SCPITransport::CreateTransport("iio", uri));
	REQUIRE(transport != nullptr);
	ctx = transport->GetContext();
	REQUIRE(ctx != nullptr);

	//Driver takes ownership of the transport
	auto sdr = SCPISDR::CreateSDR("iio", transport);
	REQUIRE(sdr != nullptr);
	return sdr;
}

/**
	@brief Gets the magnitude of the component of a complex baseband signal at the given frequency, normalized so that
	a tone with amplitude A gives A. This is a single-bin DTFT.
 */
static double ToneMagnitude(WaveformBase* iw, WaveformBase* qw, double freq)
{
	auto i = dynamic_cast<UniformAnalogWaveform*>(iw);
	auto q = dynamic_cast<UniformAnalogWaveform*>(qw);
	REQUIRE(i != nullptr);
	REQUIRE(q != nullptr);
	REQUIRE(i->size() == q->size());
	i->PrepareForCpuAccess();
	q->PrepareForCpuAccess();

	double fs = FS_PER_SECOND / static_cast<double>(i->m_timescale);
	double re = 0;
	double im = 0;
	for(size_t n=0; n<i->size(); n++)
	{
		double phase = -2 * M_PI * freq * n / fs;
		double c = cos(phase);
		double s = sin(phase);
		re += i->m_samples[n] * c - q->m_samples[n] * s;
		im += i->m_samples[n] * s + q->m_samples[n] * c;
	}
	return sqrt(re*re + im*im) / i->size();
}

TEST_CASE("IIOSDR_Creation")
{
	IIOContext* ctx;
	auto sdr = MakeSDR("mock:ad9363", ctx);

	REQUIRE(sdr->GetDriverName() == "iio");
	REQUIRE(sdr->GetVendor() == "Analog Devices");
	REQUIRE(sdr->GetName() == "PlutoSDR Rev.B (Z7010-AD9363A)");
	REQUIRE(sdr->GetTransportName() == "iio");
	REQUIRE(sdr->GetTransportConnectionString() == "mock:ad9363");

	//1R1T, so one complex RX channel with I, Q, and a center frequency scalar
	REQUIRE(sdr->GetChannelCount() == 1);
	auto chan = dynamic_cast<ComplexChannel*>(sdr->GetChannel(0));
	REQUIRE(chan != nullptr);
	REQUIRE(chan->GetHwname() == "RX1");
	REQUIRE(chan->GetStreamCount() == 3);
	REQUIRE(sdr->IsChannelEnabled(0));

	REQUIRE(sdr->HasFrequencyControls());
	REQUIRE(sdr->HasTimebaseControls());
	REQUIRE(!sdr->HasResolutionBandwidth());

	//We adopt what the hardware is doing
	REQUIRE(sdr->GetCenterFrequency(0) == 2400000000);
	REQUIRE(sdr->GetSpan() == 2000000);
	REQUIRE(sdr->GetSampleRate() == 2500000);

	//Bigger radio has two
	auto sdr2 = MakeSDR("mock:ad9361", ctx);
	REQUIRE(sdr2->GetChannelCount() == 2);
	REQUIRE(sdr2->GetChannel(1)->GetHwname() == "RX2");
	REQUIRE(sdr2->IsChannelEnabled(0));
	REQUIRE(!sdr2->IsChannelEnabled(1));

	//The driver is offered with an IIO transport in the add instrument dialog
	auto models = SCPIInstrument::GetSupportedModels("iio");
	REQUIRE(models.size() >= 1);
	REQUIRE(models[0].supportedTransports[0].transportType == SCPITransportType::TRANSPORT_IIO);
}

TEST_CASE("IIOSDR_Configuration")
{
	IIOContext* ctx;
	auto sdr = MakeSDR("mock:ad9363", ctx);
	int64_t v;

	//Changes are visible right away, but only reach the hardware on the instrument thread
	sdr->SetCenterFrequency(0, 915000000);
	REQUIRE(sdr->GetCenterFrequency(0) == 915000000);
	REQUIRE(ctx->ReadChannelAttrInt(phy, "altvoltage0", true, "frequency", v));
	REQUIRE(v == 2400000000);

	sdr->SetSpan(5000000);
	sdr->SetSampleRate(10000000);
	sdr->BackgroundProcessing();
	REQUIRE(ctx->ReadChannelAttrInt(phy, "altvoltage0", true, "frequency", v));
	REQUIRE(v == 915000000);
	REQUIRE(ctx->ReadChannelAttrInt(phy, "voltage0", false, "rf_bandwidth", v));
	REQUIRE(v == 5000000);
	REQUIRE(ctx->ReadChannelAttrInt(phy, "voltage0", false, "sampling_frequency", v));
	REQUIRE(v == 10000000);
	REQUIRE(sdr->GetCenterFrequency(0) == 915000000);
	REQUIRE(sdr->GetSpan() == 5000000);
	REQUIRE(sdr->GetSampleRate() == 10000000);

	//Nothing pending, so this must not touch the hardware again
	REQUIRE(ctx->WriteChannelAttrInt(phy, "altvoltage0", true, "frequency", 1000000000));
	sdr->BackgroundProcessing();
	REQUIRE(ctx->ReadChannelAttrInt(phy, "altvoltage0", true, "frequency", v));
	REQUIRE(v == 1000000000);

	//The hardware wins if it disagrees. Bandwidth was too wide for the AD9363 and gets clamped.
	sdr->SetSpan(50000000);
	REQUIRE(sdr->GetSpan() == 50000000);
	sdr->BackgroundProcessing();
	REQUIRE(sdr->GetSpan() == 20000000);

	//The AD9363 can't tune this low, so the request is rejected and we go back to what the radio is really doing
	sdr->SetCenterFrequency(0, 100000000);
	REQUIRE(sdr->GetCenterFrequency(0) == 100000000);
	sdr->BackgroundProcessing();
	REQUIRE(sdr->GetCenterFrequency(0) == 1000000000);

	//Requests beyond what any AD936x can do are clamped before they get to the hardware
	sdr->SetSampleRate(1000);
	REQUIRE(sdr->GetSampleRate() == 2083334);
	sdr->SetSampleRate(1000000000);
	REQUIRE(sdr->GetSampleRate() == 61440000);
	sdr->SetSpan(1);
	REQUIRE(sdr->GetSpan() == 200000);

	//Lists for the UI
	auto rates = sdr->GetSampleRatesNonInterleaved();
	REQUIRE(rates.size() > 0);
	for(auto r : rates)
	{
		REQUIRE(r >= 2083334);
		REQUIRE(r <= 61440000);
	}
	REQUIRE(sdr->GetSampleDepthsNonInterleaved().size() > 0);
}

TEST_CASE("IIOSDR_Acquire")
{
	IIOContext* ctx;
	auto sdr = MakeSDR("mock:ad9363", ctx);
	auto chan = sdr->GetChannel(0);

	const size_t depth = 4096;
	sdr->SetSampleDepth(depth);
	REQUIRE(sdr->GetSampleDepth() == depth);

	//Not armed: nothing happens
	REQUIRE(!sdr->IsTriggerArmed());
	REQUIRE(sdr->PollTrigger() == Oscilloscope::TRIGGER_MODE_STOP);

	//Default is 2.4 GHz LO, 2.5 MSPS, 2 MHz bandwidth. The mock has a tone at 2.4005 GHz.
	sdr->StartSingleTrigger();
	REQUIRE(sdr->IsTriggerArmed());
	REQUIRE(sdr->PollTrigger() == Oscilloscope::TRIGGER_MODE_TRIGGERED);
	REQUIRE(sdr->AcquireData());

	//Single shot, so we stopped
	REQUIRE(!sdr->IsTriggerArmed());
	REQUIRE(sdr->PollTrigger() == Oscilloscope::TRIGGER_MODE_STOP);

	REQUIRE(sdr->GetPendingWaveformCount() == 1);
	REQUIRE(sdr->PopPendingWaveform());
	REQUIRE(sdr->GetPendingWaveformCount() == 0);

	auto i = chan->GetData(0);
	auto q = chan->GetData(1);
	REQUIRE(i != nullptr);
	REQUIRE(q != nullptr);
	REQUIRE(i->size() == depth);
	REQUIRE(q->size() == depth);
	REQUIRE(i->m_timescale == 400000000);
	REQUIRE(q->m_timescale == 400000000);
	REQUIRE(fabs(chan->GetScalarValue(2) - 2400000000.0) < 256);

	//Signal is at +500 kHz. The image at -500 kHz would mean I and Q got swapped or one was inverted.
	double wanted = ToneMagnitude(i, q, 500000);
	double image = ToneMagnitude(i, q, -500000);
	double elsewhere = ToneMagnitude(i, q, 900000);
	REQUIRE(wanted > 0.45);
	REQUIRE(wanted < 0.55);
	REQUIRE(image < 0.02);
	REQUIRE(elsewhere < 0.02);

	//Every sample is within full scale
	auto iw = dynamic_cast<UniformAnalogWaveform*>(i);
	iw->PrepareForCpuAccess();
	for(size_t n=0; n<iw->size(); n++)
	{
		REQUIRE(iw->m_samples[n] >= -1);
		REQUIRE(iw->m_samples[n] < 1);
	}

	//Retune so the tone is at -500 kHz, it should follow
	sdr->SetCenterFrequency(0, 2401000000);
	sdr->BackgroundProcessing();
	sdr->StartSingleTrigger();
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->PopPendingWaveform());
	i = chan->GetData(0);
	q = chan->GetData(1);
	REQUIRE(ToneMagnitude(i, q, -500000) > 0.45);
	REQUIRE(ToneMagnitude(i, q, 500000) < 0.02);
	REQUIRE(fabs(chan->GetScalarValue(2) - 2401000000.0) < 256);

	//Retune far away so there's nothing in band, only noise
	sdr->SetCenterFrequency(0, 1800000000);
	sdr->BackgroundProcessing();
	sdr->StartSingleTrigger();
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->PopPendingWaveform());
	i = chan->GetData(0);
	q = chan->GetData(1);
	REQUIRE(ToneMagnitude(i, q, 500000) < 0.02);
	REQUIRE(ToneMagnitude(i, q, -500000) < 0.02);

	//Sample rate change is reflected in the waveform timing
	sdr->SetCenterFrequency(0, 2400000000);
	sdr->SetSampleRate(5000000);
	sdr->BackgroundProcessing();
	sdr->StartSingleTrigger();
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->PopPendingWaveform());
	i = chan->GetData(0);
	q = chan->GetData(1);
	REQUIRE(i->m_timescale == 200000000);
	REQUIRE(ToneMagnitude(i, q, 500000) > 0.45);
}

TEST_CASE("IIOSDR_ContinuousAndDisabledChannels")
{
	IIOContext* ctx;
	auto sdr = MakeSDR("mock:ad9363", ctx);
	sdr->SetSampleDepth(1024);

	//Continuous mode stays armed
	sdr->Start();
	REQUIRE(sdr->IsTriggerArmed());
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->IsTriggerArmed());
	REQUIRE(sdr->GetPendingWaveformCount() == 2);

	//Nothing to capture if all channels are off. Don't report a trigger, we'd busy loop.
	sdr->DisableChannel(0);
	REQUIRE(!sdr->IsChannelEnabled(0));
	REQUIRE(sdr->PollTrigger() == Oscilloscope::TRIGGER_MODE_RUN);
	REQUIRE(!sdr->AcquireData());
	REQUIRE(sdr->GetPendingWaveformCount() == 2);

	sdr->Stop();
	REQUIRE(!sdr->IsTriggerArmed());
}

TEST_CASE("IIOSDR_TwoChannels")
{
	IIOContext* ctx;
	auto sdr = MakeSDR("mock:ad9361", ctx);
	sdr->SetSampleDepth(4096);
	sdr->EnableChannel(1);

	sdr->StartSingleTrigger();
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->PopPendingWaveform());

	//Both see the tone, RX2 a bit weaker
	double rx1 = ToneMagnitude(sdr->GetChannel(0)->GetData(0), sdr->GetChannel(0)->GetData(1), 500000);
	double rx2 = ToneMagnitude(sdr->GetChannel(1)->GetData(0), sdr->GetChannel(1)->GetData(1), 500000);
	REQUIRE(rx1 > 0.45);
	REQUIRE(rx2 > 0.25);
	REQUIRE(rx2 < 0.4);

	//Only RX2 enabled
	sdr->DisableChannel(0);
	sdr->StartSingleTrigger();
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->PopPendingWaveform());
	rx2 = ToneMagnitude(sdr->GetChannel(1)->GetData(0), sdr->GetChannel(1)->GetData(1), 500000);
	REQUIRE(rx2 > 0.25);
	REQUIRE(rx2 < 0.4);
}

#endif
