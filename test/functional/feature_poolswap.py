#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test token's RPC.

- verify basic token's creation, destruction, revert, collateral locking
"""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, \
    connect_nodes_bi

class PoolPairTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 4
        # node0: main (Foundation)
        # node3: revert create (all)
        # node2: Non Foundation
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0'], ['-txnotokens=0'], ['-txnotokens=0'], ['-txnotokens=0']]


    def run_test(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI

        #self.nodes[0].generate(100)
        #self.sync_all()
        print("Generating initial chain...")
        self.setup_tokens()
        # Stop node #3 for future revert
        self.stop_node(3)

        # CREATION:
        #========================
        # 1 Getting new addresses and checking coins
        idGold = list(self.nodes[0].gettoken("GOLD").keys())[0]
        idSilver = list(self.nodes[0].gettoken("SILVER").keys())[0]
        accountGN0 = self.nodes[0].get_genesis_keys().ownerAuthAddress
        accountSN1 = self.nodes[1].get_genesis_keys().ownerAuthAddress
        initialGold = self.nodes[0].getaccount(accountGN0, {}, True)[idGold]
        initialSilver = self.nodes[1].getaccount(accountSN1, {}, True)[idSilver]
        print("Initial GOLD AccN0:", initialGold, ", id", idGold)
        print("Initial SILVER AccN1:", initialSilver, ", id", idSilver)

        owner = self.nodes[0].getnewaddress("", "legacy")

        # 2 Transferring SILVER from N1 Account to N0 Account
        self.nodes[1].accounttoaccount([], accountSN1, {accountGN0: "1000@SILVER"})
        self.nodes[1].generate(1)

        silverCheckN0 = self.nodes[0].getaccount(accountGN0, {}, True)[idSilver]
        print("Checking Silver on AccN0:", silverCheckN0, ", id", idSilver)
        silverCheckN1 = self.nodes[1].getaccount(accountSN1, {}, True)[idSilver]
        print("Checking Silver on AccN1:", silverCheckN1, ", id", idSilver)

        # 3 Creating poolpair 
        self.nodes[0].createpoolpair({
            "tokenA": "GOLD",
            "tokenB": "SILVER",
            "commission": 0.1,
            "status": True,
            "ownerFeeAddress": owner,
            "pairSymbol": "GS",
        }, [])
        self.nodes[0].generate(1)

        # only 4 tokens = DFI, GOLD, SILVER, GS
        assert_equal(len(self.nodes[0].listtokens()), 4)

        # check tokens id
        pool = self.nodes[0].getpoolpair("GS")
        idGS = list(self.nodes[0].gettoken("GS").keys())[0]
        assert(pool[idGS]['idTokenA'] == idGold)
        assert(pool[idGS]['idTokenB'] == idSilver)

        #list_pool = self.nodes[0].listpoolpairs()
        #print (list_pool)

        # 4 Adding liquidity
        self.nodes[0].addpoolliquidity({
            accountGN0: ["100@GOLD", "500@SILVER"]
        }, accountGN0, [])
        self.nodes[0].generate(1)

        goldCheckN0 = self.nodes[0].getaccount(accountGN0, {}, True)[idGold]
        print("Checking Gold on AccN0:", goldCheckN0, ", id", idGold)
        silverCheckN0 = self.nodes[0].getaccount(accountGN0, {}, True)[idSilver]
        print("Checking Silver on AccN0:", silverCheckN0, ", id", idSilver)

        # 5 Checking that liquidity is correct
        assert(goldCheckN0 == 900)
        assert(silverCheckN0 == 500)

        list_pool = self.nodes[0].listpoolpairs()
        #print (list_pool)

        assert(list_pool['130']['reserveA'] == 100)
        assert(list_pool['130']['reserveB'] == 500)

        # 6 Trying to poolswap
        self.nodes[0].poolswap({
            "from": accountGN0,
            "tokenFrom": "SILVER",
            "amountFrom": 10,
            "to": accountSN1,
            "tokenTo": "GOLD",
        }, [])
        self.nodes[0].generate(1)

        # 7 Sync
        self.sync_blocks([self.nodes[0], self.nodes[2]])

        # 8 Checking that poolswap is correct
        goldCheckN0 = self.nodes[2].getaccount(accountGN0, {}, True)[idGold]
        print("Checking Gold on AccN0:", goldCheckN0, ", id", idGold)
        silverCheckN0 = self.nodes[2].getaccount(accountGN0, {}, True)[idSilver]
        print("Checking Silver on AccN0:", silverCheckN0, ", id", idSilver)

        goldCheckN1 = self.nodes[2].getaccount(accountSN1, {}, True)[idGold]
        print("Checking Gold on AccN1:", goldCheckN1, ", id", idGold)
        silverCheckN1 = self.nodes[2].getaccount(accountSN1, {}, True)[idSilver]
        print("Checking Silver on AccN1:", silverCheckN1, ", id", idSilver)

        list_pool = self.nodes[2].listpoolpairs()
        #print (list_pool)

        assert(goldCheckN0 == 900)
        assert(silverCheckN0 == 490)
        assert(list_pool['130']['reserveA'] + goldCheckN1 == 100)
        assert(silverCheckN1 == 1000)
        assert(list_pool['130']['reserveB'] == 509) #510 - 1 (commission)

        # REVERTING:
        #========================
        print ("Reverting...")
        # Reverting creation!
        self.start_node(3)
        self.nodes[3].generate(30)

        connect_nodes_bi(self.nodes, 0, 3)
        self.sync_blocks()
        assert_equal(len(self.nodes[0].listpoolpairs()), 0)

if __name__ == '__main__':
    PoolPairTest ().main ()