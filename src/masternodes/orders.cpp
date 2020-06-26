// Copyright (c) 2019 DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/orders.h>

/// @attention make sure that it does not overlap with those in masternodes.cpp/tokens.cpp/undos.cpp/accounts.cpp !!!
const unsigned char COrdersView::ByCreationTx::prefix = 'R';

boost::optional<COrder> COrdersView::GetOrder(uint256 const & orderTx) const
{
    return ReadBy<ByCreationTx, COrder>(orderTx);
}

void COrdersView::ForEachOrder(std::function<bool(uint256 const & orderTx, COrder const & order)> callback, uint256 const & start) const
{
    ForEach<ByCreationTx, uint256, COrder>([&callback] (uint256 const & orderTx, COrder const & orderImpl) {
        return callback(orderTx, orderImpl);
    }, start);
}

Res COrdersView::DelOrder(uint256 const & orderTx)
{
    EraseBy<ByCreationTx>(orderTx);
    return Res::Ok();
}

Res COrdersView::SetOrder(uint256 const & orderTx, COrder const & order)
{
    WriteBy<ByCreationTx>(orderTx, order);
    return Res::Ok();
}
