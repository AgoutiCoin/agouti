// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2009-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "sigcache.h"

#include "crypto/sha256.h"
#include "cuckoocache.h"
#include "pubkey.h"
#include "random.h"
#include "uint256.h"
#include "util.h"

#include <boost/thread/locks.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <cassert>
#include <cstring>
#include <vector>

namespace {

/**
 * Extracts a 32-bit window from the cache entry (a SHA256 digest) for use
 * as one of the eight CuckooCache hash functions. The entry is already
 * high-entropy so different byte offsets give good uniformity.
 */
struct SignatureCacheHasher {
    template <uint8_t hash_select>
    uint32_t operator()(const uint256& e) const
    {
        static_assert(hash_select < 8, "SignatureCacheHasher: hash_select out of range");
        uint32_t u;
        std::memcpy(&u, e.begin() + 4 * hash_select, 4);
        return u;
    }
};

/**
 * Valid signature cache, backed by a CuckooCache.
 *
 * Cache entries are SHA256(nonce || sighash || pubkey || sig), where nonce is
 * a random 64-byte value seeded at startup to mitigate preimage collision abuse.
 *
 * Thread safety:
 *   Get()  — shared lock, compatible with concurrent readers
 *   Set()  — exclusive lock
 *   The CuckooCache contains() erase path is covered by the lock held in Get().
 */
class CSignatureCache
{
private:
    CSHA256 m_salted_hasher;
    typedef CuckooCache::cache<uint256, SignatureCacheHasher> map_type;
    map_type setValid;
    boost::shared_mutex cs_sigcache;
    bool m_initialised;

public:
    CSignatureCache() : m_initialised(false)
    {
        uint256 nonce = GetRandHash();
        // Write nonce twice to fill the 64-byte SHA256 block, making subsequent
        // Write() calls operate on a fresh block for efficiency.
        m_salted_hasher.Write(nonce.begin(), 32);
        m_salted_hasher.Write(nonce.begin(), 32);
    }

    void ComputeEntry(uint256& entry, const uint256& hash,
                      const std::vector<unsigned char>& vchSig,
                      const CPubKey& pubkey)
    {
        CSHA256 hasher = m_salted_hasher;
        hasher.Write(hash.begin(), 32)
              .Write(pubkey.begin(), pubkey.size())
              .Write(vchSig.data(), vchSig.size())
              .Finalize(entry.begin());
    }

    bool Get(const uint256& entry, const bool erase)
    {
        assert(m_initialised && "InitSignatureCache() must run before Get()");
        boost::shared_lock<boost::shared_mutex> lock(cs_sigcache);
        return setValid.contains(entry, erase);
    }

    void Set(const uint256& entry)
    {
        assert(m_initialised && "InitSignatureCache() must run before Set()");
        boost::unique_lock<boost::shared_mutex> lock(cs_sigcache);
        setValid.insert(entry);
    }

    uint32_t setup_bytes(size_t n)
    {
        uint32_t r = setValid.setup_bytes(n);
        m_initialised = true;
        return r;
    }
};

/* Initialised outside VerifySignature to avoid per-call atomic overhead from
 * function-local statics. */
static CSignatureCache signatureCache;

} // namespace

void InitSignatureCache()
{
    int64_t nMaxCacheSizeMiB = std::max((int64_t)0, GetArg("-maxsigcachesize", (int64_t)DEFAULT_MAX_SIG_CACHE_SIZE));
    nMaxCacheSizeMiB = std::min(nMaxCacheSizeMiB, MAX_MAX_SIG_CACHE_SIZE);
    size_t nMaxCacheSize = (size_t)nMaxCacheSizeMiB << 20;
    size_t nElems = signatureCache.setup_bytes(nMaxCacheSize);
    LogPrintf("Using %zu MiB out of %lld requested for signature cache, able to store %zu elements\n",
              (nElems * sizeof(uint256)) >> 20, nMaxCacheSizeMiB, nElems);
}

bool CachingTransactionSignatureChecker::VerifySignature(const std::vector<unsigned char>& vchSig, const CPubKey& pubkey, const uint256& sighash) const
{
    uint256 entry;
    signatureCache.ComputeEntry(entry, sighash, vchSig, pubkey);
    if (signatureCache.Get(entry, !store))
        return true;
    if (!TransactionSignatureChecker::VerifySignature(vchSig, pubkey, sighash))
        return false;
    if (store)
        signatureCache.Set(entry);
    return true;
}
