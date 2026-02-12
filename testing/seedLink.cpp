#include <string>
#include <chrono>
#ifndef NDEBUG
#include <cassert>
#endif
#include "seedLinkClientOptions.hpp"
#include "streamSelector.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

TEST_CASE("USEEDLinkToDataPacketImportProxy::StreamSelector",
          "[streamSelector]")
{
    namespace USD = USEEDLinkToDataPacketImportProxy;
    USD::StreamSelector selector;
    const std::string network{"UU"};
    const std::string station{"*"};
    const std::string channel{"HH?"};
    REQUIRE_NOTHROW(selector.setNetwork(network));
    REQUIRE_NOTHROW(selector.setStation(station));
    REQUIRE(selector.getNetwork() == network);
    REQUIRE(selector.getStation() == station);
    SECTION("No location code")
    {
        selector.setSelector(channel, USD::StreamSelector::Type::Data);
        REQUIRE(selector.getSelector() == "??HH?.D");
    }
    SECTION("Location code")
    {
        selector.setSelector("", "01", USD::StreamSelector::Type::Data);
        REQUIRE(selector.getSelector() == "01*.D");
    }
}

TEST_CASE("USEEDLinkToDataPacketImportProxy::StreamSelector",
          "[clienOptions]")
{
    namespace USD = USEEDLinkToDataPacketImportProxy;
    USD::SEEDLinkClientOptions clientOptions;
    SECTION("Defaults")
    {
        REQUIRE(clientOptions.getHost() == "rtserve.iris.washington.edu");
        REQUIRE(clientOptions.getPort() == 18000);
        REQUIRE(clientOptions.getNetworkReconnectDelay() == std::chrono::seconds {30});
        REQUIRE(clientOptions.getNetworkTimeOut() == std::chrono::seconds {300});
        REQUIRE(clientOptions.deleteStateFileOnStart() == false);
        REQUIRE(clientOptions.deleteStateFileOnStop() == false);
        REQUIRE(clientOptions.getStreamSelectors().empty() == true);
    }

    SECTION("Options")
    {
        const std::string host{"localhost"};
        const uint16_t port{54321};
        const std::chrono::seconds reconnectDelay{25};
        const std::chrono::seconds networkTimeOut{50};
        std::vector<USD::StreamSelector> selectors;

        USD::StreamSelector selector1;
        const std::string network1{"UU"};
        const std::string station1{"*"};
        REQUIRE_NOTHROW(selector1.setNetwork(network1));
        REQUIRE_NOTHROW(selector1.setStation(station1));

        USD::StreamSelector selector2;
        const std::string network2{"WY"};
        const std::string station2{"YHB"};
        const std::string channel2{"HH?"};
        REQUIRE_NOTHROW(selector2.setNetwork(network2));
        REQUIRE_NOTHROW(selector2.setStation(station2));
        selector2.setSelector(channel2, "01", USD::StreamSelector::Type::Data);

        selectors.push_back(selector1); 
        selectors.push_back(selector2);

        clientOptions.setHost(host);
        clientOptions.setPort(port);
        clientOptions.setNetworkReconnectDelay(reconnectDelay);
        clientOptions.setNetworkTimeOut(networkTimeOut);
        clientOptions.enableDeleteStateFileOnStart();
        clientOptions.enableDeleteStateFileOnStop();
        for (const auto &s : selectors)
        {
            REQUIRE(clientOptions.addStreamSelector(s));
        }
        REQUIRE(clientOptions.getHost() == host);
        REQUIRE(clientOptions.getPort() == port);
        REQUIRE(clientOptions.getNetworkReconnectDelay() == reconnectDelay);
        REQUIRE(clientOptions.getNetworkTimeOut() == networkTimeOut);
        REQUIRE(clientOptions.deleteStateFileOnStart() == true);
        REQUIRE(clientOptions.deleteStateFileOnStop() == true);
        auto selectorsBack = clientOptions.getStreamSelectors();
        REQUIRE(selectorsBack.size() == 2);
        bool okay{true};
        for (int i = 0; i < 2; ++i)
        {
            auto s = selectorsBack.at(i);
            if (s.getNetwork() == "WY")
            {
                REQUIRE(s.getStation() == "YHB");
                REQUIRE(s.getSelector() == "01HH?.D");
            }
            else if (s.getNetwork() == "UU")
            {
                REQUIRE(s.getStation() == "*");
            }
            else
            {
               okay = false;
            }
        }
        REQUIRE(okay == true);
    }
}
