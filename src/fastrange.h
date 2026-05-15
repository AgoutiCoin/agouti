// Copyright (c) 2018-2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_FASTRANGE_H
#define BITCOIN_FASTRANGE_H

#include <cstdint>

/** Fast range reduction with 32-bit input and 32-bit range. */
static inline uint32_t FastRange32(uint32_t x, uint32_t n)
{
    return (uint64_t{x} * n) >> 32;
}

#endif // BITCOIN_FASTRANGE_H
