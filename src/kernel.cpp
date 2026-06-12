/* @flow */
// Copyright (c) 2012-2013 The PPCoin developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2018-2022 The Crown developers
// Copyright (c) 2022 The Agouti developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/assign/list_of.hpp>
#include <boost/lexical_cast.hpp>

#include "db.h"
#include "kernel.h"
#include "script/interpreter.h"
#include "timedata.h"
#include "util.h"

using namespace std;

bool fTestNet = false; //Params().NetworkID() == CBaseChainParams::TESTNET;

// Modifier interval: time to elapse before new modifier is computed
// Set to 3-hour for production network and 20-minute for test network
unsigned int nModifierInterval;
int nStakeTargetSpacing = 60;
unsigned int getIntervalVersion(bool fTestNet)
{
    if (fTestNet)
        return MODIFIER_INTERVAL_TESTNET;
    else
        return MODIFIER_INTERVAL;
}

// Hard checkpoints of stake modifiers to ensure they are deterministic
static std::map<int, unsigned int> mapStakeModifierCheckpoints =
    boost::assign::map_list_of(0, 0xfd11f4e7u);

// Get time weight
int64_t GetWeight(int64_t nIntervalBeginning, int64_t nIntervalEnd)
{
    return nIntervalEnd - nIntervalBeginning - Params().StakeMinAge();
}

// Get the last stake modifier and its generation time from a given block
static bool GetLastStakeModifier(const CBlockIndex* pindex, uint64_t& nStakeModifier, int64_t& nModifierTime)
{
    if (!pindex)
        return error("GetLastStakeModifier: null pindex");
    while (pindex && pindex->pprev && !pindex->GeneratedStakeModifier())
        pindex = pindex->pprev;
    if (!pindex->GeneratedStakeModifier())
        return error("GetLastStakeModifier: no generation at genesis block");
    nStakeModifier = pindex->nStakeModifier;
    nModifierTime = pindex->GetBlockTime();
    return true;
}

// Get selection interval section (in seconds)
static int64_t GetStakeModifierSelectionIntervalSection(int nSection)
{
    assert(nSection >= 0 && nSection < 64);
    int64_t a = getIntervalVersion(fTestNet) * 63 / (63 + ((63 - nSection) * (MODIFIER_INTERVAL_RATIO - 1)));
    return a;
}

// Get stake modifier selection interval (in seconds)
static int64_t GetStakeModifierSelectionInterval()
{
    int64_t nSelectionInterval = 0;
    for (int nSection = 0; nSection < 64; nSection++) {
        nSelectionInterval += GetStakeModifierSelectionIntervalSection(nSection);
    }
    return nSelectionInterval;
}

// select a block from the candidate blocks in vSortedByTimestamp, excluding
// already selected blocks in vSelectedBlocks, and with timestamp up to
// nSelectionIntervalStop.
static bool SelectBlockFromCandidates(
    vector<pair<int64_t, uint256> >& vSortedByTimestamp,
    map<uint256, const CBlockIndex*>& mapSelectedBlocks,
    int64_t nSelectionIntervalStop,
    uint64_t nStakeModifierPrev,
    const CBlockIndex** pindexSelected)
{
    bool fModifierV2 = false;
    bool fFirstRun = true;
    bool fSelected = false;
    uint256 hashBest = 0;
    *pindexSelected = (const CBlockIndex*)0;
    BOOST_FOREACH (const PAIRTYPE(int64_t, uint256) & item, vSortedByTimestamp) {
        if (!mapBlockIndex.count(item.second))
            return error("SelectBlockFromCandidates: failed to find block index for candidate block %s", item.second.ToString().c_str());

        const CBlockIndex* pindex = mapBlockIndex[item.second];
        if (fSelected && pindex->GetBlockTime() > nSelectionIntervalStop)
            break;

        //if the lowest block height (vSortedByTimestamp[0]) is >= switch height, use new modifier calc
        if (fFirstRun){
            fModifierV2 = pindex->nHeight >= Params().ModifierUpgradeBlock();
            fFirstRun = false;
        }

        if (mapSelectedBlocks.count(pindex->GetBlockHash()) > 0)
            continue;

        // compute the selection hash by hashing an input that is unique to that block
        uint256 hashProof;
        if(fModifierV2)
            hashProof = pindex->GetBlockHash();
        else
            hashProof = pindex->IsProofOfStake() ? 0 : pindex->GetBlockHash();

        CDataStream ss(SER_GETHASH, 0);
        ss << hashProof << nStakeModifierPrev;
        uint256 hashSelection = Hash(ss.begin(), ss.end());

        // the selection hash is divided by 2**32 so that proof-of-stake block
        // is always favored over proof-of-work block. this is to preserve
        // the energy efficiency property
        if (pindex->IsProofOfStake())
            hashSelection >>= 32;

        if (fSelected && hashSelection < hashBest) {
            hashBest = hashSelection;
            *pindexSelected = (const CBlockIndex*)pindex;
        } else if (!fSelected) {
            fSelected = true;
            hashBest = hashSelection;
            *pindexSelected = (const CBlockIndex*)pindex;
        }
    }
    if (GetBoolArg("-printstakemodifier", false))
        LogPrintf("SelectBlockFromCandidates: selection hash=%s\n", hashBest.ToString().c_str());
    return fSelected;
}

// Stake Modifier (hash modifier of proof-of-stake):
// The purpose of stake modifier is to prevent a txout (coin) owner from
// computing future proof-of-stake generated by this txout at the time
// of transaction confirmation. To meet kernel protocol, the txout
// must hash with a future stake modifier to generate the proof.
// Stake modifier consists of bits each of which is contributed from a
// selected block of a given block group in the past.
// The selection of a block is based on a hash of the block's proof-hash and
// the previous stake modifier.
// Stake modifier is recomputed at a fixed time interval instead of every
// block. This is to make it difficult for an attacker to gain control of
// additional bits in the stake modifier, even after generating a chain of
// blocks.
bool ComputeNextStakeModifier(const CBlockIndex* pindexPrev, uint64_t& nStakeModifier, bool& fGeneratedStakeModifier)
{
    nStakeModifier = 0;
    fGeneratedStakeModifier = false;
    if (!pindexPrev) {
        fGeneratedStakeModifier = true;
        return true; // genesis block's modifier is 0
    }
    if (pindexPrev->nHeight == 0) {
        //Give a stake modifier to the first block
        fGeneratedStakeModifier = true;
        nStakeModifier = uint64_t("stakemodifier");
        return true;
    }

    // First find current stake modifier and its generation block time
    // if it's not old enough, return the same stake modifier
    int64_t nModifierTime = 0;
    if (!GetLastStakeModifier(pindexPrev, nStakeModifier, nModifierTime))
        return error("ComputeNextStakeModifier: unable to get last modifier");

    if (GetBoolArg("-printstakemodifier", false))
        LogPrintf("ComputeNextStakeModifier: prev modifier= %s time=%s\n", boost::lexical_cast<std::string>(nStakeModifier).c_str(), DateTimeStrFormat("%Y-%m-%d %H:%M:%S", nModifierTime).c_str());

    if (nModifierTime / getIntervalVersion(fTestNet) >= pindexPrev->GetBlockTime() / getIntervalVersion(fTestNet))
        return true;

    // Sort candidate blocks by timestamp
    vector<pair<int64_t, uint256> > vSortedByTimestamp;
    vSortedByTimestamp.reserve(64 * getIntervalVersion(fTestNet) / nStakeTargetSpacing);
    int64_t nSelectionInterval = GetStakeModifierSelectionInterval();
    int64_t nSelectionIntervalStart = (pindexPrev->GetBlockTime() / getIntervalVersion(fTestNet)) * getIntervalVersion(fTestNet) - nSelectionInterval;
    const CBlockIndex* pindex = pindexPrev;

    while (pindex && pindex->GetBlockTime() >= nSelectionIntervalStart) {
        vSortedByTimestamp.push_back(make_pair(pindex->GetBlockTime(), pindex->GetBlockHash()));
        pindex = pindex->pprev;
    }

    int nHeightFirstCandidate = pindex ? (pindex->nHeight + 1) : 0;
    reverse(vSortedByTimestamp.begin(), vSortedByTimestamp.end());
    sort(vSortedByTimestamp.begin(), vSortedByTimestamp.end());

    // Select 64 blocks from candidate blocks to generate stake modifier
    uint64_t nStakeModifierNew = 0;
    int64_t nSelectionIntervalStop = nSelectionIntervalStart;
    map<uint256, const CBlockIndex*> mapSelectedBlocks;
    for (int nRound = 0; nRound < min(64, (int)vSortedByTimestamp.size()); nRound++) {
        // add an interval section to the current selection round
        nSelectionIntervalStop += GetStakeModifierSelectionIntervalSection(nRound);

        // select a block from the candidates of current round
        if (!SelectBlockFromCandidates(vSortedByTimestamp, mapSelectedBlocks, nSelectionIntervalStop, nStakeModifier, &pindex))
            return error("ComputeNextStakeModifier: unable to select block at round %d", nRound);

        // write the entropy bit of the selected block
        nStakeModifierNew |= (((uint64_t)pindex->GetStakeEntropyBit()) << nRound);

        // add the selected block from candidates to selected list
        mapSelectedBlocks.insert(make_pair(pindex->GetBlockHash(), pindex));
        if (fDebug || GetBoolArg("-printstakemodifier", false))
            LogPrintf("ComputeNextStakeModifier: selected round %d stop=%s height=%d bit=%d\n",
                nRound, DateTimeStrFormat("%Y-%m-%d %H:%M:%S", nSelectionIntervalStop).c_str(), pindex->nHeight, pindex->GetStakeEntropyBit());
    }

    // Print selection map for visualization of the selected blocks
    if (fDebug || GetBoolArg("-printstakemodifier", false)) {
        string strSelectionMap = "";
        // '-' indicates proof-of-work blocks not selected
        strSelectionMap.insert(0, pindexPrev->nHeight - nHeightFirstCandidate + 1, '-');
        pindex = pindexPrev;
        while (pindex && pindex->nHeight >= nHeightFirstCandidate) {
            // '=' indicates proof-of-stake blocks not selected
            if (pindex->IsProofOfStake())
                strSelectionMap.replace(pindex->nHeight - nHeightFirstCandidate, 1, "=");
            pindex = pindex->pprev;
        }
        BOOST_FOREACH (const PAIRTYPE(uint256, const CBlockIndex*) & item, mapSelectedBlocks) {
            // 'S' indicates selected proof-of-stake blocks
            // 'W' indicates selected proof-of-work blocks
            strSelectionMap.replace(item.second->nHeight - nHeightFirstCandidate, 1, item.second->IsProofOfStake() ? "S" : "W");
        }
        LogPrintf("ComputeNextStakeModifier: selection height [%d, %d] map %s\n", nHeightFirstCandidate, pindexPrev->nHeight, strSelectionMap.c_str());
    }
    if (fDebug || GetBoolArg("-printstakemodifier", false)) {
        LogPrintf("ComputeNextStakeModifier: new modifier=%s time=%s\n", boost::lexical_cast<std::string>(nStakeModifierNew).c_str(), DateTimeStrFormat("%Y-%m-%d %H:%M:%S", pindexPrev->GetBlockTime()).c_str());
    }

    nStakeModifier = nStakeModifierNew;
    fGeneratedStakeModifier = true;
    return true;
}

// The stake modifier used to hash for a stake kernel is chosen as the stake
// modifier about a selection interval later than the coin generating the kernel
bool GetKernelStakeModifier(uint256 hashBlockFrom, uint64_t& nStakeModifier, int& nStakeModifierHeight, int64_t& nStakeModifierTime, bool fPrintProofOfStake)
{
    nStakeModifier = 0;
    if (!mapBlockIndex.count(hashBlockFrom))
        return error("GetKernelStakeModifier() : block not indexed");
    const CBlockIndex* pindexFrom = mapBlockIndex[hashBlockFrom];
    nStakeModifierHeight = pindexFrom->nHeight;
    nStakeModifierTime = pindexFrom->GetBlockTime();
    int64_t nStakeModifierSelectionInterval = GetStakeModifierSelectionInterval();
    const CBlockIndex* pindex = pindexFrom;
    CBlockIndex* pindexNext = chainActive[pindexFrom->nHeight + 1];

    // loop to find the stake modifier later by a selection interval
    while (nStakeModifierTime < pindexFrom->GetBlockTime() + nStakeModifierSelectionInterval) {
        if (!pindexNext) {
            // Reached chain tip before spanning the full selection interval;
            // use the best modifier found so far.
            break;
        }

        pindex = pindexNext;
        pindexNext = chainActive[pindexNext->nHeight + 1];
        if (pindex->GeneratedStakeModifier()) {
            nStakeModifierHeight = pindex->nHeight;
            nStakeModifierTime = pindex->GetBlockTime();
        }
    }
    nStakeModifier = pindex->nStakeModifier;
    return true;
}

uint256 stakeHash(unsigned int nTimeTx, CDataStream ss, unsigned int prevoutIndex, uint256 prevoutHash, unsigned int nTimeBlockFrom)
{
    //Agouti will hash in the transaction hash and the index number in order to make sure each hash is unique
    ss << nTimeBlockFrom << prevoutIndex << prevoutHash << nTimeTx;
    return Hash(ss.begin(), ss.end());
}

//test hash vs target
bool stakeTargetHit(uint256 hashProofOfStake, int64_t nValueIn, uint256 bnTargetPerCoinDay, int nHeight)
{
    //get the stake weight - weight is equal to coin amount
    int64_t nEffectiveValue = nValueIn;

    // Cap effective stake weight above activation height
    if (nHeight >= Params().StakePointerForkHeight() && nEffectiveValue > STAKE_WEIGHT_CAP)
        nEffectiveValue = STAKE_WEIGHT_CAP;

    uint256 bnCoinDayWeight = uint256(nEffectiveValue) / 100;

    // Now check if proof-of-stake hash meets target protocol
    return (uint256(hashProofOfStake) < bnCoinDayWeight * bnTargetPerCoinDay);
}

// Internal kernel hash implementation.
// Accepts pre-extracted block parameters so callers with block-index access can
// avoid deserialising the full CBlock from disk.
// All parameters are consensus-inputs to the kernel hash; callers must not omit any.
//
// Consensus inputs covered:
//   nBits            — difficulty target encoded in block header
//   hashBlockFrom    — hash of the block containing the staked coin (commits to chain
//                      position and feeds GetKernelStakeModifier)
//   nTimeBlockFrom   — timestamp of that block (enforces StakeMinAge)
//   nValueIn         — value of the staked output (stake weight)
//   prevout          — outpoint of the staked coin (prevoutHash + n, hashed into kernel)
//   nTimeTx          — coinstake transaction time (hashed into kernel)
//   nHeight          — new block height (gates StakeWeightCap fork behaviour)
//
// nStakeModifier is derived deterministically from hashBlockFrom via
// GetKernelStakeModifier and is therefore covered implicitly.
static bool CheckStakeKernelHashImpl(
    unsigned int nBits,
    const uint256& hashBlockFrom,
    unsigned int nTimeBlockFrom,
    int64_t nValueIn,
    const COutPoint& prevout,
    unsigned int& nTimeTx,
    unsigned int nHashDrift,
    bool fCheck,
    uint256& hashProofOfStake,
    bool fPrintProofOfStake,
    int nHeight)
{
    if (nTimeTx < nTimeBlockFrom)
        return error("CheckStakeKernelHash() : nTime violation");

    if (nTimeBlockFrom + Params().StakeMinAge() > nTimeTx)
        return error("CheckStakeKernelHash() : min age violation - nTimeBlockFrom=%d nStakeMinAge=%u nTimeTx=%d",
                     nTimeBlockFrom, Params().StakeMinAge(), nTimeTx);

    uint256 bnTargetPerCoinDay;
    bnTargetPerCoinDay.SetCompact(nBits);

    uint64_t nStakeModifier = 0;
    int nStakeModifierHeight = 0;
    int64_t nStakeModifierTime = 0;
    if (!GetKernelStakeModifier(hashBlockFrom, nStakeModifier, nStakeModifierHeight, nStakeModifierTime, fPrintProofOfStake)) {
        LogPrintf("CheckStakeKernelHash(): failed to get kernel stake modifier\n");
        return false;
    }

    CDataStream ss(SER_GETHASH, 0);
    ss << nStakeModifier;

    if (fCheck) {
        hashProofOfStake = stakeHash(nTimeTx, ss, prevout.n, prevout.hash, nTimeBlockFrom);
        return stakeTargetHit(hashProofOfStake, nValueIn, bnTargetPerCoinDay, nHeight);
    }

    bool fSuccess = false;
    unsigned int nTryTime = 0;
    unsigned int i;
    int nHeightStart = chainActive.Height();
    for (i = 0; i < nHashDrift; i++) {
        if (chainActive.Height() != nHeightStart)
            break;

        nTryTime = nTimeTx + nHashDrift - i;
        hashProofOfStake = stakeHash(nTryTime, ss, prevout.n, prevout.hash, nTimeBlockFrom);

        if (!stakeTargetHit(hashProofOfStake, nValueIn, bnTargetPerCoinDay, nHeight))
            continue;

        fSuccess = true;
        nTimeTx = nTryTime;

        if (fDebug || fPrintProofOfStake) {
            BlockMap::iterator it = mapBlockIndex.find(hashBlockFrom);
            int nHeightBlockFrom = (it != mapBlockIndex.end()) ? it->second->nHeight : -1;
            LogPrintf("CheckStakeKernelHash() : using modifier %s at height=%d timestamp=%s for block from height=%d timestamp=%s\n",
                boost::lexical_cast<std::string>(nStakeModifier).c_str(), nStakeModifierHeight,
                DateTimeStrFormat("%Y-%m-%d %H:%M:%S", nStakeModifierTime).c_str(),
                nHeightBlockFrom,
                DateTimeStrFormat("%Y-%m-%d %H:%M:%S", nTimeBlockFrom).c_str());
            LogPrintf("CheckStakeKernelHash() : pass protocol=%s modifier=%s nTimeBlockFrom=%u prevoutHash=%s nTimeTxPrev=%u nPrevout=%u nTimeTx=%u hashProof=%s\n",
                "0.3",
                boost::lexical_cast<std::string>(nStakeModifier).c_str(),
                nTimeBlockFrom, prevout.hash.ToString().c_str(), nTimeBlockFrom, prevout.n, nTryTime,
                hashProofOfStake.ToString().c_str());
        }
        break;
    }

    AssertLockHeld(cs_main);
    mapHashedBlocks.clear();
    mapHashedBlocks[chainActive.Tip()->nHeight] = GetTime();
    return fSuccess;
}

// Check kernel hash target and coinstake signature.
//
// The legacy kernel needs the *historical* stake coin (value + scriptPubKey)
// and the *historical* containing-block header (hash + time).  pcoinsTip
// reflects the tip-state UTXO set, not the parent-state of the block being
// validated, so it cannot be used here: under headers-first the body of the
// block being validated arrives ahead of chain connection, and the staked
// coin may already have been re-spent at the tip.  We retain the original
// GetTransaction-based historical lookup; the per-block disk read is bounded
// by the legacy pre-fork height range and not on any performance-critical
// modern path.
//
// We do, however, keep the ReadBlockFromDisk elimination: the containing
// block's time and hash are taken from its CBlockIndex entry instead of
// re-deserialising the full block.
bool CheckProofOfStake(const CBlock& block, uint256& hashProofOfStake, int nHeight)
{
    const CTransaction tx = block.vtx[1];
    if (!tx.IsCoinStake())
        return error("CheckProofOfStake() : called on non-coinstake %s", tx.GetHash().ToString().c_str());

    const CTxIn& txin = tx.vin[0];

    // Historical lookup of the staked coin's source transaction.
    uint256 hashBlock;
    CTransaction txPrev;
    if (!GetTransaction(txin.prevout.hash, txPrev, hashBlock, true))
        return error("CheckProofOfStake() : INFO: read txPrev failed");

    if (txin.prevout.n >= txPrev.vout.size())
        return error("CheckProofOfStake() : prevout %u out of range for tx %s",
                     txin.prevout.n, txin.prevout.hash.ToString().c_str());

    // Verify the coinstake input signature against the historical scriptPubKey.
    if (!VerifyScript(txin.scriptSig, txPrev.vout[txin.prevout.n].scriptPubKey,
                      STANDARD_SCRIPT_VERIFY_FLAGS, TransactionSignatureChecker(&tx, 0)))
        return error("CheckProofOfStake() : VerifySignature failed on coinstake %s",
                     tx.GetHash().ToString().c_str());

    // Locate the containing-block index entry.  hashBlock comes from
    // GetTransaction and identifies the block that confirmed txPrev; this is
    // stable across reorgs in a way chainActive[height] is not.
    BlockMap::iterator it = mapBlockIndex.find(hashBlock);
    if (it == mapBlockIndex.end())
        return error("CheckProofOfStake() : block %s not in index", hashBlock.ToString().c_str());
    CBlockIndex* pindex = it->second;

    unsigned int nInterval = 0;
    unsigned int nTime = block.nTime;
    if (!CheckStakeKernelHashImpl(block.nBits,
                                  pindex->GetBlockHash(),
                                  (unsigned int)pindex->GetBlockTime(),
                                  txPrev.vout[txin.prevout.n].nValue,
                                  txin.prevout, nTime, nInterval, true,
                                  hashProofOfStake, fDebug, nHeight))
        return error("CheckProofOfStake() : INFO: check kernel failed on coinstake %s, hashProof=%s\n",
                     tx.GetHash().ToString().c_str(), hashProofOfStake.ToString().c_str());

    return true;
}

// Version-5 StakePointer kernel hash.
// Stake modifier = hash of the ancestor block nKernelModifierOffset blocks before tip.
// Inputs are deterministic: no grinding over time or coin-age is possible.
bool CheckStakePointerKernelHash(
    unsigned int nBits,
    const COutPoint& outpoint,
    const CBlockIndex* pindexFrom,
    const CBlockIndex* pindexPrev,
    uint32_t nTimeStake,
    uint256& hashProofOfStake)
{
    // Derive the stake modifier from a committed ancestor of pindexPrev to prevent
    // last-second block manipulation.
    int nModifierHeight = pindexPrev->nHeight - Params().KernelModifierOffset();
    if (nModifierHeight < 0)
        return error("CheckStakePointerKernelHash(): modifier height %d below zero", nModifierHeight);

    const CBlockIndex* pindexModifier = pindexPrev->GetAncestor(nModifierHeight);
    if (!pindexModifier)
        return error("CheckStakePointerKernelHash(): failed to get modifier ancestor at height %d", nModifierHeight);

    uint256 stakeModifier = pindexModifier->GetBlockHash();

    // Self-defending timestamp bounds — this kernel is only called for v5
    // post-fork blocks where the 60 s future-drift rule is in effect.
    // Lower bound: must be after the previous block's median time past
    // (mirrors ContextualCheckBlockHeader).
    int64_t nMTP = pindexPrev->GetMedianTimePast();
    if ((int64_t)nTimeStake <= nMTP)
        return error("CheckStakePointerKernelHash(): nTimeStake %u <= MTP %d",
                     (unsigned)nTimeStake, (int)nMTP);
    // Upper bound: future-drift relative to network-adjusted time, mirroring the
    // 60 s PoS drift enforced in ContextualCheckBlock for v5 heights. This must
    // NOT be relative to the previous block time — doing so would cap block
    // spacing at 60 s and contradict the 600 s (10 minute) target spacing.
    if ((int64_t)nTimeStake > GetAdjustedTime() + 60)
        return error("CheckStakePointerKernelHash(): nTimeStake %u too far in the future (adjusted=%d)",
                     (unsigned)nTimeStake, (int)GetAdjustedTime());

    // Build the kernel data stream — field order is consensus-critical.
    CDataStream ss(SER_GETHASH, 0);
    ss << outpoint.hash << outpoint.n << stakeModifier
       << pindexFrom->GetBlockTime() << nTimeStake;
    hashProofOfStake = Hash(ss.begin(), ss.end());

    // Target test: hashProofOfStake / weight < bnTarget.
    // Division form avoids uint256 overflow that occurs when weight * bnTarget
    // exceeds 2^256 at low difficulty (large bnTarget), which would silently
    // wrap and make the kernel unreachable.
    uint256 bnTarget;
    bnTarget.SetCompact(nBits);
    const uint256 bnWeight = uint256((uint64_t)(MASTERNODE_COLLATERAL / 100));
    if (hashProofOfStake / bnWeight >= bnTarget)
        return false;

    if (GetBoolArg("-printcoinstake", false))
        LogPrintf("CheckStakePointerKernelHash(): pass modifier=%s timeBlockFrom=%u timeStake=%u hashProof=%s\n",
            stakeModifier.ToString().c_str(),
            pindexFrom->GetBlockTime(),
            nTimeStake,
            hashProofOfStake.ToString().c_str());

    return true;
}

// Check whether the coinstake timestamp meets protocol
bool CheckCoinStakeTimestamp(int64_t nTimeBlock, int64_t nTimeTx)
{
    // v0.3 protocol
    return (nTimeBlock == nTimeTx);
}

// Get stake modifier checksum
unsigned int GetStakeModifierChecksum(const CBlockIndex* pindex)
{
    assert(pindex->pprev || pindex->GetBlockHash() == Params().HashGenesisBlock());
    // Hash previous checksum with flags, hashProofOfStake and nStakeModifier
    CDataStream ss(SER_GETHASH, 0);
    if (pindex->pprev)
        ss << pindex->pprev->nStakeModifierChecksum;
    ss << pindex->nFlags << pindex->hashProofOfStake << pindex->nStakeModifier;
    uint256 hashChecksum = Hash(ss.begin(), ss.end());
    hashChecksum >>= (256 - 32);
    return hashChecksum.Get64();
}

// Check stake modifier hard checkpoints
bool CheckStakeModifierCheckpoints(int nHeight, unsigned int nStakeModifierChecksum)
{
    if (fTestNet) return true; // Testnet has no checkpoints
    if (mapStakeModifierCheckpoints.count(nHeight)) {
        return nStakeModifierChecksum == mapStakeModifierCheckpoints[nHeight];
    }
    return true;
}
