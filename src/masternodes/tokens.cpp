// Copyright (c) 2019 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/tokens.h>

/// @attention make sure that it does not overlap with those in masternodes.cpp !!!
const unsigned char CTokensView::ID          ::prefix = 'T';
const unsigned char CTokensView::Symbol      ::prefix = 'S';
const unsigned char CTokensView::CreationTx  ::prefix = 'c';
const unsigned char CTokensView::LastDctId   ::prefix = 'L';

const DCT_ID CTokensView::DCT_ID_START = 128;

extern const std::string CURRENCY_UNIT;

void CStableTokens::Initialize(CStableTokens & dst)
{
    // Default CToken()
    //    : symbol("")
    //    , name("")
    //    , decimal(8)
    //    , limit(0)
    //    , flags(uint8_t(TokenFlags::Default))

    CToken DFI;
    DFI.symbol = CURRENCY_UNIT;
    DFI.name = "Default Defi token";
    /// ? what about Mintable|Tradable???

    dst.tokens[0] = DFI;
    dst.indexedBySymbol[DFI.symbol] = 0;
}

std::unique_ptr<CToken> CStableTokens::Exist(DCT_ID id) const
{
    auto it = tokens.find(id);
    if (it != tokens.end()) {
        return MakeUnique<CToken>(it->second);
    }
    return {};
}

boost::optional<std::pair<DCT_ID, std::unique_ptr<CToken>>> CStableTokens::Exist(const std::string & symbol) const
{
    auto it = indexedBySymbol.find(symbol);
    if (it != indexedBySymbol.end()) {
        auto token = Exist(it->second);
        assert(token);
        return { std::make_pair(it->second, std::move(token)) };
    }
    return {};

}

bool CStableTokens::ForEach(std::function<bool (const DCT_ID &, CToken const &)> callback) const
{
    for (auto && it = tokens.begin(); it != tokens.end(); ++it) {
        if (!callback(it->first, it->second))
            return false;
    }
    return true;
}

const CStableTokens & CStableTokens::Get()
{
    static CStableTokens dst;
    static std::once_flag initialized;
    std::call_once (initialized, CStableTokens::Initialize, dst);
    return dst;
}

std::unique_ptr<CToken> CTokensView::ExistToken(DCT_ID id) const
{
    if (id < DCT_ID_START) {
        return CStableTokens::Get().Exist(id);
    }
    auto tokenImpl = ReadBy<ID, CTokenImpl>(WrapVarInt(id));
    if (tokenImpl)
        return MakeUnique<CTokenImpl>(*tokenImpl);

    return {};
}

boost::optional<std::pair<DCT_ID, std::unique_ptr<CToken> > > CTokensView::ExistToken(const std::string & symbol) const
{
    auto dst = CStableTokens::Get().Exist(symbol);
    if (dst)
        return dst;

    DCT_ID id;
    auto varint = WrapVarInt(id);
    if (ReadBy<Symbol, std::string>(symbol, varint)) {
        assert(id >= DCT_ID_START);
        return { std::make_pair(id, std::move(ExistToken(id)))};
    }
    return {};
}

boost::optional<std::pair<DCT_ID, CTokensView::CTokenImpl> > CTokensView::ExistTokenByCreationTx(const uint256 & txid) const
{
    DCT_ID id;
    auto varint = WrapVarInt(id);
    if (ReadBy<CreationTx, uint256>(txid, varint)) {
        auto tokenImpl = ReadBy<ID, CTokenImpl>(varint);
        if (tokenImpl)
            return { std::make_pair(id, std::move(*tokenImpl))};
    }
    return {};
}

void CTokensView::ForEachToken(std::function<bool (const DCT_ID &, const CToken &)> callback)
{
    if (!CStableTokens::Get().ForEach(callback))
        return; // if was inturrupted

    DCT_ID tokenId{0};
    auto hint = WrapVarInt(tokenId);

    ForEach<ID, CVarInt<VarIntMode::DEFAULT, DCT_ID>, CTokenImpl>([&tokenId, &callback] (CVarInt<VarIntMode::DEFAULT, DCT_ID> const &, CTokenImpl tokenImpl) {
        // is id catched here?
        return callback(tokenId, tokenImpl);
    }, hint);

}

bool CTokensView::CreateToken(const CTokensView::CTokenImpl & token)
{
    if (ExistToken(token.symbol)) {
        LogPrintf("Tokens creation error: token '%s' already exists!\n", token.symbol);
        return false;
    }
    // this should not happen, but for sure
    if (ExistTokenByCreationTx(token.creationTx)) {
        LogPrintf("Token creation error: token with creation tx %s already exists!\n", token.creationTx.ToString());
        return false;
    }
    DCT_ID id = IncrementLastDctId();
    WriteBy<ID>(WrapVarInt(id), token);
    WriteBy<Symbol>(token.symbol, WrapVarInt(id));
    WriteBy<CreationTx>(token.creationTx, WrapVarInt(id));
    return true;
}

bool CTokensView::RevertCreateToken(const uint256 & txid)
{
    auto pair = ExistTokenByCreationTx(txid);
    if (!pair) {
        LogPrintf("Token creation revert error: token with creation tx %s does not exist!\n", txid.ToString());
        return false;
    }
    DCT_ID id = pair->first;
    auto lastId = ReadLastDctId();
    if (!lastId || (*lastId) != id) {
        LogPrintf("Token creation revert error: revert sequence broken! (txid = %s, id = %d, LastDctId = %d)\n", txid.ToString(), id, (lastId ? *lastId : 0));
        return false;
    }
    auto const & token = pair->second;
    EraseBy<ID>(WrapVarInt(id));
    EraseBy<Symbol>(token.symbol);
    EraseBy<CreationTx>(token.creationTx);
    DecrementLastDctId();
    return true;
}

bool CTokensView::DestroyToken(DCT_ID id, const uint256 & txid, int height)
{
    if (id < DCT_ID_START) {
        LogPrintf("Token destruction error: trying to destroy stable token with DCT_ID %d\n", id);
        return false;
    }
    auto token = ExistToken(id);
    if (!token) {
        LogPrintf("Token destruction error: token with DCT_ID %d does not exist!\n", id);
        return false;
    }
    /// @todo token: check for token supply / utxos

    CTokenImpl* tokenImpl = static_cast<CTokenImplementation*>(token.get());
    assert(tokenImpl);
    if (tokenImpl->destructionTx != uint256{}) {
        LogPrintf("Token destruction error: token with DCT_ID %d was already destroyed by tx %s!\n", id, tokenImpl->destructionTx.ToString());
        return false;
    }

    tokenImpl->destructionTx = txid;
    tokenImpl->destructionHeight = height;
    WriteBy<ID>(WrapVarInt(id), *tokenImpl);
    return true;
}

bool CTokensView::RevertDestroyToken(DCT_ID id, const uint256 & txid)
{
    if (id < DCT_ID_START) {
        LogPrintf("Token destruction revert error: trying to destroy stable token with DCT_ID %d\n", id);
        return false;
    }
    auto token = ExistToken(id);
    if (!token) {
        LogPrintf("Token destruction revert error: token with DCT_ID %d does not exist!\n", id);
        return false;
    }
    auto tokenImpl = static_cast<CTokenImpl*>(token.get());
    assert(tokenImpl);
    if (tokenImpl->destructionTx != txid) {
        LogPrintf("Token destruction revert error: token with DCT_ID %d was not destroyed by tx %s!\n", id, txid.ToString());
        return false;
    }

    tokenImpl->destructionTx = uint256{};
    tokenImpl->destructionHeight = -1;
    WriteBy<ID>(WrapVarInt(id), *tokenImpl);
    return true;
}

DCT_ID CTokensView::IncrementLastDctId()
{
    DCT_ID result{DCT_ID_START};
    auto lastDctId = ReadLastDctId();
    if (lastDctId) {
        result = std::max(*lastDctId + 1, result);
    }
    assert (Write(LastDctId::prefix, result));
    return result;
}

DCT_ID CTokensView::DecrementLastDctId()
{
    auto lastDctId = ReadLastDctId();
    if (lastDctId && *lastDctId >= DCT_ID_START) {
        --(*lastDctId);
    }
    else {
        LogPrintf("Critical fault: trying to decrement nonexistent DCT_ID or it is lower than DCT_ID_START\n");
        assert (false);
    }
    assert (Write(LastDctId::prefix, *lastDctId)); // it is ok if (DCT_ID_START - 1) will be written
    return *lastDctId;
}

boost::optional<DCT_ID> CTokensView::ReadLastDctId() const
{
    DCT_ID lastDctId;
    if (Read(LastDctId::prefix, lastDctId)) {
        return {lastDctId};
    }
    return {};
}
