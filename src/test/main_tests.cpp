// Copyright (c) 2014 The Bitcoin Core developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "primitives/transaction.h"
#include "main.h"

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(main_tests)

CAmount nMoneySupplyPoWEnd = 43199500 * COIN;

namespace {

static CScript DummyStakeScript()
{
    return CScript() << OP_TRUE;
}

static CBlock MakeLegacyPoSBlock(int32_t nVersion)
{
    CBlock block;
    block.nVersion = nVersion;

    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript() << 1 << OP_0;
    coinbase.vout.resize(1);
    coinbase.vout[0].SetEmpty();

    CMutableTransaction coinstake;
    coinstake.vin.push_back(CTxIn(uint256S("01"), 0));
    coinstake.vout.resize(2);
    coinstake.vout[0].SetEmpty();
    coinstake.vout[1] = CTxOut(1 * COIN, DummyStakeScript());

    block.vtx.push_back(CTransaction(coinbase));
    block.vtx.push_back(CTransaction(coinstake));
    return block;
}

static CBlock MakeV5PoSBlock(bool withStakePointer)
{
    CBlock block;
    block.nVersion = 5;

    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript() << 1 << OP_0;
    coinbase.vout.resize(1);
    coinbase.vout[0].SetEmpty();

    CMutableTransaction coinstake;
    CTxIn marker;
    marker.prevout.SetNull();
    marker.scriptSig = CScript() << OP_PROOFOFSTAKE << 1;
    coinstake.vin.push_back(marker);
    coinstake.vout.resize(2);
    coinstake.vout[0].SetEmpty();
    coinstake.vout[1] = CTxOut(1 * COIN, DummyStakeScript());

    block.vtx.push_back(CTransaction(coinbase));
    block.vtx.push_back(CTransaction(coinstake));

    if (withStakePointer)
        block.stakePointer.hashBlock = uint256S("02");

    return block;
}

} // namespace

BOOST_AUTO_TEST_CASE(subsidy_limit_test)
{
    CAmount nSum = 0;
    for (int nHeight = 0; nHeight < 1; nHeight += 1) {
        /* premine in block 1 (60,001 AGU) */
        CAmount nSubsidy = GetBlockValue(nHeight);
        BOOST_CHECK(nSubsidy <= 60001 * COIN);
        nSum += nSubsidy;
    }

    for (int nHeight = 1; nHeight < 86400; nHeight += 1) {
        /* PoW Phase One */
        CAmount nSubsidy = GetBlockValue(nHeight);
        BOOST_CHECK(nSubsidy <= 250 * COIN);
        nSum += nSubsidy;
    }

    for (int nHeight = 86400; nHeight < 151200; nHeight += 1) {
        /* PoW Phase Two */
        CAmount nSubsidy = GetBlockValue(nHeight);
        BOOST_CHECK(nSubsidy <= 225 * COIN);
        nSum += nSubsidy;
    }

    for (int nHeight = 151200; nHeight < 259200; nHeight += 1) {
        /* PoW Phase Two */
        CAmount nSubsidy = GetBlockValue(nHeight);
        BOOST_CHECK(nSubsidy <= 45 * COIN);
        BOOST_CHECK(MoneyRange(nSubsidy));
        nSum += nSubsidy;
        BOOST_CHECK(nSum > 0 && nSum <= nMoneySupplyPoWEnd);
    }
    BOOST_CHECK(nSum == 4109975100000000ULL);
}

BOOST_AUTO_TEST_CASE(stakepointer_contextual_rejects_legacy_coinstake_after_fork)
{
    CBlockIndex prev;
    prev.nHeight = Params().StakePointerForkHeight() - 1;

    CValidationState state;
    CBlock block = MakeLegacyPoSBlock(5);

    BOOST_CHECK(!ContextualCheckBlock(block, state, &prev));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-cs-v5-required");
}

BOOST_AUTO_TEST_CASE(stakepointer_contextual_requires_pointer_after_fork)
{
    CBlockIndex prev;
    prev.nHeight = Params().StakePointerForkHeight() - 1;

    CValidationState state;
    CBlock block = MakeV5PoSBlock(false);

    BOOST_CHECK(!ContextualCheckBlock(block, state, &prev));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-stakepointer-missing");
}

BOOST_AUTO_TEST_CASE(stakepointer_checkstake_rejects_legacy_coinstake_after_fork)
{
    CBlockIndex prev;
    prev.nHeight = Params().StakePointerForkHeight() - 1;

    CBlock block = MakeLegacyPoSBlock(5);
    uint256 hashProofOfStake;

    LOCK(cs_main);
    BOOST_CHECK(!CheckStake(&prev, block, hashProofOfStake));
}

BOOST_AUTO_TEST_SUITE_END()
