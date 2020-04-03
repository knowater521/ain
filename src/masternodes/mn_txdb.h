// Copyright (c) 2019 DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_MN_TXDB_H
#define DEFI_MASTERNODES_MN_TXDB_H

#include <dbwrapper.h>
#include <masternodes/masternodes.h>

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>

class CBlockHeader;

/** Access to the masternodes database (masternodes/) */
class CEnhancedCSViewDB : public CEnhancedCSViewOld
{
private:
    boost::shared_ptr<CDBWrapper> db;
    boost::scoped_ptr<CDBBatch> batch;

public:
    CEnhancedCSViewDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);
    ~CEnhancedCSViewDB() override {}

protected:
    // for test purposes only
    CEnhancedCSViewDB();

private:
    CEnhancedCSViewDB(CEnhancedCSViewDB const & other) = delete;
    CEnhancedCSViewDB & operator=(CEnhancedCSViewDB const &) = delete;

    template <typename Container>
    bool LoadTable(char prefix, Container & data, std::function<void(typename Container::key_type const &, typename Container::mapped_type &)> callback = nullptr)
    {
        boost::scoped_ptr<CDBIterator> pcursor(const_cast<CDBWrapper*>(&*db)->NewIterator());
        pcursor->Seek(prefix);

        while (pcursor->Valid())
        {
            boost::this_thread::interruption_point();
            std::pair<char, typename Container::key_type> key;
            if (pcursor->GetKey(key) && key.first == prefix)
            {
                typename Container::mapped_type value;
                if (pcursor->GetValue(value))
                {
                    data.emplace(key.second, std::move(value));
                    if (callback)
                        callback(key.second, data.at(key.second));
                } else {
                    return error("MNDB::Load() : unable to read value");
                }
            } else {
                break;
            }
            pcursor->Next();
        }
        return true;
    }

protected:

    // "off-chain" data, should be written directly
    void WriteMintedBlockHeader(uint256 const & txid, uint64_t mintedBlocks, uint256 const & hash, CBlockHeader const & blockHeader, bool fIsFakeNet = true) override;
    bool FetchMintedHeaders(uint256 const & txid, uint64_t mintedBlocks, std::map<uint256, CBlockHeader> & blockHeaders, bool fIsFakeNet = true) override;
    void EraseMintedBlockHeader(uint256 const & txid, uint64_t mintedBlocks, uint256 const & hash) override;

    // "off-chain" data, should be written directly
    void WriteCriminal(uint256 const & mnId, CDoubleSignFact const & doubleSignFact) override;
    void EraseCriminal(uint256 const & mnId) override;

};

#endif // DEFI_MASTERNODES_MN_TXDB_H
