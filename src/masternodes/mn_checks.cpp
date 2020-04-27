// Copyright (c) 2019 DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/mn_checks.h>

#include <arith_uint256.h>
#include <chainparams.h>
#include <logging.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <txmempool.h>
#include <streams.h>
#include <univalue/include/univalue.h>

#include <algorithm>
#include <sstream>

using namespace std;

CPubKey GetPubkeyFromScriptSig(CScript const & scriptSig)
{
    CScript::const_iterator pc = scriptSig.begin();
    opcodetype opcode;
    std::vector<unsigned char> data;
    // Signature first, then pubkey. I think, that in all cases it will be OP_PUSHDATA1, but...
    if (!scriptSig.GetOp(pc, opcode, data) ||
            (opcode > OP_PUSHDATA1 &&
             opcode != OP_PUSHDATA2 &&
             opcode != OP_PUSHDATA4))
    {
        return CPubKey();
    }
    if (!scriptSig.GetOp(pc, opcode, data) ||
            (opcode > OP_PUSHDATA1 &&
             opcode != OP_PUSHDATA2 &&
             opcode != OP_PUSHDATA4))
    {
        return CPubKey();
    }
    return CPubKey(data);
}

bool HasAuth(CTransaction const & tx, CKeyID const & auth)
{
    for (auto input : tx.vin)
    {
        if (input.scriptWitness.IsNull()) {
            if (GetPubkeyFromScriptSig(input.scriptSig).GetID() == auth)
               return true;
        }
        else {
            /// @todo EXTEND IT TO SUPPORT WITNESS!!
            auto test = CPubKey(input.scriptWitness.stack.back());
            if (test.GetID() == auth)
               return true;
        }
    }
    return false;
}

bool HasAuth(CTransaction const & tx, const CCoinsViewCache &coins, uint256 const & tokenTx)
{
    const Coin& auth = coins.AccessCoin(COutPoint(tokenTx, 1));
    for (auto input : tx.vin) {
        const Coin& coin = coins.AccessCoin(input.prevout);
        assert(!coin.IsSpent());
        if (coin.out.scriptPubKey == auth.out.scriptPubKey)
            return true;
    }
    return false;
}

bool CheckCustomTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, Consensus::Params const & consensusParams, int height, int txn, bool isCheck)
{
    bool result = true;

    if (tx.vout.size() > 0)
    {
        // Check if it is masternode tx with metadata
        std::vector<unsigned char> metadata;
        CustomTxType guess = GuessCustomTxType(tx, metadata);
        switch (guess)
        {
            case CustomTxType::CreateMasternode:
                result = result && CheckCreateMasternodeTx(mnview, tx, height, txn, metadata, isCheck);
            break;
            case CustomTxType::ResignMasternode:
                result = result && CheckResignMasternodeTx(mnview, tx, height, txn, metadata, isCheck);
            break;
            case CustomTxType::CreateToken:
                result = result && CheckCreateTokenTx(mnview, tx, height, txn, metadata, isCheck);
            break;
            case CustomTxType::DestroyToken:
                result = result && CheckDestroyTokenTx(mnview, coins, tx, height, txn, metadata, isCheck);
            break;
            default:
                break;
        }
    }
    // We are always accept blocks (but skip failed txs) and fails only if it only check (but not real block processing)
    return isCheck ? result : true;
}

/*
 * Checks if given tx is 'txCreateMasternode'. Creates new MN if all checks are passed
 * Issued by: any
 */
bool CheckCreateMasternodeTx(CCustomCSView & mnview, CTransaction const & tx, int height, int txn, std::vector<unsigned char> const & metadata, bool isCheck)
{
    // Check quick conditions first
    if (tx.vout.size() < 2 ||
        tx.vout[0].nValue < GetMnCreationFee(height) || tx.vout[0].nTokenId != 0 ||
        tx.vout[1].nValue != GetMnCollateralAmount() || tx.vout[1].nTokenId != 0
        ) {
        return false;
    }
    CMasternode node(tx, height, metadata);
    bool result = mnview.CreateMasternode(tx.GetHash(), node, txn);
    if (!isCheck) {
        LogPrintf("MN %s: Creation by tx %s at block %d\n", result ? "APPLYED" : "SKIPPED", tx.GetHash().GetHex(), height);
    }
    return result;
}

bool CheckResignMasternodeTx(CCustomCSView & mnview, CTransaction const & tx, int height, int txn, const std::vector<unsigned char> & metadata, bool isCheck)
{
    uint256 const nodeId(metadata);
    auto const node = mnview.ExistMasternode(nodeId);
    if (!node || !HasAuth(tx, node->ownerAuthAddress))
        return false;

    bool result = mnview.ResignMasternode(nodeId, tx.GetHash(), height, txn);
    if (!isCheck) {
        LogPrintf("MN %s: Resign by tx %s at block %d\n", result ? "APPLYED" : "SKIPPED", tx.GetHash().GetHex(), height);
    }
    return result;
}

bool CheckCreateTokenTx(CCustomCSView & mnview, CTransaction const & tx, int height, int, std::vector<unsigned char> const & metadata, bool isCheck)
{
    // Check quick conditions first
    if (tx.vout.size() < 2 ||
        tx.vout[0].nValue < GetTokenCreationFee(height) || tx.vout[0].nTokenId != 0 ||
        tx.vout[1].nValue != GetTokenCollateralAmount() || tx.vout[1].nTokenId != 0
        ) {
        return false;
    }
    CTokenImplementation token(tx, height, metadata);
    bool result = mnview.CreateToken(token);
    if (!isCheck) {
        LogPrintf("Token %s: Creation '%s' by tx %s at block %d\n", result ? "APPLYED" : "SKIPPED", token.symbol, tx.GetHash().GetHex(), height);
    }
    return result;
}

bool CheckDestroyTokenTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, int height, int, std::vector<unsigned char> const & metadata, bool isCheck)
{
    uint256 const tokenTx(metadata);
    auto pair = mnview.ExistTokenByCreationTx(tokenTx);
    if (!pair) {
        return false;
    }
    CTokenImplementation const & token = pair->second;
    if (!HasAuth(tx, coins, token.creationTx)) {
        return false;
    }

    bool result = mnview.DestroyToken(token.creationTx, tx.GetHash(), height);
    if (!isCheck) {
        LogPrintf("Token %s: Destruction '%s' by tx %s at block %d\n", result ? "APPLYED" : "SKIPPED", token.symbol, tx.GetHash().GetHex(), height);
    }
    return result;
}

bool IsMempooledCustomTxCreate(const CTxMemPool & pool, const uint256 & txid)
{
    CTransactionRef ptx = pool.get(txid);
    std::vector<unsigned char> dummy;
    if (ptx) {
        CustomTxType txType = GuessCustomTxType(*ptx, dummy);
        return txType == CustomTxType::CreateMasternode || txType == CustomTxType::CreateToken;
    }
    return false;
}

bool IsCriminalProofTx(CTransaction const & tx, std::vector<unsigned char> & metadata)
{
    if (!tx.IsCoinBase() || tx.vout.size() != 1 || tx.vout[0].nValue != 0) {
        return false;
    }
    CScript const & memo = tx.vout[0].scriptPubKey;
    CScript::const_iterator pc = memo.begin();
    opcodetype opcode;
    if (!memo.GetOp(pc, opcode) || opcode != OP_RETURN) {
        return false;
    }
    if (!memo.GetOp(pc, opcode, metadata) ||
        (opcode > OP_PUSHDATA1 &&
         opcode != OP_PUSHDATA2 &&
         opcode != OP_PUSHDATA4) ||
        metadata.size() < DfCriminalTxMarker.size() + 1 ||
        memcmp(&metadata[0], &DfCriminalTxMarker[0], DfCriminalTxMarker.size()) != 0) {
        return false;
    }
    metadata.erase(metadata.begin(), metadata.begin() + DfCriminalTxMarker.size());
    return true;
}

bool IsAnchorRewardTx(CTransaction const & tx, std::vector<unsigned char> & metadata)
{
    if (!tx.IsCoinBase() || tx.vout.size() != 2 || tx.vout[0].nValue != 0) {
        return false;
    }
    CScript const & memo = tx.vout[0].scriptPubKey;
    CScript::const_iterator pc = memo.begin();
    opcodetype opcode;
    if (!memo.GetOp(pc, opcode) || opcode != OP_RETURN) {
        return false;
    }
    if (!memo.GetOp(pc, opcode, metadata) ||
        (opcode > OP_PUSHDATA1 &&
         opcode != OP_PUSHDATA2 &&
         opcode != OP_PUSHDATA4) ||
        metadata.size() < DfAnchorFinalizeTxMarker.size() + 1 ||
        memcmp(&metadata[0], &DfAnchorFinalizeTxMarker[0], DfAnchorFinalizeTxMarker.size()) != 0) {
        return false;
    }
    metadata.erase(metadata.begin(), metadata.begin() + DfAnchorFinalizeTxMarker.size());
    return true;
}


/*
 * Checks if given tx is probably one of 'CustomTx', returns tx type and serialized metadata in 'data'
*/
CustomTxType GuessCustomTxType(CTransaction const & tx, std::vector<unsigned char> & metadata)
{
    if (tx.vout.size() == 0)
    {
        return CustomTxType::None;
    }
    CScript const & memo = tx.vout[0].scriptPubKey;
    CScript::const_iterator pc = memo.begin();
    opcodetype opcode;
    if (!memo.GetOp(pc, opcode) || opcode != OP_RETURN)
    {
        return CustomTxType::None;
    }
    if (!memo.GetOp(pc, opcode, metadata) ||
            (opcode > OP_PUSHDATA1 &&
             opcode != OP_PUSHDATA2 &&
             opcode != OP_PUSHDATA4) ||
            metadata.size() < DfTxMarker.size() + 1 ||     // i don't know how much exactly, but at least MnTxSignature + type prefix
            memcmp(&metadata[0], &DfTxMarker[0], DfTxMarker.size()) != 0)
    {
        return CustomTxType::None;
    }
    auto txType = CustomTxCodeToType(metadata[DfTxMarker.size()]);
    metadata.erase(metadata.begin(), metadata.begin() + DfTxMarker.size() + 1);
    return txType;
}


bool IsMintTokenTx(const CTransaction & tx)
{
    std::vector<unsigned char> metadata;
    return GuessCustomTxType(tx, metadata) == CustomTxType::MintToken;
}
