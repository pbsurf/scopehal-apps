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
	@brief Unit tests for ScalarStairstepFilter
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
#include "../../lib/scopehal/SCPISDR.h"
#include "../../lib/scopehal/SCPIIIOTransport.h"

#include <thread>

using namespace std;

/**
	@brief If the graph doesn't run for a while, the stairstep takes one step and carries on from there, rather than
	taking all of the steps it missed one after another
 */
TEST_CASE("Filter_ScalarStairstep_Stall")
{
	auto stair = dynamic_cast<ScalarStairstepFilter*>(Filter::CreateFilter("Scalar Stairstep", "#ffffff"));
	REQUIRE(stair != nullptr);
	FilterReferencer ref(stair);
	stair->GetParameter("Step interval").SetIntVal(50 * FS_PER_SECOND / 1000);

	FilterGraphExecutor exec;
	set<FlowGraphNode*> nodes;
	nodes.emplace(stair);

	//Nothing happens until the first interval is up
	exec.RunBlocking(nodes);
	float first = stair->GetScalarValue(0);

	//Miss about six steps
	this_thread::sleep_for(chrono::milliseconds(300));
	exec.RunBlocking(nodes);
	float second = stair->GetScalarValue(0);
	REQUIRE(second != first);

	//Run again right away: it's not time for the next step yet
	exec.RunBlocking(nodes);
	REQUIRE(stair->GetScalarValue(0) == second);
}

#ifdef HAS_IIO

/**
	@brief Sweeps the transmit LO of the simulated AD9361 with a stairstep
 */
TEST_CASE("Filter_ScalarStairstep_SDRTxLO")
{
	auto transport = dynamic_cast<SCPIIIOTransport*>(SCPITransport::CreateTransport("iio", "mock:ad9361"));
	REQUIRE(transport != nullptr);
	auto ctx = transport->GetContext();
	REQUIRE(ctx != nullptr);
	auto sdr = SCPISDR::CreateSDR("iio", transport);
	REQUIRE(sdr != nullptr);
	REQUIRE(sdr->GetTxChannelCount() == 2);

	//Only the first transmit path has an LO input, since the LO is shared
	size_t first = sdr->GetChannelCount() - sdr->GetTxChannelCount();
	auto tx1 = dynamic_cast<SDRTransmitChannel*>(sdr->GetChannel(first));
	auto tx2 = dynamic_cast<SDRTransmitChannel*>(sdr->GetChannel(first + 1));
	REQUIRE(tx1 != nullptr);
	REQUIRE(tx2 != nullptr);
	REQUIRE(tx1->GetInputCount() == 1);
	REQUIRE(tx2->GetInputCount() == 0);

	//900 to 930 MHz in 10 MHz steps, one step every time the graph runs
	auto stair = dynamic_cast<ScalarStairstepFilter*>(Filter::CreateFilter("Scalar Stairstep", "#ffffff"));
	REQUIRE(stair != nullptr);
	FilterReferencer ref(stair);
	stair->GetParameter("Unit").SetIntVal(Unit::UNIT_HZ);
	stair->GetParameter("Begin").SetFloatVal(900000000);
	stair->GetParameter("End").SetFloatVal(930000000);
	stair->GetParameter("Step count").SetIntVal(3);
	stair->GetParameter("Step interval").SetIntVal(1);

	//Only scalars can drive the LO, not waveforms
	REQUIRE(!tx1->ValidateChannel(SDRTransmitChannel::INPUT_LO, StreamDescriptor(sdr->GetChannel(0), 0)));
	REQUIRE(tx1->ValidateChannel(SDRTransmitChannel::INPUT_LO, StreamDescriptor(stair, 0)));
	tx1->SetInput(SDRTransmitChannel::INPUT_LO, StreamDescriptor(stair, 0));

	FilterGraphExecutor exec;
	set<FlowGraphNode*> nodes;
	nodes.emplace(stair);
	nodes.emplace(tx1);

	//Each run of the graph moves the LO to wherever the stairstep is, and the radio follows
	set<int64_t> seen;
	int64_t v;
	for(size_t i=0; i<6; i++)
	{
		exec.RunBlocking(nodes);
		int64_t lo = llround(stair->GetScalarValue(0));
		REQUIRE(sdr->GetTxLOFrequency() == lo);
		sdr->BackgroundProcessing();
		REQUIRE(ctx->ReadChannelAttrInt("ad9361-phy", "altvoltage1", true, "frequency", v));
		REQUIRE(v == lo);
		seen.emplace(lo);
	}
	REQUIRE(seen == set<int64_t>{900000000, 910000000, 920000000, 930000000});

	//The LO is only set when the input changes, so with the stairstep stopped a change made elsewhere sticks
	stair->Stop();
	sdr->SetTxLOFrequency(2400000000);
	exec.RunBlocking(nodes);
	REQUIRE(sdr->GetTxLOFrequency() == 2400000000);

	//Reconnecting sends the input again, even though it hasn't changed
	tx1->SetInput(SDRTransmitChannel::INPUT_LO, StreamDescriptor(nullptr, 0), true);
	tx1->SetInput(SDRTransmitChannel::INPUT_LO, StreamDescriptor(stair, 0));
	exec.RunBlocking(nodes);
	REQUIRE(sdr->GetTxLOFrequency() == llround(stair->GetScalarValue(0)));

	//Anything other than a frequency is ignored
	stair->GetParameter("Unit").SetIntVal(Unit::UNIT_VOLTS);
	stair->Run();
	sdr->SetTxLOFrequency(2400000000);
	exec.RunBlocking(nodes);
	REQUIRE(sdr->GetTxLOFrequency() == 2400000000);

	tx1->SetInput(SDRTransmitChannel::INPUT_LO, StreamDescriptor(nullptr, 0), true);
}

#endif
