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
	@brief Unit tests for the IIO transport and the simulated (mock) IIO context
 */
#include "../../lib/scopehal/scopehal.h"

#ifdef HAS_IIO

#ifdef _CATCH2_V3
#include <catch2/catch_all.hpp>
#else
#include <catch2/catch.hpp>
#endif

#include "Primitives.h"

using namespace std;

static const char* phy = "ad9361-phy";

TEST_CASE("IIO_MockTransportIdentity")
{
	unique_ptr<SCPITransport> transport(SCPITransport::CreateTransport("iio", "mock:"));
	REQUIRE(transport != nullptr);
	REQUIRE(transport->GetName() == "iio");
	REQUIRE(transport->IsConnected());
	REQUIRE(transport->GetConnectionString() == "mock:");

	REQUIRE(transport->SendCommand("*IDN?"));
	string reply = transport->ReadReply();
	REQUIRE(reply == "Analog Devices,PlutoSDR Rev.B (Z7010-AD9363A),MOCK-ad9363-0001,v0.38");

	//Make sure SCPIDevice's parser would be happy with this
	char vendor[128] = "";
	char model[128] = "";
	char serial[128] = "";
	char version[128] = "";
	REQUIRE(4 == sscanf(reply.c_str(), "%127[^,],%127[^,],%127[^,],%127s", vendor, model, serial, version));
	REQUIRE(string(vendor) == "Analog Devices");

	//The reply is consumed once read
	REQUIRE(transport->ReadReply() == "");

	//Other SCPI is not supported
	REQUIRE(!transport->SendCommand("*RST"));

	auto iio = dynamic_cast<SCPIIIOTransport*>(transport.get());
	REQUIRE(iio != nullptr);
	REQUIRE(iio->GetContext() != nullptr);
}

TEST_CASE("IIO_BadUri")
{
	REQUIRE(IIOContext::Open("mock:nonsense") == nullptr);

	unique_ptr<SCPITransport> transport(SCPITransport::CreateTransport("iio", "mock:nonsense"));
	REQUIRE(transport != nullptr);
	REQUIRE(!transport->IsConnected());
}

TEST_CASE("IIO_MockTopology")
{
	auto ctx = IIOContext::Open("mock:ad9363");
	REQUIRE(ctx != nullptr);
	REQUIRE(ctx->GetAttributes()["hw_serial"] == "MOCK-ad9363-0001");

	REQUIRE(ctx->HasDevice(phy));
	REQUIRE(ctx->HasDevice("cf-ad9361-lpc"));
	REQUIRE(!ctx->HasDevice("nonexistent"));

	//1R1T
	REQUIRE(ctx->HasChannel(phy, "voltage0", false));
	REQUIRE(ctx->HasChannel(phy, "voltage0", true));
	REQUIRE(!ctx->HasChannel(phy, "voltage1", false));

	//LOs are output channels and can be found by ID or name
	REQUIRE(ctx->HasChannel(phy, "altvoltage0", true));
	REQUIRE(ctx->HasChannel(phy, "RX_LO", true));
	REQUIRE(!ctx->HasChannel(phy, "RX_LO", false));

	//2R2T
	auto ctx2 = IIOContext::Open("mock:ad9361");
	REQUIRE(ctx2 != nullptr);
	REQUIRE(ctx2->HasChannel(phy, "voltage1", false));
	REQUIRE(!ctx2->HasChannel(phy, "voltage2", false));
}

TEST_CASE("IIO_MockLO")
{
	auto ctx = IIOContext::Open("mock:");
	REQUIRE(ctx != nullptr);

	int64_t f;
	REQUIRE(ctx->ReadChannelAttrInt(phy, "altvoltage0", true, "frequency", f));
	REQUIRE(f == 2400000000);

	//Set by ID, read back by name
	REQUIRE(ctx->WriteChannelAttrInt(phy, "altvoltage0", true, "frequency", 915000000));
	REQUIRE(ctx->ReadChannelAttrInt(phy, "RX_LO", true, "frequency", f));
	REQUIRE(f == 915000000);

	//TX LO is independent
	REQUIRE(ctx->ReadChannelAttrInt(phy, "TX_LO", true, "frequency", f));
	REQUIRE(f == 2400000000);

	//Out of range (below the AD9363 minimum, above the maximum) is rejected and leaves the value alone
	REQUIRE(!ctx->WriteChannelAttrInt(phy, "altvoltage0", true, "frequency", 100000000));
	REQUIRE(!ctx->WriteChannelAttrInt(phy, "altvoltage0", true, "frequency", 5000000000));
	REQUIRE(ctx->ReadChannelAttrInt(phy, "altvoltage0", true, "frequency", f));
	REQUIRE(f == 915000000);

	//The AD9361 goes lower
	auto ctx2 = IIOContext::Open("mock:ad9361");
	REQUIRE(ctx2 != nullptr);
	REQUIRE(ctx2->WriteChannelAttrInt(phy, "altvoltage0", true, "frequency", 100000000));

	//Nonexistent things fail
	REQUIRE(!ctx->WriteChannelAttrInt(phy, "altvoltage0", true, "nonexistent", 1));
	REQUIRE(!ctx->WriteChannelAttrInt(phy, "altvoltage7", true, "frequency", 1));
	REQUIRE(!ctx->ReadChannelAttrInt(phy, "altvoltage0", false, "frequency", f));
}

TEST_CASE("IIO_MockRateAndBandwidth")
{
	auto ctx = IIOContext::Open("mock:");
	REQUIRE(ctx != nullptr);

	//Sample rate is shared between RX and TX
	int64_t rate;
	REQUIRE(ctx->WriteChannelAttrInt(phy, "voltage0", false, "sampling_frequency", 4000000));
	REQUIRE(ctx->ReadChannelAttrInt(phy, "voltage0", false, "sampling_frequency", rate));
	REQUIRE(rate == 4000000);
	REQUIRE(ctx->ReadChannelAttrInt(phy, "voltage0", true, "sampling_frequency", rate));
	REQUIRE(rate == 4000000);

	//Out of range rates are rejected
	REQUIRE(!ctx->WriteChannelAttrInt(phy, "voltage0", false, "sampling_frequency", 1000000));
	REQUIRE(!ctx->WriteChannelAttrInt(phy, "voltage0", false, "sampling_frequency", 100000000));
	REQUIRE(ctx->ReadChannelAttrInt(phy, "voltage0", false, "sampling_frequency", rate));
	REQUIRE(rate == 4000000);

	//Bandwidth is clamped
	int64_t bw;
	REQUIRE(ctx->WriteChannelAttrInt(phy, "voltage0", false, "rf_bandwidth", 10000000));
	REQUIRE(ctx->ReadChannelAttrInt(phy, "voltage0", false, "rf_bandwidth", bw));
	REQUIRE(bw == 10000000);
	REQUIRE(ctx->WriteChannelAttrInt(phy, "voltage0", false, "rf_bandwidth", 100000000));
	REQUIRE(ctx->ReadChannelAttrInt(phy, "voltage0", false, "rf_bandwidth", bw));
	REQUIRE(bw == 20000000);
	REQUIRE(ctx->WriteChannelAttrInt(phy, "voltage0", false, "rf_bandwidth", 1000));
	REQUIRE(ctx->ReadChannelAttrInt(phy, "voltage0", false, "rf_bandwidth", bw));
	REQUIRE(bw == 200000);

	//RX and TX bandwidth are independent
	REQUIRE(ctx->ReadChannelAttrInt(phy, "voltage0", true, "rf_bandwidth", bw));
	REQUIRE(bw == 2000000);
}

TEST_CASE("IIO_MockGain")
{
	auto ctx = IIOContext::Open("mock:");
	REQUIRE(ctx != nullptr);

	string mode;
	REQUIRE(ctx->ReadChannelAttr(phy, "voltage0", false, "gain_control_mode", mode));
	REQUIRE(mode == "slow_attack");

	//Can't set gain while AGC is running
	REQUIRE(!ctx->WriteChannelAttrDouble(phy, "voltage0", false, "hardwaregain", 30));

	//Bad mode names are rejected
	REQUIRE(!ctx->WriteChannelAttr(phy, "voltage0", false, "gain_control_mode", "bogus"));

	REQUIRE(ctx->WriteChannelAttr(phy, "voltage0", false, "gain_control_mode", "manual"));
	REQUIRE(ctx->WriteChannelAttrDouble(phy, "voltage0", false, "hardwaregain", 30));

	double gain;
	REQUIRE(ctx->ReadChannelAttrDouble(phy, "voltage0", false, "hardwaregain", gain));
	REQUIRE(fabs(gain - 30) < 1e-6);

	//Raw value has units, like the real driver
	string raw;
	REQUIRE(ctx->ReadChannelAttr(phy, "voltage0", false, "hardwaregain", raw));
	REQUIRE(raw == "30.000000 dB");

	//Out of range
	REQUIRE(!ctx->WriteChannelAttrDouble(phy, "voltage0", false, "hardwaregain", 100));

	//RSSI is read only
	double rssi;
	REQUIRE(ctx->ReadChannelAttrDouble(phy, "voltage0", false, "rssi", rssi));
	REQUIRE(!ctx->WriteChannelAttr(phy, "voltage0", false, "rssi", "1.0 dB"));
}

#endif
