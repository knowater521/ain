// Copyright (c) 2020 DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/oracles.h>

/// @attention make sure that it does not overlap with those in masternodes.cpp/tokens.cpp/undos.cpp/accounts.cpp/orders.cpp !!!
const unsigned char COraclesView::ByOracleId::prefix = 'p';
const unsigned char COraclesPriceView::ByOracleTokenId::prefix = 'q';
const unsigned char COraclesPriceView::ByExpiryHeight::prefix = 'v';
const unsigned char CMedianPriceView::ByTokenId::prefix = 's';

void COraclesView::ForEachOracleWeight(std::function<bool (const CScript & oracle, const CAmount & weight)> callback, const CScript &start) const
{
    ForEach<ByOracleId, CScript, CAmount>([&callback] (const CScript & oracle, const CAmount & weight) {
        return callback(oracle, weight);
    }, start);
}

boost::optional<CAmount> COraclesView::GetOracleWeight(const CScript &oracle) const
{
    return ReadBy<ByOracleId, CAmount>(oracle);
}

Res COraclesView::SetOracleWeight(const CCreateWeightOracleMessage &oracleMsg) {
    WriteBy<ByOracleId>(oracleMsg.oracle, oracleMsg.weight);
    return Res::Ok();
}

Res COraclesView::DelOracle(CScript const & oracle) {
    EraseBy<ByOracleId>(oracle);
    return Res::Ok();
}

void COraclesPriceView::ForEachPrice(std::function<bool (const OracleKey & oracleKey, const OracleValue & oracleValue)> callback, const OracleKey &startKey) const
{
    ForEach<ByOracleTokenId, OracleKey, OracleValue>([&callback] (const OracleKey & oracleKey, const OracleValue & oracleValue) {
        return callback(oracleKey, oracleValue);
    }, startKey);
}

void COraclesPriceView::ForEachExpiredPrice(std::function<bool (const OracleKey & oracleKey)> callback, uint32_t expiryHeight) const
{
    ForEach<ByExpiryHeight, ExpiredKey, char>([&] (ExpiredKey key, char) {
        if(key.height <= expiryHeight)
            return callback(key.oracleKey);
        else
            return false;
    }, ExpiredKey{0, OracleKey{DCT_ID{0}, CScript{0}}});
}

Res COraclesPriceView::SetOracleTokenIDPrice(const CPostPriceOracle &oracleMsg)
{
    if (oracleMsg.price != 0) {
        WriteBy<ByOracleTokenId>(OracleKey{oracleMsg.tokenID, oracleMsg.oracle}, OracleValue{oracleMsg.price, oracleMsg.timeInForce, oracleMsg.height});
        if(oracleMsg.timeInForce)
            WriteBy<ByExpiryHeight>(ExpiredKey{(uint32_t)(oracleMsg.height + oracleMsg.timeInForce), OracleKey{oracleMsg.tokenID, oracleMsg.oracle}}, '\0');
    } else {
        DeleteOraclePrice(OracleKey{oracleMsg.tokenID, oracleMsg.oracle});
    }
    return Res::Ok();
}

boost::optional<OracleValue> COraclesPriceView::GetOraclePrice(const OracleKey &oracleKey) const
{
    return ReadBy<ByOracleTokenId, OracleValue>(oracleKey);
}

Res COraclesPriceView::DeleteOraclePrice(const OracleKey &oracleKey)
{
    auto const oracle = GetOraclePrice(oracleKey);
    if(oracle) {
        EraseBy<ByOracleTokenId>(oracleKey);
        if(oracle->timeInForce)
            EraseBy<ByExpiryHeight>(ExpiredKey{(uint32_t)(oracle->height + oracle->timeInForce), oracleKey});
    }
    return Res::Ok();
}

void CMedianPriceView::ForEachMedian(std::function<bool (DCT_ID const& tokenID, CAmount const& medianPrice)> callback, const DCT_ID &startID) const
{
    ForEach<ByTokenId, DCT_ID, CAmount>([&callback] (DCT_ID const& tokenID, CAmount const& medianPrice) {
        return callback(tokenID, medianPrice);
    }, startID);
}

Res CMedianPriceView::SetTokenIdMedian(const DCT_ID &tokenID, const CAmount &medianPrice)
{
    WriteBy<ByTokenId>(tokenID, medianPrice);
    return Res::Ok();
}
