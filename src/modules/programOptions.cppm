module;
#include <iostream>
#include <string>
#include <chrono>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <boost/algorithm/string.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include "grpcOptions.hpp"
#include "seedLinkClientOptions.hpp"
#include "streamSelector.hpp"

export module ProgramOptions;

namespace USEEDLinkToDataPacketImportProxy
{

#define APPLICATION_NAME "uSEEDLinkToDataPacketImportProxy"

export struct OTelHTTPMetricsOptions
{
    std::string url{"localhost:4318"};
    std::chrono::milliseconds exportInterval{5000};
    std::chrono::milliseconds exportTimeOut{500};
    std::string suffix{"/v1/metrics"};
};

export struct OTelHTTPLogOptions
{
    std::string url{"localhost:4318"};
    std::filesystem::path certificatePath;
    std::string suffix{"/v1/logs"};
};

export struct ProgramOptions
{
    std::string applicationName{APPLICATION_NAME};
    OTelHTTPMetricsOptions otelHTTPMetricsOptions;
    OTelHTTPLogOptions otelHTTPLogOptions;
    GRPCOptions grpcOptions;
    //UDataPacketImportProxy::ProxyOptions proxyOptions;
    USEEDLinkToDataPacketImportProxy::SEEDLinkClientOptions
        seedLinkClientOptions;
    std::chrono::seconds printSummaryInterval{3600};
    std::vector<std::chrono::seconds> retrySchedule
    {
        std::chrono::seconds {1},
        std::chrono::seconds {5},
        std::chrono::seconds {15}
    };
    int maximumImportQueueSize{8192};
    int verbosity{3};
    bool exportLogs{false};
    bool exportMetrics{false};
};

/// Read the program options from the command line
export
std::pair<std::string, bool> parseCommandLineOptions(int argc, char *argv[])
{
    std::string iniFile;
    boost::program_options::options_description desc(R"""(
The uSEEDLinkToDataPacketImportProxy reads data packets via a SEEDLink client
and forwards those packets to the uDataPacketImportProxy.

Example usage is:

    uSEEDLinkToDataPacketImportProxy --ini=import.ini

Allowed options)""");
    desc.add_options()
        ("help", "Produces this help message")
        ("ini",  boost::program_options::value<std::string> (),
                 "The initialization file for this executable");
    boost::program_options::variables_map vm;
    boost::program_options::store(
        boost::program_options::parse_command_line(argc, argv, desc), vm);
    boost::program_options::notify(vm);
    if (vm.count("help"))
    {
        std::cout << desc << std::endl;
        return {iniFile, true};
    }
    if (vm.count("ini"))
    {
        iniFile = vm["ini"].as<std::string>();
        if (!std::filesystem::exists(iniFile))
        {
            throw std::runtime_error("Initialization file: " + iniFile
                                   + " does not exist");
        }
    }
    else
    {
        throw std::runtime_error("Initialization file not specified");
    }
    return {iniFile, false};
}


USEEDLinkToDataPacketImportProxy::SEEDLinkClientOptions
getSEEDLinkOptions(const boost::property_tree::ptree &propertyTree,
                   const std::string &clientName)
{
    using namespace USEEDLinkToDataPacketImportProxy;
    SEEDLinkClientOptions clientOptions;
    auto host = propertyTree.get<std::string> (clientName + ".host");
    auto port = propertyTree.get<uint16_t> (clientName + ".port", 18000);
    clientOptions.setHost(host);
    clientOptions.setPort(port);
    auto stateFile
        = propertyTree.get<std::string> (clientName + ".stateFile", "");
    if (!stateFile.empty())
    {
        std::filesystem::path stateFilePath{stateFile};
        if (stateFilePath.has_parent_path())
        {
            auto parentPath = stateFilePath.parent_path();
            if (!std::filesystem::exists(parentPath))
            {
                if (!std::filesystem::create_directories(parentPath))
                {
                    throw std::runtime_error("Could not create parent path "
                                           + parentPath.string());
                }
            }
        }
        auto deleteStateFileOnStart = clientOptions.deleteStateFileOnStart();
        deleteStateFileOnStart
            = propertyTree.get<bool> (clientName + ".deleteStateFileOnStart",
                                      deleteStateFileOnStart);
        if (deleteStateFileOnStart)
        {
            clientOptions.enableDeleteStateFileOnStart();
        }
        else
        {
            clientOptions.disableDeleteStateFileOnStart();
        }

        auto deleteStateFileOnStop = clientOptions.deleteStateFileOnStop();
        deleteStateFileOnStop
            = propertyTree.get<bool> (clientName + ".deleteStateFileOnStop",
                                      deleteStateFileOnStop);
        if (deleteStateFileOnStop)
        {
            clientOptions.enableDeleteStateFileOnStop();
        }
        else
        {
            clientOptions.disableDeleteStateFileOnStop();
        }   
    }

    for (int iSelector = 1; iSelector <= 32768; ++iSelector)
    {
        std::string selectorName{clientName
                               + ".data_selector_"
                               + std::to_string(iSelector)};
        auto selectorString
            = propertyTree.get_optional<std::string> (selectorName);
        if (selectorString)
        {
            std::vector<std::string> splitSelectors;
            boost::split(splitSelectors, *selectorString,
                         boost::is_any_of(",|"));
            // A selector string can look like:
            // UU.FORK.HH?.01 | UU.CTU.EN?.01 | ....
            for (const auto &thisSplitSelector : splitSelectors)
            {
                std::vector<std::string> thisSelector;
                auto splitSelector = thisSplitSelector;
                boost::algorithm::trim(splitSelector);

                boost::split(thisSelector, splitSelector,
                             boost::is_any_of(" \t"));
                StreamSelector selector;
                if (splitSelector.empty())
                {
                    throw std::invalid_argument("Empty selector");
                }
                // Require a network
                boost::algorithm::trim(thisSelector.at(0));
                selector.setNetwork(thisSelector.at(0));
                // Add a station?
                if (splitSelector.size() > 1)
                {
                    boost::algorithm::trim(thisSelector.at(1));
                    selector.setStation(thisSelector.at(1));
                }
                // Add channel + location code + data type
                std::string channel{"*"};
                std::string locationCode{"??"};
                if (splitSelector.size() > 2)
                {
                    boost::algorithm::trim(thisSelector.at(2));
                    channel = thisSelector.at(2);
                }
                if (splitSelector.size() > 3)
                {
                    boost::algorithm::trim(thisSelector.at(3));
                    locationCode = thisSelector.at(3);
                }
                // Data type
                auto dataType = StreamSelector::Type::All;
                if (splitSelector.size() > 4)
                {
                    boost::algorithm::trim(thisSelector.at(4));
                    if (thisSelector.at(4) == "D")
                    {
                        dataType = StreamSelector::Type::Data;
                    }
                    else if (thisSelector.at(4) == "A")
                    {
                        dataType = StreamSelector::Type::All;
                    }
                    // TODO other data types
                }
                selector.setSelector(channel, locationCode, dataType);
                clientOptions.addStreamSelector(selector);
            } // Loop on selectors
        }
    }
    return clientOptions;
}

[[nodiscard]] std::string
loadStringFromFile(const std::filesystem::path &path)
{
    std::string result;
    if (!std::filesystem::exists(path)){return result;}
    std::ifstream file(path);
    if (!file.is_open())
    {
        throw std::runtime_error("Failed to open " + path.string());
    }
    std::stringstream sstr;
    sstr << file.rdbuf();
    file.close();
    result = sstr.str();
    return result;
}

[[nodiscard]] USEEDLinkToDataPacketImportProxy::GRPCOptions getGRPCOptions(
    const boost::property_tree::ptree &propertyTree,
    const std::string &section = "GRPC")
{
    USEEDLinkToDataPacketImportProxy::GRPCOptions options;

    auto host
        = propertyTree.get<std::string> (section + ".host",
                                         options.getHost());
    if (host.empty())
    {   
        throw std::runtime_error(section + ".host is empty");
    }   
    options.setHost(host);

    uint16_t port{50000};
    options.setPort(port);

    port = propertyTree.get<uint16_t> (section + ".port", options.getPort());
    options.setPort(port); 

    auto serverCertificate
        = propertyTree.get<std::string> (section + ".serverCertificate", "");
    if (!serverCertificate.empty())
    {   
        if (!std::filesystem::exists(serverCertificate))
        {
            throw std::invalid_argument("gRPC server certificate file "
                                      + serverCertificate
                                      + " does not exist");
        }
        options.setServerCertificate(loadStringFromFile(serverCertificate));
    }   


    auto accessToken
        = propertyTree.get_optional<std::string> (section + ".accessToken");
    if (accessToken)
    {   
        if (options.getServerCertificate() == std::nullopt)
        {
            throw std::invalid_argument(
                "Must set server certificate to use access token");
        }
        options.setAccessToken(*accessToken);
    }   
 
    auto clientKey
        = propertyTree.get<std::string> (section + ".clientKey", "");
    auto clientCertificate
        = propertyTree.get<std::string> (section + ".clientCertificate", "");
    if (!clientKey.empty() && !clientCertificate.empty())
    {
        if (!std::filesystem::exists(clientKey))
        {
            throw std::invalid_argument("gRPC client key file "
                                      + clientKey
                                      + " does not exist");
        }
        if (!std::filesystem::exists(clientCertificate))
        {
            throw std::invalid_argument("gRPC client certificate file "
                                      + clientCertificate
                                      + " does not exist");
        }
        options.setClientKey(loadStringFromFile(clientKey));
        options.setClientCertificate(loadStringFromFile(clientCertificate));
    }   
    return options;
}

std::string getOTelCollectorURL(boost::property_tree::ptree &propertyTree,
                                const std::string &section)
{
    std::string result; 
    std::string otelCollectorHost
        = propertyTree.get<std::string> (section + ".host", "");
    uint16_t otelCollectorPort
        = propertyTree.get<uint16_t> (section + ".port", 4218);
    if (!otelCollectorHost.empty())
    {
        result = otelCollectorHost + ":"
               + std::to_string(otelCollectorPort);
    }    
    return result; 
}

export
ProgramOptions parseIniFile(const std::filesystem::path &iniFile)
{
    ProgramOptions options;
    if (!std::filesystem::exists(iniFile)){return options;}
    // Parse the initialization file
    boost::property_tree::ptree propertyTree;
    boost::property_tree::ini_parser::read_ini(iniFile, propertyTree);
    // Application name
    options.applicationName
        = propertyTree.get<std::string> ("General.applicationName",
                                         options.applicationName);
    if (options.applicationName.empty())
    {   
        options.applicationName = APPLICATION_NAME;
    }   
    options.verbosity
        = propertyTree.get<int> ("General.verbosity", options.verbosity);


    // Metrics
    OTelHTTPMetricsOptions metricsOptions;
    metricsOptions.url
         = getOTelCollectorURL(propertyTree, "OTelHTTPMetricsOptions");
    metricsOptions.suffix
         = propertyTree.get<std::string> ("OTelHTTPMetricsOptions.suffix",
                                          "/v1/metrics");
    if (!metricsOptions.url.empty())
    {   
        if (!metricsOptions.suffix.empty())
        {
            if (!metricsOptions.url.ends_with("/") &&
                !metricsOptions.suffix.starts_with("/"))
            {
                metricsOptions.suffix = "/" + metricsOptions.suffix;
            }
         }
    }   
    if (!metricsOptions.url.empty())
    {   
        options.exportMetrics = true;
        options.otelHTTPMetricsOptions = metricsOptions;
    } 

    OTelHTTPLogOptions logOptions;
    logOptions.url
         = getOTelCollectorURL(propertyTree, "OTelHTTPLogOptions");
    logOptions.suffix
         = propertyTree.get<std::string>
           ("OTelHTTPLogOptions.suffix", "/v1/logs");
    if (!logOptions.url.empty())
    {
        if (!logOptions.suffix.empty())
        {
            if (!logOptions.url.ends_with("/") &&
                !logOptions.suffix.starts_with("/"))
            {
                logOptions.suffix = "/" + logOptions.suffix;
            }
        }
    }
    if (!logOptions.url.empty())
    {
        options.exportLogs = true;
        options.otelHTTPLogOptions = logOptions;
    }

    options.grpcOptions = getGRPCOptions(propertyTree, "GRPC");

    // SEEDLink
    if (propertyTree.get_optional<std::string> ("SEEDLink.host"))
    {
        options.seedLinkClientOptions
             = getSEEDLinkOptions(propertyTree, "SEEDLink");
    }   

    options.grpcOptions = getGRPCOptions(propertyTree, "GRPC"); 
    return options;
}



}
