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
class CCoinsViewCache;

class CCustomCSView;

static const std::vector<unsigned char> DfTxMarker = {'D', 'f', 'T', 'x'};  // 44665478
/// @todo refactor it to unify txs!!! (need to restart blockchain)
static const std::vector<unsigned char> DfCriminalTxMarker = {'D', 'f', 'C', 'r'};
static const std::vector<unsigned char> DfAnchorFinalizeTxMarker = {'D', 'f', 'A', 'f'};

enum class CustomTxType : unsigned char
{
    None = 0,
    // masternodes:
    CreateMasternode    = 'C',
    ResignMasternode    = 'R',
    // custom tokens:
    CreateToken         = 'T',
    MintToken           = 'M',
    DestroyToken        = 'D'
};

inline CustomTxType CustomTxCodeToType(unsigned char ch) {
    char const txtypes[] = "CRTMD";
    if (memchr(txtypes, ch, strlen(txtypes)))
        return static_cast<CustomTxType>(ch);
    else
        return CustomTxType::None;
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

bool CheckCustomTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, const Consensus::Params& consensusParams, int height, int txn, bool isCheck = true);
//! Deep check (and write)
bool CheckCreateMasternodeTx(CCustomCSView & mnview, CTransaction const & tx, int height, int txn, std::vector<unsigned char> const & metadata, bool isCheck);
bool CheckResignMasternodeTx(CCustomCSView & mnview, CTransaction const & tx, int height, int txn, std::vector<unsigned char> const & metadata, bool isCheck);

bool CheckCreateTokenTx(CCustomCSView & mnview, CTransaction const & tx, int height, int txn, std::vector<unsigned char> const & metadata, bool isCheck);
bool CheckDestroyTokenTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, int height, int txn, std::vector<unsigned char> const & metadata, bool isCheck);
bool CheckMintTokenTx(CCustomCSView & mnview, CTransaction const & tx, int height, int txn, std::vector<unsigned char> const & metadata, bool isCheck);

bool IsMempooledCustomTxCreate(const CTxMemPool& pool, const uint256 & txid);

//! Checks if given tx is probably one of 'CustomTx', returns tx type and serialized metadata in 'data'
CustomTxType GuessCustomTxType(CTransaction const & tx, std::vector<unsigned char> & metadata);
bool IsCriminalProofTx(CTransaction const & tx, std::vector<unsigned char> & metadata);
bool IsAnchorRewardTx(CTransaction const & tx, std::vector<unsigned char> & metadata);

#endif // DEFI_MASTERNODES_MN_CHECKS_H
