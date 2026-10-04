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
	@brief Unit tests for formatting and parsing each type of Unit, including the special cases in their scaling
 */
#ifdef _CATCH2_V3
#include <catch2/catch_all.hpp>
#else
#include <catch2/catch.hpp>
#endif

#include "../../lib/scopehal/scopehal.h"
#include "Primitives.h"

using namespace std;

#define MU "\xce\xbc"

/**
	@brief Changes the '.' decimal marks in expected output to what the display locale uses

	PrettyPrintTabular() and PrettyPrintRange() always print for display.
 */
static string Localize(const string& expected)
{
	char separator = Unit(Unit::UNIT_VOLTS).PrettyPrintTabular(1.5, 1, 1)[1];
	string out = expected;
	for(auto& c : out)
	{
		if(c == '.')
			c = separator;
	}
	return out;
}

TEST_CASE("Unit_Types_PrettyPrint")
{
	struct Case
	{
		Unit::UnitType type;
		double value;
		const char* expected;		//PrettyPrint(value, -1)
		const char* expected4;		//PrettyPrint(value, 4)
	};
	const Case cases[] =
	{
		//SI prefixes
		{ Unit::UNIT_HZ,			999,			"999 Hz",			"999.0 Hz" },
		{ Unit::UNIT_HZ,			1000,			"1 kHz",			"1.0000 kHz" },
		{ Unit::UNIT_HZ,			12345678.9,		"12.34568 MHz",		"12.35 MHz" },
		{ Unit::UNIT_HZ,			2.4e9,			"2.4 GHz",			"2.400 GHz" },
		{ Unit::UNIT_HZ,			1e12,			"1 THz",			"1.0000 THz" },
		{ Unit::UNIT_HZ,			3e15,			"3000 THz",			"3000 THz" },
		{ Unit::UNIT_HZ,			0.002,			"2 mHz",			"2.000 mHz" },
		{ Unit::UNIT_VOLTS,			-1.5e6,			"-1.5 MV",			"-1.500 MV" },
		{ Unit::UNIT_AMPS,			1.5e6,			"1.5 MA",			"1.500 MA" },
		{ Unit::UNIT_OHMS,			1234.5,			"1.2345 k\xce\xa9",	"1.234 k\xce\xa9" },
		{ Unit::UNIT_RHO,			1.5,			"1.5 \xcf\x81",		"1.500 \xcf\x81" },
		{ Unit::UNIT_BITRATE,		1.5e6,			"1.5 Mbps",			"1.500 Mbps" },
		{ Unit::UNIT_SAMPLERATE,	1.5e6,			"1.5 MS/s",			"1.500 MS/s" },
		{ Unit::UNIT_SAMPLEDEPTH,	1.5e6,			"1.5 MS",			"1.500 MS" },
		{ Unit::UNIT_WATTS,			1234.5,			"1.2345 kW",		"1.234 kW" },
		{ Unit::UNIT_RPM,			1234.5,			"1.2345 kRPM",		"1.234 kRPM" },
		{ Unit::UNIT_FARADS,		0.002,			"2 mF",				"2.000 mF" },
		{ Unit::UNIT_VOLT_SEC,		1.5,			"1.5 Vs",			"1.500 Vs" },
		{ Unit::UNIT_COUNTS_SCI,	1234.5,			"1.2345 k#",		"1.234 k#" },

		//Units that are not SI base units, each with its own range of prefixes
		{ Unit::UNIT_FS,			999,			"999 fs",			"999.0 fs" },
		{ Unit::UNIT_FS,			1234.5,			"1.2345 ps",		"1.234 ps" },
		{ Unit::UNIT_FS,			-1.5e6,			"-1.5 ns",			"-1.500 ns" },
		{ Unit::UNIT_FS,			2.4e9,			"2.4 " MU "s",		"2.400 " MU "s" },
		{ Unit::UNIT_FS,			1e12,			"1 ms",				"1.0000 ms" },
		{ Unit::UNIT_FS,			3e15,			"3 s",				"3.000 s" },
		{ Unit::UNIT_PM,			1234.5,			"1.2345 nm",		"1.234 nm" },
		{ Unit::UNIT_PM,			1.5e6,			"1.5 " MU "m",		"1.500 " MU "m" },
		{ Unit::UNIT_PM,			2.4e9,			"2.4 mm",			"2.400 mm" },
		{ Unit::UNIT_PM,			1e12,			"1 m",				"1.0000 m" },
		{ Unit::UNIT_PM,			3e15,			"3 km",				"3.000 km" },
		{ Unit::UNIT_MICROVOLTS,	999,			"999 " MU "V",		"999.0 " MU "V" },
		{ Unit::UNIT_MICROVOLTS,	1000,			"1 mV",				"1.0000 mV" },
		{ Unit::UNIT_MICROVOLTS,	1.5e6,			"1.5 V",			"1.500 V" },
		{ Unit::UNIT_MICROVOLTS,	2.4e9,			"2.4 kV",			"2.400 kV" },
		{ Unit::UNIT_MICROVOLTS,	1e12,			"1 MV",				"1.0000 MV" },
		{ Unit::UNIT_MICROHZ,		1.5,			"1.5 " MU "Hz",		"1.500 " MU "Hz" },
		{ Unit::UNIT_MICROHZ,		1e12,			"1 MHz",			"1.0000 MHz" },
		{ Unit::UNIT_MICROHZ,		3e15,			"3 GHz",			"3.000 GHz" },
		{ Unit::UNIT_MICROAMPS,		1.5,			"1.5 " MU "A",		"1.500 " MU "A" },
		{ Unit::UNIT_MICROAMPS,		1234.5,			"1.2345 mA",		"1.234 mA" },
		{ Unit::UNIT_MICROAMPS,		1.5e6,			"1.5 A",			"1.500 A" },
		{ Unit::UNIT_MICROAMPS,		1e12,			"1 MA",				"1.0000 MA" },

		//No space before the prefix
		{ Unit::UNIT_UI,			1.5,			"1.5 UI",			"1.500 UI" },
		{ Unit::UNIT_UI,			1234.5,			"1.2345k UI",		"1.234k UI" },
		{ Unit::UNIT_UI,			0.002,			"2m UI",			"2.000m UI" },
		{ Unit::UNIT_COUNTS,		1234.5,			"1234.5",			"1234" },
		{ Unit::UNIT_COUNTS,		-1.5e6,			"-1500000",			"-1500000" },

		//No SI prefixes
		{ Unit::UNIT_PERCENT,		0.002,			"0.2 %",			"0.2000 %" },
		{ Unit::UNIT_PERCENT,		1.5,			"150 %",			"150.0 %" },
		{ Unit::UNIT_DB,			1234.5,			"1234.5 dB",		"1234 dB" },
		{ Unit::UNIT_DBM,			1.5,			"1.5 dBm",			"1.500 dBm" },
		{ Unit::UNIT_DEGREES,		1234.5,			"1234.5 \xc2\xb0",	"1234 \xc2\xb0" },
		{ Unit::UNIT_CELSIUS,		1.5,			"1.5 \xc2\xb0" "C",	"1.500 \xc2\xb0" "C" },
		{ Unit::UNIT_MILLIVOLTS,	1234.5,			"1234.5 mV",		"1234 mV" },

		//Binary prefixes for bytes
		{ Unit::UNIT_BYTES,			1000,			"1000 B",			"1000.0 B" },
		{ Unit::UNIT_BYTES,			1234.5,			"1.205566 kB",		"1.206 kB" },
		{ Unit::UNIT_BYTES,			1536,			"1.5 kB",			"1.500 kB" },
		{ Unit::UNIT_BYTES,			1048576,		"1 MB",				"1.0000 MB" },
		{ Unit::UNIT_BYTES,			1610612736,		"1.5 GB",			"1.500 GB" },

		//Special formats
		{ Unit::UNIT_HEXNUM,		1234.5,			"0x4d2",			"0x4d2" },
		{ Unit::UNIT_HEXNUM,		1536,			"0x600",			"0x600" },
		{ Unit::UNIT_LOG_BER,		0,				"1.00e+00",			"1.00e+00" },
		{ Unit::UNIT_LOG_BER,		1.5,			"3.16e+01",			"3.16e+01" },
		{ Unit::UNIT_RATIO_SCI,		1234.5,			"1.23e+03",			"1.23e+03" },
		{ Unit::UNIT_RATIO_SCI,		-1.5e6,			"-1.50e+06",		"-1.50e+06" },
		{ Unit::UNIT_RATIO_SCI,		0.002,			"2.00e-03",			"2.00e-03" },
		{ Unit::UNIT_VOLTS,			numeric_limits<double>::max(),	UNIT_OVERLOAD_LABEL,	UNIT_OVERLOAD_LABEL },
	};

	for(auto& c : cases)
	{
		Unit u(c.type);
		INFO("type " << u.ToString() << " value " << c.value);
		CHECK(u.PrettyPrint(c.value, -1, false) == c.expected);
		CHECK(u.PrettyPrint(c.value, 4, false) == c.expected4);
	}
}

TEST_CASE("Unit_Types_PrettyPrintTabular")
{
	struct Case
	{
		Unit::UnitType type;
		double value;
		const char* expected43;		//PrettyPrintTabular(value, 4, 3)
		const char* expected11;		//PrettyPrintTabular(value, 1, 1)
	};
	const Case cases[] =
	{
		{ Unit::UNIT_HZ,			1.5e6,			"1.500 MHz",		"1.5 MHz" },
		{ Unit::UNIT_FS,			12345678.9,		"12.346 ns",		"12.3 ns" },
		{ Unit::UNIT_PERCENT,		1.5,			"150.000 %",		"150.0 %" },
		{ Unit::UNIT_UI,			1234.5,			"1.234k UI",		"1.2k UI" },
		{ Unit::UNIT_CELSIUS,		1.5,			"1.500 \xc2\xb0" "C",	"1.5 \xc2\xb0" "C" },
		{ Unit::UNIT_BYTES,			1536,			"1.500 kB",			"1.5 kB" },
		{ Unit::UNIT_HEXNUM,		1536,			"0x600",			"0x600" },
		{ Unit::UNIT_RATIO_SCI,		1234.5,			"1.23e+03",			"1.23e+03" },
	};

	for(auto& c : cases)
	{
		Unit u(c.type);
		INFO("type " << u.ToString() << " value " << c.value);
		CHECK(u.PrettyPrintTabular(c.value, 4, 3) == Localize(c.expected43));
		CHECK(u.PrettyPrintTabular(c.value, 1, 1) == Localize(c.expected11));
	}
}

TEST_CASE("Unit_Types_PrettyPrintInt64")
{
	struct Case
	{
		Unit::UnitType type;
		int64_t value;
		const char* expected;		//PrettyPrintInt64(value, -1)
		const char* expected0;		//PrettyPrintInt64(value, 0)
		const char* expected9;		//PrettyPrintInt64(value, 9)
	};
	const Case cases[] =
	{
		{ Unit::UNIT_FS,			1500,				"1.5 ps",			"1 ps",			"1.5 ps" },
		{ Unit::UNIT_FS,			1234567891,			"1.2345 " MU "s",	"1 " MU "s",	"1.234567891 " MU "s" },
		{ Unit::UNIT_VOLTS,			1048576,			"1.0485 MV",		"1 MV",			"1.048576 MV" },
		{ Unit::UNIT_PM,			-1500000,			"-1.5 " MU "m",		"-1 " MU "m",	"-1.5 " MU "m" },
		{ Unit::UNIT_PM,			1000000000000000LL,	"1 km",				"1 km",			"1 km" },
		{ Unit::UNIT_MICROVOLTS,	1500,				"1.5 mV",			"1 mV",			"1.5 mV" },
		{ Unit::UNIT_MICROAMPS,		-1500000,			"-1.5 A",			"-1 A",			"-1.5 A" },
		{ Unit::UNIT_BYTES,			1500,				"1.4648 kB",		"1 kB",			"1.46484375 kB" },
		{ Unit::UNIT_BYTES,			1048576,			"1 MB",				"1 MB",			"1 MB" },
		{ Unit::UNIT_PERCENT,		1500,				"150000 %",			"150000 %",		"150000 %" },
		{ Unit::UNIT_DB,			-1500000,			"-1500000 dB",		"-1500000 dB",	"-1500000 dB" },
		{ Unit::UNIT_COUNTS,		1234567891,			"1234567891",		"1234567891",	"1234567891" },
		{ Unit::UNIT_UI,			1500,				"1.5k UI",			"1k UI",		"1.5k UI" },
		{ Unit::UNIT_HEXNUM,		1500,				"0x5dc",			"0x5dc",		"0x5dc" },
		{ Unit::UNIT_HEXNUM,		-1500000,			"0xffffffffffe91ca0",	"0xffffffffffe91ca0",	"0xffffffffffe91ca0" },
	};

	for(auto& c : cases)
	{
		Unit u(c.type);
		INFO("type " << u.ToString() << " value " << c.value);
		CHECK(u.PrettyPrintInt64(c.value, -1, false) == c.expected);
		CHECK(u.PrettyPrintInt64(c.value, 0, false) == c.expected0);
		CHECK(u.PrettyPrintInt64(c.value, 9, false) == c.expected9);
	}
}

TEST_CASE("Unit_Types_PrettyPrintRange")
{
	struct Case
	{
		Unit::UnitType type;
		double pixelMin;
		double pixelMax;
		double rangeMin;
		double rangeMax;
		const char* expected;
	};
	const Case cases[] =
	{
		//Mismatched digit after the decimal point, before it, a negative value, and zero in the pixel
		{ Unit::UNIT_VOLTS,		1.3979,		1.4152,		0,		2,			"1.4 V" },
		{ Unit::UNIT_VOLTS,		0.12345,	0.12399,	0,		1,			"0.1239 V" },
		{ Unit::UNIT_VOLTS,		-0.5,		-0.49,		-1,		1,			"-0.5 V" },
		{ Unit::UNIT_VOLTS,		-0.01,		0.01,		-1,		1,			"0 V" },

		//Prefix is chosen from the range, not the pixel
		{ Unit::UNIT_VOLTS,		125,		133,		0,		1000,		"0.13 kV" },
		{ Unit::UNIT_VOLTS,		1e6 + 100,	1e6 + 300,	0,		2e6,		"1.0003 MV" },
		{ Unit::UNIT_VOLTS,		4.9e-3,		5.1e-3,		0,		0.01,		"5 mV" },
		{ Unit::UNIT_FS,		2.5e9,		2.5001e9,	0,		5e9,		"2.5001 " MU "s" },
		{ Unit::UNIT_UI,		125,		133,		0,		1000,		"0.13k UI" },
		{ Unit::UNIT_PERCENT,	0.12345,	0.12399,	0,		1,			"12.39 %" },
		{ Unit::UNIT_BYTES,		4660,		4664,		0,		65536,		"4.554 kB" },
		{ Unit::UNIT_LOG_BER,	125,		133,		0,		1000,		"1e125" },
	};

	for(auto& c : cases)
	{
		Unit u(c.type);
		INFO("type " << u.ToString() << " pixel " << c.pixelMin << " to " << c.pixelMax);
		CHECK(u.PrettyPrintRange(c.pixelMin, c.pixelMax, c.rangeMin, c.rangeMax) == Localize(c.expected));
	}
}

TEST_CASE("Unit_Types_Parse")
{
	struct Case
	{
		Unit::UnitType type;
		const char* text;
		double expected;			//ParseString(text)
		int64_t expected64;			//ParseStringInt64(text), which truncates
	};
	const Case cases[] =
	{
		//Every prefix, with and without the unit
		{ Unit::UNIT_HZ,			"1.5 THz",		1.5e12,		1500000000000LL },
		{ Unit::UNIT_HZ,			"1.5 GHz",		1.5e9,		1500000000LL },
		{ Unit::UNIT_HZ,			"1.5 M",		1.5e6,		1500000LL },
		{ Unit::UNIT_HZ,			"1.5 kHz",		1.5e3,		1500LL },
		{ Unit::UNIT_HZ,			"1.5 K",		1.5e3,		1500LL },
		{ Unit::UNIT_HZ,			"-3k",			-3e3,		-3000LL },
		{ Unit::UNIT_HZ,			"1.5 mHz",		1.5e-3,		0LL },
		{ Unit::UNIT_HZ,			"1.5 uHz",		1.5e-6,		0LL },
		{ Unit::UNIT_HZ,			"1.5 " MU "Hz",	1.5e-6,		0LL },
		{ Unit::UNIT_HZ,			"1.5 n",		1.5e-9,		0LL },
		{ Unit::UNIT_HZ,			"1.5 pHz",		1.5e-12,	0LL },
		{ Unit::UNIT_HZ,			"1.5 f",		1.5e-15,	0LL },
		{ Unit::UNIT_HZ,			"42",			42,			42LL },
		{ Unit::UNIT_HZ,			"-2.5",			-2.5,		-2LL },

		//Units that are not SI base units
		{ Unit::UNIT_FS,			"42",			4.2e16,		42000000000000000LL },
		{ Unit::UNIT_FS,			"1.5 ns",		1.5e6,		1500000LL },
		{ Unit::UNIT_FS,			"1.5 p",		1500,		1500LL },
		{ Unit::UNIT_FS,			"-3u",			-3e9,		-3000000000LL },
		{ Unit::UNIT_FS,			"-3m",			-3e12,		-3000000000000LL },
		{ Unit::UNIT_PM,			"1.5 pm",		1.5,		1LL },
		{ Unit::UNIT_PM,			"-3n",			-3000,		-3000LL },
		{ Unit::UNIT_PM,			"1.5 m",		1.5e12,		1500000000000LL },	//meters, not milli
		{ Unit::UNIT_PM,			"1.5 k",		1.5e15,		1500000000000000LL },
		{ Unit::UNIT_MICROVOLTS,	"1.5 V",		1.5e6,		1500000LL },
		{ Unit::UNIT_MICROVOLTS,	"1.5 m",		1500,		1500LL },
		{ Unit::UNIT_MICROVOLTS,	"-3k",			-3e9,		-3000000000LL },
		{ Unit::UNIT_MICROHZ,		"1.5 Hz",		1.5e6,		1500000LL },
		{ Unit::UNIT_MICROHZ,		"1.5 G",		1.5e15,		1500000000000000LL },
		{ Unit::UNIT_MICROHZ,		"-3m",			-3000,		-3000LL },
		{ Unit::UNIT_MILLIVOLTS,	"1.5 mV",		1.5,		1LL },
		{ Unit::UNIT_MILLIVOLTS,	"1.5 k",		1500,		1500LL },

		//Percent is stored as a fraction
		{ Unit::UNIT_PERCENT,		"1.5 %",		0.015,		0LL },
		{ Unit::UNIT_PERCENT,		"1.5 k",		15,			15LL },

		//Binary prefixes for bytes
		{ Unit::UNIT_BYTES,			"1.5 kB",		1536,		1536LL },
		{ Unit::UNIT_BYTES,			"-3K",			-3072,		-3072LL },
		{ Unit::UNIT_BYTES,			"1.5 MB",		1572864,	1572864LL },
		{ Unit::UNIT_BYTES,			"1.5 GB",		1610612736,	1610612736LL },
		{ Unit::UNIT_BYTES,			"1.5 TB",		1649267441664.0,	1649267441664LL },
		{ Unit::UNIT_BYTES,			"1.5 m",		1.5e-3,		0LL },

		//Hex needs the 0x
		{ Unit::UNIT_HEXNUM,		"0x1f",			31,			31LL },
		{ Unit::UNIT_HEXNUM,		"0xffffffff",	4294967295.0,	4294967295LL },
		{ Unit::UNIT_HEXNUM,		"42",			0,			0LL },
	};

	for(auto& c : cases)
	{
		Unit u(c.type);
		INFO("type " << u.ToString() << " text \"" << c.text << "\"");
		CHECK(fabs(u.ParseString(c.text, false) - c.expected) <= 1e-12 * fabs(c.expected));
		CHECK(u.ParseStringInt64(c.text, false) == c.expected64);
	}

	//Overload is only known to the floating point parser
	CHECK(Unit(Unit::UNIT_VOLTS).ParseString(UNIT_OVERLOAD_LABEL, false) == numeric_limits<double>::max());
}

TEST_CASE("Unit_Types_RoundTrip")
{
	//What PrettyPrint() shows parses back to the value, to the precision shown
	const Unit::UnitType types[] =
	{
		Unit::UNIT_FS, Unit::UNIT_HZ, Unit::UNIT_VOLTS, Unit::UNIT_PERCENT, Unit::UNIT_DB, Unit::UNIT_DBM,
		Unit::UNIT_COUNTS, Unit::UNIT_UI, Unit::UNIT_DEGREES, Unit::UNIT_CELSIUS, Unit::UNIT_PM,
		Unit::UNIT_MILLIVOLTS, Unit::UNIT_MICROVOLTS, Unit::UNIT_MICROHZ, Unit::UNIT_BYTES
	};
	const double values[] = { 1.5, 1234.5, -1.5e6, 2.4e9, 0.002 };

	for(auto type : types)
	{
		Unit u(type);
		for(auto v : values)
		{
			string text = u.PrettyPrint(v, -1, false);
			INFO("type " << u.ToString() << " value " << v << " text \"" << text << "\"");
			CHECK(fabs(u.ParseString(text, false) - v) <= 1e-6 * fabs(v));
		}
	}
}
