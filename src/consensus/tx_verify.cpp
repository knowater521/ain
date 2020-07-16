// Copyright (c) 2017-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/tx_verify.h>

#include <consensus/consensus.h>
#include <masternodes/masternodes.h>
#include <masternodes/mn_checks.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <consensus/validation.h>

// TODO remove the following dependencies
#include <chain.h>
#include <coins.h>
#include <util/moneystr.h>

extern bool fIsFakeNet;
extern bool fCriminals;

bool IsFinalTx(const CTransaction &tx, int nBlockHeight, int64_t nBlockTime)
{
    if (tx.nLockTime == 0)
        return true;
    if ((int64_t)tx.nLockTime < ((int64_t)tx.nLockTime < LOCKTIME_THRESHOLD ? (int64_t)nBlockHeight : nBlockTime))
        return true;
    for (const auto& txin : tx.vin) {
        if (!(txin.nSequence == CTxIn::SEQUENCE_FINAL))
            return false;
    }
    return true;
}

std::pair<int, int64_t> CalculateSequenceLocks(const CTransaction &tx, int flags, std::vector<int>* prevHeights, const CBlockIndex& block)
{
    assert(prevHeights->size() == tx.vin.size());

    // Will be set to the equivalent height- and time-based nLockTime
    // values that would be necessary to satisfy all relative lock-
    // time constraints given our view of block chain history.
    // The semantics of nLockTime are the last invalid height/time, so
    // use -1 to have the effect of any height or time being valid.
    int nMinHeight = -1;
    int64_t nMinTime = -1;

    // tx.nVersion is signed integer so requires cast to unsigned otherwise
    // we would be doing a signed comparison and half the range of nVersion
    // wouldn't support BIP 68.
    bool fEnforceBIP68 = static_cast<uint32_t>(tx.nVersion) >= 2
                      && flags & LOCKTIME_VERIFY_SEQUENCE;

    // Do not enforce sequence numbers as a relative lock time
    // unless we have been instructed to
    if (!fEnforceBIP68) {
        return std::make_pair(nMinHeight, nMinTime);
    }

    for (size_t txinIndex = 0; txinIndex < tx.vin.size(); txinIndex++) {
        const CTxIn& txin = tx.vin[txinIndex];

        // Sequence numbers with the most significant bit set are not
        // treated as relative lock-times, nor are they given any
        // consensus-enforced meaning at this point.
        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG) {
            // The height of this input is not relevant for sequence locks
            (*prevHeights)[txinIndex] = 0;
            continue;
        }

        int nCoinHeight = (*prevHeights)[txinIndex];

        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG) {
            int64_t nCoinTime = block.GetAncestor(std::max(nCoinHeight-1, 0))->GetMedianTimePast();
            // NOTE: Subtract 1 to maintain nLockTime semantics
            // BIP 68 relative lock times have the semantics of calculating
            // the first block or time at which the transaction would be
            // valid. When calculating the effective block time or height
            // for the entire transaction, we switch to using the
            // semantics of nLockTime which is the last invalid block
            // time or height.  Thus we subtract 1 from the calculated
            // time or height.

            // Time-based relative lock-times are measured from the
            // smallest allowed timestamp of the block containing the
            // txout being spent, which is the median time past of the
            // block prior.
            nMinTime = std::max(nMinTime, nCoinTime + (int64_t)((txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) << CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) - 1);
        } else {
            nMinHeight = std::max(nMinHeight, nCoinHeight + (int)(txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) - 1);
        }
    }

    return std::make_pair(nMinHeight, nMinTime);
}

bool EvaluateSequenceLocks(const CBlockIndex& block, std::pair<int, int64_t> lockPair)
{
    assert(block.pprev);
    int64_t nBlockTime = block.pprev->GetMedianTimePast();
    if (lockPair.first >= block.nHeight || lockPair.second >= nBlockTime)
        return false;

    return true;
}

bool SequenceLocks(const CTransaction &tx, int flags, std::vector<int>* prevHeights, const CBlockIndex& block)
{
    return EvaluateSequenceLocks(block, CalculateSequenceLocks(tx, flags, prevHeights, block));
}

unsigned int GetLegacySigOpCount(const CTransaction& tx)
{
    unsigned int nSigOps = 0;
    for (const auto& txin : tx.vin)
    {
        nSigOps += txin.scriptSig.GetSigOpCount(false);
    }
    for (const auto& txout : tx.vout)
    {
        nSigOps += txout.scriptPubKey.GetSigOpCount(false);
    }
    return nSigOps;
}

unsigned int GetP2SHSigOpCount(const CTransaction& tx, const CCoinsViewCache& inputs)
{
    if (tx.IsCoinBase())
        return 0;

    unsigned int nSigOps = 0;
    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        const Coin& coin = inputs.AccessCoin(tx.vin[i].prevout);
        assert(!coin.IsSpent());
        const CTxOut &prevout = coin.out;
        if (prevout.scriptPubKey.IsPayToScriptHash())
            nSigOps += prevout.scriptPubKey.GetSigOpCount(tx.vin[i].scriptSig);
    }
    return nSigOps;
}

int64_t GetTransactionSigOpCost(const CTransaction& tx, const CCoinsViewCache& inputs, int flags)
{
    int64_t nSigOps = GetLegacySigOpCount(tx) * WITNESS_SCALE_FACTOR;

    if (tx.IsCoinBase())
        return nSigOps;

    if (flags & SCRIPT_VERIFY_P2SH) {
        nSigOps += GetP2SHSigOpCount(tx, inputs) * WITNESS_SCALE_FACTOR;
    }

    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        const Coin& coin = inputs.AccessCoin(tx.vin[i].prevout);
        assert(!coin.IsSpent());
        const CTxOut &prevout = coin.out;
        nSigOps += CountWitnessSigOps(tx.vin[i].scriptSig, prevout.scriptPubKey, &tx.vin[i].scriptWitness, flags);
    }
    return nSigOps;
}

bool Consensus::CheckTxInputs(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& inputs, const CCustomCSView * mnview, int nSpendHeight, CAmount& txfee)
{
    // are the actual inputs available?
    if (!inputs.HaveInputs(tx)) {
        return state.Invalid(ValidationInvalidReason::TX_MISSING_INPUTS, false, REJECT_INVALID, "bad-txns-inputs-missingorspent",
                         strprintf("%s: inputs missing/spent", __func__));
    }

    TAmounts nValuesIn;
    for (unsigned int i = 0; i < tx.vin.size(); ++i) {
        const COutPoint &prevout = tx.vin[i].prevout;
        const Coin& coin = inputs.AccessCoin(prevout);
        assert(!coin.IsSpent());

        // If prev is coinbase, check that it's matured
        if (coin.IsCoinBase() && nSpendHeight - coin.nHeight < COINBASE_MATURITY) {
            return state.Invalid(ValidationInvalidReason::TX_PREMATURE_SPEND, false, REJECT_INVALID, "bad-txns-premature-spend-of-coinbase",
                strprintf("tried to spend coinbase at depth %d", nSpendHeight - coin.nHeight));
        }

        // Check for negative or overflow input values
        nValuesIn[coin.out.nTokenId] += coin.out.nValue;
        if (!MoneyRange(coin.out.nValue) || !MoneyRange(nValuesIn[coin.out.nTokenId])) {
            return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-txns-inputvalues-outofrange");
        }
        /// @todo tokens: later match the range with TotalSupply

        if (prevout.n == 1 && !mnview->CanSpend(prevout.hash, nSpendHeight)) {
            return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-txns-collateral-locked",
                strprintf("tried to spend locked collateral for %s", prevout.hash.ToString())); /// @todo may be somehow place the height of unlocking?
        }
    }

    /// @attention Keep the order of checks not to break old tests
    TAmounts values_out = GetNonMintedValuesOut(tx);

    // special (old) case for 'DFI'
    if (nValuesIn[DCT_ID{0}] < values_out[DCT_ID{0}]) {
        return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-txns-in-belowout",
            strprintf("value in (%s) < value out (%s)", FormatMoney(nValuesIn[DCT_ID{0}]), FormatMoney(values_out[DCT_ID{0}])));
    }

    // Tally transaction fees
    const CAmount txfee_aux = nValuesIn[DCT_ID{0}] - values_out[DCT_ID{0}];
    if (!MoneyRange(txfee_aux)) {
        return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-txns-fee-outofrange");
    }
    txfee = txfee_aux;

    // after fee calc it is guaranteed that both values[0] exists (even if zero)
    if (tx.nVersion < CTransaction::TOKENS_MIN_VERSION && (nValuesIn.size() > 1 || values_out.size() > 1)) {
        return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-txns-tokens-in-old-version-tx");
    }

    // check for tokens values
    std::vector<unsigned char> dummy;
    const auto txType = GuessCustomTxType(tx, dummy);
    // @todo get rid of unique checks for those functions. values_out doesn't contain minted tokens anyway due to GetNonMintedValuesOut (at least for accountToUtxo txs)
    // @todo after all special checks are cleaned, only ::NotAllowedToFail() should depend on whether it's allowed to fail or not
    if (txType != CustomTxType::MintToken && nValuesIn.size() != values_out.size()) {
        return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-txns-tokens-differ",
            strprintf("token values in (%s) != values out (%s)", nValuesIn.size(), values_out.size()));
    }
    if (txType == CustomTxType::MintToken && nValuesIn.size() != 1) { // it is definitely type zero
        return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-txns-minttokens-inputs",
            strprintf("token inputs for MintToken tx should be Defi coins only"));
    }

    if (NotAllowedToFail(txType)) {
        CCustomCSView mnview_dummy(const_cast<CCustomCSView&>(*mnview)); // don't write into actual DB
        auto res = ApplyCustomTx(mnview_dummy, inputs, tx, Params(), nSpendHeight, true);
        if (!res.ok && (res.code & CustomTxErrCodes::Fatal)) {
            return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-txns-customtx", res.msg);
        }
    }

    for (auto && it = values_out.begin(); it != values_out.end(); ++it) {
        DCT_ID tokenId = it->first;
        if (tokenId == DCT_ID{0}) // skip defi, check rest
            continue; // @todo why isn't it a vulnerability? Can I mint DST 0 freely?

        if (txType == CustomTxType::MintToken) { // @todo use balances for minting, hence no need for special processing
            if (tokenId < CTokensView::DCT_ID_START) {
                return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-txns-minttokens-id-stable",
                    strprintf("token id (%s) is StableCoin and can't be minted", tokenId.ToString()));
            }
            auto token = mnview->ExistToken(tokenId);
            if (!token) {
                return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-txns-minttokens-id-absent",
                    strprintf("token id (%s) does not exist", tokenId.ToString()));
            }
            CTokenImplementation const & tokenImpl = static_cast<CTokenImplementation const &>(*token);
            // @todo sometimes it doesn't work. please store owner in DB, instead of recovering from UTXO
            if (!HasTokenAuth(tx, inputs, tokenImpl.creationTx)) {
                return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-txns-minttokens-auth",
                    strprintf("missed auth inputs for token id (%s), are you an owner of that token?", tokenId.ToString()));
            }
        } else {
            if (it->second < nValuesIn[tokenId]) {
                return state.Invalid(ValidationInvalidReason::CONSENSUS, false, REJECT_INVALID, "bad-txns-minttokens-in-belowout",
                    strprintf("token (%s) value in (%s) < value out (%s)", tokenId.ToString(), FormatMoney(nValuesIn[tokenId]), FormatMoney(it->second)));

            }
        }
    }
    return true;
}
