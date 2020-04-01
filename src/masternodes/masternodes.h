// Copyright (c) 2019 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_MASTERNODES_H
#define DEFI_MASTERNODES_MASTERNODES_H

#include <amount.h>
#include <chainparams.h> // Params()
#include <flushablestorage.h>
#include <pubkey.h>
#include <script/script.h>
#include <serialize.h>
#include <uint256.h>

#include <map>
#include <set>
#include <stdint.h>
#include <iostream>

#include <primitives/block.h>

#include <boost/optional.hpp>
#include <functional>

class CTransaction;
class CAnchor;

static const std::vector<unsigned char> DfTxMarker = {'D', 'f', 'T', 'x'};  // 44665478

static const unsigned int DOUBLE_SIGN_MINIMUM_PROOF_INTERVAL = 100;

enum class MasternodesTxType : unsigned char
{
    None = 0,
    CreateMasternode    = 'C',
    ResignMasternode    = 'R'
};

template<typename Stream>
inline void Serialize(Stream& s, MasternodesTxType txType)
{
    Serialize(s, static_cast<unsigned char>(txType));
}

template<typename Stream>
inline void Unserialize(Stream& s, MasternodesTxType & txType) {
    unsigned char ch;
    Unserialize(s, ch);
    txType = ch == 'C' ? MasternodesTxType::CreateMasternode :
             ch == 'R' ? MasternodesTxType::ResignMasternode :
                         MasternodesTxType::None;
}

// Works instead of constants cause 'regtest' differs (don't want to overcharge chainparams)
int GetMnActivationDelay();
int GetMnResignDelay();
int GetMnHistoryFrame();
CAmount GetMnCollateralAmount();
CAmount GetMnCreationFee(int height);

//! Checks if given tx is probably one of custom 'MasternodeTx', returns tx type and serialized metadata in 'data'
MasternodesTxType GuessMasternodeTxType(CTransaction const & tx, std::vector<unsigned char> & metadata);

bool IsDoubleSignRestricted(uint64_t height1, uint64_t height2);
bool IsDoubleSigned(CBlockHeader const & oneHeader, CBlockHeader const & twoHeader, CKeyID & minter);

bool ExtractCriminalProofFromTx(CTransaction const & tx, std::vector<unsigned char> & metadata);
bool ExtractAnchorRewardFromTx(CTransaction const & tx, std::vector<unsigned char> & metadata);

class CMasternode
{
public:
    enum State {
        PRE_ENABLED,
        ENABLED,
        PRE_RESIGNED,
        RESIGNED,
        PRE_BANNED,
        BANNED,
        UNKNOWN // unreachable
    };

    //! Minted blocks counter
    uint32_t mintedBlocks;

    //! Owner auth address == collateral address. Can be used as an ID.
    CKeyID ownerAuthAddress;
    char ownerType;

    //! Operator auth address. Can be equal to ownerAuthAddress. Can be used as an ID
    CKeyID operatorAuthAddress;
    char operatorType;

    //! MN creation block height
    int32_t creationHeight;
    //! Resign height
    int32_t resignHeight;
    //! Criminal ban height
    int32_t banHeight;

    //! This fields are for transaction rollback (by disconnecting block)
    uint256 resignTx;
    uint256 banTx;

    //! empty constructor
    CMasternode();
    //! construct a CMasternode from a CTransaction, at a given height
    CMasternode(CTransaction const & tx, int heightIn, std::vector<unsigned char> const & metadata);

    //! constructor helper, runs without any checks
    void FromTx(CTransaction const & tx, int heightIn, std::vector<unsigned char> const & metadata);

    State GetState() const;
    State GetState(int h) const;
    bool IsActive() const;
    bool IsActive(int h) const;

    static std::string GetHumanReadableState(State state);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(mintedBlocks);
        READWRITE(ownerAuthAddress);
        READWRITE(ownerType);
        READWRITE(operatorAuthAddress);
        READWRITE(operatorType);

        READWRITE(creationHeight);
        READWRITE(resignHeight);
        READWRITE(banHeight);

        READWRITE(resignTx);
        READWRITE(banTx);
    }

    //! equality test
    friend bool operator==(CMasternode const & a, CMasternode const & b);
    friend bool operator!=(CMasternode const & a, CMasternode const & b);
};

class CDoubleSignFact
{
public:
    CBlockHeader blockHeader;
    CBlockHeader conflictBlockHeader;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(blockHeader);
        READWRITE(conflictBlockHeader);
    }

    friend bool operator==(CDoubleSignFact const & a, CDoubleSignFact const & b);
    friend bool operator!=(CDoubleSignFact const & a, CDoubleSignFact const & b);
};

typedef std::map<uint256, CMasternode> CMasternodes;  // nodeId -> masternode object,
typedef std::map<CKeyID, uint256> CMasternodesByAuth; // for two indexes, owner->nodeId, operator->nodeId

class CEnhancedCSViewCache;
class CEnhancedCSViewHistory;

class CEnhancedCSViewOld
{
    using RewardTxHash = uint256;
    using AnchorTxHash = uint256;
public:
    // Block of typedefs
    struct CMasternodeIDs
    {
        uint256 id;
        CKeyID operatorAuthAddress;
        CKeyID ownerAuthAddress;
    };
    typedef std::map<int, std::pair<uint256, MasternodesTxType> > CMnTxsUndo; // txn, undoRec
    typedef std::map<int, CMnTxsUndo> CMnBlocksUndo;
    typedef std::map<uint256, CDoubleSignFact> CMnCriminals; // nodeId, two headers
    typedef std::map<AnchorTxHash, RewardTxHash> CAnchorsRewards;
    typedef std::set<CKeyID> CTeam;

    enum class AuthIndex { ByOwner, ByOperator };

protected:
    int lastHeight;
    CMasternodes allNodes;
    CMasternodesByAuth nodesByOwner;
    CMasternodesByAuth nodesByOperator;

    CMnCriminals criminals;
    CAnchorsRewards rewards;
    CTeam currentTeam;
    CAmount foundationsDebt;

    CMnBlocksUndo blocksUndo;

    CEnhancedCSViewOld() : lastHeight(0) {}

public:
    CEnhancedCSViewOld & operator=(CEnhancedCSViewOld const & other) = delete;

    void ApplyCache(CEnhancedCSViewOld const * cache);
    void Clear();

    bool IsEmpty() const
    {
        return allNodes.empty() && nodesByOwner.empty() && nodesByOperator.empty() && blocksUndo.empty();
    }

    virtual ~CEnhancedCSViewOld() {}

    void SetLastHeight(int h)
    {
        lastHeight = h;
    }
    int GetLastHeight() const
    {
        return lastHeight;
    }

    void IncrementMintedBy(CKeyID const & minter)
    {
        auto it = ExistMasternode(AuthIndex::ByOperator, minter);
        assert(it);
        auto const & nodeId = (*it)->second;
        auto nodePtr = ExistMasternode(nodeId);
        assert(nodePtr);
        auto & node = allNodes[nodeId] = *nodePtr; // cause may be cached!!
        ++node.mintedBlocks;
    }

    void DecrementMintedBy(CKeyID const & minter)
    {
        auto it = ExistMasternode(AuthIndex::ByOperator, minter);
        assert(it);
        auto const & nodeId = (*it)->second;
        auto nodePtr = ExistMasternode(nodeId);
        assert(nodePtr);
        auto & node = allNodes[nodeId] = *nodePtr; // cause may be cached!!
        --node.mintedBlocks;
    }

    virtual CMasternodes GetMasternodes() const
    {
        return allNodes;
    }

    //! Initial load of all data
    virtual bool Load() { assert(false); }
    virtual bool Flush() { assert(false); }

    virtual boost::optional<CMasternodesByAuth::const_iterator>
    ExistMasternode(AuthIndex where, CKeyID const & auth) const;

    virtual CMasternode const * ExistMasternode(uint256 const & id) const;

    // "off-chain" data, should be written directly
    virtual void WriteMintedBlockHeader(uint256 const & txid, uint64_t const mintedBlocks, uint256 const & hash, CBlockHeader const & blockHeader, bool fIsFakeNet = true) { assert(false); }
    virtual bool FetchMintedHeaders(uint256 const & txid, uint64_t const mintedBlocks, std::map<uint256, CBlockHeader> & blockHeaders, bool fIsFakeNet = true) { assert(false); }
    virtual void EraseMintedBlockHeader(uint256 const & txid, uint64_t const mintedBlocks, uint256 const & hash) { assert(false); }

    // "off-chain" data, should be written directly
    virtual void WriteCriminal(uint256 const & mnId, CDoubleSignFact const & doubleSignFact) { assert(false); }
    virtual void EraseCriminal(uint256 const & mnId) { assert(false); }

    bool CanSpend(uint256 const & nodeId, int height) const;
    bool IsAnchorInvolved(uint256 const & nodeId, int height) const;

    bool OnMasternodeCreate(uint256 const & nodeId, CMasternode const & node, int txn);
    bool OnMasternodeResign(uint256 const & nodeId, uint256 const & txid, int height, int txn);
    CEnhancedCSViewCache OnUndoBlock(int height);

    void PruneOlder(int height);

    // Masternodes Teams
    void SetTeam(CTeam newTeam);
    const CTeam &GetCurrentTeam();
    CTeam CalcNextTeam(uint256 stakeModifier, const CMasternodes * masternodes = nullptr);

    // Criminals
    void AddCriminalProof(uint256 const & id, CBlockHeader const & blockHeader, CBlockHeader const & conflictBlockHeader);
    void RemoveCriminalProofs(uint256 const &criminalID);
    CMnCriminals GetUnpunishedCriminals() const;

    bool BanCriminal(const uint256 txid, std::vector<unsigned char> & metadata, int height);
    bool UnbanCriminal(const uint256 txid, std::vector<unsigned char> & metadata);

    // Anchors Rewards
    virtual RewardTxHash GetRewardForAnchor(AnchorTxHash const &btcTxHash) const
    {
        auto it = rewards.find(btcTxHash);
        return it != rewards.end()? it->second : RewardTxHash{};
    }
    virtual CAnchorsRewards ListAnchorRewards() const
    {
        return rewards;
    }
    void AddRewardForAnchor(AnchorTxHash const &btcTxHash, uint256 const & rewardTxHash);
    void RemoveRewardForAnchor(AnchorTxHash const &btcTxHash);
    void CreateAndRelayConfirmMessageIfNeed(const CAnchor & anchor, const uint256 & btcTxHash);

    // FoundationsDebt
    CAmount GetFoundationsDebt();
    void SetFoundationsDebt(CAmount debt);

    // Outside

protected:
    virtual CMnBlocksUndo::mapped_type const & GetBlockUndo(CMnBlocksUndo::key_type key) const;

private:
    boost::optional<CMasternodeIDs> AmI(AuthIndex where) const;
public:
    boost::optional<CMasternodeIDs> AmIOperator() const;
    boost::optional<CMasternodeIDs> AmIOwner() const;

    friend class CEnhancedCSViewCache;
    friend class CEnhancedCSViewHistory;
};


class CEnhancedCSViewCache : public CEnhancedCSViewOld
{
protected:
    CEnhancedCSViewOld * base;

public:
    CEnhancedCSViewCache(CEnhancedCSViewOld * other)
        : CEnhancedCSViewOld()
        , base(other)
    {
        assert(base);
        lastHeight = base->lastHeight;
        currentTeam = base->currentTeam;
        foundationsDebt = base->foundationsDebt;
        // cached items are empty!
    }

    ~CEnhancedCSViewCache() override {}

    CMasternodes GetMasternodes() const override
    {
        auto const baseNodes = base->GetMasternodes();
        CMasternodes result(allNodes);
        result.insert(baseNodes.begin(), baseNodes.end());
        return result;
    }

    RewardTxHash GetRewardForAnchor(AnchorTxHash const &btcTxHash) const override
    {
        auto it = rewards.find(btcTxHash);
        return it != rewards.end()? it->second : base->GetRewardForAnchor(btcTxHash);
    }

    CAnchorsRewards ListAnchorRewards() const override
    {
        auto const baseRewards = base->ListAnchorRewards();
        CAnchorsRewards result(rewards);
        result.insert(baseRewards.begin(), baseRewards.end());
        return result;
    }

    boost::optional<CMasternodesByAuth::const_iterator>
    ExistMasternode(AuthIndex where, CKeyID const & auth) const override
    {
        CMasternodesByAuth const & index = (where == AuthIndex::ByOwner) ? nodesByOwner : nodesByOperator;
        auto it = index.find(auth);
        if (it == index.end())
        {
            return base->ExistMasternode(where, auth);
        }
        if (it->second.IsNull())
        {
            return {};
        }
        return {it};
    }

    CMasternode const * ExistMasternode(uint256 const & id) const override
    {
        CMasternodes::const_iterator it = allNodes.find(id);
        return it == allNodes.end() ? base->ExistMasternode(id) : it->second != CMasternode() ? &it->second : nullptr;
    }

    CMnBlocksUndo::mapped_type const & GetBlockUndo(CMnBlocksUndo::key_type key) const override
    {
        CMnBlocksUndo::const_iterator it = blocksUndo.find(key);
        return it == blocksUndo.end() ? base->GetBlockUndo(key) : it->second;
    }

    bool Flush() override
    {
        base->ApplyCache(this);
        Clear();

        return true;
    }
};


class CEnhancedCSViewHistory : public CEnhancedCSViewCache
{
protected:
    std::map<int, CEnhancedCSViewCache> historyDiff;

public:
    CEnhancedCSViewHistory(CEnhancedCSViewOld * top) : CEnhancedCSViewCache(top) { assert(top); }

    bool Flush() override { assert(false); } // forbidden!!!

    CEnhancedCSViewHistory & GetState(int targetHeight);
};

// Prefixes to the masternodes database (masternodes/)

const unsigned char DB_MASTERNODES = 'M';     // main masternodes table
const unsigned char DB_MN_OPERATORS = 'o';    // masternodes' operators index
const unsigned char DB_MN_OWNERS = 'w';       // masternodes' owners index
const unsigned char DB_MASTERNODESUNDO = 'U'; // undo table
const unsigned char DB_MN_HEIGHT = 'H';       // single record with last processed chain height

const unsigned char DB_MN_BLOCK_HEADERS = 'h';
const unsigned char DB_MN_CRIMINALS = 'm';
const unsigned char DB_MN_ANCHOR_REWARD = 'r';
const unsigned char DB_MN_CURRENT_TEAM = 't';
const unsigned char DB_MN_FOUNDERS_DEBT = 'd';

class CMasternodesView : public virtual CStorageView
{
public:
//    CMasternodesView() = default;

    boost::optional<CMasternode> ExistMasternode(uint256 const & id) const {
        return ReadBy<ID, CMasternode>(id);
    }

    boost::optional<uint256> ExistMasternodeByOperator(CKeyID const & id) const {
        return ReadBy<Operator, uint256>(id);
    }

    boost::optional<uint256> ExistMasternodeByOwner(CKeyID const & id) const {
        return ReadBy<Owner, uint256>(id);
    }

    void ForEachMasternode(std::function<bool(uint256 const & id, CMasternode & node)> callback) {
        ForEach<ID, uint256, CMasternode>(callback);
    }

    bool CanSpend(const uint256 & nodeId, int height) const
    {
        auto node = ExistMasternode(nodeId);
        // if not exist or (resigned && delay passed)
        return !node || (node->GetState(height) == CMasternode::RESIGNED) || (node->GetState(height) == CMasternode::BANNED);
    }

    void IncrementMintedBy(CKeyID const & minter)
    {
        auto nodeId = ExistMasternodeByOperator(minter);
        assert(nodeId);
        auto node = ExistMasternode(*nodeId);
        assert(node);
        ++node->mintedBlocks;
        WriteBy<ID>(*nodeId, *node);
    }

    void DecrementMintedBy(CKeyID const & minter)
    {
        auto nodeId = ExistMasternodeByOperator(minter);
        assert(nodeId);
        auto node = ExistMasternode(*nodeId);
        assert(node);
        ++node->mintedBlocks;
        WriteBy<ID>(*nodeId, *node);
    }

    bool BanCriminal(const uint256 txid, std::vector<unsigned char> & metadata, int height)
    {
        std::pair<CBlockHeader, CBlockHeader> criminal;
        uint256 nodeId;
        CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
        ss >> criminal.first >> criminal.second >> nodeId; // mnid is totally unnecessary!

        CKeyID minter;
        if (IsDoubleSigned(criminal.first, criminal.second, minter)) {
            auto node = ExistMasternode(nodeId);
            if (node && node->operatorAuthAddress == minter && node->banTx.IsNull()) {
                node->banTx = txid;
                node->banHeight = height;
                WriteBy<ID>(nodeId, *node);

                return true;
            }
        }
        return false;
    }

    bool UnbanCriminal(const uint256 txid, std::vector<unsigned char> & metadata)
    {
        std::pair<CBlockHeader, CBlockHeader> criminal;
        uint256 nodeId;
        CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
        ss >> criminal.first >> criminal.second >> nodeId; // mnid is totally unnecessary!

        // there is no need to check doublesigning or smth, we just rolling back previously approved (or ignored) banTx!
        auto node = ExistMasternode(nodeId);
        if (node && node->banTx == txid) {
            node->banTx = {};
            node->banHeight = -1;
            WriteBy<ID>(nodeId, *node);
            return true;
        }
        return false;
    }

    boost::optional<std::pair<CKeyID, uint256>> AmIOperator() const
    {
        CTxDestination dest = DecodeDestination(gArgs.GetArg("-masternode_operator", ""));
        CKeyID const authAddress = dest.which() == 1 ? CKeyID(*boost::get<PKHash>(&dest)) : (dest.which() == 4 ? CKeyID(*boost::get<WitnessV0KeyHash>(&dest)) : CKeyID());
        if (!authAddress.IsNull()) {
            auto nodeId = ExistMasternodeByOperator(authAddress);
            if (nodeId)
                return { std::make_pair(authAddress, *nodeId) };
        }
        return {};
    }

    boost::optional<std::pair<CKeyID, uint256>> AmIOwner() const
    {
        CTxDestination dest = DecodeDestination(gArgs.GetArg("-masternode_owner", ""));
        CKeyID const authAddress = dest.which() == 1 ? CKeyID(*boost::get<PKHash>(&dest)) : (dest.which() == 4 ? CKeyID(*boost::get<WitnessV0KeyHash>(&dest)) : CKeyID());
        if (!authAddress.IsNull()) {
            auto nodeId = ExistMasternodeByOwner(authAddress);
            if (nodeId)
                return { std::make_pair(authAddress, *nodeId) };
        }
        return {};
    }


    // tags
    struct ID { static const unsigned char prefix; };
    struct Operator { static const unsigned char prefix; };
    struct Owner { static const unsigned char prefix; };
};

class CLastHeightView : public virtual CStorageView
{
public:
    int GetLastHeight() const {
        int result;
        if (Read(DB_MN_HEIGHT, result))
            return result;
        return 0;
    }
    void SetLastHeight(int height) {
        Write(DB_MN_HEIGHT, height);
    }
};

class CFoundationsDebtView : public virtual CStorageView
{
public:
    CAmount GetFoundationsDebt() const {
        CAmount debt(0);
        if(Read(DB_MN_FOUNDERS_DEBT, debt))
            assert(debt >= 0);
        return debt;
    }
    void SetFoundationsDebt(CAmount debt) {
        assert(debt >= 0);
        Write(DB_MN_FOUNDERS_DEBT, debt);
    }
};

class CTeamView : public virtual CStorageView
{
public:
    using CTeam = std::set<CKeyID>;
    void SetTeam(CTeam const & newTeam) {
        Write(DB_MN_CURRENT_TEAM, newTeam);
    }

    CTeam GetCurrentTeam() const {
        CTeam team;
        if (Read(DB_MN_CURRENT_TEAM, team) && team.size() > 0)
            return team;

        return Params().GetGenesisTeam();
    }
};

class CAnchorRewardsView : public virtual CStorageView
{
public:
    using RewardTxHash = uint256;
    using AnchorTxHash = uint256;

    boost::optional<RewardTxHash> GetRewardForAnchor(AnchorTxHash const &btcTxHash) const {
        return ReadBy<BtcTx, RewardTxHash>(btcTxHash);
    }

//    virtual CAnchorsRewards ListAnchorRewards() const
//    {
//        return rewards;
//    }
    void AddRewardForAnchor(AnchorTxHash const &btcTxHash, RewardTxHash const & rewardTxHash) {
        WriteBy<BtcTx>(btcTxHash, rewardTxHash);
    }
    void RemoveRewardForAnchor(AnchorTxHash const &btcTxHash) {
        EraseBy<BtcTx>(btcTxHash);
    }
    void ForEachAnchorReward(std::function<bool(AnchorTxHash const &, RewardTxHash &)> callback) {
        ForEach<BtcTx, AnchorTxHash, RewardTxHash>(callback);
    }

    struct BtcTx { static const unsigned char prefix; };
};

class CEnhancedCSView
        : public CMasternodesView
        , public CLastHeightView
        , public CTeamView
        , public CFoundationsDebtView
        , public CAnchorRewardsView
{
public:
//    explicit CEnhanced123(std::size_t cacheSize, bool fMemory = false, bool fWipe = false)
//        : storage(new CStorageLevelDB(GetDataDir() / "masternodes", cacheSize, fMemory, fWipe)) {}
    CEnhancedCSView(CStorageKV & st)
        : CStorageView(new CFlushableStorageKV(st))
    {}
    // cache-upon-a-cache (not a copy!) constructor
    CEnhancedCSView(CEnhancedCSView & other)
        : CStorageView(new CFlushableStorageKV(other.DB()))
    {}

    // cause depends on current mns
    CTeamView::CTeam CalcNextTeam(uint256 const & stakeModifier);

    /// @todo newbase move to networking?
    void CreateAndRelayConfirmMessageIfNeed(const CAnchor & anchor, const uint256 & btcTxHash);

    bool OnMasternodeCreate(uint256 const & nodeId, CMasternode const & node, int txn)
    {
        // Check auth addresses and that there in no MN with such owner or operator
        if ((node.operatorType != 1 && node.operatorType != 4 && node.ownerType != 1 && node.ownerType != 4) ||
            node.ownerAuthAddress.IsNull() || node.operatorAuthAddress.IsNull() ||
            ExistMasternode(nodeId) ||
            ExistMasternodeByOwner(node.ownerAuthAddress) ||
            ExistMasternodeByOperator(node.operatorAuthAddress)
            ) {
            return false;
        }

        WriteBy<ID>(nodeId, node);
        WriteBy<Owner>(node.ownerAuthAddress, nodeId);
        WriteBy<Operator>(node.operatorAuthAddress, nodeId);

        /// @todo newbase
//        blocksUndo[node.creationHeight][txn] = std::make_pair(nodeId, MasternodesTxType::CreateMasternode);

        return true;
    }

    bool OnMasternodeResign(uint256 const & nodeId, uint256 const & txid, int height, int txn)
    {
        // auth already checked!
        auto node = ExistMasternode(nodeId);
        if (!node) {
            return false;
        }
        auto state = node->GetState(height);
        if ((state != CMasternode::PRE_ENABLED && state != CMasternode::ENABLED) /*|| IsAnchorInvolved(nodeId, height)*/) { // if already spoiled by resign or ban, or need for anchor
            return false;
        }

        node->resignTx =  txid;
        node->resignHeight = height;
        WriteBy<ID>(nodeId, *node);

        /// @todo newbase
//        blocksUndo[height][txn] = std::make_pair(nodeId, MasternodesTxType::ResignMasternode);

        return true;
    }

    // simplified version of undo, without any unnecessary undo data
    void OnUndoTx(CTransaction const & tx)
    {
        TBytes metadata;
        MasternodesTxType txType = GuessMasternodeTxType(tx, metadata);

        uint256 txid = tx.GetHash();
        switch (txType)
        {
            case MasternodesTxType::CreateMasternode:
            {
                auto node = ExistMasternode(txid);
                if (node) {
                    EraseBy<ID>(txid);
                    EraseBy<Operator>(node->operatorAuthAddress);
                    EraseBy<Operator>(node->ownerAuthAddress);
                }
            }
            break;
            case MasternodesTxType::ResignMasternode:
            {
                uint256 nodeId(metadata);
                auto node = ExistMasternode(nodeId);
                if (node && node->resignTx == txid) {
                    node->resignHeight = -1;
                    node->resignTx = {};
                    WriteBy<ID>(nodeId, *node);
                }
            }
            break;
            default:
                break;
        }
    }


    bool Flush() { return DB().Flush(); }
};


/** Global variable that points to the CMasternodeView (should be protected by cs_main) */
//extern std::unique_ptr<CEnhancedCSView> penhancedview;
extern std::unique_ptr<CStorageLevelDB> penhancedDB;
//extern std::unique_ptr<CEnhanced123> penhanced123;
extern std::unique_ptr<CEnhancedCSView> penhancedview;

#endif // DEFI_MASTERNODES_MASTERNODES_H
