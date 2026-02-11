#include <iostream>
#include <string>
#include <csignal>
#include <cstdlib>
#include <atomic>
#include <filesystem>
#include <tbb/concurrent_queue.h>
#include <absl/log/initialize.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <uDataPacketImportAPI/v1/packet.pb.h>
#include <uDataPacketImportAPI/v1/frontend.grpc.pb.h>
#include "seedLinkClient.hpp"
#include "seedLinkClientOptions.hpp"

import ProgramOptions;
import PacketWriter;
import Logger;
import Metrics;

namespace
{

std::atomic<bool> mInterrupted{false};

class Process
{
public:

    Process
    (
        const USEEDLinkToDataPacketImportProxy::ProgramOptions &options,
        std::shared_ptr<spdlog::logger> logger
    ) :
        mOptions(options),
        mLogger(logger)
    {
        mSEEDLinkClient 
            = std::make_unique<USEEDLinkToDataPacketImportProxy::SEEDLinkClient>
              (mAddPacketCallbackFunction,
               mOptions.seedLinkClientOptions,
               mLogger);
        mMaximumImportQueueSize = mOptions.maximumImportQueueSize;
        mMaximumExportQueueSize = mOptions.maximumExportQueueSize;
        mImportQueue.set_capacity(mMaximumImportQueueSize);
        mExportQueue.set_capacity(mMaximumExportQueueSize);

        if (mOptions.exportMetrics)
        {
/*
            // Good packets
            mPacketsReceivedCounter
                = meter->CreateInt64ObservableCounter(
                    "seismic_data.import.seedlink.client.packets.valid",
                    "Number of valid data packets received from SEEDLink client.",
                    "{packets}");
            mPacketsReceivedCounter->AddCallback(
                observePacketsReceived,
                nullptr);

           // Future packets
    mFuturePacketsReceivedCounter
        = meter->CreateInt64ObservableCounter(
             "seismic_data.import.seedlink.client.packets.future",
             "Number of future packets received from SEEDLink client.",
             "{packets}");
    mFuturePacketsReceivedCounter->AddCallback(
        observeFuturePacketsReceived, nullptr);

    // Expired packets
    mExpiredPacketsReceivedCounter
        = meter->CreateInt64ObservableCounter(
             "seismic_data.import.seedlink.client.packets.expired",
             "Number of expired packets received from SEEDLink client.",
             "{packets}");
    mExpiredPacketsReceivedCounter->AddCallback(
        observeExpiredPacketsReceived, nullptr);

    // Total packets
    mTotalPacketsReceivedCounter
        = meter->CreateInt64ObservableCounter(
             "seismic_data.import.seedlink.client.packets.all",
             "Total number of packets received from SEEDLink client.  This includes future and expired packets.",
             "{packets}");
*/
        }
    }

    ~Process() 
    {
        stop();
    }

    void start()
    {
        //stop();
        std::this_thread::sleep_for(std::chrono::milliseconds {10});
        mKeepRunning.store(true);
        mFutures.push_back(
            std::async(&Process::tabulateMetricsAndPropagatePackets, this));
        mFutures.push_back(std::async(&Process::sendPacketsToProxy, this));
        mFutures.push_back(mSEEDLinkClient->start());
        handleMainThread();
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
                SPDLOG_LOGGER_CRITICAL(mLogger,
                                       "Fatal error detected from thread: {}",
                                       std::string {e.what()});
                isOkay = false;
            }
        }
        return isOkay;
    }

    /// Export packets
    void sendPacketsToProxy()
    {
#ifndef NDEBUG
        assert(mLogger != nullptr);
#endif
        constexpr std::chrono::milliseconds timeOut{10};
        auto retrySchedule = mOptions.retrySchedule; 
        for (int kRetry = 0;
             kRetry < static_cast<int> (retrySchedule.size());
             ++kRetry)
        {
            std::unique_lock<std::mutex> lock(mShutdownMutex);
            mShutdownCondition.wait_for(lock,
                                        retrySchedule.at(kRetry),
                                        [this]
                                        {
                                            return mShutdownRequested;
                                        });
            lock.unlock();
            if (!mKeepRunning){break;}
            bool isRetry = kRetry > 0 ? true : false;
            auto [status, hadSuccessfulWrite]
                = USEEDLinkToDataPacketImportProxy::PacketWriter::
                     publishSynchronously(mOptions.grpcOptions,
                                          timeOut,
                                          isRetry,
                                          &mExportQueue, //ImportQueue,
                                          &mKeepRunning,
                                          mLogger);
            // Handle the return codes
            if (status.ok())
            {
                if (!mKeepRunning.load())
                {
                    SPDLOG_LOGGER_INFO(mLogger, "RPC successfully finished");
                    break;
                }
                else
                {
                    SPDLOG_LOGGER_WARN(mLogger,
                        "RPC successfully finished but I should keep writing");
                }
            }
            else
            {
                int errorCode(status.error_code());
                std::string errorMessage(status.error_message());
                if (errorCode == grpc::StatusCode::UNAVAILABLE)
                {
                    SPDLOG_LOGGER_WARN(mLogger,
                                       "Server unavailable (message: {})",
                                       errorMessage);
                }
                else if (errorCode == grpc::StatusCode::CANCELLED)
                {
                    if (mKeepRunning.load())
                    {
                        SPDLOG_LOGGER_WARN(mLogger,
                                           "Server-side cancel (message: {})",
                                           errorMessage);
                    }
                    else
                    {
                        break;
                    }
                }
                else
                {
                    SPDLOG_LOGGER_ERROR(mLogger,
                             "Publish RPC failed with error code {} (what: {})",
                             errorCode,  errorMessage);
                    break;
                }
            }
        } // Loop on retries
        if (mKeepRunning.load())
        {
            SPDLOG_LOGGER_CRITICAL(mLogger,
                                   "Publisher thread quitting!");
            throw std::runtime_error("Premature end of publisher thread");
        }
        SPDLOG_LOGGER_INFO(mLogger, "Publisher thread exiting");
    } 

    /// Thread that tabulates metrics and forwards packets to outbound queue
    void tabulateMetricsAndPropagatePackets()
    {
        constexpr std::chrono::milliseconds timeOut{15};
        auto &metrics
            = USEEDLinkToDataPacketImportProxy::Metrics::MetricsSingleton::getInstance();
        while (mKeepRunning.load())
        {
            // Check the packet
            UDataPacketImportAPI::V1::Packet packet;
            if (mImportQueue.try_pop(packet))
            {
                try
                {
                    metrics.tabulateMetrics(packet);
                }
                catch (const std::exception &e)
                {
                    SPDLOG_LOGGER_WARN(mLogger,
                                       "Failed to update metrics because {}",
                                       std::string {e.what()});
                }
                while (mExportQueue.size() >= mMaximumExportQueueSize)
                {
                    UDataPacketImportAPI::V1::Packet workSpace;
                    if (!mExportQueue.try_pop(workSpace))
                    {
                        SPDLOG_LOGGER_WARN(mLogger, 
                            "Failed to pop front of queue while adding packet");
                        break;
                    }
                }
                // Send the packet
                if (!mExportQueue.try_push(std::move(packet)))
                {
                    SPDLOG_LOGGER_WARN(mLogger,
                        "Failed to add packet to export queue");
                }
            }
            else
            {
                std::this_thread::sleep_for(timeOut);
            }
        } // Loop
        SPDLOG_LOGGER_DEBUG(mLogger,
                      "Thread exiting metrics and packet propagation function");
    }

    /// Used by data client to add packets
    void addPacketCallback(UDataPacketImportAPI::V1::Packet &&packet)
    {
        try
        {
            while (mImportQueue.size() >= mMaximumImportQueueSize)
            {
                UDataPacketImportAPI::V1::Packet workSpace;
                if (!mImportQueue.try_pop(workSpace))
                {
                    SPDLOG_LOGGER_WARN(mLogger, 
                        "Failed to pop front of queue while adding packet");
                    break;
                }
            }
            // Send the packet
            if (!mImportQueue.try_push(std::move(packet)))
            {
                SPDLOG_LOGGER_WARN(mLogger,
                    "Failed to add packet to import queue");
            }
        }
        catch (const std::exception &e)
        {
            SPDLOG_LOGGER_WARN(mLogger, "Failed to add packet because {}",
                               std::string {e.what()});
        }
    }

    // Print some summary statistics
    void printSummary()
    {   
        if (mOptions.printSummaryInterval.count() <= 0){return;}
        auto now 
            = std::chrono::duration_cast<std::chrono::microseconds>
              ((std::chrono::high_resolution_clock::now()).time_since_epoch());
        if (now > mLastPrintSummary + mOptions.printSummaryInterval)
        {
            mLastPrintSummary = now;
/*
            auto nPublishers = mProxy->getNumberOfPublishers();
            auto nSubscribers = mProxy->getNumberOfSubscribers(); 
            auto nReceived = mMetrics->getReceivedPacketsCount();
            auto nSent = mMetrics->getSentPacketsCount();
            auto nPacketsReceived = nReceived - mReportNumberOfPacketsReceived;
            auto nPacketsSent = nSent - mReportNumberOfPacketsSent;
            mReportNumberOfPacketsReceived = nReceived;
            mReportNumberOfPacketsSent = nSent;
            SPDLOG_LOGGER_INFO(mLogger,
                               "Current number of publishers {}.  Current number of subscribers {}.  Packets received since last report {}.  Packets sent since last report {}.",
                               nPublishers,
                               nSubscribers,
                               nPacketsReceived,
                               nPacketsSent);
*/
        }
    } 

    // Calling thread from Run gets stuck here then fails through to
    // destructor
    void handleMainThread()
    {
        SPDLOG_LOGGER_DEBUG(mLogger, "Main thread entering waiting loop");
        catchSignals();
        {
            while (!mStopRequested)
            {
                if (mInterrupted)
                {
                    SPDLOG_LOGGER_INFO(mLogger,
                                       "SIGINT/SIGTERM signal received!");
                    mStopRequested = true;
                    mShutdownRequested = true;
                    mShutdownCondition.notify_all();
                    break;
                }
                if (!checkFuturesOkay(std::chrono::milliseconds {5}))
                {
                    SPDLOG_LOGGER_CRITICAL(
                       mLogger,
                       "Futures exception caught; terminating app");
                    mStopRequested = true;
                    mShutdownRequested = true;
                    mShutdownCondition.notify_all();
                    break;
                }
                printSummary();
                std::unique_lock<std::mutex> lock(mStopMutex);
                mStopCondition.wait_for(lock,
                                        std::chrono::milliseconds {100},
                                        [this]
                                        {
                                              return mStopRequested;
                                        });
                lock.unlock();
            }
        }
        if (mStopRequested)
        {
            SPDLOG_LOGGER_DEBUG(mLogger, "Stop request received.  Exiting...");
            stop();
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
    USEEDLinkToDataPacketImportProxy::ProgramOptions mOptions;
    std::shared_ptr<spdlog::logger> mLogger{nullptr};
    tbb::concurrent_bounded_queue<UDataPacketImportAPI::V1::Packet> mImportQueue;
    tbb::concurrent_bounded_queue<UDataPacketImportAPI::V1::Packet> mExportQueue;
    std::unique_ptr<USEEDLinkToDataPacketImportProxy::SEEDLinkClient>
        mSEEDLinkClient{nullptr};
    std::function<void(UDataPacketImportAPI::V1::Packet &&)>
        mAddPacketCallbackFunction
    {
        std::bind(&::Process::addPacketCallback, this,
                  std::placeholders::_1)
    };
    //std::shared_ptr<grpc::Channel> mPublisherChannel{nullptr};
    //std::unique_ptr<UDataPacketImportAPI::V1::Frontend::Stub>
    //    mPublisherStub{nullptr};
    std::vector<std::future<void>> mFutures;
    std::chrono::microseconds mLastPrintSummary
    {   
        std::chrono::duration_cast<std::chrono::microseconds>
            ((std::chrono::high_resolution_clock::now()).time_since_epoch())
    };
    int mMaximumImportQueueSize{8192};
    int mMaximumExportQueueSize{16384};
    mutable std::mutex mStopMutex;
    mutable std::mutex mShutdownMutex;
    std::condition_variable mStopCondition;
    std::condition_variable mShutdownCondition;
    std::atomic<bool> mKeepRunning{true};
    bool mStopRequested{false};
    bool mShutdownRequested{false};
};

}

int main(int argc, char *argv[])
{
    // Get the ini file from the command line
    std::filesystem::path iniFile;
    try 
    {   
        auto [iniFileName, isHelp]
            = USEEDLinkToDataPacketImportProxy::parseCommandLineOptions(
                 argc, argv);
        if (isHelp){return EXIT_SUCCESS;}
        iniFile = iniFileName;
    }   
    catch (const std::exception &e) 
    {
        auto consoleLogger = spdlog::stdout_color_st("console");
        SPDLOG_LOGGER_CRITICAL(consoleLogger,
                               "Failed getting command line options because {}",
                               std::string {e.what()});
        return EXIT_FAILURE;
    }   

    USEEDLinkToDataPacketImportProxy::ProgramOptions programOptions;
    try 
    {
        programOptions
            = USEEDLinkToDataPacketImportProxy::parseIniFile(iniFile);
    }
    catch (const std::exception &e)
    {
        auto consoleLogger = spdlog::stdout_color_st("console");
        SPDLOG_LOGGER_CRITICAL(consoleLogger,
                               "Failed parsing ini file because {}",
                               std::string {e.what()});
        return EXIT_FAILURE;
    }

    if (getenv("OTEL_SERVICE_NAME") == nullptr)
    {
        constexpr int overwrite{1};
        setenv("OTEL_SERVICE_NAME",
               programOptions.applicationName.c_str(),
               overwrite);
     }

    auto logger
        = USEEDLinkToDataPacketImportProxy::Logger::initialize(programOptions);
    // Initialize the metrics singleton
    USEEDLinkToDataPacketImportProxy::Metrics::initializeMetricsSingleton();

    try 
    {   
        if (programOptions.exportMetrics)
        {   
            SPDLOG_LOGGER_INFO(logger, "Initializing metrics");
            USEEDLinkToDataPacketImportProxy::Metrics::initialize(programOptions);
        }
    }   
    catch (const std::exception &e) 
    {
        SPDLOG_LOGGER_CRITICAL(logger,
                               "Failed to initialize metrics because {}",
                               std::string {e.what()});
        if (programOptions.exportLogs)
        {   
            USEEDLinkToDataPacketImportProxy::Logger::cleanup();
        }
        return EXIT_FAILURE;
    }   

    //absl::InitializeLog();
    try
    {
        ::Process process(programOptions, logger);
        process.start();
        if (programOptions.exportMetrics)
        {
            USEEDLinkToDataPacketImportProxy::Metrics::cleanup();
        } 
        if (programOptions.exportLogs)
        {
            USEEDLinkToDataPacketImportProxy::Logger::cleanup();
        }
    }
    catch (const std::exception &e)
    {
        SPDLOG_LOGGER_CRITICAL(logger, "Main process failed with {}",
                               std::string {e.what()});
        if (programOptions.exportMetrics)
        {
            USEEDLinkToDataPacketImportProxy::Metrics::cleanup();
        }
        if (programOptions.exportLogs)
        {
            USEEDLinkToDataPacketImportProxy::Logger::cleanup();
        }
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

///--------------------------------------------------------------------------///
///                            Utility Functions                             ///
///--------------------------------------------------------------------------///
/*
namespace
{   
        
void setVerbosityForSPDLOG(const int verbosity,
                           std::shared_ptr<spdlog::logger> &logger)
{
    if (verbosity <= 1)
    {   
        logger->set_level(spdlog::level::critical);
    }   
    if (verbosity == 2){logger->set_level(spdlog::level::warn);}
    if (verbosity == 3){logger->set_level(spdlog::level::info);}
    if (verbosity >= 4){logger->set_level(spdlog::level::debug);}
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
                    spdlog::warn("Could not create parent path "
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

    // Otel
    if (propertyTree.get_optional<std::string> ("OTelHTTPCollector.host"))
    {
        OpenTelemetryHTTPOptions otelHTTPOptions;
        auto otelHTTPHost
            = propertyTree.get<std::string>
              ("OTelHTTPCollector.host", "localhost");
        if (otelHTTPHost.empty())
        {
            throw std::invalid_argument("OTel HTTP host is empty");
        }
        auto otelHTTPPort
            = propertyTree.get<uint16_t>
              ("OTelHTTPCollector.port", 4318);
        otelHTTPOptions.endpoint = otelHTTPHost + ":"
                                 + std::to_string(otelHTTPPort);
        options.otelHTTPOptions = otelHTTPOptions;
    }

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

    // SEEDLink
    if (propertyTree.get_optional<std::string> ("SEEDLink.host"))
    {
        options.seedLinkClientOptions
             = ::getSEEDLinkOptions(propertyTree, "SEEDLink");
    }   

    options.grpcOptions = ::getGRPCOptions(propertyTree, "GRPC"); 
    return options;
}

}
*/
