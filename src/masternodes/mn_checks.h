// Copyright (c) 2019 DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_MN_CHECKS_H
#define DEFI_MASTERNODES_MN_CHECKS_H

#include <consensus/params.h>
#include <masternodes/masternodes.h>
#include <vector>

class CBlock;
class CTransaction;
class CTxMemPool;

class CCustomCSView;

static const std::vector<unsigned char> DfTxMarker = {'D', 'f', 'T', 'x'};  // 44665478
/// @todo refactor it to unify txs!!! (need to restart blockchain)
static const std::vector<unsigned char> DfCriminalTxMarker = {'D', 'f', 'C', 'r'};
static const std::vector<unsigned char> DfAnchorFinalizeTxMarker = {'D', 'f', 'A', 'f'};

enum class CustomTxType : unsigned char
{
    None = 0,
    CreateMasternode    = 'C',
    ResignMasternode    = 'R',
};

inline CustomTxType CustomTxCodeToType(unsigned char ch) {
    switch (ch) {
        case 'C':
        case 'R':
            return static_cast<CustomTxType>(ch);
        default:
            return CustomTxType::None;
    }
}

template<typename Stream>
inline void Serialize(Stream& s, CustomTxType txType)
{
    Serialize(s, static_cast<unsigned char>(txType));
}

template<typename Stream>
inline void Unserialize(Stream& s, CustomTxType & txType) {
    unsigned char ch;
    Unserialize(s, ch);

    txType = CustomTxCodeToType(ch);
}

bool CheckMasternodeTx(CCustomCSView & mnview, CTransaction const & tx, const Consensus::Params& consensusParams, int height, int txn, bool isCheck = true);

bool CheckInputsForCollateralSpent(CCustomCSView & mnview, CTransaction const & tx, int nHeight, bool isCheck);
//! Deep check (and write)
bool CheckCreateMasternodeTx(CCustomCSView & mnview, CTransaction const & tx, int height, int txn, std::vector<unsigned char> const & metadata, bool isCheck);
bool CheckResignMasternodeTx(CCustomCSView & mnview, CTransaction const & tx, int height, int txn, std::vector<unsigned char> const & metadata, bool isCheck);

bool IsMempooledMnCreate(const CTxMemPool& pool, const uint256 & txid);

//! Checks if given tx is probably one of custom 'MasternodeTx', returns tx type and serialized metadata in 'data'
CustomTxType GuessCustomTxType(CTransaction const & tx, std::vector<unsigned char> & metadata);
bool IsCriminalProofTx(CTransaction const & tx, std::vector<unsigned char> & metadata);
bool IsAnchorRewardTx(CTransaction const & tx, std::vector<unsigned char> & metadata);

#endif // DEFI_MASTERNODES_MN_CHECKS_H
