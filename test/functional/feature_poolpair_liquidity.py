#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test pool's RPC.

- verify basic accounts operation
"""

from test_framework.test_framework import DefiTestFramework

from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, \
    connect_nodes_bi

class PoolLiquidityTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        # node0: main
        # node1: secondary tester
        # node2: revert create (all)
        self.setup_clean_chain = True
        self.extra_args = [['-txnotokens=0'], ['-txnotokens=0'], ['-txnotokens=0']]

    def run_test(self):
        assert_equal(len(self.nodes[0].listtokens()), 1) # only one token == DFI

        print("Generating initial chain...")
        self.setup_tokens()

        # stop node #2 for future revert
        self.stop_node(2)

        idGold = list(self.nodes[0].gettoken("GOLD").keys())[0]
        idSilver = list(self.nodes[0].gettoken("SILVER").keys())[0]
        accountGold = self.nodes[0].get_genesis_keys().ownerAuthAddress
        accountSilver = self.nodes[1].get_genesis_keys().ownerAuthAddress
        initialGold = self.nodes[0].getaccount(accountGold, {}, True)[idGold]
        initialSilver = self.nodes[1].getaccount(accountSilver, {}, True)[idSilver]
        print("Initial GOLD:", initialGold, ", id", idGold)
        print("Initial SILVER:", initialSilver, ", id", idSilver)

        owner = self.nodes[0].getnewaddress("", "legacy")

        # transfer silver
        self.nodes[1].accounttoaccount([], accountSilver, {accountGold: "1000@SILVER"})
        self.nodes[1].generate(1)

        # create pool
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

        ################
        # TEST TEST TEST
        print ("-------------------------------------")
        try:
            self.nodes[0].createpoolpair({
                "tokenA": "SILVER",
                "tokenB": "GOLD",
                "commission": 0.1,
                "status": True,
                "ownerFeeAddress": owner,
                "pairSymbol": "GGSS",
            }, [])
            self.nodes[0].generate(1)

            assert_equal(len(self.nodes[0].listtokens()), 5)
        except JSONRPCException as e:
            errorString = e.error['message']
            print (errorString)

        try:
            list_pool = self.nodes[0].listpoolpairs({}, False)
            print (list_pool)
        except JSONRPCException as e:
            errorString = e.error['message']
            print (errorString)
        print ("-------------------------------------")
        ################

        # Add liquidity
        #========================
        # one token
        try:
            self.nodes[0].addpoolliquidity({
                accountGold: "100@SILVER"
            }, accountGold, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("the pool pair requires two tokens" in errorString)

        # missing amount
        try:
            self.nodes[0].addpoolliquidity({
                accountGold: ["0@GOLD", "0@SILVER"]
            }, accountGold, [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Amount out of range" in errorString)

        # transfer
        try:
            self.nodes[0].addpoolliquidity({
                accountGold: ["100@GOLD", "100@SILVER"]
            }, accountGold, [])
            self.nodes[0].generate(1)
        except JSONRPCException as e:
            errorString = e.error['message']
        print (errorString)

        #print(self.nodes[0].getaccount(accountGold, {}, True))

        # Remove liquidity
        #========================
        # missing pool
        try:
            self.nodes[0].removepoolliquidity(accountGold, "100@DFI", [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("there is no such pool pair" in errorString)

        # missing amount
        try:
            self.nodes[0].removepoolliquidity(accountGold, "0@GS", [])
        except JSONRPCException as e:
            errorString = e.error['message']
        
        # missing (account exists, but does not belong)
        try:
            self.nodes[0].removepoolliquidity(owner, "100@GS", [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Are you an owner?" in errorString)

        # missing from (account exist, but no tokens) ???
        try:
            self.nodes[0].removepoolliquidity(accountGold, "100@GS", [])
        except JSONRPCException as e:
            errorString = e.error['message']
        assert("Amount 0 is less than" in errorString)

        # REVERTING:
        #========================
        print ("Reverting...")
        self.start_node(2)
        self.nodes[2].generate(20)

        connect_nodes_bi(self.nodes, 1, 2)
        self.sync_blocks()

        assert_equal(self.nodes[0].getaccount(accountGold, {}, True)[idGold], initialGold)
        assert_equal(self.nodes[0].getaccount(accountSilver, {}, True)[idSilver], initialSilver)

        assert_equal(len(self.nodes[0].getrawmempool()), 2) # 2 txs


if __name__ == '__main__':
    PoolLiquidityTest ().main ()