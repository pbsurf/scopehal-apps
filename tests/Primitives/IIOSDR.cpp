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
#include "../../lib/scopehal/SCPIIIOTransport.h"
#include "../../lib/scopehal/SCPISDR.h"
#include "../../lib/scopehal/IIOSDR.h"

#ifdef HAS_IIO

#ifdef _CATCH2_V3
#include <catch2/catch_all.hpp>
#else
#include <catch2/catch.hpp>
#endif

#include "Primitives.h"
#include "../../lib/scopehal/ComplexChannel.h"
#include <filesystem>
#include <fstream>

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
	@brief Full scale of the ADC referred to the input, with the mock's AGC running

	The mock's signals are defined as a fraction of full scale at 20 dB gain, which is where its AGC settles. The driver
	takes the gain out, so the samples are a tenth of that.
 */
static const double g_mockFullScale = 0.1;

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

/**
	@brief Checks that a receive channel has the streams the driver creates (I, Q, center frequency, RSSI)
 */
static void CheckRXStreams(InstrumentChannel* chan)
{
	REQUIRE(chan->GetStreamCount() == 4);
	REQUIRE(chan->GetType(0) == Stream::STREAM_TYPE_ANALOG);
	REQUIRE(chan->GetType(1) == Stream::STREAM_TYPE_ANALOG);
	REQUIRE(chan->GetType(2) == Stream::STREAM_TYPE_ANALOG_SCALAR);
	REQUIRE(chan->GetType(3) == Stream::STREAM_TYPE_ANALOG_SCALAR);
	REQUIRE((chan->GetYAxisUnits(0) == Unit(Unit::UNIT_VOLTS)));
	REQUIRE((chan->GetYAxisUnits(1) == Unit(Unit::UNIT_VOLTS)));
	REQUIRE((chan->GetYAxisUnits(2) == Unit(Unit::UNIT_HZ)));
	REQUIRE((chan->GetYAxisUnits(3) == Unit(Unit::UNIT_DB)));
	REQUIRE(chan->GetStreamName(2) == "center");
	REQUIRE(chan->GetStreamName(3) == "rssi");
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

	//1R1T, so one complex RX channel with I, Q, center frequency and RSSI scalars, then one transmit channel
	REQUIRE(sdr->GetChannelCount() == 2);
	auto chan = dynamic_cast<ComplexChannel*>(sdr->GetChannel(0));
	REQUIRE(chan != nullptr);
	REQUIRE(chan->GetHwname() == "RX1");
	REQUIRE(chan->GetStreamCount() == 4);
	REQUIRE(chan->GetStreamName(3) == "rssi");
	REQUIRE(chan->GetType(3) == Stream::STREAM_TYPE_ANALOG_SCALAR);
	REQUIRE((chan->GetYAxisUnits(3) == Unit(Unit::UNIT_DB)));
	REQUIRE((chan->GetStreamFlags(3) & Stream::STREAM_INFREQUENTLY_USED) != 0);
	REQUIRE(isnan(chan->GetScalarValue(3)));
	REQUIRE(sdr->IsChannelEnabled(0));
	auto txchan = dynamic_cast<SDRTransmitChannel*>(sdr->GetChannel(1));
	REQUIRE(txchan != nullptr);
	REQUIRE(txchan->GetHwname() == "TX1");
	REQUIRE(txchan->GetStreamCount() == 0);
	REQUIRE(txchan->GetTxIndex() == 0);
	REQUIRE(sdr->GetInstrumentTypesForChannel(0) == Instrument::INST_OSCILLOSCOPE);
	REQUIRE(sdr->GetInstrumentTypesForChannel(1) == Instrument::INST_RF_GEN);

	REQUIRE(sdr->HasFrequencyControls());
	REQUIRE(sdr->HasTimebaseControls());
	REQUIRE(!sdr->HasResolutionBandwidth());

	//We adopt what the hardware is doing
	REQUIRE(sdr->GetCenterFrequency(0) == 2400000000);
	REQUIRE(sdr->GetSpan() == 2000000);
	REQUIRE(sdr->GetSampleRate() == 2500000);

	//Bigger radio has two
	auto sdr2 = MakeSDR("mock:ad9361", ctx);
	REQUIRE(sdr2->GetChannelCount() == 4);
	REQUIRE(sdr2->GetChannel(1)->GetHwname() == "RX2");
	REQUIRE(sdr2->GetChannel(2)->GetHwname() == "TX1");
	REQUIRE(sdr2->GetChannel(3)->GetHwname() == "TX2");
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

	//The limits the radio publishes are used to clamp requests right away, so the UI shows the truth immediately.
	//This is an AD9363, which is more restricted than an AD9361.
	sdr->SetCenterFrequency(0, 100000000);
	REQUIRE(sdr->GetCenterFrequency(0) == 325000000);
	sdr->SetCenterFrequency(0, 5000000000);
	REQUIRE(sdr->GetCenterFrequency(0) == 3800000000);
	sdr->SetCenterFrequency(0, 325000000);
	sdr->BackgroundProcessing();
	REQUIRE(ctx->ReadChannelAttrInt(phy, "altvoltage0", true, "frequency", v));
	REQUIRE(v == 325000000);
	REQUIRE(sdr->GetCenterFrequency(0) == 325000000);

	//The AD9361 goes lower and wider (the sample rate has to be up too, or a span this wide would be swept)
	IIOContext* ctx2;
	auto sdr2 = MakeSDR("mock:ad9361", ctx2);
	sdr2->SetSampleRate(61440000);
	sdr2->SetCenterFrequency(0, 100000000);
	sdr2->SetSpan(50000000);
	REQUIRE(sdr2->GetCenterFrequency(0) == 100000000);
	REQUIRE(sdr2->GetSpan() == 50000000);
	sdr2->BackgroundProcessing();
	REQUIRE(ctx2->ReadChannelAttrInt(phy, "altvoltage0", true, "frequency", v));
	REQUIRE(v == 100000000);
	REQUIRE(ctx2->ReadChannelAttrInt(phy, "voltage0", false, "rf_bandwidth", v));
	REQUIRE(v == 50000000);

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
	REQUIRE(wanted > 0.45 * g_mockFullScale);
	REQUIRE(wanted < 0.55 * g_mockFullScale);
	REQUIRE(image < 0.02 * g_mockFullScale);
	REQUIRE(elsewhere < 0.02 * g_mockFullScale);

	//Every sample is within full scale
	auto iw = dynamic_cast<UniformAnalogWaveform*>(i);
	iw->PrepareForCpuAccess();
	for(size_t n=0; n<iw->size(); n++)
	{
		REQUIRE(iw->m_samples[n] >= -g_mockFullScale);
		REQUIRE(iw->m_samples[n] < g_mockFullScale);
	}

	//Retune so the tone is at -500 kHz, it should follow
	sdr->SetCenterFrequency(0, 2401000000);
	sdr->BackgroundProcessing();
	sdr->StartSingleTrigger();
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->PopPendingWaveform());
	i = chan->GetData(0);
	q = chan->GetData(1);
	REQUIRE(ToneMagnitude(i, q, -500000) > 0.45 * g_mockFullScale);
	REQUIRE(ToneMagnitude(i, q, 500000) < 0.02 * g_mockFullScale);
	REQUIRE(fabs(chan->GetScalarValue(2) - 2401000000.0) < 256);

	//Retune far away so there's nothing in band, only noise
	sdr->SetCenterFrequency(0, 1800000000);
	sdr->BackgroundProcessing();
	sdr->StartSingleTrigger();
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->PopPendingWaveform());
	i = chan->GetData(0);
	q = chan->GetData(1);
	REQUIRE(ToneMagnitude(i, q, 500000) < 0.02 * g_mockFullScale);
	REQUIRE(ToneMagnitude(i, q, -500000) < 0.02 * g_mockFullScale);

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
	REQUIRE(ToneMagnitude(i, q, 500000) > 0.45 * g_mockFullScale);
}

TEST_CASE("IIOSDR_Sweep")
{
	IIOContext* ctx;
	auto sdr = MakeSDR("mock:ad9363", ctx);
	auto chan = sdr->GetChannel(0);
	int64_t v;

	//Sweeping is off by default, so the span is limited to what the AD9363 can capture at once (20 MHz)
	const size_t depth = 4096;
	sdr->SetSampleDepth(depth);
	sdr->SetSampleRate(20000000);
	sdr->SetCenterFrequency(0, 2420000000);
	REQUIRE(sdr->CanSweep());
	REQUIRE(!sdr->IsSweepEnabled());
	sdr->SetSpan(60000000);
	REQUIRE(sdr->GetSpan() == 20000000);

	//With sweeping on, a 60 MHz span has to be swept
	sdr->SetSweepEnabled(true);
	REQUIRE(sdr->IsSweepEnabled());
	sdr->SetSpan(60000000);
	REQUIRE(sdr->GetSpan() == 60000000);
	sdr->BackgroundProcessing();
	REQUIRE(sdr->GetSpan() == 60000000);
	REQUIRE(sdr->GetCenterFrequency(0) == 2420000000);
	REQUIRE(ctx->ReadChannelAttrInt(phy, "voltage0", false, "rf_bandwidth", v));
	REQUIRE(v == 20000000);

	//Steps are half the bandwidth, rounded down to a whole number of FFT bins (10 MHz is exactly 2048), centered on
	//the span
	const double bin = 20000000.0 / depth;
	const double step = floor(10000000 / bin) * bin;
	const size_t nsteps = 6;
	REQUIRE(step == 10000000);

	//A single trigger goes all the way across the sweep, then stops
	sdr->StartSingleTrigger();
	for(size_t i=0; i<nsteps; i++)
	{
		REQUIRE(sdr->IsTriggerArmed());
		REQUIRE(sdr->PollTrigger() == Oscilloscope::TRIGGER_MODE_TRIGGERED);
		REQUIRE(sdr->AcquireData());
		REQUIRE(sdr->PopPendingWaveform());

		double expected = 2420000000 + (i - (nsteps - 1) / 2.0) * step;
		REQUIRE(fabs(chan->GetScalarValue(2) - expected) < 256);
		REQUIRE(ctx->ReadChannelAttrInt(phy, "altvoltage0", true, "frequency", v));
		REQUIRE(fabs(v - expected) < 1);
	}
	REQUIRE(!sdr->IsTriggerArmed());

	//The tone at 2.412 GHz shows up in the second step
	double lo2 = 2420000000 - ((nsteps - 1) / 2.0 - 1) * step;
	sdr->StartSingleTrigger();
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->PopPendingWaveform());
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->PopPendingWaveform());
	REQUIRE(fabs(chan->GetScalarValue(2) - lo2) < 256);
	REQUIRE(ToneMagnitude(chan->GetData(0), chan->GetData(1), 2412000000 - lo2) > 0.35 * g_mockFullScale);

	//Changing some other setting mid sweep doesn't disturb it, and the LO isn't mistaken for the center frequency
	sdr->SetGainMode(0, "manual");
	sdr->BackgroundProcessing();
	REQUIRE(sdr->GetCenterFrequency(0) == 2420000000);
	REQUIRE(sdr->GetSpan() == 60000000);
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->PopPendingWaveform());
	REQUIRE(fabs(chan->GetScalarValue(2) - (lo2 + step)) < 256);

	//Changing the sweep starts it over
	sdr->SetCenterFrequency(0, 2430000000);
	sdr->BackgroundProcessing();
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->PopPendingWaveform());
	REQUIRE(fabs(chan->GetScalarValue(2) - (2430000000 - (nsteps - 1) / 2.0 * step)) < 256);
	sdr->Stop();

	//Narrow enough to capture at once: no more sweeping, and the LO goes back to the center
	sdr->SetSpan(10000000);
	sdr->BackgroundProcessing();
	REQUIRE(ctx->ReadChannelAttrInt(phy, "altvoltage0", true, "frequency", v));
	REQUIRE(v == 2430000000);
	REQUIRE(ctx->ReadChannelAttrInt(phy, "voltage0", false, "rf_bandwidth", v));
	REQUIRE(v == 10000000);
	sdr->StartSingleTrigger();
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->PopPendingWaveform());
	REQUIRE(!sdr->IsTriggerArmed());
	REQUIRE(fabs(chan->GetScalarValue(2) - 2430000000.0) < 256);

	//Lowering the sample rate below the span starts sweeping again, with the analog bandwidth following the rate
	sdr->SetSampleRate(5000000);
	sdr->BackgroundProcessing();
	REQUIRE(ctx->ReadChannelAttrInt(phy, "voltage0", false, "rf_bandwidth", v));
	REQUIRE(v == 5000000);
	REQUIRE(sdr->GetSpan() == 10000000);
	sdr->StartSingleTrigger();
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->PopPendingWaveform());
	REQUIRE(sdr->IsTriggerArmed());
	REQUIRE(chan->GetScalarValue(2) < 2430000000.0 - 1000000);
	sdr->Stop();

	//The sweep never goes outside the tuning range
	sdr->SetSampleRate(20000000);
	sdr->SetCenterFrequency(0, 3800000000);
	sdr->SetSpan(100000000);
	sdr->BackgroundProcessing();
	sdr->StartSingleTrigger();
	double last = 0;
	for(size_t i=0; (i < 20) && sdr->IsTriggerArmed(); i++)
	{
		REQUIRE(sdr->AcquireData());
		REQUIRE(sdr->PopPendingWaveform());
		double lo = chan->GetScalarValue(2);
		REQUIRE(lo > last);
		REQUIRE(lo <= 3800000000.0 + 256);
		last = lo;
	}
	REQUIRE(!sdr->IsTriggerArmed());
	REQUIRE(fabs(last - 3800000000.0) < 256);

	//Turning sweeping off cuts the span down to one capture, and the LO goes back to the center
	sdr->SetSweepEnabled(false);
	REQUIRE(sdr->GetSpan() == 20000000);
	sdr->BackgroundProcessing();
	REQUIRE(ctx->ReadChannelAttrInt(phy, "altvoltage0", true, "frequency", v));
	REQUIRE(v == 3800000000);
	REQUIRE(ctx->ReadChannelAttrInt(phy, "voltage0", false, "rf_bandwidth", v));
	REQUIRE(v == 20000000);

	//Without sweeping, a span wider than the sample rate still isn't swept
	sdr->SetSampleRate(5000000);
	sdr->BackgroundProcessing();
	sdr->StartSingleTrigger();
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->PopPendingWaveform());
	REQUIRE(!sdr->IsTriggerArmed());
	REQUIRE(fabs(chan->GetScalarValue(2) - 3800000000.0) < 256);
}

TEST_CASE("IIOSDR_SweepStep")
{
	IIOContext* ctx;
	auto sdr = MakeSDR("mock:ad9363", ctx);
	auto iio = dynamic_pointer_cast<IIOSDR>(sdr);
	REQUIRE(iio != nullptr);
	auto chan = sdr->GetChannel(0);

	//The LO step can be changed (ngscopeclient sets it from a preference), and is limited to 10 - 100%
	REQUIRE(iio->GetSweepStepFraction() == 0.5);
	iio->SetSweepStepFraction(0.01);
	REQUIRE(iio->GetSweepStepFraction() == 0.1);
	iio->SetSweepStepFraction(2);
	REQUIRE(iio->GetSweepStepFraction() == 1.0);

	//Half the bandwidth is 10 MHz (a whole number of FFT bins), so it takes six steps to cover 60 MHz
	const size_t depth = 4096;
	sdr->SetSampleDepth(depth);
	sdr->SetSampleRate(20000000);
	sdr->SetCenterFrequency(0, 2420000000);
	sdr->SetSweepEnabled(true);
	sdr->SetSpan(60000000);
	iio->SetSweepStepFraction(0.5);
	sdr->BackgroundProcessing();

	const size_t nsteps = 6;
	sdr->StartSingleTrigger();
	for(size_t i=0; i<nsteps; i++)
	{
		REQUIRE(sdr->IsTriggerArmed());
		REQUIRE(sdr->AcquireData());
		REQUIRE(sdr->PopPendingWaveform());

		double expected = 2420000000 + (i - (nsteps - 1) / 2.0) * 10000000;
		REQUIRE(fabs(chan->GetScalarValue(2) - expected) < 256);
	}
	REQUIRE(!sdr->IsTriggerArmed());

	//Changing it mid sweep starts the sweep over with the new steps
	sdr->StartSingleTrigger();
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->PopPendingWaveform());
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->PopPendingWaveform());
	iio->SetSweepStepFraction(0.8);
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->PopPendingWaveform());
	const double bin = 20000000.0 / depth;
	const double step = floor(16000000 / bin) * bin;
	REQUIRE(fabs(chan->GetScalarValue(2) - (2420000000 - 1.5 * step)) < 256);
	sdr->Stop();
}

TEST_CASE("IIOSDR_SweepQueued")
{
	//Captures can pile up while the filter graph is busy. Each one has to come out with the center frequency it was
	//captured at, not wherever the LO has got to since.
	IIOContext* ctx;
	auto sdr = MakeSDR("mock:ad9363", ctx);
	auto chan = sdr->GetChannel(0);

	const size_t depth = 4096;
	sdr->SetSampleDepth(depth);
	sdr->SetSampleRate(20000000);
	sdr->SetCenterFrequency(0, 2420000000);
	sdr->SetSweepEnabled(true);
	sdr->SetSpan(60000000);
	sdr->BackgroundProcessing();

	//Default steps are half the bandwidth, 10 MHz
	const double step = 10000000;
	const size_t nsteps = 6;

	sdr->StartSingleTrigger();
	for(size_t i=0; i<nsteps; i++)
		REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->GetPendingWaveformCount() == nsteps);

	for(size_t i=0; i<nsteps; i++)
	{
		REQUIRE(sdr->PopPendingWaveform());
		double expected = 2420000000 + (i - (nsteps - 1) / 2.0) * step;
		REQUIRE(fabs(chan->GetScalarValue(2) - expected) < 1);
	}
	REQUIRE(!sdr->PopPendingWaveform());

	//Clearing the queue clears the values with it, so the next capture still gets its own
	sdr->StartSingleTrigger();
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->AcquireData());
	sdr->ClearPendingWaveforms();
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->PopPendingWaveform());
	REQUIRE(fabs(chan->GetScalarValue(2) - (2420000000 + (2 - (nsteps - 1) / 2.0) * step)) < 1);
	REQUIRE(!sdr->PopPendingWaveform());
	sdr->Stop();
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
	REQUIRE(rx1 > 0.45 * g_mockFullScale);
	REQUIRE(rx2 > 0.25 * g_mockFullScale);
	REQUIRE(rx2 < 0.4 * g_mockFullScale);

	//Only RX2 enabled
	sdr->DisableChannel(0);
	sdr->StartSingleTrigger();
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->PopPendingWaveform());
	rx2 = ToneMagnitude(sdr->GetChannel(1)->GetData(0), sdr->GetChannel(1)->GetData(1), 500000);
	REQUIRE(rx2 > 0.25 * g_mockFullScale);
	REQUIRE(rx2 < 0.4 * g_mockFullScale);
}

TEST_CASE("IIOSDR_RSSI")
{
	IIOContext* ctx;
	auto sdr = MakeSDR("mock:ad9361", ctx);
	sdr->SetSampleDepth(4096);
	sdr->EnableChannel(1);
	auto rx1 = sdr->GetChannel(0);
	auto rx2 = sdr->GetChannel(1);

	//Both receive paths can read it, the transmit paths can't. Off by default.
	REQUIRE(sdr->HasRSSI(0));
	REQUIRE(sdr->HasRSSI(1));
	REQUIRE(!sdr->HasRSSI(2));
	REQUIRE(!sdr->IsRSSIEnabled(0));
	REQUIRE(!sdr->IsRSSIEnabled(1));

	//Nothing is read while it's off
	sdr->StartSingleTrigger();
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->PopPendingWaveform());
	REQUIRE(isnan(rx1->GetScalarValue(3)));
	REQUIRE(isnan(rx2->GetScalarValue(3)));

	//Only read for the channel that asked for it. The mock reports "70.00 dB" like the real driver, which leaves off
	//the sign, so we should see -70.
	sdr->SetRSSIEnabled(1, true);
	REQUIRE(!sdr->IsRSSIEnabled(0));
	REQUIRE(sdr->IsRSSIEnabled(1));
	sdr->StartSingleTrigger();
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->PopPendingWaveform());
	REQUIRE(isnan(rx1->GetScalarValue(3)));
	REQUIRE(rx2->GetScalarValue(3) == -70);

	//Turning it off clears the value on the next acquisition rather than leaving a stale one
	sdr->SetRSSIEnabled(1, false);
	sdr->StartSingleTrigger();
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->PopPendingWaveform());
	REQUIRE(isnan(rx2->GetScalarValue(3)));

	//Out of range channels are ignored
	sdr->SetRSSIEnabled(2, true);
	REQUIRE(!sdr->IsRSSIEnabled(2));
	sdr->SetRSSIEnabled(99, true);
	REQUIRE(!sdr->IsRSSIEnabled(99));
}

TEST_CASE("IIOSDR_Gain")
{
	IIOContext* ctx;
	auto sdr = MakeSDR("mock:ad9363", ctx);

	REQUIRE(sdr->HasGainControl(0));
	REQUIRE(!sdr->HasGainControl(1));

	//Modes come from the radio. A radio powers up running AGC, so the gain is not adjustable.
	auto modes = sdr->GetGainModes(0);
	REQUIRE(modes.size() == 4);
	REQUIRE(modes[0] == "manual");
	REQUIRE(sdr->GetGainMode(0) == "slow_attack");
	REQUIRE(!sdr->IsGainAdjustable(0));

	auto range = sdr->GetGainRange(0);
	REQUIRE(range.first == -1);
	REQUIRE(range.second == 73);
	REQUIRE(sdr->GetGain(0) == 20);

	//You can ask for a gain in AGC mode, but it doesn't reach the hardware until the mode changes
	sdr->SetGain(0, 30);
	REQUIRE(sdr->GetGain(0) == 30);
	sdr->BackgroundProcessing();
	double hw;
	string hwmode;
	REQUIRE(ctx->ReadChannelAttrDouble(phy, "voltage0", false, "hardwaregain", hw));
	REQUIRE(hw == 20);
	REQUIRE(sdr->GetGain(0) == 30);

	sdr->SetGainMode(0, "manual");
	REQUIRE(sdr->IsGainAdjustable(0));
	sdr->BackgroundProcessing();
	REQUIRE(ctx->ReadChannelAttr(phy, "voltage0", false, "gain_control_mode", hwmode));
	REQUIRE(hwmode == "manual");
	REQUIRE(ctx->ReadChannelAttrDouble(phy, "voltage0", false, "hardwaregain", hw));
	REQUIRE(hw == 30);
	REQUIRE(sdr->GetGain(0) == 30);

	//Now changes apply directly
	sdr->SetGain(0, 45.5);
	sdr->BackgroundProcessing();
	REQUIRE(ctx->ReadChannelAttrDouble(phy, "voltage0", false, "hardwaregain", hw));
	REQUIRE(fabs(hw - 45.5) < 1e-3);

	//Clamped to the range
	sdr->SetGain(0, 100);
	REQUIRE(sdr->GetGain(0) == 73);
	sdr->SetGain(0, -50);
	REQUIRE(sdr->GetGain(0) == -1);

	//Unknown modes are ignored
	sdr->SetGainMode(0, "bogus");
	REQUIRE(sdr->GetGainMode(0) == "manual");

	//And back to AGC
	sdr->SetGainMode(0, "fast_attack");
	REQUIRE(!sdr->IsGainAdjustable(0));
	sdr->BackgroundProcessing();
	REQUIRE(ctx->ReadChannelAttr(phy, "voltage0", false, "gain_control_mode", hwmode));
	REQUIRE(hwmode == "fast_attack");

	//AD9361 has a different range
	IIOContext* ctx2;
	auto sdr2 = MakeSDR("mock:ad9361", ctx2);
	REQUIRE(sdr2->HasGainControl(1));
	range = sdr2->GetGainRange(1);
	REQUIRE(range.first == -3);
	REQUIRE(range.second == 71);

	//Second channel is independent
	sdr2->SetGainMode(1, "manual");
	sdr2->SetGain(1, 10);
	sdr2->BackgroundProcessing();
	REQUIRE(sdr2->GetGainMode(0) == "slow_attack");
	REQUIRE(sdr2->GetGainMode(1) == "manual");
	REQUIRE(ctx2->ReadChannelAttrDouble(phy, "voltage1", false, "hardwaregain", hw));
	REQUIRE(hw == 10);
}

TEST_CASE("IIOSDR_GainCompensated")
{
	IIOContext* ctx;
	auto sdr = MakeSDR("mock:ad9363", ctx);
	auto chan = sdr->GetChannel(0);
	sdr->SetSampleDepth(4096);

	//In manual mode the mock's signal level follows the gain (20 dB is the reference, +/- 6 dB is 2x).
	//The driver takes the gain out, so the level at the input stays the same.
	sdr->SetGainMode(0, "manual");
	sdr->SetGain(0, 20);
	sdr->BackgroundProcessing();
	sdr->StartSingleTrigger();
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->PopPendingWaveform());
	double at20 = ToneMagnitude(chan->GetData(0), chan->GetData(1), 500000);
	REQUIRE(at20 > 0.045);
	REQUIRE(at20 < 0.055);

	sdr->SetGain(0, 14);
	sdr->BackgroundProcessing();
	sdr->StartSingleTrigger();
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->PopPendingWaveform());
	double at14 = ToneMagnitude(chan->GetData(0), chan->GetData(1), 500000);
	REQUIRE(at14 == Catch::Approx(at20).epsilon(0.05));

	//With AGC running the gain is read back on every acquisition, and taken out too
	sdr->SetGainMode(0, "fast_attack");
	sdr->BackgroundProcessing();
	REQUIRE(sdr->GetGain(0) == 20);

	//Pretend the AGC moved the gain to 26 dB behind our back (the mock's AGC doesn't, so do it by hand)
	REQUIRE(ctx->WriteChannelAttr(phy, "voltage0", false, "gain_control_mode", "manual"));
	REQUIRE(ctx->WriteChannelAttrDouble(phy, "voltage0", false, "hardwaregain", 26));
	sdr->StartSingleTrigger();
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->PopPendingWaveform());
	REQUIRE(sdr->GetGain(0) == 26);
	double agc = ToneMagnitude(chan->GetData(0), chan->GetData(1), 500000);
	REQUIRE(agc == Catch::Approx(at20).epsilon(0.05));
}

/**
	@brief Writes a file in the temporary directory, and deletes it when it goes out of scope
 */
class TempFile
{
public:
	TempFile(const string& name, const string& contents)
	: m_path((filesystem::temp_directory_path() / ("ngscopeclient-test-" + name)).string())
	{
		ofstream out(m_path);
		out << contents;
	}

	~TempFile()
	{
		error_code ec;
		filesystem::remove(m_path, ec);
	}

	string m_path;
};

TEST_CASE("IIOSDR_ExternalGain")
{
	IIOContext* ctx;
	auto sdr = MakeSDR("mock:ad9363", ctx);
	auto chan = sdr->GetChannel(0);
	auto ochan = dynamic_cast<OscilloscopeChannel*>(chan);
	sdr->SetSampleDepth(4096);

	REQUIRE(sdr->HasLevelCorrection(0));
	REQUIRE(!sdr->HasLevelCorrection(1));
	REQUIRE(sdr->GetExternalGain(0) == 0);

	//Default range is the full scale of the ADC, referred to the input
	REQUIRE(ochan->GetVoltageRange(0) == Catch::Approx(2 * g_mockFullScale));
	REQUIRE(ochan->GetVoltageRange(1) == Catch::Approx(2 * g_mockFullScale));

	sdr->StartSingleTrigger();
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->PopPendingWaveform());
	double before = ToneMagnitude(chan->GetData(0), chan->GetData(1), 500000);

	//A 20 dB pad in front of the input: the signal there is 10x bigger, and so is the range so it looks the same
	ochan->SetOffset(0.01, 0);
	sdr->SetExternalGain(0, -20);
	REQUIRE(sdr->GetExternalGain(0) == -20);
	REQUIRE(ochan->GetVoltageRange(0) == Catch::Approx(20 * g_mockFullScale));
	REQUIRE(ochan->GetVoltageRange(1) == Catch::Approx(20 * g_mockFullScale));
	REQUIRE(ochan->GetOffset(0) == Catch::Approx(0.1));

	//20 dB of receive gain and a 20 dB pad cancel out
	REQUIRE(sdr->GetInputGain(0, 2400000000, 20) == Catch::Approx(0));

	sdr->StartSingleTrigger();
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->PopPendingWaveform());
	double after = ToneMagnitude(chan->GetData(0), chan->GetData(1), 500000);
	REQUIRE(after == Catch::Approx(before * 10).epsilon(0.05));
}

TEST_CASE("IIOSDR_CalibrationFile")
{
	IIOContext* ctx;
	auto sdr = MakeSDR("mock:ad9363", ctx);
	auto chan = sdr->GetChannel(0);
	sdr->SetSampleDepth(4096);

	REQUIRE(sdr->GetCalibrationFile(0) == "");
	REQUIRE(sdr->GetCalibrationError(0) == "");
	REQUIRE(sdr->GetCalibrationGain(0, 2400000000) == 0);

	SECTION("Text")
	{
		//Header, comments, units, and points out of order
		TempFile f("cal.csv",
			"freq_hz,gain_db\n"
			"# A comment\n"
			"\n"
			"2.5e9, 2\n"
			"2 GHz, 6 dB   # another\n"
			"1000000000;10\n");
		REQUIRE(sdr->SetCalibrationFile(0, f.m_path));
		REQUIRE(sdr->GetCalibrationFile(0) == f.m_path);
		REQUIRE(sdr->GetCalibrationError(0) == "");

		//Interpolated between points, flat outside them
		REQUIRE(sdr->GetCalibrationGain(0, 2000000000) == Catch::Approx(6));
		REQUIRE(sdr->GetCalibrationGain(0, 1500000000) == Catch::Approx(8));
		REQUIRE(sdr->GetCalibrationGain(0, 2400000000) == Catch::Approx(2.8));
		REQUIRE(sdr->GetCalibrationGain(0, 100000000) == Catch::Approx(10));
		REQUIRE(sdr->GetCalibrationGain(0, 6000000000) == Catch::Approx(2));
		REQUIRE(sdr->GetInputGain(0, 2400000000, 20) == Catch::Approx(22.8));

		//And it's taken out of the signal (the mock's tone is at 2.4005 GHz, the LO is 2.4 GHz)
		sdr->StartSingleTrigger();
		REQUIRE(sdr->AcquireData());
		REQUIRE(sdr->PopPendingWaveform());
		double mag = ToneMagnitude(chan->GetData(0), chan->GetData(1), 500000);
		REQUIRE(mag == Catch::Approx(0.5 * g_mockFullScale * pow(10, -2.8 / 20)).epsilon(0.05));

		//Whitespace separated
		TempFile f2("cal.txt", "1e9 1\n2e9 3\n");
		REQUIRE(sdr->SetCalibrationFile(0, f2.m_path));
		REQUIRE(sdr->GetCalibrationGain(0, 1500000000) == Catch::Approx(2));

		//Back to none
		REQUIRE(sdr->SetCalibrationFile(0, ""));
		REQUIRE(sdr->GetCalibrationGain(0, 1500000000) == 0);
	}

	SECTION("Single point")
	{
		TempFile f("cal.csv", "915e6, -3\n");
		REQUIRE(sdr->SetCalibrationFile(0, f.m_path));
		REQUIRE(sdr->GetCalibrationGain(0, 100000000) == Catch::Approx(-3));
		REQUIRE(sdr->GetCalibrationGain(0, 6000000000) == Catch::Approx(-3));
	}

	SECTION("Touchstone")
	{
		//S21 of 0.5 (-6 dB) and 0.25 (-12 dB)
		TempFile f("cal.s2p",
			"# GHz S MA R 50\n"
			"1 0 0 0.5 0 0 0 0 0\n"
			"2 0 0 0.25 0 0 0 0 0\n");
		REQUIRE(sdr->SetCalibrationFile(0, f.m_path));
		REQUIRE(sdr->GetCalibrationError(0) == "");
		REQUIRE(sdr->GetCalibrationGain(0, 1000000000) == Catch::Approx(-6.0206).margin(0.01));
		REQUIRE(sdr->GetCalibrationGain(0, 2000000000) == Catch::Approx(-12.0412).margin(0.01));
	}

	SECTION("Errors")
	{
		//A bad file leaves no calibration, but remembers the path so it can be fixed
		TempFile f("bad.csv", "1e9, 1\n2e9, oops\n");
		REQUIRE(!sdr->SetCalibrationFile(0, f.m_path));
		REQUIRE(sdr->GetCalibrationFile(0) == f.m_path);
		REQUIRE(sdr->GetCalibrationError(0).find("Line 2") != string::npos);
		REQUIRE(sdr->GetCalibrationGain(0, 1000000000) == 0);

		TempFile empty("empty.csv", "# nothing\n");
		REQUIRE(!sdr->SetCalibrationFile(0, empty.m_path));
		REQUIRE(sdr->GetCalibrationError(0) != "");

		REQUIRE(!sdr->SetCalibrationFile(0, "/nonexistent/cal.csv"));
		REQUIRE(sdr->GetCalibrationError(0) != "");

		TempFile s1p("cal.s1p", "# GHz S MA R 50\n1 0.5 0\n");
		REQUIRE(!sdr->SetCalibrationFile(0, s1p.m_path));
		REQUIRE(sdr->GetCalibrationError(0) != "");

		//Fixing it clears the error
		TempFile good("good.csv", "1e9, 1\n");
		REQUIRE(sdr->SetCalibrationFile(0, good.m_path));
		REQUIRE(sdr->GetCalibrationError(0) == "");
	}
}

TEST_CASE("IIOSDR_LevelCorrectionSessionRoundTrip")
{
	IIOContext* ctx;
	auto sdr = MakeSDR("mock:ad9361", ctx);
	TempFile f("cal.csv", "1e9, 1\n3e9, 5\n");
	sdr->SetExternalGain(1, -10);
	REQUIRE(sdr->SetCalibrationFile(1, f.m_path));
	auto ochan = dynamic_cast<OscilloscopeChannel*>(sdr->GetChannel(1));
	float range = ochan->GetVoltageRange(0);

	IDTable table;
	auto node = sdr->SerializeConfiguration(table);

	IIOContext* ctx2;
	auto sdr2 = MakeSDR("mock:ad9361", ctx2);
	IDTable idmap;
	sdr2->LoadConfiguration(2, node, idmap);

	REQUIRE(sdr2->GetExternalGain(0) == 0);
	REQUIRE(sdr2->GetExternalGain(1) == -10);
	REQUIRE(sdr2->GetCalibrationFile(0) == "");
	REQUIRE(sdr2->GetCalibrationFile(1) == f.m_path);
	REQUIRE(sdr2->GetCalibrationGain(1, 2000000000) == Catch::Approx(3));

	//The saved range already goes with the external gain, so it isn't scaled again
	auto ochan2 = dynamic_cast<OscilloscopeChannel*>(sdr2->GetChannel(1));
	REQUIRE(ochan2->GetVoltageRange(0) == Catch::Approx(range));
}

TEST_CASE("IIOSDR_Limits")
{
	IIOContext* ctx;
	auto sdr = MakeSDR("mock:ad9363", ctx);

	//Everything offered in the UI is within what the radio can do
	for(auto r : sdr->GetSampleRatesNonInterleaved())
	{
		REQUIRE(r >= 2083334);
		REQUIRE(r <= 61440000);
	}

	sdr->SetSampleRate(1000);
	REQUIRE(sdr->GetSampleRate() == 2083334);
	sdr->SetSampleRate(1000000000);
	REQUIRE(sdr->GetSampleRate() == 61440000);
	sdr->SetSpan(1);
	REQUIRE(sdr->GetSpan() == 200000);
}

TEST_CASE("IIOSDR_SessionRoundTrip")
{
	IIOContext* ctx;
	auto sdr = MakeSDR("mock:ad9361", ctx);

	sdr->SetCenterFrequency(0, 915000000);
	sdr->SetSpan(5000000);
	sdr->SetSampleRate(10000000);
	sdr->SetSampleDepth(16384);
	sdr->EnableChannel(1);
	sdr->DisableChannel(0);
	sdr->SetGainMode(0, "hybrid");
	sdr->SetGainMode(1, "manual");
	sdr->SetGain(1, 33);
	sdr->SetRSSIEnabled(1, true);
	sdr->BackgroundProcessing();

	IDTable table;
	auto node = sdr->SerializeConfiguration(table);
	REQUIRE(node["driver"].as<string>() == "iio");
	REQUIRE(node["transport"].as<string>() == "iio");
	REQUIRE(node["args"].as<string>() == "mock:ad9361");

	//Load it into a fresh instance, which starts out with the radio's defaults
	IIOContext* ctx2;
	auto sdr2 = MakeSDR("mock:ad9361", ctx2);
	REQUIRE(sdr2->GetCenterFrequency(0) == 2400000000);
	IDTable idmap;
	sdr2->LoadConfiguration(2, node, idmap);
	sdr2->BackgroundProcessing();

	REQUIRE(sdr2->GetCenterFrequency(0) == 915000000);
	REQUIRE(sdr2->GetSpan() == 5000000);
	REQUIRE(sdr2->GetSampleRate() == 10000000);
	REQUIRE(sdr2->GetSampleDepth() == 16384);
	REQUIRE(!sdr2->IsChannelEnabled(0));
	REQUIRE(sdr2->IsChannelEnabled(1));
	REQUIRE(sdr2->GetGainMode(0) == "hybrid");
	REQUIRE(sdr2->GetGainMode(1) == "manual");
	REQUIRE(sdr2->GetGain(1) == 33);
	REQUIRE(!sdr2->IsRSSIEnabled(0));
	REQUIRE(sdr2->IsRSSIEnabled(1));

	//Loading a session must not change what kind of streams the channels have
	for(size_t i=0; i<2; i++)
		CheckRXStreams(sdr2->GetChannel(i));

	//And it all made it to the radio
	int64_t v;
	double gain;
	REQUIRE(ctx2->ReadChannelAttrInt(phy, "altvoltage0", true, "frequency", v));
	REQUIRE(v == 915000000);
	REQUIRE(ctx2->ReadChannelAttrInt(phy, "voltage0", false, "rf_bandwidth", v));
	REQUIRE(v == 5000000);
	REQUIRE(ctx2->ReadChannelAttrDouble(phy, "voltage1", false, "hardwaregain", gain));
	REQUIRE(gain == 33);
	REQUIRE(!sdr2->IsSweepEnabled());
}

TEST_CASE("IIOSDR_SessionFromBeforeRSSI")
{
	IIOContext* ctx;
	auto sdr = MakeSDR("mock:ad9363", ctx);
	sdr->SetCenterFrequency(0, 915000000);
	sdr->BackgroundProcessing();

	//Make it look like a session saved before the RSSI stream existed
	IDTable table;
	auto node = sdr->SerializeConfiguration(table);
	auto cnode = node["channels"]["ch0"];
	REQUIRE(cnode["nstreams"].as<size_t>() == 4);
	cnode["nstreams"] = 3;
	cnode["streams"].remove("stream3");
	cnode.remove("rssi");

	//Loading it must keep the streams the driver has, not rebuild them all as the type of the first one
	IIOContext* ctx2;
	auto sdr2 = MakeSDR("mock:ad9363", ctx2);
	IDTable idmap;
	sdr2->LoadConfiguration(2, node, idmap);
	sdr2->BackgroundProcessing();
	REQUIRE(sdr2->GetCenterFrequency(0) == 915000000);
	REQUIRE(!sdr2->IsRSSIEnabled(0));
	CheckRXStreams(sdr2->GetChannel(0));
}

TEST_CASE("IIOSDR_SessionRoundTripSweep")
{
	//A swept span wider than one capture has to survive loading, even though the span is restored before sweeping
	//is turned back on
	IIOContext* ctx;
	auto sdr = MakeSDR("mock:ad9361", ctx);
	sdr->SetSweepEnabled(true);
	sdr->SetCenterFrequency(0, 2420000000);
	sdr->SetSpan(200000000);
	sdr->BackgroundProcessing();

	IDTable table;
	auto node = sdr->SerializeConfiguration(table);
	REQUIRE(node["sweep"].as<bool>());

	IIOContext* ctx2;
	auto sdr2 = MakeSDR("mock:ad9361", ctx2);
	REQUIRE(!sdr2->IsSweepEnabled());
	IDTable idmap;
	sdr2->LoadConfiguration(2, node, idmap);
	sdr2->BackgroundProcessing();

	REQUIRE(sdr2->IsSweepEnabled());
	REQUIRE(sdr2->GetCenterFrequency(0) == 2420000000);
	REQUIRE(sdr2->GetSpan() == 200000000);
}

TEST_CASE("IIOSDR_Scan")
{
	//We can't count on any hardware being attached, but scanning must work and never report simulated devices
	for(auto& it : IIOContext::Scan())
	{
		REQUIRE(it.first.size() > 0);
		REQUIRE(it.first.find("mock:") != 0);
	}

	//Endpoint enumeration for the add instrument dialog is the same list
	REQUIRE(SCPITransport::EnumEndpoints("iio").size() == IIOContext::Scan().size());
}


/**
	@brief Reads the state of a DDS in the simulated radio
 */
static void ReadDDS(IIOContext* ctx, const string& name, int64_t& freq, double& scale, int64_t& phase, int64_t& raw)
{
	const char* dds = "cf-ad9361-dds-core-lpc";
	REQUIRE(ctx->ReadChannelAttrInt(dds, name, true, "frequency", freq));
	REQUIRE(ctx->ReadChannelAttrDouble(dds, name, true, "scale", scale));
	REQUIRE(ctx->ReadChannelAttrInt(dds, name, true, "phase", phase));
	REQUIRE(ctx->ReadChannelAttrInt(dds, name, true, "raw", raw));
}

TEST_CASE("IIOSDR_Transmit")
{
	IIOContext* ctx;
	auto sdr = MakeSDR("mock:ad9361", ctx);

	REQUIRE(sdr->GetTxChannelCount() == 2);
	REQUIRE(sdr->GetTxToneCount(0) == 2);
	REQUIRE(sdr->GetTxToneCount(1) == 2);
	REQUIRE(sdr->GetTxToneCount(2) == 0);

	//We adopt what the DDS is doing: tone 1 running at 1 MHz and 25%, tone 2 off (muted via amplitude, not disabled)
	REQUIRE(sdr->GetTxLOFrequency() == 2400000000);
	REQUIRE(sdr->GetTxToneFrequency(0, 0) == 1000000);
	REQUIRE(sdr->GetTxToneAmplitude(0, 0) == Catch::Approx(0.25));
	REQUIRE(sdr->GetTxToneAmplitude(0, 1) == Catch::Approx(0));

	//The DDS itself is always left enabled: a tone is muted via amplitude=0, since toggling "raw" per tone can
	//glitch other, unrelated tones on this DDS core
	{
		int64_t f;
		double s;
		int64_t p;
		int64_t r;
		ReadDDS(ctx, "TX1_I_F2", f, s, p, r);
		REQUIRE(r == 1);
	}

	//Limits: tones can be up to half the sample rate either side of the LO
	auto range = sdr->GetTxToneFrequencyRange(0);
	REQUIRE(range.first == -1250000);
	REQUIRE(range.second == 1250000);
	auto lorange = sdr->GetTxLOFrequencyRange();
	REQUIRE(lorange.first == 70000000);
	REQUIRE(lorange.second == 6000000000);

	//Changes only reach the radio when the instrument thread gets to them
	int64_t freq;
	double scale;
	int64_t phase;
	int64_t raw;
	sdr->SetTxToneFrequency(1, 1, 250000);
	sdr->SetTxToneAmplitude(1, 1, 0.5);
	ReadDDS(ctx, "TX2_I_F2", freq, scale, phase, raw);
	REQUIRE(scale == Catch::Approx(0));
	sdr->BackgroundProcessing();

	//Both I and Q run at the tone frequency, 90 degrees apart, with I leading for a positive frequency
	ReadDDS(ctx, "TX2_I_F2", freq, scale, phase, raw);
	REQUIRE(freq == 250000);
	REQUIRE(scale == Catch::Approx(0.5));
	REQUIRE(phase == 90000);
	REQUIRE(raw == 1);
	ReadDDS(ctx, "TX2_Q_F2", freq, scale, phase, raw);
	REQUIRE(freq == 250000);
	REQUIRE(scale == Catch::Approx(0.5));
	REQUIRE(phase == 0);
	REQUIRE(raw == 1);

	//The other tone and path are unaffected
	ReadDDS(ctx, "TX1_I_F1", freq, scale, phase, raw);
	REQUIRE(freq == 1000000);
	REQUIRE(scale == Catch::Approx(0.25));
	REQUIRE(raw == 1);
	ReadDDS(ctx, "TX2_I_F1", freq, scale, phase, raw);
	REQUIRE(raw == 1);

	//Negative frequency swaps I and Q, so the tone is below the LO
	sdr->SetTxToneFrequency(1, 1, -300000);
	sdr->BackgroundProcessing();
	ReadDDS(ctx, "TX2_I_F2", freq, scale, phase, raw);
	REQUIRE(freq == 300000);
	REQUIRE(phase == 0);
	ReadDDS(ctx, "TX2_Q_F2", freq, scale, phase, raw);
	REQUIRE(freq == 300000);
	REQUIRE(phase == 90000);
	REQUIRE(sdr->GetTxToneFrequency(1, 1) == -300000);

	//Turning a tone off: muted via amplitude, DDS stays enabled
	sdr->SetTxToneAmplitude(1, 1, 0);
	sdr->BackgroundProcessing();
	ReadDDS(ctx, "TX2_I_F2", freq, scale, phase, raw);
	REQUIRE(scale == Catch::Approx(0));
	REQUIRE(raw == 1);

	//Out of range requests are clamped
	sdr->SetTxToneFrequency(0, 0, 5000000);
	REQUIRE(sdr->GetTxToneFrequency(0, 0) == 1250000);
	sdr->SetTxToneFrequency(0, 0, -5000000);
	REQUIRE(sdr->GetTxToneFrequency(0, 0) == -1250000);
	sdr->SetTxToneAmplitude(0, 0, 2);
	REQUIRE(sdr->GetTxToneAmplitude(0, 0) == 1);
	sdr->SetTxToneAmplitude(0, 0, -1);
	REQUIRE(sdr->GetTxToneAmplitude(0, 0) == 0);

	//Bad indexes are ignored
	sdr->SetTxToneFrequency(2, 0, 1000);
	sdr->SetTxToneFrequency(0, 2, 1000);
	REQUIRE(sdr->GetTxToneFrequency(2, 0) == 0);
	REQUIRE(sdr->GetTxToneFrequency(0, 2) == 0);
}

TEST_CASE("IIOSDR_TransmitAttenuation")
{
	IIOContext* ctx;
	auto sdr = MakeSDR("mock:ad9361", ctx);

	//The radio reports a (negative) gain, we call it a positive attenuation. We adopt what it's doing.
	REQUIRE(sdr->GetTxAttenuation(0) == 10);
	REQUIRE(sdr->GetTxAttenuation(1) == 10);
	auto range = sdr->GetTxAttenuationRange(0);
	REQUIRE(range.first == 0);
	REQUIRE(range.second == 89.75f);

	//Each path is independent
	sdr->SetTxAttenuation(1, 20.5);
	REQUIRE(sdr->GetTxAttenuation(1) == 20.5);
	double gain;
	REQUIRE(ctx->ReadChannelAttrDouble(phy, "voltage1", true, "hardwaregain", gain));
	REQUIRE(gain == -10);
	sdr->BackgroundProcessing();
	REQUIRE(ctx->ReadChannelAttrDouble(phy, "voltage1", true, "hardwaregain", gain));
	REQUIRE(gain == -20.5);
	REQUIRE(ctx->ReadChannelAttrDouble(phy, "voltage0", true, "hardwaregain", gain));
	REQUIRE(gain == -10);
	REQUIRE(sdr->GetTxAttenuation(0) == 10);

	//The radio rounds to 0.25 dB and we report what it did
	sdr->SetTxAttenuation(0, 3.1);
	sdr->BackgroundProcessing();
	REQUIRE(sdr->GetTxAttenuation(0) == 3);

	//Out of range values are clamped
	sdr->SetTxAttenuation(0, -5);
	REQUIRE(sdr->GetTxAttenuation(0) == 0);
	sdr->SetTxAttenuation(0, 200);
	REQUIRE(sdr->GetTxAttenuation(0) == 89.75f);

	//Bad index is ignored
	sdr->SetTxAttenuation(2, 5);
	REQUIRE(sdr->GetTxAttenuation(2) == 0);

	//Saved in sessions
	sdr->SetTxAttenuation(0, 6);
	sdr->SetTxAttenuation(1, 12.25);
	sdr->BackgroundProcessing();
	IDTable table;
	auto node = sdr->SerializeConfiguration(table);

	IIOContext* ctx2;
	auto sdr2 = MakeSDR("mock:ad9361", ctx2);
	REQUIRE(sdr2->GetTxAttenuation(0) == 10);
	IDTable idmap;
	sdr2->LoadConfiguration(2, node, idmap);
	sdr2->BackgroundProcessing();
	REQUIRE(sdr2->GetTxAttenuation(0) == 6);
	REQUIRE(sdr2->GetTxAttenuation(1) == 12.25);
	REQUIRE(ctx2->ReadChannelAttrDouble(phy, "voltage1", true, "hardwaregain", gain));
	REQUIRE(gain == -12.25);
}

TEST_CASE("IIOSDR_TransmitLO")
{
	IIOContext* ctx;
	auto sdr = MakeSDR("mock:ad9361", ctx);

	//The TX LO is independent of the RX LO
	sdr->SetTxLOFrequency(915000000);
	REQUIRE(sdr->GetTxLOFrequency() == 915000000);
	sdr->BackgroundProcessing();

	int64_t v;
	REQUIRE(ctx->ReadChannelAttrInt(phy, "altvoltage1", true, "frequency", v));
	REQUIRE(v == 915000000);
	REQUIRE(ctx->ReadChannelAttrInt(phy, "altvoltage0", true, "frequency", v));
	REQUIRE(v == 2400000000);
	REQUIRE(sdr->GetCenterFrequency(0) == 2400000000);
	REQUIRE(sdr->GetTxLOFrequency() == 915000000);

	sdr->SetCenterFrequency(0, 433920000);
	sdr->BackgroundProcessing();
	REQUIRE(sdr->GetTxLOFrequency() == 915000000);

	//Clamped to what the radio can do
	sdr->SetTxLOFrequency(10);
	REQUIRE(sdr->GetTxLOFrequency() == 70000000);
	sdr->SetTxLOFrequency(20000000000);
	REQUIRE(sdr->GetTxLOFrequency() == 6000000000);
}

TEST_CASE("IIOSDR_TransmitFollowsSampleRate")
{
	IIOContext* ctx;
	auto sdr = MakeSDR("mock:ad9363", ctx);
	REQUIRE(sdr->GetTxChannelCount() == 1);

	//Faster sample rate lets the tones go higher
	sdr->SetSampleRate(10000000);
	sdr->SetTxToneFrequency(0, 0, 4000000);
	sdr->BackgroundProcessing();
	REQUIRE(sdr->GetTxToneFrequencyRange(0).second == 5000000);
	REQUIRE(sdr->GetTxToneFrequency(0, 0) == 4000000);

	int64_t freq;
	double scale;
	int64_t phase;
	int64_t raw;
	ReadDDS(ctx, "TX1_I_F1", freq, scale, phase, raw);
	REQUIRE(freq == 4000000);
}

TEST_CASE("IIOSDR_TransmitSessionRoundTrip")
{
	IIOContext* ctx;
	auto sdr = MakeSDR("mock:ad9361", ctx);

	sdr->SetTxLOFrequency(868000000);
	sdr->SetTxToneFrequency(0, 0, -123000);
	sdr->SetTxToneAmplitude(0, 0, 0.75);
	sdr->SetTxToneFrequency(1, 1, 456000);
	sdr->SetTxToneAmplitude(1, 1, 0.125);
	sdr->BackgroundProcessing();

	IDTable table;
	auto node = sdr->SerializeConfiguration(table);
	REQUIRE(node["tx"]);

	IIOContext* ctx2;
	auto sdr2 = MakeSDR("mock:ad9361", ctx2);
	REQUIRE(sdr2->GetTxLOFrequency() == 2400000000);
	IDTable idmap;
	sdr2->LoadConfiguration(2, node, idmap);
	sdr2->BackgroundProcessing();

	REQUIRE(sdr2->GetTxLOFrequency() == 868000000);
	REQUIRE(sdr2->GetTxToneFrequency(0, 0) == -123000);
	REQUIRE(sdr2->GetTxToneAmplitude(0, 0) == Catch::Approx(0.75));
	REQUIRE(sdr2->GetTxToneFrequency(1, 1) == 456000);
	REQUIRE(sdr2->GetTxToneAmplitude(1, 1) == Catch::Approx(0.125));

	//And it all made it to the radio
	int64_t freq;
	double scale;
	int64_t phase;
	int64_t raw;
	ReadDDS(ctx2, "TX1_Q_F1", freq, scale, phase, raw);
	REQUIRE(freq == 123000);
	REQUIRE(phase == 90000);
	REQUIRE(raw == 1);
	ReadDDS(ctx2, "TX2_I_F2", freq, scale, phase, raw);
	REQUIRE(freq == 456000);
	REQUIRE(raw == 1);
}

#endif
