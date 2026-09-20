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

	//The limits the radio publishes are used to clamp requests right away, so the UI shows the truth immediately.
	//This is an AD9363, which is more restricted than an AD9361.
	sdr->SetSpan(50000000);
	REQUIRE(sdr->GetSpan() == 20000000);
	sdr->SetCenterFrequency(0, 100000000);
	REQUIRE(sdr->GetCenterFrequency(0) == 325000000);
	sdr->SetCenterFrequency(0, 5000000000);
	REQUIRE(sdr->GetCenterFrequency(0) == 3800000000);
	sdr->SetCenterFrequency(0, 325000000);
	sdr->BackgroundProcessing();
	REQUIRE(ctx->ReadChannelAttrInt(phy, "altvoltage0", true, "frequency", v));
	REQUIRE(v == 325000000);
	REQUIRE(sdr->GetCenterFrequency(0) == 325000000);
	REQUIRE(ctx->ReadChannelAttrInt(phy, "voltage0", false, "rf_bandwidth", v));
	REQUIRE(v == 20000000);

	//The AD9361 goes lower and wider
	IIOContext* ctx2;
	auto sdr2 = MakeSDR("mock:ad9361", ctx2);
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
	REQUIRE(sdr->GetGain(0) == 71);

	//You can ask for a gain in AGC mode, but it doesn't reach the hardware until the mode changes
	sdr->SetGain(0, 30);
	REQUIRE(sdr->GetGain(0) == 30);
	sdr->BackgroundProcessing();
	double hw;
	string hwmode;
	REQUIRE(ctx->ReadChannelAttrDouble(phy, "voltage0", false, "hardwaregain", hw));
	REQUIRE(hw == 71);
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

TEST_CASE("IIOSDR_GainAffectsSignal")
{
	IIOContext* ctx;
	auto sdr = MakeSDR("mock:ad9363", ctx);
	auto chan = sdr->GetChannel(0);
	sdr->SetSampleDepth(4096);

	//In manual mode the mock's signal level follows the gain (20 dB is the reference, +/- 6 dB is 2x)
	sdr->SetGainMode(0, "manual");
	sdr->SetGain(0, 20);
	sdr->BackgroundProcessing();
	sdr->StartSingleTrigger();
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->PopPendingWaveform());
	double at20 = ToneMagnitude(chan->GetData(0), chan->GetData(1), 500000);
	REQUIRE(at20 > 0.45);
	REQUIRE(at20 < 0.55);

	sdr->SetGain(0, 14);
	sdr->BackgroundProcessing();
	sdr->StartSingleTrigger();
	REQUIRE(sdr->AcquireData());
	REQUIRE(sdr->PopPendingWaveform());
	double at14 = ToneMagnitude(chan->GetData(0), chan->GetData(1), 500000);
	REQUIRE(at14 > 0.22);
	REQUIRE(at14 < 0.28);
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

	//And it all made it to the radio
	int64_t v;
	double gain;
	REQUIRE(ctx2->ReadChannelAttrInt(phy, "altvoltage0", true, "frequency", v));
	REQUIRE(v == 915000000);
	REQUIRE(ctx2->ReadChannelAttrInt(phy, "voltage0", false, "rf_bandwidth", v));
	REQUIRE(v == 5000000);
	REQUIRE(ctx2->ReadChannelAttrDouble(phy, "voltage1", false, "hardwaregain", gain));
	REQUIRE(gain == 33);
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

#endif
