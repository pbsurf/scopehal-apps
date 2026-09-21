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
	@brief Unit tests for stepping digits of numeric text (Unit::StepNumericText)
 */
#ifdef _CATCH2_V3
#include <catch2/catch_all.hpp>
#else
#include <catch2/catch.hpp>
#endif

#include "../../lib/scopehal/scopehal.h"
#include "Primitives.h"

using namespace std;

/**
	@brief Steps text with the cursor at the position of the '|' in the input, and checks the result which also has
	the new cursor position marked with '|'
 */
static void CheckStep(const string& in, bool up, const string& expected)
{
	auto cursor = in.find('|');
	REQUIRE(cursor != string::npos);
	string text = in;
	text.erase(cursor, 1);

	string out;
	int outCursor;
	INFO("input " << in << (up ? " up" : " down"));
	REQUIRE(Unit::StepNumericText(text, cursor, up, out, outCursor));
	out.insert(outCursor, "|");
	REQUIRE(out == expected);
}

static void CheckNoStep(const string& in)
{
	auto cursor = in.find('|');
	string text = in;
	text.erase(cursor, 1);

	string out = "unchanged";
	int outCursor = 1234;
	INFO("input " << in);
	REQUIRE(!Unit::StepNumericText(text, cursor, true, out, outCursor));
	REQUIRE(out == "unchanged");
	REQUIRE(outCursor == 1234);
}

TEST_CASE("Unit_StepNumericText_Basic")
{
	//Digit to the left of the cursor
	CheckStep("2.4| GHz", true, "2.5| GHz");
	CheckStep("2.4| GHz", false, "2.3| GHz");
	CheckStep("2|.4 GHz", true, "3|.4 GHz");
	CheckStep("2.|4 GHz", true, "3.|4 GHz");
	CheckStep("12|3 kHz", true, "13|3 kHz");
	CheckStep("1|23 kHz", true, "2|23 kHz");
	CheckStep("123| kHz", true, "124| kHz");

	//Fractional digits
	CheckStep("1.234| ms", true, "1.235| ms");
	CheckStep("1.23|4 ms", true, "1.24|4 ms");
	CheckStep("1.2|34 ms", true, "1.3|34 ms");

	//No unit, just a number
	CheckStep("100|", true, "101|");
	CheckStep("5|.5", true, "6|.5");

	//Cursor in front of the first digit steps the place above it
	CheckStep("|2.4 GHz", true, "1|2.4 GHz");
	CheckStep("|25 MHz", true, "1|25 MHz");
}

TEST_CASE("Unit_StepNumericText_CursorAfterNumber")
{
	//Immediately after the last digit steps that digit, as usual
	CheckStep("2.4| GHz", true, "2.5| GHz");
	CheckStep("500| kHz", true, "501| kHz");
	CheckStep("2.4|GHz", true, "2.5|GHz");

	//Beyond the number adds another decimal place and steps that, leaving the cursor after the new digit
	CheckStep("2.4 |GHz", true, "2.41| GHz");
	CheckStep("2.4 |GHz", false, "2.39| GHz");
	CheckStep("2.4 GHz|", true, "2.41| GHz");
	CheckStep("2.4 G|Hz", true, "2.41| GHz");
	CheckStep("2.4G|Hz", true, "2.41|GHz");

	//An integer gets a decimal mark as well
	CheckStep("500 |kHz", true, "500.1| kHz");
	CheckStep("500 |kHz", false, "499.9| kHz");
	CheckStep("500 kHz|", true, "500.1| kHz");
	CheckStep("5|", true, "6|");

	//Signs and zero
	CheckStep("-0.5 |V", false, "-0.51| V");
	CheckStep("-0.5 |V", true, "-0.49| V");
	CheckStep("0 |V", true, "0.1| V");
	CheckStep("0 |V", false, "-0.1| V");
	CheckStep("2.40 |GHz", true, "2.401| GHz");

	//Existing decimal mark is reused
	CheckStep("2,4 |GHz", true, "2,41| GHz");
	CheckStep("5. |Hz", true, "5.1| Hz");

	//Repeating keeps changing the same place, because the cursor is now inside the number
	string text = "2.4 GHz";
	int cursor = 7;
	for(int i=0; i<3; i++)
	{
		string out;
		int outCursor;
		REQUIRE(Unit::StepNumericText(text, cursor, true, out, outCursor));
		text = out;
		cursor = outCursor;
	}
	REQUIRE(text == "2.43 GHz");
	REQUIRE(cursor == 4);
	for(int i=0; i<5; i++)
	{
		string out;
		int outCursor;
		REQUIRE(Unit::StepNumericText(text, cursor, false, out, outCursor));
		text = out;
		cursor = outCursor;
	}
	REQUIRE(text == "2.38 GHz");
	REQUIRE(cursor == 4);
}

TEST_CASE("Unit_StepNumericText_Carry")
{
	//Carry into a new digit without changing the prefix. The cursor stays next to the digit it was next to.
	CheckStep("999| MHz", true, "1000| MHz");
	CheckStep("9|9 MHz", true, "10|9 MHz");
	CheckStep("99| MHz", true, "100| MHz");
	CheckStep("9.9| V", true, "10.0| V");
	CheckStep("0.99| V", true, "1.00| V");
	CheckStep("199| MHz", true, "200| MHz");

	//Borrow, and losing a digit
	CheckStep("1000| MHz", false, "999| MHz");
	CheckStep("100| MHz", false, "99| MHz");
	CheckStep("1.00| V", false, "0.99| V");
	CheckStep("10.0| V", false, "9.9| V");
	CheckStep("1|00 MHz", false, "|0 MHz");
}

TEST_CASE("Unit_StepNumericText_Zero")
{
	CheckStep("0| V", true, "1| V");
	CheckStep("0.0| V", true, "0.1| V");
	CheckStep("0.00| V", true, "0.01| V");

	//Down through zero flips the sign
	CheckStep("0| V", false, "-1| V");
	CheckStep("0.5| V", false, "0.4| V");
	CheckStep("0.05| V", false, "0.04| V");
	CheckStep("0.1| V", false, "0.0| V");
	CheckStep("0.0| V", false, "-0.1| V");
	CheckStep("0.00| V", false, "-0.01| V");
	CheckStep("1| V", false, "0| V");
	CheckStep("0.5| V", false, "0.4| V");

	//Stepping a bigger place than the value. Right after the decimal mark is the ones digit.
	CheckStep("2.|5 V", false, "1.|5 V");
	CheckStep("0.|5 V", false, "-0.|5 V");
	CheckStep("0.|1 V", false, "-0.|9 V");
}

TEST_CASE("Unit_StepNumericText_Negative")
{
	//Up/down move the value, not the magnitude
	CheckStep("-5| V", true, "-4| V");
	CheckStep("-5| V", false, "-6| V");
	CheckStep("-1| V", true, "0| V");
	CheckStep("-0.5| V", true, "-0.4| V");
	CheckStep("-0.|5 V", true, "0.|5 V");
	CheckStep("-9| V", false, "-10| V");
	CheckStep("-99| V", false, "-100| V");
	CheckStep("-100| V", true, "-99| V");

	//A leading plus sign is preserved
	CheckStep("+5| V", true, "+6| V");
}

TEST_CASE("Unit_StepNumericText_Formats")
{
	//Leading whitespace and prefixes are kept
	CheckStep("  12|3 kHz", true, "  13|3 kHz");
	CheckStep("2.4| μs", true, "2.5| μs");
	CheckStep("2.4| ms", true, "2.5| ms");

	//Comma as the decimal mark
	CheckStep("2,4| GHz", true, "2,5| GHz");
	CheckStep("2,|4 GHz", true, "3,|4 GHz");
	CheckStep("9,9| V", true, "10,0| V");

	//Trailing decimal mark and no integer digits
	CheckStep("5|. Hz", true, "6|. Hz");
	CheckStep(".5| V", true, "0.6| V");

	//Big numbers
	CheckStep("123456789012| Hz", true, "123456789013| Hz");
	CheckStep("99999999999| Hz", true, "100000000000| Hz");
}

TEST_CASE("Unit_StepNumericText_NotNumbers")
{
	CheckNoStep("|");
	CheckNoStep("Auto|");
	CheckNoStep("|-");
	CheckNoStep("-| V");
	CheckNoStep(".| V");
	CheckNoStep("Full|");
}

TEST_CASE("Unit_StepNumericText_RepeatedSteps")
{
	//Holding down the key: the cursor stays with the same digit and the string never reformats
	string text = "999 MHz";
	int cursor = 3;
	for(int i=0; i<5; i++)
	{
		string out;
		int outCursor;
		REQUIRE(Unit::StepNumericText(text, cursor, true, out, outCursor));
		text = out;
		cursor = outCursor;
	}
	REQUIRE(text == "1004 MHz");
	REQUIRE(cursor == 4);

	//What comes out is parseable and correct
	Unit hz(Unit::UNIT_HZ);
	REQUIRE(fabs(hz.ParseString(text) - 1.004e9) < 1);

	for(int i=0; i<10; i++)
	{
		string out;
		int outCursor;
		REQUIRE(Unit::StepNumericText(text, cursor, false, out, outCursor));
		text = out;
		cursor = outCursor;
	}
	REQUIRE(text == "994 MHz");
	REQUIRE(cursor == 3);
}
