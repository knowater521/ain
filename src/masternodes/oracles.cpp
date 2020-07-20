// Copyright (c) 2020 DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/oracles.h>

/// @attention make sure that it does not overlap with those in masternodes.cpp/tokens.cpp/undos.cpp/accounts.cpp/orders.cpp !!!
const unsigned char COraclesView::ByOracleId::prefix = 'p';
const unsigned char COraclesPriceView::ByOracleTokenId::prefix = 'q';

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

void COraclesPriceView::ForEachPrice(std::function<bool (const OracleKey & oracleKey, const CAmount & price)> callback, const OracleKey &startKey) const
{
    ForEach<ByOracleTokenId, OracleKey, CAmount>([&callback] (const OracleKey & oracleKey, const CAmount & price) {
        return callback(oracleKey, price);
    }, startKey);
}

Res COraclesPriceView::SetOracleTokenIDPrice(const CPostPriceOracleTokenID &oracleMsg)
{
    if (oracleMsg.price != 0) {
        WriteBy<ByOracleTokenId>(OracleKey{oracleMsg.oracle, oracleMsg.tokenID}, oracleMsg.price);
    } else {
        EraseBy<ByOracleTokenId>(OracleKey{oracleMsg.oracle, oracleMsg.tokenID});
    }
    return Res::Ok();
}

boost::optional<CAmount> COraclesPriceView::GetPrice(const OracleKey &oracleKey) const
{
    return ReadBy<ByOracleTokenId, CAmount>(oracleKey);
}
