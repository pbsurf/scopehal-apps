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
