//------------------------------------------------------------------------------
/*
    This file is part of clio: https://github.com/XRPLF/clio
    Copyright (c) 2023, the clio developers.

    Permission to use, copy, modify, and distribute this software for any
    purpose with or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL,  DIRECT,  INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include "rpc/Errors.h"
#include "rpc/common/AnyHandler.h"
#include "rpc/common/Types.h"
#include "rpc/handlers/ServerInfo.h"
#include "util/Fixtures.h"
#include "util/MockBackend.h"
#include "util/MockCounters.h"
#include "util/MockETLService.h"
#include "util/MockLoadBalancer.h"
#include "util/MockSubscriptionManager.h"
#include "util/TestObject.h"

#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <optional>

using namespace rpc;
namespace json = boost::json;
using namespace testing;

using TestServerInfoHandler =
    BaseServerInfoHandler<MockSubscriptionManager, MockLoadBalancer, MockETLService, MockCounters>;

constexpr static auto LEDGERHASH = "4BC50C9B0D8515D3EAAE1E74B29A95804346C491EE1A95BF25E4AAB854A6A652";
constexpr static auto CLIENTIP = "1.1.1.1";

class RPCServerInfoHandlerTest : public HandlerBaseTest,
                                 public MockLoadBalancerTest,
                                 public MockSubscriptionManagerTest,
                                 public MockCountersTest {
protected:
    void
    SetUp() override
    {
        HandlerBaseTest::SetUp();
        MockLoadBalancerTest::SetUp();
        MockSubscriptionManagerTest::SetUp();
        MockCountersTest::SetUp();

        rawBackendPtr = dynamic_cast<MockBackend*>(mockBackendPtr.get());
        ASSERT_NE(rawBackendPtr, nullptr);
        mockBackendPtr->updateRange(10);  // min
        mockBackendPtr->updateRange(30);  // max
    }

    void
    TearDown() override
    {
        MockCountersTest::TearDown();
        MockSubscriptionManagerTest::TearDown();
        MockLoadBalancerTest::TearDown();
        HandlerBaseTest::TearDown();
    }

    static void
    validateNormalOutput(rpc::ReturnType const& output)
    {
        ASSERT_TRUE(output);
        auto const& result = output.value().as_object();
        EXPECT_TRUE(result.contains("info"));

        auto const& info = result.at("info").as_object();
        EXPECT_TRUE(info.contains("complete_ledgers"));
        EXPECT_STREQ(info.at("complete_ledgers").as_string().c_str(), "10-30");
        EXPECT_TRUE(info.contains("load_factor"));
        EXPECT_TRUE(info.contains("clio_version"));
        EXPECT_TRUE(info.contains("libxrpl_version"));
        EXPECT_TRUE(info.contains("validated_ledger"));
        EXPECT_TRUE(info.contains("time"));
        EXPECT_TRUE(info.contains("uptime"));

        auto const& validated = info.at("validated_ledger").as_object();
        EXPECT_TRUE(validated.contains("age"));
        EXPECT_EQ(validated.at("age").as_uint64(), 3u);
        EXPECT_TRUE(validated.contains("hash"));
        EXPECT_STREQ(validated.at("hash").as_string().c_str(), LEDGERHASH);
        EXPECT_TRUE(validated.contains("seq"));
        EXPECT_EQ(validated.at("seq").as_uint64(), 30u);
        EXPECT_TRUE(validated.contains("base_fee_xrp"));
        EXPECT_EQ(validated.at("base_fee_xrp").as_double(), 1e-06);
        EXPECT_TRUE(validated.contains("reserve_base_xrp"));
        EXPECT_EQ(validated.at("reserve_base_xrp").as_double(), 3e-06);
        EXPECT_TRUE(validated.contains("reserve_inc_xrp"));
        EXPECT_EQ(validated.at("reserve_inc_xrp").as_double(), 2e-06);

        auto const& cache = info.at("cache").as_object();
        EXPECT_TRUE(cache.contains("size"));
        EXPECT_TRUE(cache.contains("is_full"));
        EXPECT_TRUE(cache.contains("latest_ledger_seq"));
        EXPECT_TRUE(cache.contains("object_hit_rate"));
        EXPECT_TRUE(cache.contains("successor_hit_rate"));
    }

    static void
    validateAdminOutput(rpc::ReturnType const& output, bool shouldHaveBackendCounters = false)
    {
        auto const& result = output.value().as_object();
        auto const& info = result.at("info").as_object();
        EXPECT_TRUE(info.contains("etl"));
        EXPECT_TRUE(info.contains("counters"));
        if (shouldHaveBackendCounters) {
            ASSERT_TRUE(info.contains("backend_counters")) << boost::json::serialize(info);
            EXPECT_TRUE(info.at("backend_counters").is_object());
            EXPECT_TRUE(!info.at("backend_counters").as_object().empty());
        }
    }

    static void
    validateRippledOutput(rpc::ReturnType const& output)
    {
        auto const& result = output.value().as_object();
        auto const& info = result.at("info").as_object();
        EXPECT_TRUE(info.contains("load_factor"));
        EXPECT_EQ(info.at("load_factor").as_int64(), 234);
        EXPECT_TRUE(info.contains("validation_quorum"));
        EXPECT_EQ(info.at("validation_quorum").as_int64(), 456);
        EXPECT_TRUE(info.contains("rippled_version"));
        EXPECT_STREQ(info.at("rippled_version").as_string().c_str(), "1234");
        EXPECT_TRUE(info.contains("network_id"));
        EXPECT_EQ(info.at("network_id").as_int64(), 2);
    }

    static void
    validateCacheOutput(rpc::ReturnType const& output)
    {
        auto const& result = output.value().as_object();
        auto const& info = result.at("info").as_object();
        auto const& cache = info.at("cache").as_object();
        EXPECT_EQ(cache.at("size").as_uint64(), 1u);
        EXPECT_EQ(cache.at("is_full").as_bool(), false);
        EXPECT_EQ(cache.at("latest_ledger_seq").as_uint64(), 30u);
        EXPECT_EQ(cache.at("object_hit_rate").as_double(), 1.0);
        EXPECT_EQ(cache.at("successor_hit_rate").as_double(), 1.0);
    }

    MockBackend* rawBackendPtr = nullptr;
};

TEST_F(RPCServerInfoHandlerTest, NoLedgerInfoErrorsOutWithInternal)
{
    EXPECT_CALL(*rawBackendPtr, fetchLedgerBySequence).WillOnce(Return(std::nullopt));

    auto const handler = AnyHandler{TestServerInfoHandler{
        mockBackendPtr, mockSubscriptionManagerPtr, mockLoadBalancerPtr, mockETLServicePtr, *mockCountersPtr
    }};

    runSpawn([&](auto yield) {
        auto const req = json::parse("{}");
        auto const output = handler.process(req, Context{yield});

        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.error());
        EXPECT_EQ(err.at("error").as_string(), "internal");
        EXPECT_EQ(err.at("error_message").as_string(), "Internal error.");
    });
}

TEST_F(RPCServerInfoHandlerTest, NoFeesErrorsOutWithInternal)
{
    auto const ledgerinfo = CreateLedgerInfo(LEDGERHASH, 30);
    EXPECT_CALL(*rawBackendPtr, fetchLedgerBySequence).WillOnce(Return(ledgerinfo));
    EXPECT_CALL(*rawBackendPtr, doFetchLedgerObject).WillOnce(Return(std::nullopt));

    auto const handler = AnyHandler{TestServerInfoHandler{
        mockBackendPtr, mockSubscriptionManagerPtr, mockLoadBalancerPtr, mockETLServicePtr, *mockCountersPtr
    }};

    runSpawn([&](auto yield) {
        auto const req = json::parse("{}");
        auto const output = handler.process(req, Context{yield});

        ASSERT_FALSE(output);
        auto const err = rpc::makeError(output.error());
        EXPECT_EQ(err.at("error").as_string(), "internal");
        EXPECT_EQ(err.at("error_message").as_string(), "Internal error.");
    });
}

TEST_F(RPCServerInfoHandlerTest, DefaultOutputIsPresent)
{
    MockLoadBalancer* rawBalancerPtr = mockLoadBalancerPtr.get();
    MockCounters* rawCountersPtr = mockCountersPtr.get();
    MockETLService* rawETLServicePtr = mockETLServicePtr.get();

    auto const ledgerinfo = CreateLedgerInfo(LEDGERHASH, 30, 3);  // 3 seconds old
    EXPECT_CALL(*rawBackendPtr, fetchLedgerBySequence).WillOnce(Return(ledgerinfo));

    auto const feeBlob = CreateFeeSettingBlob(1, 2, 3, 4, 0);
    EXPECT_CALL(*rawBackendPtr, doFetchLedgerObject).WillOnce(Return(feeBlob));

    EXPECT_CALL(*rawBalancerPtr, forwardToRippled(testing::_, testing::Eq(CLIENTIP), testing::_))
        .WillOnce(Return(std::nullopt));

    EXPECT_CALL(*rawCountersPtr, uptime).WillOnce(Return(std::chrono::seconds{1234}));

    EXPECT_CALL(*rawETLServicePtr, isAmendmentBlocked).WillOnce(Return(false));

    auto const handler = AnyHandler{TestServerInfoHandler{
        mockBackendPtr, mockSubscriptionManagerPtr, mockLoadBalancerPtr, mockETLServicePtr, *mockCountersPtr
    }};

    runSpawn([&](auto yield) {
        auto const req = json::parse("{}");
        auto const output = handler.process(req, Context{yield, {}, false, CLIENTIP});

        validateNormalOutput(output);

        // no admin section present by default
        auto const& result = output.value().as_object();
        auto const& info = result.at("info").as_object();
        EXPECT_FALSE(info.contains("etl"));
        EXPECT_FALSE(info.contains("counters"));
    });
}

TEST_F(RPCServerInfoHandlerTest, AmendmentBlockedIsPresentIfSet)
{
    MockLoadBalancer* rawBalancerPtr = mockLoadBalancerPtr.get();
    MockCounters* rawCountersPtr = mockCountersPtr.get();
    MockETLService* rawETLServicePtr = mockETLServicePtr.get();

    auto const ledgerinfo = CreateLedgerInfo(LEDGERHASH, 30, 3);  // 3 seconds old
    EXPECT_CALL(*rawBackendPtr, fetchLedgerBySequence).WillOnce(Return(ledgerinfo));

    auto const feeBlob = CreateFeeSettingBlob(1, 2, 3, 4, 0);
    EXPECT_CALL(*rawBackendPtr, doFetchLedgerObject).WillOnce(Return(feeBlob));

    EXPECT_CALL(*rawBalancerPtr, forwardToRippled(testing::_, testing::Eq(CLIENTIP), testing::_))
        .WillOnce(Return(std::nullopt));

    EXPECT_CALL(*rawCountersPtr, uptime).WillOnce(Return(std::chrono::seconds{1234}));

    EXPECT_CALL(*rawETLServicePtr, isAmendmentBlocked).WillOnce(Return(true));

    auto const handler = AnyHandler{TestServerInfoHandler{
        mockBackendPtr, mockSubscriptionManagerPtr, mockLoadBalancerPtr, mockETLServicePtr, *mockCountersPtr
    }};

    runSpawn([&](auto yield) {
        auto const req = json::parse("{}");
        auto const output = handler.process(req, Context{yield, {}, false, CLIENTIP});

        validateNormalOutput(output);

        auto const& info = output.value().as_object().at("info").as_object();
        EXPECT_TRUE(info.contains("amendment_blocked"));
        EXPECT_EQ(info.at("amendment_blocked").as_bool(), true);
    });
}

TEST_F(RPCServerInfoHandlerTest, AdminSectionPresentWhenAdminFlagIsSet)
{
    MockLoadBalancer* rawBalancerPtr = mockLoadBalancerPtr.get();
    MockCounters* rawCountersPtr = mockCountersPtr.get();
    MockSubscriptionManager* rawSubscriptionManagerPtr = mockSubscriptionManagerPtr.get();
    MockETLService* rawETLServicePtr = mockETLServicePtr.get();

    auto const empty = json::object{};
    auto const ledgerinfo = CreateLedgerInfo(LEDGERHASH, 30, 3);  // 3 seconds old
    EXPECT_CALL(*rawBackendPtr, fetchLedgerBySequence).WillOnce(Return(ledgerinfo));

    auto const feeBlob = CreateFeeSettingBlob(1, 2, 3, 4, 0);
    EXPECT_CALL(*rawBackendPtr, doFetchLedgerObject).WillOnce(Return(feeBlob));

    EXPECT_CALL(*rawBalancerPtr, forwardToRippled).WillOnce(Return(empty));

    EXPECT_CALL(*rawCountersPtr, uptime).WillOnce(Return(std::chrono::seconds{1234}));

    EXPECT_CALL(*rawETLServicePtr, isAmendmentBlocked).WillOnce(Return(false));

    // admin calls
    EXPECT_CALL(*rawCountersPtr, report).WillOnce(Return(empty));

    EXPECT_CALL(*rawSubscriptionManagerPtr, report).WillOnce(Return(empty));

    EXPECT_CALL(*rawETLServicePtr, getInfo).WillOnce(Return(empty));

    auto const handler = AnyHandler{TestServerInfoHandler{
        mockBackendPtr, mockSubscriptionManagerPtr, mockLoadBalancerPtr, mockETLServicePtr, *mockCountersPtr
    }};

    runSpawn([&](auto yield) {
        auto const req = json::parse("{}");
        auto const output = handler.process(req, Context{yield, {}, true});

        validateNormalOutput(output);
        validateAdminOutput(output);
    });
}

TEST_F(RPCServerInfoHandlerTest, BackendCountersPresentWhenRequestWithParam)
{
    MockLoadBalancer* rawBalancerPtr = mockLoadBalancerPtr.get();
    MockCounters* rawCountersPtr = mockCountersPtr.get();
    MockSubscriptionManager* rawSubscriptionManagerPtr = mockSubscriptionManagerPtr.get();
    MockETLService* rawETLServicePtr = mockETLServicePtr.get();

    auto const empty = json::object{};
    auto const ledgerinfo = CreateLedgerInfo(LEDGERHASH, 30, 3);  // 3 seconds old
    EXPECT_CALL(*rawBackendPtr, fetchLedgerBySequence).WillOnce(Return(ledgerinfo));

    auto const feeBlob = CreateFeeSettingBlob(1, 2, 3, 4, 0);
    EXPECT_CALL(*rawBackendPtr, doFetchLedgerObject).WillOnce(Return(feeBlob));

    EXPECT_CALL(*rawBalancerPtr, forwardToRippled).WillOnce(Return(empty));

    EXPECT_CALL(*rawCountersPtr, uptime).WillOnce(Return(std::chrono::seconds{1234}));

    EXPECT_CALL(*rawETLServicePtr, isAmendmentBlocked).WillOnce(Return(false));

    // admin calls
    EXPECT_CALL(*rawCountersPtr, report).WillOnce(Return(empty));

    EXPECT_CALL(*rawSubscriptionManagerPtr, report).WillOnce(Return(empty));

    EXPECT_CALL(*rawETLServicePtr, getInfo).WillOnce(Return(empty));

    EXPECT_CALL(*rawBackendPtr, stats).WillOnce(Return(boost::json::object{{"read_cout", 10}, {"write_count", 3}}));

    auto const handler = AnyHandler{TestServerInfoHandler{
        mockBackendPtr, mockSubscriptionManagerPtr, mockLoadBalancerPtr, mockETLServicePtr, *mockCountersPtr
    }};

    runSpawn([&](auto yield) {
        auto const req = json::parse(R"(
        {
            "backend_counters": true
        }
        )");
        auto const output = handler.process(req, Context{yield, {}, true});

        validateNormalOutput(output);
        validateAdminOutput(output, true);
    });
}

TEST_F(RPCServerInfoHandlerTest, RippledForwardedValuesPresent)
{
    MockLoadBalancer* rawBalancerPtr = mockLoadBalancerPtr.get();
    MockCounters* rawCountersPtr = mockCountersPtr.get();
    MockSubscriptionManager* rawSubscriptionManagerPtr = mockSubscriptionManagerPtr.get();
    MockETLService* rawETLServicePtr = mockETLServicePtr.get();

    auto const empty = json::object{};
    auto const ledgerinfo = CreateLedgerInfo(LEDGERHASH, 30, 3);  // 3 seconds old
    EXPECT_CALL(*rawBackendPtr, fetchLedgerBySequence).WillOnce(Return(ledgerinfo));

    auto const feeBlob = CreateFeeSettingBlob(1, 2, 3, 4, 0);
    EXPECT_CALL(*rawBackendPtr, doFetchLedgerObject).WillOnce(Return(feeBlob));

    EXPECT_CALL(*rawCountersPtr, uptime).WillOnce(Return(std::chrono::seconds{1234}));

    EXPECT_CALL(*rawETLServicePtr, isAmendmentBlocked).WillOnce(Return(false));

    auto const rippledObj = json::parse(R"({
        "result": {
            "info": {
                "build_version": "1234",
                "validation_quorum": 456,
                "load_factor": 234,
                "network_id": 2
            }
        }
    })");
    EXPECT_CALL(*rawBalancerPtr, forwardToRippled).WillOnce(Return(rippledObj.as_object()));

    // admin calls
    EXPECT_CALL(*rawCountersPtr, report).WillOnce(Return(empty));

    EXPECT_CALL(*rawSubscriptionManagerPtr, report).WillOnce(Return(empty));

    EXPECT_CALL(*rawETLServicePtr, getInfo).WillOnce(Return(empty));

    auto const handler = AnyHandler{TestServerInfoHandler{
        mockBackendPtr, mockSubscriptionManagerPtr, mockLoadBalancerPtr, mockETLServicePtr, *mockCountersPtr
    }};

    runSpawn([&](auto yield) {
        auto const req = json::parse("{}");
        auto const output = handler.process(req, Context{yield, {}, true});

        validateNormalOutput(output);
        validateAdminOutput(output);
        validateRippledOutput(output);
    });
}

TEST_F(RPCServerInfoHandlerTest, RippledForwardedValuesMissingNoExceptionThrown)
{
    MockLoadBalancer* rawBalancerPtr = mockLoadBalancerPtr.get();
    MockCounters* rawCountersPtr = mockCountersPtr.get();
    MockSubscriptionManager* rawSubscriptionManagerPtr = mockSubscriptionManagerPtr.get();
    MockETLService* rawETLServicePtr = mockETLServicePtr.get();

    auto const empty = json::object{};
    auto const ledgerinfo = CreateLedgerInfo(LEDGERHASH, 30, 3);  // 3 seconds old
    EXPECT_CALL(*rawBackendPtr, fetchLedgerBySequence).WillOnce(Return(ledgerinfo));

    auto const feeBlob = CreateFeeSettingBlob(1, 2, 3, 4, 0);
    EXPECT_CALL(*rawBackendPtr, doFetchLedgerObject).WillOnce(Return(feeBlob));

    EXPECT_CALL(*rawCountersPtr, uptime).WillOnce(Return(std::chrono::seconds{1234}));

    EXPECT_CALL(*rawETLServicePtr, isAmendmentBlocked).WillOnce(Return(false));

    auto const rippledObj = json::parse(R"({
        "result": {
            "info": {}
        }
    })");
    EXPECT_CALL(*rawBalancerPtr, forwardToRippled).WillOnce(Return(rippledObj.as_object()));

    // admin calls
    EXPECT_CALL(*rawCountersPtr, report).WillOnce(Return(empty));

    EXPECT_CALL(*rawSubscriptionManagerPtr, report).WillOnce(Return(empty));

    EXPECT_CALL(*rawETLServicePtr, getInfo).WillOnce(Return(empty));

    auto const handler = AnyHandler{TestServerInfoHandler{
        mockBackendPtr, mockSubscriptionManagerPtr, mockLoadBalancerPtr, mockETLServicePtr, *mockCountersPtr
    }};

    runSpawn([&](auto yield) {
        auto const req = json::parse("{}");
        auto const output = handler.process(req, Context{yield, {}, true});

        validateNormalOutput(output);
        validateAdminOutput(output);
    });
}

// TODO: In the future we'd like to refactor to add mock and test for cache
