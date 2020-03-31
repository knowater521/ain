#include <chainparams.h>
#include <flushablestorage.h>
#include <masternodes/masternodes.h>
#include <test/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(storage_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(basic)
{
    using TBytes = std::vector<unsigned char>;
    CStorageLevelDB st(GetDataDir() / "testkv", 1000000, false, true);

    st.Write({1}, {'A'} );
//    st.Flush();
    TBytes value;
    st.Read({1}, value);
    printf("Result: %s\n", std::string(value.begin(), value.end()).c_str());

    auto view = MakeUnique<CEnhanced123>(st);
//    view->Test();
    st.Read({1}, value);
    printf("Result non flushed: %s\n", std::string(value.begin(), value.end()).c_str());
    view->Flush() && st.Flush();
    st.Read({1}, value);
    printf("Result: %s\n", std::string(value.begin(), value.end()).c_str());

    auto team = view->CalcNextTeam(uint256());
    (void) team;

}


BOOST_AUTO_TEST_SUITE_END()
