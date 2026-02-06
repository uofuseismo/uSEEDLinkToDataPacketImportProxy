#include <iostream>
#include <string>
#include <csignal>
#include <atomic>
#include <tbb/concurrent_queue.h>
#include <absl/log/initialize.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <boost/algorithm/string.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <opentelemetry/exporters/otlp/otlp_http_exporter_options.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_log_record_exporter_factory.h>
#include <opentelemetry/exporters/ostream/log_record_exporter_factory.h>
#include <opentelemetry/logs/provider.h>
#include <opentelemetry/sdk/logs/logger_provider_factory.h>
#include <opentelemetry/sdk/logs/simple_log_record_processor_factory.h>
#include "seedLinkClient.hpp"
#include "seedLinkClientOptions.hpp"
#include "streamSelector.hpp"
#include "grpcOptions.hpp"
#include "packetWriter.hpp"
#include "otelSpdlogSink.hpp"
#include "uDataPacketImportAPI/v1/packet.pb.h"

#define APPLICATION_NAME "uSEEDLinkToDataPacketImportProxy"

namespace
{

std::shared_ptr<opentelemetry::sdk::logs::LoggerProvider> loggerProvider{nullptr};

std::atomic<bool> mInterrupted{false};

struct OpenTelemetryHTTPOptions
{
    std::string endpoint;
    std::filesystem::path certificatePath;
    std::string suffix{"/v1/logs"};
};

struct ProgramOptions
{
    std::string applicationName{APPLICATION_NAME};
    std::string prometheusURL{"localhost:9200"};
    ::OpenTelemetryHTTPOptions otelHTTPOptions;
    USEEDLinkToDataPacketImportProxy::SEEDLinkClientOptions
        seedLinkClientOptions;
    USEEDLinkToDataPacketImportProxy::GRPCOptions grpcOptions;
    int maximumImportQueueSize{8192};
    int verbosity{3};
};

void setVerbosityForSPDLOG(const int, std::shared_ptr<spdlog::logger> &);
[[nodiscard]] std::pair<std::string, bool> parseCommandLineOptions(int, char *[]);
[[nodiscard]] ProgramOptions parseIniFile(const std::filesystem::path &);

void cleanupLogger()
{
    if (loggerProvider)
    {
        loggerProvider->ForceFlush();
        loggerProvider.reset();
        std::shared_ptr<opentelemetry::logs::LoggerProvider> none;
        opentelemetry::logs::Provider::SetLoggerProvider(none);
    }
}

class Process
{
public:

    Process(const ::ProgramOptions &options,
            std::shared_ptr<spdlog::logger> logger) :
        mOptions(options),
        mLogger(logger)
    {
        mSEEDLinkClient 
            = std::make_unique<USEEDLinkToDataPacketImportProxy::SEEDLinkClient>
              (mAddPacketCallbackFunction,
               mOptions.seedLinkClientOptions,
               mLogger);

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
        std::this_thread::sleep_for(std::chrono::milliseconds {10});
        mKeepRunning.store(true);
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
        std::vector<std::chrono::seconds> retrySchedule{
           std::chrono::seconds {0},
           std::chrono::seconds {1}, 
           std::chrono::seconds {5},
           std::chrono::seconds {15}};
        for (int kRetry = 0;
             kRetry < static_cast<int> (retrySchedule.size()); ++kRetry)
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
            auto channel = ::createChannel(mOptions.grpcOptions, mLogger, isRetry);
            auto stub = UDataPacketImportAPI::V1::Frontend::NewStub(channel);
            grpc::ClientContext context;
            context.set_wait_for_ready(false);
            UDataPacketImportAPI::V1::PublishResponse publishResponse;
            std::unique_ptr
            <
                grpc::ClientWriter<UDataPacketImportAPI::V1::Packet>
            > writer(stub->Publish(&context,  &publishResponse)); 
#ifndef NDEBUG
            assert(writer != nullptr);
#endif
            while (mKeepRunning.load())
            {
                UDataPacketImportAPI::V1::Packet packet;
                if (mImportQueue.try_pop(packet))
                {
                    if (!writer->Write(packet))
                    {
                        SPDLOG_LOGGER_WARN(mLogger, "Broken stream");
                        break;
                    }
                    kRetry = 0;
                }
                else
                {
                    std::this_thread::sleep_for(timeOut);
                }
            }
            if (!mKeepRunning.load())
            {
                writer->WritesDone();
            }
            auto status = writer->Finish();
            if (status.ok())
            {
                if (!mKeepRunning.load())
                {
                    SPDLOG_LOGGER_INFO(mLogger, "RPC successfully finished");
                    break;
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
                                           "Server-side cancele (message: {})",
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
                }
            }
        } // Loop on retries
        SPDLOG_LOGGER_INFO(mLogger, "Publisher thread exiting");
    } 

    /// Used by data client to add packets
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
                        SPDLOG_LOGGER_WARN(mLogger, 
                            "Failed to pop front of queue while adding packet");
                        break;
                    }
                }
            }
            // Check the packet
            if (!mImportQueue.try_push(packet))
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
    ::ProgramOptions mOptions;
    std::shared_ptr<spdlog::logger> mLogger{nullptr};
    tbb::concurrent_bounded_queue<UDataPacketImportAPI::V1::Packet> mImportQueue;
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
    size_t mMaximumImportQueueSize{8192};
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
        auto [iniFileName, isHelp] = ::parseCommandLineOptions(argc, argv);
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

    ::ProgramOptions programOptions;
    try 
    {
        programOptions = ::parseIniFile(iniFile);
    }
    catch (const std::exception &e)
    {
        auto consoleLogger = spdlog::stdout_color_st("console");
        SPDLOG_LOGGER_CRITICAL(consoleLogger,
                               "Failed parsing ini file because {}",
                               std::string {e.what()});
        return EXIT_FAILURE;
    }

    // Create logger
    std::shared_ptr<spdlog::logger> logger{nullptr};
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt> (); 
    if (!programOptions.otelHTTPOptions.endpoint.empty())
    { 
/*
        constexpr int overwrite{1};
        setenv("OTEL_SERVICE_NAME",
               programOptions.applicationName.c_str(),
               overwrite);
        namespace otel = opentelemetry;
        otel::exporter::otlp::OtlpHttpLogRecordExporterOptions httpOptions;
        httpOptions.url = programOptions.otelHTTPOptions.endpoint
                        + programOptions.otelHTTPOptions.suffix;
        //httpOptions.use_ssl_credentials = false;
        //httpOptions.ssl_credentials_cacert_path = programOptions.otelGRPCOptions.certificatePath;
        using providerPtr
            = otel::nostd::shared_ptr<opentelemetry::logs::LoggerProvider>;
        //auto exporter
        //    = otel::exporter::logs::OStreamLogRecordExporterFactory::Create();
        auto exporter
              = otel::exporter::otlp::OtlpHttpLogRecordExporterFactory::Create(httpOptions);
        auto processor
            = otel::sdk::logs::SimpleLogRecordProcessorFactory::Create(
                 std::move(exporter));
        loggerProvider
            = otel::sdk::logs::LoggerProviderFactory::Create(
                std::move(processor));
        std::shared_ptr<opentelemetry::logs::LoggerProvider> apiProvider = loggerProvider;
        otel::logs::Provider::SetLoggerProvider(apiProvider);

        auto otelLogger
            = std::make_shared<spdlog::sinks::opentelemetry_sink_mt> ();
        logger
            = std::make_shared<spdlog::logger>
              (spdlog::logger ("OTelLogger", {otelLogger, consoleSink}));
*/
    }
    else
    {
        logger
            = std::make_shared<spdlog::logger>
              (spdlog::logger ("", {consoleSink}));
    }
    ::setVerbosityForSPDLOG(programOptions.verbosity, logger);
    //::setVerbosityForSPDLOG(programOptions.verbosity, otelLogger);

absl::InitializeLog();
    try
    {
        ::Process process(programOptions, logger);
        process.start();
//        cleanupLogger();
    }
    catch (const std::exception &e)
    {
        SPDLOG_LOGGER_CRITICAL(logger, "Main process failed wtih {}",
                               std::string {e.what()});
//        cleanupLogger();
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

///--------------------------------------------------------------------------///
///                            Utility Functions                             ///
///--------------------------------------------------------------------------///
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
