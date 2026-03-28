// Copyright (c) 2022 The Agouti developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "stakepointer.h"
#include "hash.h"

// Must match the message magic used in SignMessage / VerifyMessage throughout this codebase.
static const std::string strStakePointerMsgMagic = "DarkNet Signed Message:\n";

bool StakePointer::VerifyCollateralSignOver() const
{
    // Canonical message: "StakePointer sign-over:" + hex key-ID of the proof-of-stake key.
    std::string strMessage = std::string("StakePointer sign-over:") +
                             pubKeyProofOfStake.GetID().ToString();

    CHashWriter ss(SER_GETHASH, 0);
    ss << strStakePointerMsgMagic;
    ss << strMessage;
    uint256 hash = ss.GetHash();

    return pubKeyCollateral.Verify(hash, vchSigCollateralSignOver);
}
