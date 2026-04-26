// Copyright (c) 2012-2013 The PPCoin developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2018-2022 The Crown developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_KERNEL_H
#define BITCOIN_KERNEL_H

#include "main.h"


// MODIFIER_INTERVAL: time to elapse before new modifier is computed
static const unsigned int MODIFIER_INTERVAL = 60;
static const unsigned int MODIFIER_INTERVAL_TESTNET = 60;
extern unsigned int nModifierInterval;
extern unsigned int getIntervalVersion(bool fTestNet);

// MODIFIER_INTERVAL_RATIO:
// ratio of group interval length between the last group and the first group
static const int MODIFIER_INTERVAL_RATIO = 3;

// Stake weight cap: maximum effective UTXO value for stake weight calculation (50000 AGU)
static const int STAKE_WEIGHT_CAP_HEIGHT = 2675000;
static const int64_t STAKE_WEIGHT_CAP = 50000 * COIN;

// Masternode collateral required to produce version-5 PoS blocks
static const CAmount MASTERNODE_COLLATERAL = 3000 * COIN;

// Compute the hash modifier for proof-of-stake
bool ComputeNextStakeModifier(const CBlockIndex* pindexPrev, uint64_t& nStakeModifier, bool& fGeneratedStakeModifier);

// Check whether stake kernel meets hash target
// Sets hashProofOfStake on success return
uint256 stakeHash(unsigned int nTimeTx, CDataStream ss, unsigned int prevoutIndex, uint256 prevoutHash, unsigned int nTimeBlockFrom);
bool stakeTargetHit(uint256 hashProofOfStake, int64_t nValueIn, uint256 bnTargetPerCoinDay, int nHeight);
bool CheckStakeKernelHash(unsigned int nBits, const CBlock& blockFrom, const CTransaction& txPrev, const COutPoint& prevout, unsigned int& nTimeTx, unsigned int nHashDrift, bool fCheck, uint256& hashProofOfStake, bool fPrintProofOfStake, int nHeight);

// Check kernel hash target and coinstake signature
// Sets hashProofOfStake on success return
bool CheckProofOfStake(const CBlock& block, uint256& hashProofOfStake, int nHeight);

// Version-5 StakePointer kernel — no coin-age, no tx.nTime.
// outpoint:   the COutPoint derived from stakePointer.txid / stakePointer.nPos
// pindexFrom: CBlockIndex of stakePointer.hashBlock
// pindexPrev: CBlockIndex of the previous block (height - 1)
// nTimeStake: block.nTime of the new block being assembled / validated
bool CheckStakePointerKernelHash(
    unsigned int nBits,
    const COutPoint& outpoint,
    const CBlockIndex* pindexFrom,
    const CBlockIndex* pindexPrev,
    uint32_t nTimeStake,
    uint256& hashProofOfStake);

// Check whether the coinstake timestamp meets protocol
bool CheckCoinStakeTimestamp(int64_t nTimeBlock, int64_t nTimeTx);

// Get stake modifier checksum
unsigned int GetStakeModifierChecksum(const CBlockIndex* pindex);

// Check stake modifier hard checkpoints
bool CheckStakeModifierCheckpoints(int nHeight, unsigned int nStakeModifierChecksum);

// Get time weight using supplied timestamps
int64_t GetWeight(int64_t nIntervalBeginning, int64_t nIntervalEnd);

#endif // BITCOIN_KERNEL_H
