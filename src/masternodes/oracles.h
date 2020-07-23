// Copyright (c) 2020 DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_ORACLES_H
#define DEFI_MASTERNODES_ORACLES_H

#include <masternodes/oracle.h>
#include <flushablestorage.h>
#include <masternodes/res.h>

class COraclesView : public virtual CStorageView
{
public:
    void ForEachOracleWeight(std::function<bool(CScript const & oracle, CAmount const & weight)> callback, CScript const & start) const;

    boost::optional<CAmount> GetOracleWeight(CScript const & oracle) const;
    Res SetOracleWeight(CCreateWeightOracleMessage const & oracleMsg);
    Res DelOracle(CScript const & oracle);

    struct ByOracleId { static const unsigned char prefix; };//DB Tag
};

struct OracleKey {
    DCT_ID tokenID;
    CScript oracle;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(VARINT(tokenID.v));
        READWRITE(oracle);
    }
};

struct OracleValue
{
    CAmount price;
    uint32_t timeInForce; // expiry time in blocks
    uint32_t height;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(price);
        READWRITE(timeInForce);
        READWRITE(height);
    }
};


class COraclesPriceView : public virtual CStorageView
{
public:
    void ForEachPrice(std::function<bool(OracleKey const & oracleKey, OracleValue const & oracleValue)> callback, OracleKey const & startKey) const;

    Res SetOracleTokenIDPrice(CPostPriceOracle const & oracleMsg);
    boost::optional<OracleValue> GetOraclePrice(OracleKey const & oracleKey) const;

    struct ByOracleTokenId { static const unsigned char prefix; };//tag in DB
};

class CMedianPriceView : public virtual CStorageView
{
public:
    void ForEachMedian(std::function<bool(DCT_ID const& tokenID, CAmount const& medianPrice)> callback, DCT_ID const & startID) const;

    Res SetTokenIdMedian(DCT_ID const& tokenID, CAmount const& medianPrice);

    struct ByTokenId { static const unsigned char prefix; };
};

#endif //DEFI_MASTERNODES_ORACLES_H
