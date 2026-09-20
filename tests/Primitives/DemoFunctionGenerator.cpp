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
	@brief Unit test for the simulated function generator driver
 */
#ifdef _CATCH2_V3
#include <catch2/catch_all.hpp>
#else
#include <catch2/catch.hpp>
#endif

#include "../../lib/scopehal/scopehal.h"
#include "Primitives.h"

using namespace std;

static shared_ptr<SCPIFunctionGenerator> CreateDemoGenerator()
{
	//Create it through the driver registry, like the application does
	auto transport = SCPITransport::CreateTransport("null", "");
	REQUIRE(transport != nullptr);
	auto gen = SCPIFunctionGenerator::CreateFunctionGenerator("demofuncgen", transport);
	REQUIRE(gen != nullptr);
	return gen;
}

TEST_CASE("Primitive_DemoFunctionGenerator")
{
	auto gen = CreateDemoGenerator();
	REQUIRE(gen->GetChannelCount() == 2);

	//Both channels have every optional control, and offer only shapes we can name
	for(int i=0; i<2; i++)
	{
		REQUIRE(gen->GetInstrumentTypesForChannel(i) == Instrument::INST_FUNCTION);
		REQUIRE(gen->HasFunctionDutyCycleControls(i));
		REQUIRE(gen->HasFunctionRiseFallTimeControls(i));
		REQUIRE(gen->HasFunctionImpedanceControls(i));

		auto shapes = gen->GetAvailableWaveformShapes(i);
		REQUIRE(!shapes.empty());
		REQUIRE(find(shapes.begin(), shapes.end(), FunctionGenerator::SHAPE_SINE) != shapes.end());
		REQUIRE(find(shapes.begin(), shapes.end(), FunctionGenerator::SHAPE_ARB) == shapes.end());
		for(auto s : shapes)
			REQUIRE(FunctionGenerator::GetShapeOfName(FunctionGenerator::GetNameOfShape(s)) == s);
	}

	//Settings read back, and channels are independent
	gen->SetFunctionChannelFrequency(0, 12345);
	gen->SetFunctionChannelFrequency(1, 54321);
	REQUIRE(gen->GetFunctionChannelFrequency(0) == 12345);
	REQUIRE(gen->GetFunctionChannelFrequency(1) == 54321);

	gen->SetFunctionChannelShape(1, FunctionGenerator::SHAPE_TRIANGLE);
	REQUIRE(gen->GetFunctionChannelShape(1) == FunctionGenerator::SHAPE_TRIANGLE);
	REQUIRE(gen->GetFunctionChannelShape(0) == FunctionGenerator::SHAPE_SINE);

	gen->SetFunctionChannelActive(0, false);
	gen->SetFunctionChannelActive(1, true);
	REQUIRE(!gen->GetFunctionChannelActive(0));
	REQUIRE(gen->GetFunctionChannelActive(1));

	gen->SetFunctionChannelOutputImpedance(0, FunctionGenerator::IMPEDANCE_HIGH_Z);
	gen->SetFunctionChannelOutputImpedance(1, FunctionGenerator::IMPEDANCE_50_OHM);
	REQUIRE(gen->GetFunctionChannelOutputImpedance(0) == FunctionGenerator::IMPEDANCE_HIGH_Z);
	REQUIRE(gen->GetFunctionChannelOutputImpedance(1) == FunctionGenerator::IMPEDANCE_50_OHM);

	//Out of range values are limited, like a real instrument would
	gen->SetFunctionChannelFrequency(0, 1e9);
	REQUIRE(gen->GetFunctionChannelFrequency(0) == 25e6f);
	gen->SetFunctionChannelFrequency(0, 0);
	REQUIRE(gen->GetFunctionChannelFrequency(0) > 0);

	gen->SetFunctionChannelAmplitude(0, 1000);
	REQUIRE(gen->GetFunctionChannelAmplitude(0) == 20);
	gen->SetFunctionChannelAmplitude(0, 0);
	REQUIRE(gen->GetFunctionChannelAmplitude(0) > 0);

	gen->SetFunctionChannelOffset(0, 100);
	REQUIRE(gen->GetFunctionChannelOffset(0) == 10);
	gen->SetFunctionChannelOffset(0, -100);
	REQUIRE(gen->GetFunctionChannelOffset(0) == -10);

	gen->SetFunctionChannelDutyCycle(0, 2);
	REQUIRE(gen->GetFunctionChannelDutyCycle(0) == 1);
	gen->SetFunctionChannelDutyCycle(0, -1);
	REQUIRE(gen->GetFunctionChannelDutyCycle(0) == 0);

	gen->SetFunctionChannelRiseTime(0, 0);
	REQUIRE(gen->GetFunctionChannelRiseTime(0) == 1e6f);
	gen->SetFunctionChannelFallTime(0, 1e30f);
	REQUIRE(gen->GetFunctionChannelFallTime(0) == 1e12f);

	//A shape it doesn't offer is ignored
	gen->SetFunctionChannelShape(0, FunctionGenerator::SHAPE_SQUARE);
	gen->SetFunctionChannelShape(0, FunctionGenerator::SHAPE_ARB);
	REQUIRE(gen->GetFunctionChannelShape(0) == FunctionGenerator::SHAPE_SQUARE);
}

TEST_CASE("Primitive_DemoFunctionGenerator_Serialization")
{
	auto gen = CreateDemoGenerator();

	gen->SetFunctionChannelActive(0, true);
	gen->SetFunctionChannelAmplitude(0, 3.5);
	gen->SetFunctionChannelOffset(0, -1.25);
	gen->SetFunctionChannelFrequency(0, 4321);
	gen->SetFunctionChannelShape(0, FunctionGenerator::SHAPE_SAWTOOTH_UP);
	gen->SetFunctionChannelDutyCycle(0, 0.75);
	gen->SetFunctionChannelRiseTime(0, 5e6);
	gen->SetFunctionChannelFallTime(0, 7e6);
	gen->SetFunctionChannelOutputImpedance(0, FunctionGenerator::IMPEDANCE_HIGH_Z);

	gen->SetFunctionChannelActive(1, false);
	gen->SetFunctionChannelAmplitude(1, 0.25);
	gen->SetFunctionChannelShape(1, FunctionGenerator::SHAPE_HAMMING);
	gen->SetFunctionChannelOutputImpedance(1, FunctionGenerator::IMPEDANCE_50_OHM);

	IDTable table;
	auto node = gen->SerializeConfiguration(table);

	//The saved configuration names the driver, so a session can recreate the instrument
	REQUIRE(node["driver"].as<string>() == "demofuncgen");
	REQUIRE(node["transport"].as<string>() == "null");

	//Load it into a fresh instance
	auto gen2 = CreateDemoGenerator();
	IDTable idmap;
	gen2->LoadConfiguration(2, node, idmap);

	for(int i=0; i<2; i++)
	{
		REQUIRE(gen2->GetFunctionChannelActive(i) == gen->GetFunctionChannelActive(i));
		REQUIRE(gen2->GetFunctionChannelAmplitude(i) == gen->GetFunctionChannelAmplitude(i));
		REQUIRE(gen2->GetFunctionChannelOffset(i) == gen->GetFunctionChannelOffset(i));
		REQUIRE(gen2->GetFunctionChannelFrequency(i) == gen->GetFunctionChannelFrequency(i));
		REQUIRE(gen2->GetFunctionChannelShape(i) == gen->GetFunctionChannelShape(i));
		REQUIRE(gen2->GetFunctionChannelDutyCycle(i) == gen->GetFunctionChannelDutyCycle(i));
		REQUIRE(gen2->GetFunctionChannelRiseTime(i) == gen->GetFunctionChannelRiseTime(i));
		REQUIRE(gen2->GetFunctionChannelFallTime(i) == gen->GetFunctionChannelFallTime(i));
		REQUIRE(gen2->GetFunctionChannelOutputImpedance(i) == gen->GetFunctionChannelOutputImpedance(i));
	}
}
