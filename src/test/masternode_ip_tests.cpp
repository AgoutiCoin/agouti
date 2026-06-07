// Copyright (c) 2026 The Agouti developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "key.h"
#include "masternode.h"
#include "masternodeman.h"
#include "obfuscation.h"
#include "random.h"
#include "serialize.h"
#include "streams.h"
#include "timedata.h"
#include "version.h"

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(masternode_ip_tests)

static CTxIn MakeVin()
{
    CTxIn vin;
    vin.prevout.hash = GetRandHash();
    vin.prevout.n = 0;
    return vin;
}

// Add a fresh enabled masternode to the global manager with the given hot key.
static CMasternode* AddMasternode(const CTxIn& vin, const CService& addr, const CPubKey& pubKeyMN)
{
    CMasternode mn;
    mn.vin = vin;
    mn.addr = addr;
    mn.pubKeyMasternode = pubKeyMN;
    mn.activeState = CMasternode::MASTERNODE_ENABLED;
    mn.protocolVersion = PROTOCOL_VERSION;
    mn.sigTime = GetAdjustedTime();
    mnodeman.Add(mn);
    return mnodeman.Find(vin);
}

// Build and sign an IP update at a specific signing time.
static CMasternodeIPUpdate MakeSignedUpdate(const CTxIn& vin, const CService& addr,
                                            CKey key, CPubKey pub, int64_t nTime)
{
    SetMockTime(nTime);
    CMasternodeIPUpdate mnip(vin, addr);
    BOOST_CHECK(mnip.Sign(key, pub));
    return mnip;
}

BOOST_AUTO_TEST_CASE(accepts_fresh_update_and_mutates_addr)
{
    CKey key;
    key.MakeNewKey(true);
    CPubKey pub = key.GetPubKey();

    CTxIn vin = MakeVin();
    CService addrOld("1.2.3.4", 12345);
    CService addrNew("5.6.7.8", 12345);

    CMasternode* pmn = AddMasternode(vin, addrOld, pub);
    BOOST_REQUIRE(pmn != NULL);

    int64_t t = GetAdjustedTime();
    CMasternodeIPUpdate mnip = MakeSignedUpdate(vin, addrNew, key, pub, t);

    int nDos = 0;
    BOOST_CHECK(mnip.CheckAndUpdate(nDos));
    BOOST_CHECK_EQUAL(nDos, 0);
    BOOST_CHECK(pmn->addr == addrNew);
    BOOST_CHECK_EQUAL(pmn->nLastIPUpdateTime, mnip.sigTime);

    SetMockTime(0);
}

BOOST_AUTO_TEST_CASE(rejects_bad_signature)
{
    CKey key;
    key.MakeNewKey(true);
    CPubKey pub = key.GetPubKey();

    CTxIn vin = MakeVin();
    CService addrOld("1.2.3.4", 12345);
    CService addrNew("5.6.7.8", 12345);

    CMasternode* pmn = AddMasternode(vin, addrOld, pub);
    BOOST_REQUIRE(pmn != NULL);

    int64_t t = GetAdjustedTime();
    CMasternodeIPUpdate mnip = MakeSignedUpdate(vin, addrNew, key, pub, t);
    // Corrupt the signature.
    BOOST_REQUIRE(!mnip.vchSig.empty());
    mnip.vchSig[mnip.vchSig.size() - 1] ^= 0x01;

    int nDos = 0;
    BOOST_CHECK(!mnip.CheckAndUpdate(nDos));
    BOOST_CHECK_EQUAL(nDos, 100);
    BOOST_CHECK(pmn->addr == addrOld); // unchanged

    SetMockTime(0);
}

BOOST_AUTO_TEST_CASE(rejects_unknown_vin)
{
    CKey key;
    key.MakeNewKey(true);
    CPubKey pub = key.GetPubKey();

    CTxIn vin = MakeVin(); // never added to the manager
    CService addrNew("5.6.7.8", 12345);

    int64_t t = GetAdjustedTime();
    CMasternodeIPUpdate mnip = MakeSignedUpdate(vin, addrNew, key, pub, t);

    int nDos = 0;
    BOOST_CHECK(!mnip.CheckAndUpdate(nDos));
    BOOST_CHECK_EQUAL(nDos, 0); // benign, not a DoS

    SetMockTime(0);
}

BOOST_AUTO_TEST_CASE(idempotent_replay_of_latest)
{
    CKey key;
    key.MakeNewKey(true);
    CPubKey pub = key.GetPubKey();

    CTxIn vin = MakeVin();
    CService addrOld("1.2.3.4", 12345);
    CService addrNew("5.6.7.8", 12345);

    CMasternode* pmn = AddMasternode(vin, addrOld, pub);
    BOOST_REQUIRE(pmn != NULL);

    int64_t t = GetAdjustedTime();
    CMasternodeIPUpdate mnip = MakeSignedUpdate(vin, addrNew, key, pub, t);

    int nDos = 0;
    BOOST_CHECK(mnip.CheckAndUpdate(nDos)); // first apply
    mnodeman.UpdateLatestIPUpdate(mnip);

    // Direct replay of the same update: accepted, no churn, no DoS.
    nDos = 0;
    BOOST_CHECK(mnip.CheckAndUpdate(nDos));
    BOOST_CHECK_EQUAL(nDos, 0);
    BOOST_CHECK(pmn->addr == addrNew);
    BOOST_CHECK_EQUAL(pmn->nLastIPUpdateTime, mnip.sigTime);

    // Internal re-apply path must also be idempotent and DoS-free.
    BOOST_CHECK(mnodeman.ApplyLatestIPUpdate(vin));
    BOOST_CHECK(pmn->addr == addrNew);

    SetMockTime(0);
}

BOOST_AUTO_TEST_CASE(rejects_older_after_newer)
{
    CKey key;
    key.MakeNewKey(true);
    CPubKey pub = key.GetPubKey();

    CTxIn vin = MakeVin();
    CService addr0("1.2.3.4", 12345);
    CService addrA("5.6.7.8", 12345);
    CService addrB("9.9.9.9", 12345);

    CMasternode* pmn = AddMasternode(vin, addr0, pub);
    BOOST_REQUIRE(pmn != NULL);

    int64_t t1 = GetAdjustedTime();
    int64_t t2 = t1 + 600;

    CMasternodeIPUpdate mnipA = MakeSignedUpdate(vin, addrA, key, pub, t1);
    CMasternodeIPUpdate mnipB = MakeSignedUpdate(vin, addrB, key, pub, t2);

    // Apply newer (B) first.
    SetMockTime(t2);
    int nDos = 0;
    BOOST_CHECK(mnipB.CheckAndUpdate(nDos));
    mnodeman.UpdateLatestIPUpdate(mnipB);
    BOOST_CHECK(pmn->addr == addrB);

    // Now the older (A) must be rejected without DoS, addr stays B.
    nDos = 0;
    BOOST_CHECK(!mnipA.CheckAndUpdate(nDos));
    BOOST_CHECK_EQUAL(nDos, 0);
    BOOST_CHECK(pmn->addr == addrB);

    SetMockTime(0);
}

BOOST_AUTO_TEST_CASE(latest_per_vin_keeps_only_newest)
{
    CKey key;
    key.MakeNewKey(true);
    CPubKey pub = key.GetPubKey();

    CTxIn vin = MakeVin();
    CService addrA("5.6.7.8", 12345);
    CService addrB("9.9.9.9", 12345);

    int64_t t1 = 1000000;
    int64_t t2 = t1 + 600;

    CMasternodeIPUpdate mnipA = MakeSignedUpdate(vin, addrA, key, pub, t1);
    CMasternodeIPUpdate mnipB = MakeSignedUpdate(vin, addrB, key, pub, t2);

    CMasternodeMan man;
    man.UpdateLatestIPUpdate(mnipA);
    man.UpdateLatestIPUpdate(mnipB);
    // Re-applying the older one must not replace the latest.
    man.UpdateLatestIPUpdate(mnipA);

    BOOST_CHECK_EQUAL(man.mapLatestMasternodeIPUpdate.size(), 1U);
    BOOST_REQUIRE(man.mapLatestMasternodeIPUpdate.count(vin.prevout));
    BOOST_CHECK_EQUAL(man.mapLatestMasternodeIPUpdate[vin.prevout].sigTime, t2);
    BOOST_CHECK(man.mapLatestMasternodeIPUpdate[vin.prevout].addr == addrB);

    SetMockTime(0);
}

BOOST_AUTO_TEST_CASE(durable_latest_survives_serialization)
{
    CKey key;
    key.MakeNewKey(true);
    CPubKey pub = key.GetPubKey();

    CTxIn vin = MakeVin();
    CService addrB("9.9.9.9", 12345);

    int64_t t = 1000000;
    CMasternodeIPUpdate mnipB = MakeSignedUpdate(vin, addrB, key, pub, t);

    CMasternodeMan man;
    man.UpdateLatestIPUpdate(mnipB);

    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << man;

    CMasternodeMan man2;
    ss >> man2;

    BOOST_REQUIRE(man2.mapLatestMasternodeIPUpdate.count(vin.prevout));
    BOOST_CHECK_EQUAL(man2.mapLatestMasternodeIPUpdate[vin.prevout].sigTime, t);
    BOOST_CHECK(man2.mapLatestMasternodeIPUpdate[vin.prevout].addr == addrB);

    SetMockTime(0);
}

BOOST_AUTO_TEST_SUITE_END()
