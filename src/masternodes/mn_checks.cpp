// Copyright (c) 2019 DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/mn_checks.h>
#include <masternodes/res.h>
#include <masternodes/balances.h>

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
#include <cstring>

using namespace std;

static ResVal<CBalances> BurntTokens(CTransaction const & tx) {
    CBalances balances{};
    for (const auto& out : tx.vout) {
        if (out.scriptPubKey.size() > 0 && out.scriptPubKey[0] == OP_RETURN) {
            auto res = balances.Add(out.TokenAmount());
            if (!res.ok) {
                return {res};
            }
        }
    }
    return {balances, Res::Ok()};
}

static ResVal<CBalances> MintedTokens(CTransaction const & tx, uint32_t mintingOutputsStart) {
    CBalances balances{};
    for (uint32_t i = mintingOutputsStart; i < (uint32_t) tx.vout.size(); i++) {
        auto res = balances.Add(tx.vout[i].TokenAmount());
        if (!res.ok) {
            return {res};
        }
    }
    return {balances, Res::Ok()};
}

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
            auto test = CPubKey(input.scriptWitness.stack.back());
            if (test.GetID() == auth)
               return true;
        }
    }
    return false;
}

bool HasAuth(CTransaction const & tx, CCoinsViewCache const & coins, CScript const & auth)
{
    for (auto input : tx.vin) {
        const Coin& coin = coins.AccessCoin(input.prevout);
        assert(!coin.IsSpent());
        if (coin.out.scriptPubKey == auth)
            return true;
    }
    return false;
}

bool HasTokenAuth(CTransaction const & tx, CCoinsViewCache const & coins, uint256 const & tokenTx)
{
    const Coin& auth = coins.AccessCoin(COutPoint(tokenTx, 1));
    return HasAuth(tx, coins, auth.out.scriptPubKey);
}

// @todo get rid of "is check" argument and logging inside of this function
Res ApplyCustomTx(CCustomCSView & base_mnview, CCoinsViewCache const & coins, CTransaction const & tx, Consensus::Params const & consensusParams, uint32_t height, bool isCheck)
{
    Res res = Res::Ok();

    if ((tx.IsCoinBase() && height > 0) || tx.vout.empty()) { // genesis contains custom coinbase txs
        return Res::Ok(); // not "custom" tx
    }

    CCustomCSView mnview(base_mnview);

    try {
        // Check if it is custom tx with metadata
        std::vector<unsigned char> metadata;
        CustomTxType guess = GuessCustomTxType(tx, metadata);
        switch (guess)
        {
            case CustomTxType::CreateMasternode:
                res.ok = CheckCreateMasternodeTx(mnview, tx, height, metadata, isCheck); // @todo return Res
                break;
            case CustomTxType::ResignMasternode:
                res.ok = CheckResignMasternodeTx(mnview, tx, height, metadata, isCheck);
                break;
            case CustomTxType::CreateToken:
                res.ok = CheckCreateTokenTx(mnview, tx, height, metadata, isCheck);
                break;
            case CustomTxType::DestroyToken:
                res.ok = CheckDestroyTokenTx(mnview, coins, tx, height, metadata, isCheck);
                break;
            case CustomTxType::CreateOrder:
                res = ApplyCreateOrderTx(mnview, coins, tx, height, metadata);
                break;
            case CustomTxType::DestroyOrder:
                res = ApplyDestroyOrderTx(mnview, coins, tx, height, metadata);
                break;
            case CustomTxType::UtxosToAccount:
                res = ApplyUtxosToAccountTx(mnview, tx, metadata);
                break;
            case CustomTxType::AccountToUtxos:
                res = ApplyAccountToUtxosTx(mnview, coins, tx, metadata);
                break;
            case CustomTxType::AccountToAccount:
                res = ApplyAccountToAccountTx(mnview, coins, tx, metadata);
                break;
            default:
                return Res::Ok(); // not "custom" tx
        }
        // list of transactions which aren't allowed to fail:
        if (!res.ok && NotAllowedToFail(guess)) {
            res.code |= CustomTxErrCodes::Fatal;
        }
    } catch (std::exception& e) {
        res = Res::Err(e.what());
    } catch (...) {
        res = Res::Err("unexpected error");
    }

    if (!res.ok) {
        return res;
    }

    // construct undo
    auto& flushable = dynamic_cast<CFlushableStorageKV&>(mnview.GetRaw());
    auto undo = CUndo::Construct(base_mnview.GetRaw(), flushable.GetRaw());
    // flush changes
    mnview.Flush();
    // write undo
    if (!undo.before.empty()) {
        base_mnview.SetUndo(UndoKey{height, tx.GetHash()}, undo);
    }

    return res;
}

/*
 * Checks if given tx is 'txCreateMasternode'. Creates new MN if all checks are passed
 * Issued by: any
 */
bool CheckCreateMasternodeTx(CCustomCSView & mnview, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, bool isCheck)
{
    // Check quick conditions first
    if (tx.vout.size() < 2 ||
        tx.vout[0].nValue < GetMnCreationFee(height) || tx.vout[0].nTokenId != DCT_ID{0} ||
        tx.vout[1].nValue != GetMnCollateralAmount() || tx.vout[1].nTokenId != DCT_ID{0}
        ) {
        return false;
    }
    CMasternode node(tx, height, metadata);
    bool result = mnview.CreateMasternode(tx.GetHash(), node);
    if (!isCheck) {
        LogPrintf("MN %s: Creation by tx %s at block %d\n", result ? "APPLIED" : "SKIPPED", tx.GetHash().GetHex(), height);
    }
    return result;
}

bool CheckResignMasternodeTx(CCustomCSView & mnview, CTransaction const & tx, uint32_t height, const std::vector<unsigned char> & metadata, bool isCheck)
{
    uint256 const nodeId(metadata);
    auto const node = mnview.ExistMasternode(nodeId);
    if (!node || !HasAuth(tx, node->ownerAuthAddress))
        return false;

    bool result = mnview.ResignMasternode(nodeId, tx.GetHash(), height);
    if (!isCheck) {
        LogPrintf("MN %s: Resign by tx %s at block %d\n", result ? "APPLIED" : "SKIPPED", tx.GetHash().GetHex(), height);
    }
    return result;
}

// @todo All CheckXXXTx in this file should be renamed to ApplyXXXTx
bool CheckCreateTokenTx(CCustomCSView & mnview, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, bool isCheck)
{
    // Check quick conditions first
    if (tx.vout.size() < 2 ||
        tx.vout[0].nValue < GetTokenCreationFee(height) || tx.vout[0].nTokenId != DCT_ID{0} ||
        tx.vout[1].nValue != GetTokenCollateralAmount() || tx.vout[1].nTokenId != DCT_ID{0}
        ) {
        return false;
    }
    CTokenImplementation token(tx, height, metadata);
    bool result = mnview.CreateToken(token);
    if (!isCheck) {
        LogPrintf("Token %s: Creation '%s' by tx %s at block %d\n", result ? "APPLIED" : "SKIPPED", token.symbol, tx.GetHash().GetHex(), height);
    }
    return result;
}

bool CheckDestroyTokenTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata, bool isCheck)
{
    uint256 const tokenTx(metadata);
    auto pair = mnview.ExistTokenByCreationTx(tokenTx);
    if (!pair) {
        return false;
    }
    CTokenImplementation const & token = pair->second;
    if (!HasTokenAuth(tx, coins, token.creationTx)) {
        return false;
    }

    bool result = mnview.DestroyToken(token.creationTx, tx.GetHash(), height);
    if (!isCheck) {
        LogPrintf("Token %s: Destruction '%s' by tx %s at block %d\n", result ? "APPLIED" : "SKIPPED", token.symbol, tx.GetHash().GetHex(), height);
    }
    return result;
}

Res ApplyCreateOrderTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata)
{
    // Check quick conditions first
    CCreateOrderMessage orderMsg;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> orderMsg; // @todo make Apply* functions fully exception-less for additional clarity. add helper function for deserialization
    if (!ss.empty()) {
        return Res::Err("Creation of order: deserialization failed: excess %d bytes", ss.size());
    }
    COrder order(orderMsg, height);
    const auto base = tfm::format("Creation of order, take=%s, give=%s, premium=%s", order.take.ToString(), order.give.ToString(), order.premium.ToString());

    if (order.give.nValue == 0 || order.take.nValue == 0) {
        return Res::Err("%s: %s", base, "zero order value(s)");
    }
    if (order.give.nTokenId == order.take.nTokenId) {
        return Res::Err("%s: %s", base, "token IDs to buy/sell must be different");
    }
    // check auth
    if (!HasAuth(tx, coins, order.owner)) {
        return Res::Err("%s: %s", base, "tx must have at least one input from order owner");
    }
    // subtract funds
    auto res = mnview.SubBalance(order.owner, order.give);
    if (!res.ok) {
        return Res::ErrCode(CustomTxErrCodes::NotEnoughBalance, "%s: %s", base, res.msg);
    }
    res = mnview.SubBalance(order.owner, order.premium);
    if (!res.ok) {
        return Res::ErrCode(CustomTxErrCodes::NotEnoughBalance, "%s: %s", base, res.msg);
    }
    // check tokens are tradeable
    const auto tokenTake = mnview.ExistToken(order.take.nTokenId);
    if (!tokenTake || !tokenTake->IsTradeable()) {
        return Res::Err("%s: tokenID %s isn't tradeable", base, order.take.nTokenId.ToString());
    }
    const auto tokenGive = mnview.ExistToken(order.take.nTokenId);
    if (!tokenGive || !tokenGive->IsTradeable()) {
        return Res::Err("%s: tokenID %s isn't tradeable", base, order.give.nTokenId.ToString());
    }

    res = mnview.SetOrder(tx.GetHash(), order);
    if (!res.ok) {
        return Res::Err("%s: %s", base, res.msg);
    }
    return Res::Ok(base);
}

Res ApplyDestroyOrderTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, uint32_t height, std::vector<unsigned char> const & metadata)
{
    if (metadata.size() != sizeof(uint256)) {
        return Res::Err("Order destruction: %s", "metadata must contain 32 bytes");
    }
    uint256 const orderTx(metadata);
    const auto base = tfm::format("Destruction of order %s", orderTx.GetHex());
    auto order = mnview.GetOrder(orderTx);
    if (!order) {
        return Res::Err("%s: %s", base, "order not found");
    }
    const bool isExpired = order->timeInForce != 0 && height >= (order->creationHeight + order->timeInForce);
    if (!isExpired && !HasAuth(tx, coins, order->owner)) {
        return Res::Err("%s: %s", base, "non-expired order destruction isn't authorized");
    }

    // defund tokens
    auto res = mnview.AddBalance(order->owner, order->give);
    if (!res.ok) {
        return Res::Err("%s: %s", base, res.msg);
    }
    res = mnview.AddBalance(order->owner, order->premium);
    if (!res.ok) {
        return Res::Err("%s: %s", base, res.msg);
    }

    res = mnview.DelOrder(orderTx);
    if (!res.ok) {
        return Res::Err("%s: %s", base, res.msg);
    }
    return Res::Ok(base);
}

Res ApplyUtxosToAccountTx(CCustomCSView & mnview, CTransaction const & tx, std::vector<unsigned char> const & metadata)
{
    // deserialize
    CUtxosToAccountMessage msg;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> msg;
    if (!ss.empty()) {
        return Res::Err("UtxosToAccount tx deserialization failed: excess %d bytes", ss.size());
    }
    const auto base = tfm::format("Transfer UtxosToAccount: %s", msg.ToString());

    // check enough tokens are "burnt"
    const auto burnt = BurntTokens(tx);
    CBalances mustBeBurnt = SumAllTransfers(msg.to);
    if (!burnt.ok) {
        return Res::Err("%s: %s", base, burnt.msg);
    }
    if (burnt.val->balances != mustBeBurnt.balances) {
        return Res::Err("%s: transfer tokens mismatch burnt tokens: (%s) != (%s)", base, mustBeBurnt.ToString(), burnt.val->ToString());
    }
    // transfer
    for (const auto& kv : msg.to) {
        const auto res = mnview.AddBalances(kv.first, kv.second);
        if (!res.ok) {
            return Res::Err("%s: %s", base, res.msg);
        }
    }
    return Res::Ok(base);
}

Res ApplyAccountToUtxosTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, std::vector<unsigned char> const & metadata)
{
    // deserialize
    CAccountToUtxosMessage msg;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> msg;
    if (!ss.empty()) {
        return Res::Err("AccountToUtxos tx deserialization failed: excess %d bytes", ss.size());
    }
    const auto base = tfm::format("Transfer AccountToUtxos: %s", msg.ToString());

    // check auth
    if (!HasAuth(tx, coins, msg.from)) {
        return Res::Err("%s: %s", base, "tx must have at least one input from account owner");
    }
    // check that all tokens are minted, and no excess tokens are minted
    const auto minted = MintedTokens(tx, msg.mintingOutputsStart);
    if (!minted.ok) {
        return Res::Err("%s: %s", base, minted.msg);
    }
    if (msg.balances != *minted.val) {
        return Res::Err("%s: amount of minted tokens in UTXOs and metadata do not match: (%s) != (%s)", base, minted.val->ToString(), msg.balances.ToString());
    }
    // transfer
    const auto res = mnview.SubBalances(msg.from, msg.balances);
    if (!res.ok) {
        return Res::ErrCode(CustomTxErrCodes::NotEnoughBalance, "%s: %s", base, res.msg);
    }
    return Res::Ok(base);
}

Res ApplyAccountToAccountTx(CCustomCSView & mnview, CCoinsViewCache const & coins, CTransaction const & tx, std::vector<unsigned char> const & metadata)
{
    // deserialize
    CAccountToAccountMessage msg;
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> msg;
    if (!ss.empty()) {
        return Res::Err("AccountToAccount tx deserialization failed: excess %d bytes", ss.size());
    }
    const auto base = tfm::format("Transfer AccountToAccount: %s", msg.ToString());

    // check auth
    if (!HasAuth(tx, coins, msg.from)) {
        return Res::Err("%s: %s", base, "tx must have at least one input from account owner");
    }
    // transfer
    auto res = mnview.SubBalances(msg.from, SumAllTransfers(msg.to));
    if (!res.ok) {
        return Res::ErrCode(CustomTxErrCodes::NotEnoughBalance, "%s: %s", base, res.msg);
    }
    for (const auto& kv : msg.to) {
        const auto res = mnview.AddBalances(kv.first, kv.second);
        if (!res.ok) {
            return Res::Err("%s: %s", base, res.msg);
        }
    }
    return Res::Ok(base);
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
