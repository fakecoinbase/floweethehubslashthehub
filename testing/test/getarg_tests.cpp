/*
 * This file is part of the Flowee project
 * Copyright (C) 2012-2015 The Bitcoin Core developers
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "util.h"
#include "test/test_bitcoin.h"
#include <policy/policy.h>

#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>
#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(getarg_tests, BasicTestingSetup)

static void ResetArgs(const std::string& strArg)
{
    std::vector<std::string> vecArg;
    if (strArg.size())
      boost::split(vecArg, strArg, boost::is_space(), boost::token_compress_on);

    // Insert dummy executable name:
    vecArg.insert(vecArg.begin(), "testbitcoin");

    // Convert to char*:
    std::vector<const char*> vecChar;
    BOOST_FOREACH(std::string& s, vecArg)
        vecChar.push_back(s.c_str());

    ParseParameters(vecChar.size(), &vecChar[0], Settings::Hub());
}

BOOST_AUTO_TEST_CASE(boolarg)
{
    ResetArgs("-listen");
    BOOST_CHECK(GetBoolArg("-listen", false));
    BOOST_CHECK(GetBoolArg("-listen", true));

    BOOST_CHECK(!GetBoolArg("-fo", false));
    BOOST_CHECK(GetBoolArg("-fo", true));

    BOOST_CHECK(!GetBoolArg("-fooo", false));
    BOOST_CHECK(GetBoolArg("-fooo", true));

    for (auto strValue : std::list<std::string>{"0", "f", "n", "false", "no"}) {
        ResetArgs("-listen=" + strValue);
        BOOST_CHECK(!GetBoolArg("-listen", false));
        BOOST_CHECK(!GetBoolArg("-listen", true));
    }

    for (auto strValue : std::list<std::string>{"", "1", "t", "y", "true", "yes"}) {
        ResetArgs("-listen=" + strValue);
        BOOST_CHECK(GetBoolArg("-listen", false));
        BOOST_CHECK(GetBoolArg("-listen", true));
    }

    // New 0.6 feature: auto-map -nosomething to !-something:
    ResetArgs("-nolisten");
    BOOST_CHECK(!GetBoolArg("-listen", false));
    BOOST_CHECK(!GetBoolArg("-listen", true));

    ResetArgs("-nolisten=1");
    BOOST_CHECK(!GetBoolArg("-listen", false));
    BOOST_CHECK(!GetBoolArg("-listen", true));

    ResetArgs("-listen -nolisten");  // -nolisten should win
    BOOST_CHECK(!GetBoolArg("-listen", false));
    BOOST_CHECK(!GetBoolArg("-listen", true));

    ResetArgs("-listen=1 -nolisten=1");  // -nolisten should win
    BOOST_CHECK(!GetBoolArg("-listen", false));
    BOOST_CHECK(!GetBoolArg("-listen", true));

    ResetArgs("-listen=0 -nolisten=0");  // -nolisten=0 should win
    BOOST_CHECK(GetBoolArg("-listen", false));
    BOOST_CHECK(GetBoolArg("-listen", true));

    // New 0.6 feature: treat -- same as -:
    ResetArgs("--listen=1");
    BOOST_CHECK(GetBoolArg("-listen", false));
    BOOST_CHECK(GetBoolArg("-listen", true));

    ResetArgs("--nolisten=1");
    BOOST_CHECK(!GetBoolArg("-listen", false));
    BOOST_CHECK(!GetBoolArg("-listen", true));

    BOOST_CHECK_THROW(ResetArgs("-listen=text"), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(stringarg)
{
    ResetArgs("");
    BOOST_CHECK_EQUAL(GetArg("-uacomment", ""), "");
    BOOST_CHECK_EQUAL(GetArg("-uacomment", "eleven"), "eleven");

    ResetArgs("-connect -listen"); // -connect is an optional string argument
    BOOST_CHECK_EQUAL(GetArg("-connect", ""), "");
    BOOST_CHECK_EQUAL(GetArg("-connect", "eleven"), "");

    ResetArgs("-connect=");
    BOOST_CHECK_EQUAL(GetArg("-connect", ""), "");
    BOOST_CHECK_EQUAL(GetArg("-connect", "eleven"), "");

    ResetArgs("-uacomment=11");
    BOOST_CHECK_EQUAL(GetArg("-uacomment", ""), "11");
    BOOST_CHECK_EQUAL(GetArg("-uacomment", "eleven"), "11");

    ResetArgs("-uacomment=eleven");
    BOOST_CHECK_EQUAL(GetArg("-uacomment", ""), "eleven");
    BOOST_CHECK_EQUAL(GetArg("-uacomment", "eleven"), "eleven");

    BOOST_CHECK_THROW(ResetArgs("-uacomment"), std::runtime_error);
    BOOST_CHECK_THROW(ResetArgs("-uacomment="), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(intarg)
{
    ResetArgs("");
    BOOST_CHECK_EQUAL(GetArg("-maxconnections", 11), 11);
    BOOST_CHECK_EQUAL(GetArg("-maxconnections", 0), 0);

    ResetArgs("-maxconnections"); // -maxconnections is an optional int argument
    BOOST_CHECK_EQUAL(GetArg("-maxconnections", 11), 0);

    ResetArgs("-maxconnections=11 -maxreceivebuffer=12");
    BOOST_CHECK_EQUAL(GetArg("-maxconnections", 0), 11);
    BOOST_CHECK_EQUAL(GetArg("-maxreceivebuffer", 11), 12);

    ResetArgs("-conf=-1");
    BOOST_CHECK_EQUAL(GetArg("-conf", 0), -1);

    BOOST_CHECK_THROW(ResetArgs("-maxreceivebuffer"), std::runtime_error);
    BOOST_CHECK_THROW(ResetArgs("-maxreceivebuffer="), std::runtime_error);
    BOOST_CHECK_THROW(ResetArgs("-maxreceivebuffer=NaN"), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(doubledash)
{
    ResetArgs("--listen");
    BOOST_CHECK_EQUAL(GetBoolArg("-listen", false), true);

    ResetArgs("--uacomment=verbose --maxconnections=1");
    BOOST_CHECK_EQUAL(GetArg("-uacomment", ""), "verbose");
    BOOST_CHECK_EQUAL(GetArg("-maxconnections", 0), 1);
}

BOOST_AUTO_TEST_CASE(boolargno)
{
    ResetArgs("-nolisten");
    BOOST_CHECK(!GetBoolArg("-listen", true));
    BOOST_CHECK(!GetBoolArg("-listen", false));

    ResetArgs("-nolisten=1");
    BOOST_CHECK(!GetBoolArg("-listen", true));
    BOOST_CHECK(!GetBoolArg("-listen", false));

    ResetArgs("-nolisten=0");
    BOOST_CHECK(GetBoolArg("-listen", true));
    BOOST_CHECK(GetBoolArg("-listen", false));

    ResetArgs("-listen --nolisten"); // --nolisten should win
    BOOST_CHECK(!GetBoolArg("-listen", true));
    BOOST_CHECK(!GetBoolArg("-listen", false));

    ResetArgs("-nolisten -listen"); // -listen always wins:
    BOOST_CHECK(GetBoolArg("-listen", true));
    BOOST_CHECK(GetBoolArg("-listen", false));
}

BOOST_AUTO_TEST_CASE(blockSizeAcceptLimit)
{
    ResetArgs("-excessiveblocksize=5004000");
    BOOST_CHECK_EQUAL(Policy::blockSizeAcceptLimit(), 5004000);

    ResetArgs("-blocksizeacceptlimitbytes=5004000");
    BOOST_CHECK_EQUAL(Policy::blockSizeAcceptLimit(), 5004000);

    // blocksizeacceptlimit always wins
    ResetArgs("-excessiveblocksize=5004000 -blocksizeacceptlimit=1.2");
    BOOST_CHECK_EQUAL(Policy::blockSizeAcceptLimit(), 1200000);

    // blocksizeacceptlimit always wins
    ResetArgs("-blocksizeacceptlimitbytes=5004000 -blocksizeacceptlimit=1.2");
    BOOST_CHECK_EQUAL(Policy::blockSizeAcceptLimit(), 1200000);

    // blocksizeacceptlimitbytes always wins
    ResetArgs("-excessiveblocksize=5004000 -blocksizeacceptlimitbytes=6004000");
    BOOST_CHECK_EQUAL(Policy::blockSizeAcceptLimit(), 6004000);

    ResetArgs("-blocksizeacceptlimit=1.2");
    BOOST_CHECK_EQUAL(Policy::blockSizeAcceptLimit(), 1200000);

    ResetArgs("-blocksizeacceptlimit=1.25");
    BOOST_CHECK_EQUAL(Policy::blockSizeAcceptLimit(), 1200000);
}

BOOST_AUTO_TEST_CASE(unrecognizedArgs)
{
    BOOST_CHECK_THROW(ResetArgs("-unrecognized_arg"), std::runtime_error);
    BOOST_CHECK_THROW(ResetArgs("-listen -unrecognized_arg"), std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
