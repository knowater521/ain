// Copyright (c) 2019 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_TOKENS_H
#define DEFI_MASTERNODES_TOKENS_H

#include <flushablestorage.h>

#include <amount.h>
#include <script/script.h>
#include <serialize.h>
#include <uint256.h>

class CKey;
class CPubkey;

using DCT_ID = uint32_t;    // VARINT ?

class CToken
{
public:
    static const uint8_t MAX_TOKEN_NAME_LENGTH = 128;
    static const uint8_t MAX_TOKEN_SYMBOL_LENGTH = 8;
    enum class TokenFlags : uint8_t
    {
        None = 0,
        Mintable = 0x01,
        Tradeable = 0x02,
        Default = TokenFlags::Mintable | TokenFlags::Tradeable
    };

    //! basic properties
    std::string symbol;
    std::string name;
    uint8_t decimal;    // now fixed to 8
    CAmount limit;      // now isn't tracked
    uint8_t flags;      // minting support, tradeability

    CToken()
        : symbol("")
        , name("")
        , decimal(8)
        , limit(0)
        , flags(uint8_t(TokenFlags::Default))
    {}

    bool IsMintable() const
    {
        return flags & (uint8_t)TokenFlags::Mintable;
    }
    bool IsTradeable() const
    {
        return flags & (uint8_t)TokenFlags::Tradeable;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(symbol);
        READWRITE(name);
        READWRITE(decimal);
        READWRITE(limit);
        READWRITE(flags);
    }
};

class CTokenImplementation : public CToken
{
public:
    //! tx related properties
    CScript owner;
    uint256 creationTx;
    uint256 destructionTx;
    int32_t creationHeight;
    int32_t destructionHeight;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CToken, *this);
        READWRITE(owner);
        READWRITE(creationTx);
        READWRITE(destructionTx);
        READWRITE(creationHeight);
        READWRITE(destructionHeight);
    }
};

class CStableTokens {
    CStableTokens() = default;
    ~CStableTokens() = default;
    CStableTokens(CStableTokens const &) = delete;
    CStableTokens& operator=(CStableTokens const &) = delete;

    static void Initialize(CStableTokens & dst);

    std::map<DCT_ID, CToken> tokens;
    std::map<std::string, DCT_ID> indexedBySymbol;

public:
    static CStableTokens const & Get();
    std::unique_ptr<CToken> Exist(DCT_ID id) const;
    boost::optional<std::pair<DCT_ID, std::unique_ptr<CToken>>> Exist(std::string const & symbol) const;
    bool ForEach(std::function<bool(DCT_ID const & id, CToken const & token)> callback) const;
};


class CTokensView : public virtual CStorageView
{
public:
    static const DCT_ID DCT_ID_START; // = 128;
    static const unsigned char DB_TOKEN_LASTID; // = 'L';

    using CTokenImpl = CTokenImplementation;
    std::unique_ptr<CToken> ExistToken(DCT_ID id) const;
    boost::optional<std::pair<DCT_ID, std::unique_ptr<CToken>>> ExistToken(std::string const & symbol) const;
    // the only possible type of token (with creationTx) is CTokenImpl
    boost::optional<std::pair<DCT_ID, CTokenImpl>> ExistTokenByCreationTx(uint256 const & txid) const;

    void ForEachToken(std::function<bool(DCT_ID const & id, CToken const & token)> callback);

    bool CreateToken(CTokenImpl const & token);
    bool RevertCreateToken(uint256 const & txid);
    bool DestroyToken(DCT_ID id, uint256 const & txid, int height);
    bool RevertDestroyToken(DCT_ID id, uint256 const & txid);

    // tags
    struct ID { static const unsigned char prefix; };
    struct Symbol { static const unsigned char prefix; };
    struct CreationTx { static const unsigned char prefix; };
    struct LastDctId { static const unsigned char prefix; };

private:
    // have to incapsulate "last token id" related methods here
    DCT_ID IncrementLastDctId();
    DCT_ID DecrementLastDctId();
    boost::optional<DCT_ID> ReadLastDctId() const;
};



#endif // DEFI_MASTERNODES_TOKENS_H
