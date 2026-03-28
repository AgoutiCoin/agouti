// Copyright (c) 2018-2022 The Crown developers
// Copyright (c) 2022 The Agouti developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_STAKEPOINTER_H
#define BITCOIN_STAKEPOINTER_H

#include "pubkey.h"
#include "serialize.h"
#include "uint256.h"

#include <vector>

/** Binds a masternode to a recent reward payment for version-5 PoS kernel.
 *
 *  Only masternodes that received a payment within nStakePointerValidityPeriod
 *  blocks of the current tip may produce PoS blocks once the StakePointer fork
 *  is active.  The pointer expires, cannot be reused, and must not reference
 *  a block inside the reorg window.
 *
 *  CONSENSUS RISK – any change to field order or types alters serialisation.
 *
 *  NOTE: serialisation order differs from Crown Core, which serialises
 *  pubKeyProofOfStake before pubKeyCollateral.  Agouti serialises
 *  pubKeyCollateral first.  Do not assume wire compatibility with Crown.
 */
class StakePointer
{
public:
    uint256      hashBlock;                              // hash of the block that paid this MN
    uint256      txid;                                   // txid of the payment transaction
    unsigned int nPos;                                   // vout index of the MN reward output
    CPubKey      pubKeyCollateral;                       // MN collateral public key
    CPubKey      pubKeyProofOfStake;                     // delegated signing key (may equal pubKeyCollateral)
    std::vector<unsigned char> vchSigCollateralSignOver; // signature from collateral key granting rights
                                                         // to pubKeyProofOfStake; empty when keys are identical

    StakePointer() { SetNull(); }

    void SetNull()
    {
        hashBlock.SetNull();
        txid.SetNull();
        nPos = 0;
        pubKeyCollateral   = CPubKey();
        pubKeyProofOfStake = CPubKey();
        vchSigCollateralSignOver.clear();
    }

    bool IsNull() const { return hashBlock.IsNull(); }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(hashBlock);
        READWRITE(txid);
        READWRITE(nPos);
        READWRITE(pubKeyCollateral);
        READWRITE(pubKeyProofOfStake);
        READWRITE(vchSigCollateralSignOver);
    }

    /** Verify that pubKeyCollateral signed staking rights over to pubKeyProofOfStake.
     *  Only called when the two keys differ. */
    bool VerifyCollateralSignOver() const;
};

#endif // BITCOIN_STAKEPOINTER_H
