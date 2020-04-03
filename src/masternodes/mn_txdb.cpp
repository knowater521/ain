// Copyright (c) 2019 DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/mn_txdb.h>

#include <chainparams.h>
#include <uint256.h>

#include <stdint.h>

#include <boost/thread.hpp>

using namespace std;

struct DBMNBlockHeadersSearchKey
{
    uint256 masternodeID;
    uint64_t mintedBlocks;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(masternodeID);
        READWRITE(mintedBlocks);
    }
};

struct DBMNBlockHeadersKey
{
    char prefix;
    DBMNBlockHeadersSearchKey searchKey;
    uint256 blockHash;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(prefix);
        READWRITE(searchKey);
        READWRITE(blockHash);
    }
};

struct DBMNBlockedCriminalCoins
{
    char prefix;
    uint256 txid;
    uint32_t index;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(prefix);
        READWRITE(txid);
        READWRITE(index);
    }
};

CEnhancedCSViewDB::CEnhancedCSViewDB(size_t nCacheSize, bool fMemory, bool fWipe)
    : db(new CDBWrapper(GetDataDir() / "masternodes", nCacheSize, fMemory, fWipe))
{
}

// for test purposes only
CEnhancedCSViewDB::CEnhancedCSViewDB()
    : db(nullptr)
{
}

void CEnhancedCSViewDB::WriteMintedBlockHeader(uint256 const & txid, uint64_t const mintedBlocks, uint256 const & hash, CBlockHeader const & blockHeader, bool fIsFakeNet)
{
    if (fIsFakeNet) {
        return;
    }
    // directly!
    db->Write(DBMNBlockHeadersKey{DB_MN_BLOCK_HEADERS, DBMNBlockHeadersSearchKey{txid, mintedBlocks}, hash}, blockHeader);
}

bool CEnhancedCSViewDB::FetchMintedHeaders(uint256 const & txid, uint64_t const mintedBlocks, std::map<uint256, CBlockHeader> & blockHeaders, bool fIsFakeNet)
{
    if (fIsFakeNet) {
        return false;
    }

    if (blockHeaders.size() != 0) {
        blockHeaders.clear();
    }

    pair<char, DBMNBlockHeadersSearchKey> prefix{DB_MN_BLOCK_HEADERS, DBMNBlockHeadersSearchKey{txid, mintedBlocks}};
    boost::scoped_ptr<CDBIterator> pcursor(const_cast<CDBWrapper*>(&*db)->NewIterator());
    pcursor->Seek(prefix);

    while (pcursor->Valid())
    {
        boost::this_thread::interruption_point();
        DBMNBlockHeadersKey key;
        if (pcursor->GetKey(key) &&
            key.prefix == DB_MN_BLOCK_HEADERS &&
            key.searchKey.masternodeID == txid &&
            key.searchKey.mintedBlocks == mintedBlocks) {
            CBlockHeader blockHeader;
            if (pcursor->GetValue(blockHeader)) {
                blockHeaders.emplace(key.blockHash, std::move(blockHeader));
            } else {
                return error("MNDB::FoundMintedBlockHeader() : unable to read value");
            }
        } else {
            break;
        }
        pcursor->Next();
    }
    return true;
}

void CEnhancedCSViewDB::EraseMintedBlockHeader(uint256 const & txid, uint64_t const mintedBlocks, uint256 const & hash)
{
    // directly!
    db->Erase(DBMNBlockHeadersKey{DB_MN_BLOCK_HEADERS, DBMNBlockHeadersSearchKey{txid, mintedBlocks}, hash});
}

void CEnhancedCSViewDB::WriteCriminal(uint256 const & mnId, CDoubleSignFact const & doubleSignFact)
{
    // directly!
    db->Write(make_pair(DB_MN_CRIMINALS, mnId), doubleSignFact);
}

void CEnhancedCSViewDB::EraseCriminal(uint256 const & mnId)
{
    // directly!
    db->Erase(make_pair(DB_MN_CRIMINALS, mnId));
}


//void CEnhancedCSViewDB::WriteDeadIndex(int height, uint256 const & txid, char type)
//{
//    BatchWrite(make_pair(make_pair(DB_PRUNEDEAD, static_cast<int32_t>(height)), txid), type);
//}

//void CEnhancedCSViewDB::EraseDeadIndex(int height, uint256 const & txid)
//{
//    BatchErase(make_pair(make_pair(DB_PRUNEDEAD, static_cast<int32_t>(height)), txid));
//}


