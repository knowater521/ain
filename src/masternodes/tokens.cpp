// Copyright (c) 2019 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/tokens.h>
#include <core_io.h>
#include <primitives/transaction.h>

/// @attention make sure that it does not overlap with those in masternodes.cpp !!!
const unsigned char CTokensView::ID          ::prefix = 'T';
const unsigned char CTokensView::Symbol      ::prefix = 'S';
const unsigned char CTokensView::CreationTx  ::prefix = 'c';
const unsigned char CTokensView::LastDctId   ::prefix = 'L';

const DCT_ID CTokensView::DCT_ID_START = 128;

extern const std::string CURRENCY_UNIT;

std::string trim_ws(std::string const & str)
{
    std::string const ws = " \n\r\t";
    size_t first = str.find_first_not_of(ws);
    if (std::string::npos == first)
    {
        return str;
    }
    size_t last = str.find_last_not_of(ws);
    return str.substr(first, (last - first + 1));
}

CTokenImplementation::CTokenImplementation(const CTransaction & tx, int heightIn, const std::vector<unsigned char> & metadata)
{
    FromTx(tx, heightIn, metadata);
}

void CTokenImplementation::FromTx(CTransaction const & tx, int heightIn, std::vector<unsigned char> const & metadata)
{
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> static_cast<CToken &>(*this);

    creationTx = tx.GetHash();
    creationHeight = heightIn;
    destructionTx = {};
    destructionHeight = -1;
}

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

std::unique_ptr<CToken> CTokensView::ExistTokenGuessId(const std::string & str, DCT_ID & id) const
{
    std::string const key = trim_ws(str);

    if (key.empty()) {
        id = 0;
        return ExistToken(0);
    }
    if (ParseUInt32(key, &id))
        return ExistToken(id);

    uint256 tx;
    if (ParseHashStr(key, tx)) {
        auto pair = ExistTokenByCreationTx(tx);
        if (pair) {
            id = pair->first;
            return MakeUnique<CToken>(pair->second);
        }
    }
    else {
        auto pair = ExistToken(key);
        if (pair) {
            id = pair->first;
            return std::move(pair->second);
        }
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

bool CTokensView::DestroyToken(uint256 const & tokenTx, const uint256 & txid, int height)
{
    auto pair = ExistTokenByCreationTx(tokenTx);
    if (!pair) {
        LogPrintf("Token destruction error: token with creationTx %s does not exist!\n", tokenTx.ToString());
        return false;
    }
    /// @todo token: check for token supply / utxos

    CTokenImpl & tokenImpl = pair->second;
    if (tokenImpl.destructionTx != uint256{}) {
        LogPrintf("Token destruction error: token with creationTx %s was already destroyed by tx %s!\n", tokenTx.ToString(), tokenImpl.destructionTx.ToString());
        return false;
    }

    tokenImpl.destructionTx = txid;
    tokenImpl.destructionHeight = height;
    WriteBy<ID>(WrapVarInt(pair->first), tokenImpl);
    return true;
}

bool CTokensView::RevertDestroyToken(uint256 const & tokenTx, const uint256 & txid)
{
    auto pair = ExistTokenByCreationTx(tokenTx);
    if (!pair) {
        LogPrintf("Token destruction revert error: token with creationTx %s does not exist!\n", tokenTx.ToString());
        return false;
    }
    CTokenImpl & tokenImpl = pair->second;
    if (tokenImpl.destructionTx != txid) {
        LogPrintf("Token destruction revert error: token with creationTx %s was not destroyed by tx %s!\n", tokenTx.ToString(), txid.ToString());
        return false;
    }

    tokenImpl.destructionTx = uint256{};
    tokenImpl.destructionHeight = -1;
    WriteBy<ID>(WrapVarInt(pair->first), tokenImpl);
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
    DCT_ID lastDctId{DCT_ID_START};
    if (Read(LastDctId::prefix, lastDctId)) {
        return {lastDctId};
    }
    return {};
}


