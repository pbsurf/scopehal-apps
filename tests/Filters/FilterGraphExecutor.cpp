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
	@brief Unit tests for FilterGraphExecutor scheduling and shutdown
 */
#ifdef _CATCH2_V3
#include <catch2/catch_all.hpp>
#else
#include <catch2/catch.hpp>
#endif

#include "../../lib/scopehal/scopehal.h"
#include "../../lib/scopehal/FilterGraphExecutor.h"
#include "Filters.h"

using namespace std;

///@brief Counts its refreshes. With an upstream counter, also checks that the upstream ran first in the same run
class CounterFilter : public Filter
{
public:
	CounterFilter(const string& name, CounterFilter* upstream)
		: Filter("#ffffff", CAT_MATH)
		, m_upstream(upstream)
	{
		m_displayname = name;
		AddStream(Unit(Unit::UNIT_COUNTS), "count", Stream::STREAM_TYPE_ANALOG_SCALAR);
		if(upstream)
		{
			CreateInput("din");
			SetInput("din", StreamDescriptor(upstream, 0));
		}
	}

	virtual string GetProtocolDisplayName() override
	{ return "Counter"; }

	virtual void Refresh(vk::raii::CommandBuffer& /*cmdBuf*/, shared_ptr<QueueHandle> /*queue*/) override
	{
		//Take a little time so runs overlap with the workers' bookkeeping
		this_thread::sleep_for(chrono::microseconds(50));

		size_t n = ++m_count;
		if(m_upstream && (m_upstream->m_count.load() != n))
			m_orderErrors ++;
		SetScalarValue(0, n);
	}

	atomic<size_t> m_count{0};
	atomic<size_t> m_orderErrors{0};

protected:
	CounterFilter* m_upstream;
};

TEST_CASE("FilterGraphExecutor_RunBlocking")
{
	//Chain a -> b, plus an independent c
	auto a = new CounterFilter("a", nullptr);
	FilterReferencer aref(a);
	auto b = new CounterFilter("b", a);
	FilterReferencer bref(b);
	auto c = new CounterFilter("c", nullptr);
	FilterReferencer cref(c);

	FilterGraphExecutor exec;
	set<FlowGraphNode*> nodes = {a, b, c};

	//Every node must have run exactly once, in dependency order, by the time RunBlocking() returns.
	//The executor used to occasionally return early, leaving a run's filters unrun or still running.
	const size_t iters = 2000;
	for(size_t i=1; i<=iters; i++)
	{
		exec.RunBlocking(nodes);

		INFO("iteration " << i);
		REQUIRE(a->m_count == i);
		REQUIRE(b->m_count == i);
		REQUIRE(c->m_count == i);
		REQUIRE(b->m_orderErrors == 0);
	}
}

TEST_CASE("FilterGraphExecutor_Shutdown")
{
	auto a = new CounterFilter("a", nullptr);
	FilterReferencer aref(a);
	auto b = new CounterFilter("b", a);
	FilterReferencer bref(b);
	set<FlowGraphNode*> nodes = {a, b};

	//A worker waiting for work used to be able to miss both the last node's and the destructor's wakeup,
	//hanging the destructor in join()
	const size_t iters = 50;
	for(size_t i=1; i<=iters; i++)
	{
		FilterGraphExecutor exec;
		exec.RunBlocking(nodes);
		REQUIRE(b->m_count == i);
	}
}
