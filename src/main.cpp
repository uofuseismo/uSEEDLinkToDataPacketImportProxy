#include <iostream>
#include <string>
#include <csignal>
#include <atomic>
#include <tbb/concurrent_queue.h>
#include <spdlog/spdlog.h>
#include <boost/program_options.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include "seedLinkClient.hpp"
#include "seedLinkClientOptions.hpp"
#include "grpcOptions.hpp"
#include "packetWriter.hpp"
#include "otelSpdlogSink.hpp"
#include "uDataPacketImportAPI/v1/packet.pb.h"

#define APPLICATION_NAME "uSEEDLinkToDataPacketImportProxy"

namespace
{
std::atomic<bool> mInterrupted{false};

struct ProgramOptions
{
    std::string applicationName{APPLICATION_NAME};
    std::string prometheusURL{"localhost:9200"};
    USEEDLinkToDataPacketImportProxy::SEEDLinkClientOptions seedLinkOptions;
    USEEDLinkToDataPacketImportProxy::GRPCOptions grpcOptions;
    int maximumImportQueueSize{8192};
    int verbosity{3};
};

void setVerbosityForSPDLOG(int );
[[nodiscard]] std::pair<std::string, bool> parseCommandLineOptions(int, char *[]);
[[nodiscard]] ProgramOptions parseIniFile(const std::filesystem::path &);

class Process
{
public:

    Process(const ::ProgramOptions &options) :
        mOptions(options)
    {
        mSEEDLinkClient 
            = std::make_unique<USEEDLinkToDataPacketImportProxy::SEEDLinkClient>
              (mAddPacketCallbackFunction, mOptions.seedLinkOptions);

        mMaximumImportQueueSize = mOptions.maximumImportQueueSize;
        mImportQueue.set_capacity(mMaximumImportQueueSize);
    }

    ~Process() 
    {
        stop();
    }

    void start()
    {
        //stop();
        mKeepRunning.store(true);
        mFutures.push_back(mSEEDLinkClient->start());
    }

    void stop()
    {
        mKeepRunning.store(false);
        mSEEDLinkClient->stop();
        for (auto &future : mFutures)
        {
            if (future.valid()){future.get();}
        }
    }

    /// Check futures
    [[nodiscard]]
    bool checkFuturesOkay(const std::chrono::milliseconds &timeOut)
    {
        bool isOkay{true};
        for (auto &future : mFutures)
        {
            try
            {
                auto status = future.wait_for(timeOut);
                if (status == std::future_status::ready)
                {
                    future.get();
                }
            }
            catch (const std::exception &e)
            {
                spdlog::critical("Fatal error in SEEDLink import: "
                               + std::string {e.what()});
                isOkay = false;
            }
        }
        return isOkay;
    }


    void addPacketCallback(UDataPacketImportAPI::V1::Packet &&packet)
    {
        try
        {
            auto approximateSize = mImportQueue.size();
            if (approximateSize > mMaximumImportQueueSize)
            {
                while (mImportQueue.size() >= mMaximumImportQueueSize)
                {
                    UDataPacketImportAPI::V1::Packet workSpace;
                    if (!mImportQueue.try_pop(workSpace))
                    {
                        spdlog::warn("Failed to pop front of queue");
                        break;
                    }
                }
            }
            // Check the packet
            if (!mImportQueue.try_push(packet))
            {
                spdlog::warn("Failed to add packet");
            }
        }
        catch (const std::exception &e)
        {
            spdlog::warn("Failed to add packet because "
                       + std::string {e.what()});
        }
    }

    /// Handles sigterm and sigint
    static void signalHandler(const int )
    {
        mInterrupted = true;
    }

    static void catchSignals()
    {
        struct sigaction action;
        action.sa_handler = signalHandler;
        action.sa_flags = 0;
        sigemptyset(&action.sa_mask);
        sigaction(SIGINT,  &action, NULL);
        sigaction(SIGTERM, &action, NULL);
    }

//private:
    ::ProgramOptions mOptions;
    tbb::concurrent_bounded_queue<UDataPacketImportAPI::V1::Packet> mImportQueue;
    std::unique_ptr<USEEDLinkToDataPacketImportProxy::SEEDLinkClient>
        mSEEDLinkClient{nullptr};
    std::function<void(UDataPacketImportAPI::V1::Packet &&)>
        mAddPacketCallbackFunction
    {
        std::bind(&::Process::addPacketCallback, this,
                  std::placeholders::_1)
    };
    std::vector<std::future<void>> mFutures;
    size_t mMaximumImportQueueSize{8192};
    std::atomic<bool> mKeepRunning{true};
};

}

int main(int argc, char *argv[])
{
    // Get the ini file from the command line
    std::filesystem::path iniFile;
    try 
    {   
        auto [iniFileName, isHelp] = ::parseCommandLineOptions(argc, argv);
        if (isHelp){return EXIT_SUCCESS;}
        iniFile = iniFileName;
    }   
    catch (const std::exception &e) 
    {   
        spdlog::critical(e.what());
        return EXIT_FAILURE;
    }   

    ::ProgramOptions programOptions;
    try 
    {
        programOptions = ::parseIniFile(iniFile);
    }
    catch (const std::exception &e)
    {
        spdlog::error(e.what());
        return EXIT_FAILURE;
    }
    ::setVerbosityForSPDLOG(programOptions.verbosity);

    return EXIT_SUCCESS;
}

///--------------------------------------------------------------------------///
///                            Utility Functions                             ///
///--------------------------------------------------------------------------///
namespace
{   
        
void setVerbosityForSPDLOG(const int verbosity)
{
    if (verbosity <= 1)
    {   
        spdlog::set_level(spdlog::level::critical);
    }
    if (verbosity == 2){spdlog::set_level(spdlog::level::warn);}
    if (verbosity == 3){spdlog::set_level(spdlog::level::info);}
    if (verbosity >= 4){spdlog::set_level(spdlog::level::debug);}
}

/// Read the program options from the command line
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
        options.setServerCertificate(::loadStringFromFile(serverCertificate));
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
        options.setClientKey(::loadStringFromFile(clientKey));
        options.setClientCertificate(::loadStringFromFile(clientCertificate));
    }   
    return options;
}

::ProgramOptions parseIniFile(const std::filesystem::path &iniFile)
{
    ::ProgramOptions options;
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

    // Prometheus
    uint16_t prometheusPort
        = propertyTree.get<uint16_t> ("Prometheus.port", 9200);
    std::string prometheusHost
        = propertyTree.get<std::string> ("Prometheus.host", "localhost");
    if (!prometheusHost.empty())
    {
        options.prometheusURL = prometheusHost + ":"
                              + std::to_string(prometheusPort);
    }


    options.grpcOptions = ::getGRPCOptions(propertyTree, "GRPC"); 
    return options;
}


}
