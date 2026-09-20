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
	@brief Unit test for value formatting with SI prefixes, in particular that enough digits are shown to tell
	closely spaced values apart (e.g. when zoomed far in on an FFT)
 */
#ifdef _CATCH2_V3
#include <catch2/catch_all.hpp>
#else
#include <catch2/catch.hpp>
#endif

#include "../../lib/scopehal/scopehal.h"
#include "Primitives.h"

using namespace std;

TEST_CASE("Primitive_UnitPrettyPrintInt64")
{
	Unit hz(Unit::UNIT_HZ);
	Unit fs(Unit::UNIT_FS);

	//Values are formatted exactly, down to 1 Hz at gigahertz scale
	REQUIRE(hz.PrettyPrintInt64(1000000000, 9, false) == "1 GHz");
	REQUIRE(hz.PrettyPrintInt64(1000000001, 9, false) == "1.000000001 GHz");
	REQUIRE(hz.PrettyPrintInt64(1000400000, 9, false) == "1.0004 GHz");
	REQUIRE(hz.PrettyPrintInt64(1000999999, 9, false) == "1.000999999 GHz");
	REQUIRE(hz.PrettyPrintInt64(1001000000, 9, false) == "1.001 GHz");
	REQUIRE(hz.PrettyPrintInt64(1234567891, 9, false) == "1.234567891 GHz");

	//Trailing zeroes are trimmed
	REQUIRE(hz.PrettyPrintInt64(1500, 9, false) == "1.5 kHz");
	REQUIRE(hz.PrettyPrintInt64(100000000000, 9, false) == "100 GHz");

	//Time domain, including negative values
	REQUIRE(fs.PrettyPrintInt64(5000123456, 9, false) == "5.000123456 " "\xce\xbc" "s");
	REQUIRE(fs.PrettyPrintInt64(-1500000, 9, false) == "-1.5 ns");
	REQUIRE(fs.PrettyPrintInt64(0, 9, false) == "0 fs");
}

TEST_CASE("Primitive_UnitPrettyPrintInt64WithResolution")
{
	Unit hz(Unit::UNIT_HZ);
	Unit fs(Unit::UNIT_FS);

	//Digits below the resolution are dropped, and the value is rounded rather than truncated
	REQUIRE(hz.PrettyPrintInt64WithResolution(1234567891, 1, false) == "1.234567891 GHz");
	REQUIRE(hz.PrettyPrintInt64WithResolution(1234567891, 10, false) == "1.23456789 GHz");
	REQUIRE(hz.PrettyPrintInt64WithResolution(1234567891, 1000, false) == "1.234568 GHz");
	REQUIRE(hz.PrettyPrintInt64WithResolution(1234567891, 1e6, false) == "1.235 GHz");
	REQUIRE(hz.PrettyPrintInt64WithResolution(1234567891, 1e9, false) == "1 GHz");

	//A resolution that isn't a power of ten still shows enough digits to tell neighboring positions apart
	REQUIRE(hz.PrettyPrintInt64WithResolution(1234567891, 1500, false) == "1.234568 GHz");

	//Time domain and negative values
	REQUIRE(fs.PrettyPrintInt64WithResolution(5000123456, 1000, false) == "5.000123 " "\xce\xbc" "s");
	REQUIRE(fs.PrettyPrintInt64WithResolution(-1500000, 1000, false) == "-1.5 ns");

	//No usable resolution: as precise as possible
	REQUIRE(hz.PrettyPrintInt64WithResolution(1000000001, 0, false) == "1.000000001 GHz");
}

TEST_CASE("Primitive_UnitPrettyPrintInt64_MicroHz")
{
	//FFT frequency axes are in uHz, so a GHz is 1e15 and needs many more digits than a plain Hz axis does
	Unit uhz(Unit::UNIT_MICROHZ);

	REQUIRE(uhz.PrettyPrintInt64(1234567890123456LL, Unit::MAX_INT64_DECIMALS, false) == "1.234567890123456 GHz");
	REQUIRE(uhz.PrettyPrintInt64(1000000000000001LL, Unit::MAX_INT64_DECIMALS, false) == "1.000000000000001 GHz");
	REQUIRE(uhz.PrettyPrintInt64(1500000000LL, Unit::MAX_INT64_DECIMALS, false) == "1.5 kHz");

	//Small negative values keep their sign
	REQUIRE(uhz.PrettyPrintInt64(-1500000LL, Unit::MAX_INT64_DECIMALS, false) == "-1.5 Hz");

	//Resolution finer than 1 Hz above 1 GHz: 0.1 Hz per pixel
	REQUIRE(uhz.PrettyPrintInt64WithResolution(1234567890123456LL, 1e5, false) == "1.2345678901 GHz");
	REQUIRE(uhz.PrettyPrintInt64WithResolution(1234567890123456LL, 1e6, false) == "1.23456789 GHz");
	REQUIRE(uhz.PrettyPrintInt64WithResolution(1234567890123456LL, 1e9, false) == "1.234568 GHz");

	//Same on the other side of 1 GHz, for comparison
	REQUIRE(uhz.PrettyPrintInt64WithResolution(123456789012345LL, 1e5, false) == "123.456789 MHz");
}

TEST_CASE("Primitive_UnitPrettyPrint")
{
	Unit hz(Unit::UNIT_HZ);
	Unit volts(Unit::UNIT_VOLTS);

	//Small differences between large values must not be rounded away
	REQUIRE(hz.PrettyPrint(1000000000.0, -1, false) == "1 GHz");
	REQUIRE(hz.PrettyPrint(1000400000.0, -1, false) == "1.0004 GHz");
	REQUIRE(hz.PrettyPrint(1001000000.0, -1, false) == "1.001 GHz");
	REQUIRE(hz.PrettyPrint(1234567.0, -1, false) == "1.234567 MHz");

	//Floating point noise is still ignored
	REQUIRE(volts.PrettyPrint((double)0.1f, -1, false) == "100 mV");
	REQUIRE(volts.PrettyPrint(0.1 + 0.2, -1, false) == "300 mV");
	REQUIRE(volts.PrettyPrint(3.3, -1, false) == "3.3 V");
	REQUIRE(volts.PrettyPrint(3.3004, -1, false) == "3.3004 V");

	//Explicit number of digits is unchanged
	REQUIRE(hz.PrettyPrint(1234567.0, 4, false) == "1.235 MHz");
}
