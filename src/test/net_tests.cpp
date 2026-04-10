// Copyright (c) 2026 The Agouti developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "net.h"
#include "timedata.h"

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(net_tests)

BOOST_AUTO_TEST_CASE(outbound_connection_attempt_test)
{
    const int64_t nNow = GetAdjustedTime();

    CAddress routable(CService("8.8.8.8", Params().GetDefaultPort()));
    routable.nLastTry = 0;

    // Valid address with no issues should connect
    BOOST_CHECK(GetOutboundConnectionAttempt(routable, false, nNow, 1) == OUTBOUND_CONNECTION_CONNECT);

    // Invalid address (empty CAddress) must stop the loop
    BOOST_CHECK(GetOutboundConnectionAttempt(CAddress(), false, nNow, 1) == OUTBOUND_CONNECTION_STOP);

    // Over the 100-try limit must stop the loop
    BOOST_CHECK(GetOutboundConnectionAttempt(routable, false, nNow, 101) == OUTBOUND_CONNECTION_STOP);

    // Address whose network group is already connected must be skipped (not stop)
    // This is the core fix: previously this was a break, causing the loop to abort
    // and miss other valid candidates in addrman.
    BOOST_CHECK(GetOutboundConnectionAttempt(routable, true, nNow, 1) == OUTBOUND_CONNECTION_SKIP);

    // Our own address (IsLocal) must be skipped, not stop
    // Use a routable address and register it as local via AddLocal with LOCAL_MANUAL
    // score (bypasses the fDiscover guard and IsLimited check).
    CService localService("1.2.3.4", Params().GetDefaultPort());
    BOOST_CHECK(AddLocal(localService, LOCAL_MANUAL));
    CAddress local(localService);
    local.nLastTry = 0;
    BOOST_CHECK(GetOutboundConnectionAttempt(local, false, nNow, 1) == OUTBOUND_CONNECTION_SKIP);
    RemoveLocal(localService);

    // A limited network address must be skipped
    SetLimited(NET_IPV4, true);
    BOOST_CHECK(GetOutboundConnectionAttempt(routable, false, nNow, 1) == OUTBOUND_CONNECTION_SKIP);
    SetLimited(NET_IPV4, false); // restore

    // Very recently tried addresses are skipped until nTries >= 30
    CAddress recent(routable);
    recent.nLastTry = nNow;
    BOOST_CHECK(GetOutboundConnectionAttempt(recent, false, nNow, 1)  == OUTBOUND_CONNECTION_SKIP);
    BOOST_CHECK(GetOutboundConnectionAttempt(recent, false, nNow, 30) == OUTBOUND_CONNECTION_CONNECT);

    // Non-default port addresses are skipped until nTries >= 50
    CAddress nondefault(CService("9.9.9.9", Params().GetDefaultPort() + 1));
    nondefault.nLastTry = 0;
    BOOST_CHECK(GetOutboundConnectionAttempt(nondefault, false, nNow, 1)  == OUTBOUND_CONNECTION_SKIP);
    BOOST_CHECK(GetOutboundConnectionAttempt(nondefault, false, nNow, 50) == OUTBOUND_CONNECTION_CONNECT);
}

BOOST_AUTO_TEST_SUITE_END()
