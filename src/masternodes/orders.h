// Copyright (c) 2019 DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_ORDERS_H
#define DEFI_ORDERS_H

#include <masternodes/order.h>
#include <flushablestorage.h>
#include <masternodes/res.h>


class COrdersView : public virtual CStorageView
{
public:
    void ForEachOrder(std::function<bool(uint256 const & orderTx, COrder const & order)> callback, uint256 const & start) const;

    boost::optional<COrder> GetOrder(uint256 const & orderTx) const;
    Res SetOrder(uint256 const & orderTx, COrder const & order);
    Res DelOrder(uint256 const & orderTx);

    // tags
    struct ByCreationTx { static const unsigned char prefix; };
};

#endif //DEFI_ORDERS_H
