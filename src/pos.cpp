#include <pos.h>
#include <pos_kernel.h>
#include <pow.h>
#include <chain.h>
#include <validation.h>

#include <wallet/wallet.h>
#include <txdb.h>

static bool CheckStakeModifier(const CBlockIndex* pindexPrev, const CBlock& block) {
    if (block.hashPrevBlock.IsNull())
        return block.stakeModifier.IsNull();

    uint256 id = block.ExtractMasternodeID();
    return block.stakeModifier == pos::ComputeStakeModifier(pindexPrev->stakeModifier, id);
}

bool CheckBlockProof(const CBlockIndex* pindexPrev, const CBlock& block, CCoinsViewCache& view,
                     const Consensus::Params& params) {
    if (!CheckStakeModifier(pindexPrev, block)) {
        return false;
    }

    return pos::CheckProofOfStake(pindexPrev, block, view, params);

}

namespace pos {

/// Check PoS signatures (PoS block hashes are signed with coinstake out pubkey)
    bool CheckHeaderSignature(const CBlockHeader& block) {
        if (block.sig.empty()) {
            LogPrintf("CheckBlockSignature: Bad Block - PoS signature is empty\n");
            return false;
        }

        CPubKey recoveredPubKey{};
        if (!recoveredPubKey.RecoverCompact(block.GetHashToSign(), block.sig)) {
            LogPrintf("CheckBlockSignature: Bad Block - malformed signature\n");
            return false;
        }

        return true;
    }

    bool CheckProofOfStake_headerOnly(const CBlockHeader& block, const Consensus::Params& params) {
        const int64_t coinstakeTime = (int64_t) block.GetBlockTime();

        // checking PoS kernel is faster, so check it first
        if (!CheckKernelHash(block.stakeModifier, block.nBits, coinstakeTime, params).hashOk) {
            return false;
        }
        return CheckHeaderSignature(block);
    }

    bool CheckProofOfStake(const CBlockIndex* pindexPrev, const CBlock& block, CCoinsViewCache& view,
                           const Consensus::Params& params) {
        if (!block.HasCoinstakeTx()) {
            return error("CheckProofOfStake(): called on a non-PoS block %s", block.GetHash().ToString());
        }

        // check staker's pubKeyHash
        {
            std::vector<CTxDestination> addressRet;
            txnouttype typeRet;
            int nRet;
            if (!ExtractDestinations(block.vtx[1]->vout[1].scriptPubKey, typeRet, addressRet, nRet))
                return error("CheckProofOfStake(): coinstakeTx scriptPubKey must be P2PKH");
            if (!(typeRet == txnouttype::TX_PUBKEYHASH && addressRet.size() == 1))
                return error("CheckProofOfStake(): coinstakeTx scriptPubKey must be P2PKH");
        }

        // check staker's coin
        {
//            if (stakeCoin.IsSpent()) // TODO: SS check collateral don't spend
//                return error("CheckProofOfStake : Could not find previous transaction for PoS %s\n",
//                             body.coinstakePrevout.hash.ToString());
        }
        const int64_t coinstakeTime = (int64_t) block.GetBlockTime();


        // checking PoS kernel is faster, so check it first
        if (!CheckKernelHash(block.stakeModifier, block.nBits, coinstakeTime, params).hashOk) {
            return false;
        }
        return CheckHeaderSignature(block);
    }
}
