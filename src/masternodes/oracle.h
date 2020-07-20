// Copyright (c) 2020 DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_ORACLE_H
#define DEFI_MASTERNODES_ORACLE_H

#include <amount.h>
#include <script/script.h>
#include <serialize.h>
#include <uint256.h>

class CTransaction;

struct CCreateWeightOracleMessage
{
    //! basic properties
    CAmount weight; // oracle amount
    CScript oracle; // address

    virtual ~CCreateWeightOracleMessage() = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(weight);
        READWRITE(oracle);
    }
};

#endif //DEFI_MASTERNODES_ORACLE_H
