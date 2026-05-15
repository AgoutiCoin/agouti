// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2009-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRIPT_SIGCACHE_H
#define BITCOIN_SCRIPT_SIGCACHE_H

#include "script/interpreter.h"

#include <vector>

// DoS prevention: limit signature cache to 32 MiB by default
// (sizeof(uint256) == 32 => ~1M entries per MiB)
static const unsigned int DEFAULT_MAX_SIG_CACHE_SIZE = 32;
// Hard cap at 16 GiB
static const int64_t MAX_MAX_SIG_CACHE_SIZE = 16384;

class CPubKey;

class CachingTransactionSignatureChecker : public TransactionSignatureChecker
{
private:
    bool store;

public:
    CachingTransactionSignatureChecker(const CTransaction* txToIn, unsigned int nInIn, bool storeIn = true)
        : TransactionSignatureChecker(txToIn, nInIn), store(storeIn) {}

    bool VerifySignature(const std::vector<unsigned char>& vchSig, const CPubKey& vchPubKey, const uint256& sighash) const;
};

void InitSignatureCache();

#endif // BITCOIN_SCRIPT_SIGCACHE_H
