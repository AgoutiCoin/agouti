// Copyright (c) 2018-2022 The Crown developers
// Copyright (c) 2022 The Agouti developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "stakepointer.h"
#include "hash.h"

// Must match the message magic used in SignMessage / VerifyMessage throughout this codebase.
static const std::string strStakePointerMsgMagic = "DarkNet Signed Message:\n";

uint256 StakePointer::CollateralSignOverHash(const CPubKey& pubKeyPoS)
{
    std::string strMessage = std::string("StakePointer sign-over:") +
                             pubKeyPoS.GetID().ToString();

    CHashWriter ss(SER_GETHASH, 0);
    ss << strStakePointerMsgMagic;
    ss << strMessage;
    return ss.GetHash();
}

bool StakePointer::VerifyCollateralSignOver() const
{
    uint256 hash = CollateralSignOverHash(pubKeyProofOfStake);
    return pubKeyCollateral.Verify(hash, vchSigCollateralSignOver);
}

bool StakePointer::CreateCollateralSignOver(const CKey& keyCollateral,
                                            const CPubKey& pubKeyPoS,
                                            std::vector<unsigned char>& vchSigOut)
{
    uint256 hash = CollateralSignOverHash(pubKeyPoS);
    return keyCollateral.Sign(hash, vchSigOut);
}
