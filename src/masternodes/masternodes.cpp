// Copyright (c) 2019 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/masternodes.h>
#include <masternodes/anchors.h>

//#include <chainparams.h>
#include <net_processing.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/standard.h>
#include <validation.h>
#include <wallet/wallet.h>

#include <algorithm>
#include <functional>

std::unique_ptr<CEnhancedCSView> penhancedview;

std::unique_ptr<CStorageLevelDB> penhancedDB;

std::unique_ptr<CCriminalsView> pcriminals;

static const std::map<char, MasternodesTxType> MasternodesTxTypeToCode =
{
    {'C', MasternodesTxType::CreateMasternode },
    {'R', MasternodesTxType::ResignMasternode }
};

int GetMnActivationDelay()
{
    return Params().GetConsensus().mn.activationDelay;
}

int GetMnResignDelay()
{
    return Params().GetConsensus().mn.resignDelay;
}

int GetMnHistoryFrame()
{
    return Params().GetConsensus().mn.historyFrame;
}


CAmount GetMnCollateralAmount()
{
    return Params().GetConsensus().mn.collateralAmount;
}

CAmount GetMnCreationFee(int height)
{
    return Params().GetConsensus().mn.creationFee;
}

CMasternode::CMasternode()
    : mintedBlocks(0)
    , ownerAuthAddress()
    , ownerType(0)
    , operatorAuthAddress()
    , operatorType(0)
    , creationHeight(0)
    , resignHeight(-1)
    , banHeight(-1)
    , resignTx()
    , banTx()
{
}

CMasternode::CMasternode(const CTransaction & tx, int heightIn, const std::vector<unsigned char> & metadata)
{
    FromTx(tx, heightIn, metadata);
}

void CMasternode::FromTx(CTransaction const & tx, int heightIn, std::vector<unsigned char> const & metadata)
{
    CDataStream ss(metadata, SER_NETWORK, PROTOCOL_VERSION);
    ss >> operatorType;
    ss >> operatorAuthAddress;

    ownerType = 0;
    ownerAuthAddress = {};

    CTxDestination dest;
    if (ExtractDestination(tx.vout[1].scriptPubKey, dest)) {
        if (dest.which() == 1) {
            ownerType = 1;
            ownerAuthAddress = CKeyID(*boost::get<PKHash>(&dest));
        }
        else if (dest.which() == 4) {
            ownerType = 4;
            ownerAuthAddress = CKeyID(*boost::get<WitnessV0KeyHash>(&dest));
        }
    }

    creationHeight = heightIn;
    resignHeight = -1;
    banHeight = -1;

    resignTx = {};
    banTx = {};
    mintedBlocks = 0;
}

CMasternode::State CMasternode::GetState() const
{
    return GetState(::ChainActive().Height());
}

CMasternode::State CMasternode::GetState(int h) const
{
    assert (banHeight == -1 || resignHeight == -1); // mutually exclusive!: ban XOR resign

    if (resignHeight == -1 && banHeight == -1) { // enabled or pre-enabled
        // Special case for genesis block
        if (creationHeight == 0 || h >= creationHeight + GetMnActivationDelay()) {
            return State::ENABLED;
        }
        return State::PRE_ENABLED;
    }
    if (resignHeight != -1) { // pre-resigned or resigned
        if (h < resignHeight + GetMnResignDelay()) {
            return State::PRE_RESIGNED;
        }
        return State::RESIGNED;
    }
    if (banHeight != -1) { // pre-banned or banned
        if (h < banHeight + GetMnResignDelay()) {
            return State::PRE_BANNED;
        }
        return State::BANNED;
    }
    return State::UNKNOWN;
}

bool CMasternode::IsActive() const
{
    return IsActive(::ChainActive().Height());
}

bool CMasternode::IsActive(int h) const
{
    State state = GetState(h);
    return state == ENABLED || state == PRE_RESIGNED || state == PRE_BANNED;
}

std::string CMasternode::GetHumanReadableState(State state)
{
    switch (state) {
        case PRE_ENABLED:
            return "PRE_ENABLED";
        case ENABLED:
            return "ENABLED";
        case PRE_RESIGNED:
            return "PRE_RESIGNED";
        case RESIGNED:
            return "RESIGNED";
        case PRE_BANNED:
            return "PRE_BANNED";
        case BANNED:
            return "BANNED";
        default:
            return "UNKNOWN";
    }
}

bool operator==(CMasternode const & a, CMasternode const & b)
{
    return (a.mintedBlocks == b.mintedBlocks &&
            a.ownerType == b.ownerType &&
            a.ownerAuthAddress == b.ownerAuthAddress &&
            a.operatorType == b.operatorType &&
            a.operatorAuthAddress == b.operatorAuthAddress &&
            a.creationHeight == b.creationHeight &&
            a.resignHeight == b.resignHeight &&
            a.banHeight == b.banHeight &&
            a.resignTx == b.resignTx &&
            a.banTx == b.banTx
            );
}

bool operator!=(CMasternode const & a, CMasternode const & b)
{
    return !(a == b);
}

bool operator==(CDoubleSignFact const & a, CDoubleSignFact const & b)
{
    return (a.blockHeader.GetHash() == b.blockHeader.GetHash() &&
            a.conflictBlockHeader.GetHash() == b.conflictBlockHeader.GetHash()
    );
}

bool operator!=(CDoubleSignFact const & a, CDoubleSignFact const & b)
{
    return !(a == b);
}

/*
 * Searching MN index 'nodesByOwner' or 'nodesByOperator' for given 'auth' key
 */
//boost::optional<CMasternodesByAuth::const_iterator>
//CEnhancedCSViewOld::ExistMasternode(CEnhancedCSViewOld::AuthIndex where, CKeyID const & auth) const
//{
//    CMasternodesByAuth const & index = (where == AuthIndex::ByOwner) ? nodesByOwner : nodesByOperator;
//    auto it = index.find(auth);
//    if (it == index.end() || it->second.IsNull())
//    {
//        return {};
//    }
//    return {it};
//}

/*
 * Searching all masternodes for given 'id'
 */
//CMasternode const * CEnhancedCSViewOld::ExistMasternode(uint256 const & id) const
//{
//    CMasternodes::const_iterator it = allNodes.find(id);
//    return it != allNodes.end() && it->second != CMasternode() ? &it->second : nullptr;
//}

/*
 * Check that given tx is not a masternode id or masternode was resigned enough time in the past
 */
//bool CEnhancedCSViewOld::CanSpend(const uint256 & nodeId, int height) const
//{
//    auto nodePtr = ExistMasternode(nodeId);
//    // if not exist or (resigned && delay passed)
//    return !nodePtr || (nodePtr->GetState(height) == CMasternode::RESIGNED) || (nodePtr->GetState(height) == CMasternode::BANNED);
//}

/*
 * Check that given node is involved in anchor's subsystem for a given height (or smth like that)
 */
//bool CEnhancedCSViewOld::IsAnchorInvolved(const uint256 & nodeId, int height) const
//{
//    /// @todo to be implemented
//    return false;
//}


bool ExtractCriminalProofFromTx(CTransaction const & tx, std::vector<unsigned char> & metadata)
{
    if (!tx.IsCoinBase() || tx.vout.size() == 0) {
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

bool ExtractAnchorRewardFromTx(CTransaction const & tx, std::vector<unsigned char> & metadata)
{
    if (tx.vout.size() != 2) {
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
 * Checks if given tx is probably one of 'MasternodeTx', returns tx type and serialized metadata in 'data'
*/
MasternodesTxType GuessMasternodeTxType(CTransaction const & tx, std::vector<unsigned char> & metadata)
{
    if (tx.vout.size() == 0)
    {
        return MasternodesTxType::None;
    }
    CScript const & memo = tx.vout[0].scriptPubKey;
    CScript::const_iterator pc = memo.begin();
    opcodetype opcode;
    if (!memo.GetOp(pc, opcode) || opcode != OP_RETURN)
    {
        return MasternodesTxType::None;
    }
    if (!memo.GetOp(pc, opcode, metadata) ||
            (opcode > OP_PUSHDATA1 &&
             opcode != OP_PUSHDATA2 &&
             opcode != OP_PUSHDATA4) ||
            metadata.size() < DfTxMarker.size() + 1 ||     // i don't know how much exactly, but at least MnTxSignature + type prefix
            memcmp(&metadata[0], &DfTxMarker[0], DfTxMarker.size()) != 0)
    {
        return MasternodesTxType::None;
    }
    auto const & it = MasternodesTxTypeToCode.find(metadata[DfTxMarker.size()]);
    if (it == MasternodesTxTypeToCode.end())
    {
        return MasternodesTxType::None;
    }
    metadata.erase(metadata.begin(), metadata.begin() + DfTxMarker.size() + 1);
    return it->second;
}

bool IsDoubleSignRestricted(uint64_t height1, uint64_t height2)
{
    return (std::max(height1, height2) - std::min(height1, height2)) <= DOUBLE_SIGN_MINIMUM_PROOF_INTERVAL;
}

bool IsDoubleSigned(CBlockHeader const & oneHeader, CBlockHeader const & twoHeader, CKeyID & minter)
{
    // not necessary to check if such masternode exists or active. this is proof by itself!
    CKeyID firstKey, secondKey;
    if (!oneHeader.ExtractMinterKey(firstKey) || !twoHeader.ExtractMinterKey(secondKey)) {
        return false;
    }

    if (IsDoubleSignRestricted(oneHeader.height, twoHeader.height) &&
        firstKey == secondKey &&
        oneHeader.mintedBlocks == twoHeader.mintedBlocks &&
        oneHeader.GetHash() != twoHeader.GetHash()
        ) {
        minter = firstKey;
        return true;
    }
    return false;
}

const unsigned char CMasternodesView::ID      ::prefix = DB_MASTERNODES;
const unsigned char CMasternodesView::Operator::prefix = DB_MN_OPERATORS;
const unsigned char CMasternodesView::Owner   ::prefix = DB_MN_OWNERS;
const unsigned char CAnchorRewardsView::BtcTx ::prefix = DB_MN_ANCHOR_REWARD;

const unsigned char CMintedHeadersView::MintedHeaders ::prefix = DB_MN_BLOCK_HEADERS;
const unsigned char CCriminalProofsView::Proofs       ::prefix = DB_MN_CRIMINALS;



CTeamView::CTeam CEnhancedCSView::CalcNextTeam(const uint256 & stakeModifier)
{
    int anchoringTeamSize = Params().GetConsensus().mn.anchoringTeamSize;

    std::map<arith_uint256, CKeyID, std::less<arith_uint256>> priorityMN;
    ForEachMasternode([&stakeModifier, &priorityMN] (uint256 const & id, CMasternode & node) {
        if(!node.IsActive())
            return true;

        CDataStream ss{SER_GETHASH, PROTOCOL_VERSION};
        ss << id << stakeModifier;
        priorityMN.insert(std::make_pair(UintToArith256(Hash(ss.begin(), ss.end())), node.operatorAuthAddress));
        return true;
    });

    CTeam newTeam;
    auto && it = priorityMN.begin();
    for (int i = 0; i < anchoringTeamSize && it != priorityMN.end(); ++i, ++it) {
        newTeam.insert(it->second);
    }
    return newTeam;
}

/// @todo newbase move to networking?
void CEnhancedCSView::CreateAndRelayConfirmMessageIfNeed(const CAnchor & anchor, const uint256 & btcTxHash)
{
    auto myIDs = AmIOperator();
    if (!myIDs || !ExistMasternode(myIDs->second)->IsActive())
        return ;
    CKeyID const & operatorAuthAddress = myIDs->first;
    CTeam const currentTeam = GetCurrentTeam();
    if (currentTeam.find(operatorAuthAddress) == currentTeam.end()) {
        LogPrintf("AnchorConfirms::CreateAndRelayConfirmMessageIfNeed: Warning! I am not in a team %s\n", operatorAuthAddress.ToString());
        return ;
    }

    std::vector<std::shared_ptr<CWallet>> wallets = GetWallets();
    CKey masternodeKey{};
    for (auto const wallet : wallets) {
        if (wallet->GetKey(operatorAuthAddress, masternodeKey)) {
            break;
        }
        masternodeKey = CKey{};
    }

    if (!masternodeKey.IsValid()) {
        LogPrintf("AnchorConfirms::CreateAndRelayConfirmMessageIfNeed: Warning! Masternodes is't valid %s\n", operatorAuthAddress.ToString());
        // return error("%s: Can't read masternode operator private key", __func__);
        return ;
    }

    auto prev = panchors->ExistAnchorByTx(anchor.previousAnchor);
    auto confirmMessage = CAnchorConfirmMessage::Create(anchor, prev? prev->anchor.height : 0, btcTxHash, masternodeKey);
    if (panchorAwaitingConfirms->Add(confirmMessage)) {
        LogPrintf("AnchorConfirms::CreateAndRelayConfirmMessageIfNeed: Create message %s\n", confirmMessage.GetHash().GetHex());
        RelayAnchorConfirm(confirmMessage.GetHash(), *g_connman);
    }
    else {
        LogPrintf("AnchorConfirms::CreateAndRelayConfirmMessageIfNeed: Warning! not need relay %s because message (or vote!) already exist\n", confirmMessage.GetHash().GetHex());
    }

}
