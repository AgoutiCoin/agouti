/**
 * @file       Denominations.h
 *
 * @brief      Denomination info for the Zerocoin library.
 *
 * @copyright  Copyright 2017 PIVX Developers
 * @license    This project is released under the MIT license.
 **/

#ifndef DENOMINATIONS_H_
#define DENOMINATIONS_H_

#include <cstdint>
#include <string>
#include <vector>

namespace libzerocoin {

enum  CoinDenomination {
    ZQ_ERROR = 0,
    ZQ_ONE = 1,
    ZQ_FIVE = 5,
    ZQ_TEN = 10,
    ZQ_FIFTY = 50,
    ZQ_ONE_HUNDRED = 100,
    ZQ_FIVE_HUNDRED = 500,
    ZQ_ONE_THOUSAND = 1000,
    ZQ_FIVE_THOUSAND = 5000
};

// Order is with the Smallest Denomination first and is important for a particular routine that this order is maintained
const std::vector<CoinDenomination> zerocoinDenomList = {ZQ_ONE, ZQ_FIVE, ZQ_TEN, ZQ_FIFTY, ZQ_ONE_HUNDRED, ZQ_FIVE_HUNDRED, ZQ_ONE_THOUSAND, ZQ_FIVE_THOUSAND};
// These are the max number you'd need at any one Denomination before moving to the higher denomination. Last number is 4, since it's the max number of
// possible spends at the moment    /
const std::vector<int> maxCoinsAtDenom   = {4, 1, 4, 1, 4, 1, 4, 4};

inline int64_t ZerocoinDenominationToInt(const CoinDenomination& denomination)
{
    return static_cast<int64_t>(denomination);
}

inline CoinDenomination IntToZerocoinDenomination(int64_t amount)
{
    switch (amount) {
    case 1:    return ZQ_ONE;
    case 5:    return ZQ_FIVE;
    case 10:   return ZQ_TEN;
    case 50:   return ZQ_FIFTY;
    case 100:  return ZQ_ONE_HUNDRED;
    case 500:  return ZQ_FIVE_HUNDRED;
    case 1000: return ZQ_ONE_THOUSAND;
    case 5000: return ZQ_FIVE_THOUSAND;
    default:   return ZQ_ERROR;
    }
}

inline int64_t ZerocoinDenominationToAmount(const CoinDenomination& denomination)
{
    return ZerocoinDenominationToInt(denomination) * 100000000LL;
}

inline CoinDenomination AmountToZerocoinDenomination(int64_t amount)
{
    int64_t residual = amount - 100000000LL * (amount / 100000000LL);
    if (residual == 0)
        return IntToZerocoinDenomination(amount / 100000000LL);
    return ZQ_ERROR;
}

CoinDenomination AmountToClosestDenomination(int64_t nAmount, int64_t& nRemaining);
CoinDenomination get_denomination(std::string denomAmount);
int64_t get_amount(std::string denomAmount);

} /* namespace libzerocoin */
#endif /* DENOMINATIONS_H_ */
