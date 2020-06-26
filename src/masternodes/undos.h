// Copyright (c) 2019 DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFICHAIN_UNDOS_H
#define DEFICHAIN_UNDOS_H

#include <masternodes/undo.h>
#include <flushablestorage.h>
#include <masternodes/res.h>

class CUndosView : public virtual CStorageView {
public:
    void ForEachUndo(std::function<bool(UndoKey key, CUndo const & Undo)> callback, UndoKey start) const;

    boost::optional<CUndo> GetUndo(UndoKey key) const;
    Res SetUndo(UndoKey key, CUndo const & undo);
    Res DelUndo(UndoKey key);

    // tags
    struct ByUndoKey { static const unsigned char prefix; };
};


#endif //DEFICHAIN_UNDOS_H
