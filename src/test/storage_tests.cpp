#include <masternodes/masternodes.h>
#include <test/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(storage_tests, TestingSetup)


int GetTokensCount()
{
    int counter{0};
    pcustomcsview->ForEachToken([&counter] (DCT_ID const & id, CToken const & token) {
//        printf("DCT_ID: %d, Token: %s: %s\n", id, token.symbol.c_str(), token.name.c_str()); // dump for debug
        ++counter;
        return true;
    });
    return counter;
}

BOOST_AUTO_TEST_CASE(tokens)
{
    BOOST_REQUIRE(GetTokensCount() == 1);
    {   // search by id
        auto token = pcustomcsview->ExistToken(DCT_ID{0});
        BOOST_REQUIRE(token);
        BOOST_REQUIRE(token->symbol == "DFI");
    }
    {   // search by symbol
        auto pair = pcustomcsview->ExistToken("DFI");
        BOOST_REQUIRE(pair);
        BOOST_REQUIRE(pair->first == DCT_ID{0});
        BOOST_REQUIRE(pair->second);
        BOOST_REQUIRE(pair->second->symbol == "DFI");
    }

    // token creation
    CTokenImplementation token1;
    token1.symbol = "DCT1";
    token1.creationTx = uint256S("0x1111");
    BOOST_REQUIRE(pcustomcsview->CreateToken(token1));
    BOOST_REQUIRE(GetTokensCount() == 2);
    {   // search by id
        auto token = pcustomcsview->ExistToken(DCT_ID{128});
        BOOST_REQUIRE(token);
        BOOST_REQUIRE(token->symbol == "DCT1");
    }
    {   // search by symbol
        auto pair = pcustomcsview->ExistToken("DCT1");
        BOOST_REQUIRE(pair);
        BOOST_REQUIRE(pair->first == DCT_ID{128});
        BOOST_REQUIRE(pair->second);
        BOOST_REQUIRE(pair->second->symbol == "DCT1");
    }
    {   // search by tx
        auto pair = pcustomcsview->ExistTokenByCreationTx(uint256S("0x1111"));
        BOOST_REQUIRE(pair);
        BOOST_REQUIRE(pair->first == DCT_ID{128});
        BOOST_REQUIRE(pair->second.creationTx == uint256S("0x1111"));
    }

    // another token creation
    BOOST_REQUIRE(pcustomcsview->CreateToken(token1) == false); /// duplicate symbol & tx
    token1.symbol = "DCT2";
    BOOST_REQUIRE(pcustomcsview->CreateToken(token1) == false); /// duplicate tx
    token1.creationTx = uint256S("0x2222");
    BOOST_REQUIRE(pcustomcsview->CreateToken(token1));
    BOOST_REQUIRE(GetTokensCount() == 3);
    {   // search by id
        auto token = pcustomcsview->ExistToken(DCT_ID{129});
        BOOST_REQUIRE(token);
        BOOST_REQUIRE(token->symbol == "DCT2");
    }
    {   // search by symbol
        auto pair = pcustomcsview->ExistToken("DCT2");
        BOOST_REQUIRE(pair);
        BOOST_REQUIRE(pair->first == DCT_ID{129});
        BOOST_REQUIRE(pair->second);
        BOOST_REQUIRE(pair->second->symbol == "DCT2");
    }
    {   // search by tx
        auto pair = pcustomcsview->ExistTokenByCreationTx(uint256S("0x2222"));
        BOOST_REQUIRE(pair);
        BOOST_REQUIRE(pair->first == DCT_ID{129});
        BOOST_REQUIRE(pair->second.creationTx == uint256S("0x2222"));
    }

    // revert create token
    BOOST_REQUIRE(pcustomcsview->RevertCreateToken(uint256S("0xffff")) == false);
    BOOST_REQUIRE(pcustomcsview->RevertCreateToken(uint256S("0x1111")) == false);
    BOOST_REQUIRE(pcustomcsview->RevertCreateToken(uint256S("0x2222")));
    BOOST_REQUIRE(GetTokensCount() == 2);
    {   // search by id
        auto token = pcustomcsview->ExistToken(DCT_ID{128});
        BOOST_REQUIRE(token);
        BOOST_REQUIRE(token->symbol == "DCT1");
    }

    // create again, with same tx and dctid
    token1.symbol = "DCT3";
    token1.creationTx = uint256S("0x2222"); // SAME!
    BOOST_REQUIRE(pcustomcsview->CreateToken(token1));
    BOOST_REQUIRE(GetTokensCount() == 3);
    {   // search by id
        auto token = pcustomcsview->ExistToken(DCT_ID{129});
        BOOST_REQUIRE(token);
        BOOST_REQUIRE(token->symbol == "DCT3");
    }

    // destroy token
//    BOOST_REQUIRE(pcustomcsview->DestroyToken(0, uint256S("0x2222"), 999) == false); // stable coin!
    BOOST_REQUIRE(pcustomcsview->DestroyToken(uint256S("0x3333"), uint256S("0xaaaa"), 999) == false); // nonexist
    BOOST_REQUIRE(pcustomcsview->DestroyToken(uint256S("0x2222"), uint256S("0xaaaa"), 999)); // ok
    BOOST_REQUIRE(pcustomcsview->DestroyToken(uint256S("0x2222"), uint256S("0xbbbb"), 999) == false); // already destroyed
    {   // search by id
        auto token = pcustomcsview->ExistToken(DCT_ID{129});
        BOOST_REQUIRE(token);
        auto tokenImpl = static_cast<CTokenImplementation &>(*token);
        BOOST_REQUIRE(tokenImpl.destructionHeight == 999);
        BOOST_REQUIRE(tokenImpl.destructionTx == uint256S("0xaaaa"));
    }

    // revert destroy token
//    BOOST_REQUIRE(pcustomcsview->RevertDestroyToken(0, uint256S("0x2222")) == false); // stable coin!
    BOOST_REQUIRE(pcustomcsview->RevertDestroyToken(uint256S("0x3333"), uint256S("0xaaaa")) == false); // nonexist
    BOOST_REQUIRE(pcustomcsview->RevertDestroyToken(uint256S("0x1111"), uint256S("0xaaaa")) == false); // not destroyed, active
    BOOST_REQUIRE(pcustomcsview->RevertDestroyToken(uint256S("0x2222"), uint256S("0xbbbb")) == false); // destroyed, but wrong tx for revert
    BOOST_REQUIRE(pcustomcsview->RevertDestroyToken(uint256S("0x2222"), uint256S("0xaaaa"))); // ok!
    {   // search by id
        auto token = pcustomcsview->ExistToken(DCT_ID{129});
        BOOST_REQUIRE(token);
        auto tokenImpl = static_cast<CTokenImplementation &>(*token);
        BOOST_REQUIRE(tokenImpl.destructionHeight == -1);
        BOOST_REQUIRE(tokenImpl.destructionTx == uint256{});
    }
    BOOST_REQUIRE(GetTokensCount() == 3);
}


BOOST_AUTO_TEST_SUITE_END()
