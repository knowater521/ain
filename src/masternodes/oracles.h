// Copyright (c) 2020 DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_ORACLES_H
#define DEFI_MASTERNODES_ORACLES_H

#include <masternodes/oracle.h>
#include <flushablestorage.h>
#include <masternodes/res.h>

class COraclesWeightView : public virtual CStorageView
{
public:
    void ForEachPriceOracleWeight(std::function<bool(CScript const & oracle, CAmount const & weight)> callback, CScript const & start) const;

    boost::optional<CAmount> GetOracleWeight(CScript const & oracle) const;
    Res SetOracleWeight(CCreateWeightOracleMessage const & oracleMsg);
    Res DelOracleWeight(CScript const & oracle);

    // tags
    struct ByOracleWeightId { static const unsigned char prefix; };
};

struct OracleKey {
    CScript oracle;
    uint32_t tokenID;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(oracle);
        READWRITE(tokenID);
    }
};

class COraclesPriceView : public virtual CStorageView
{
public:
    void ForEachPrice(std::function<bool(OracleKey const & oracleKey, CAmount const & price)> callback, OracleKey const & startKey) const;

    Res SetOracleTokenIDPrice(CPostPriceOracleTokenID const & oracleMsg);
    boost::optional<CAmount> GetPrice(OracleKey const & oracleKey) const;

    struct ByOracleTokenId { static const unsigned char prefix; };//tag in DB
};



#endif //DEFI_MASTERNODES_ORACLES_H
