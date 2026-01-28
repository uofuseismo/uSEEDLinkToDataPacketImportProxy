#include <libslink.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "seedLinkClient.hpp"
#include "seedLinkClientOptions.hpp"
#include "streamSelector.hpp"
#include "packetConverter.hpp"
#include "version.hpp"

using namespace USEEDLinkToDataPacketImportProxy;

class SEEDLinkClient::SEEDLinkClientImpl
{
public:
    explicit SEEDLinkClientImpl(
        const std::function<void (UDataPacketImportAPI::V1::Packet &&)> &callback,
        const SEEDLinkClientOptions &options,
        std::shared_ptr<spdlog::logger> &logger) :
        mAddPacketCallback(callback),
        mOptions(options),
        mLogger(logger)
    {
        if (mLogger == nullptr)
        {
            mLogger = spdlog::stdout_color_st("seedLinkConsole");
        }
        initialize(options);
    }        
    /// Destructor
    ~SEEDLinkClientImpl()
    {
        stop();
        disconnect();
    }
    /// Terminate the SEED link client connection
    void disconnect()
    {   
        if (mSEEDLinkConnection != nullptr)
        {
            if (mSEEDLinkConnection->link != -1)
            {
                SPDLOG_LOGGER_DEBUG(mLogger, "Disconnecting SEEDLink...");
                sl_disconnect(mSEEDLinkConnection);
            }
            if (mUseStateFile)
            {
                SPDLOG_LOGGER_DEBUG(mLogger,
                                    "Saving state prior to disconnect...");
                sl_savestate(mSEEDLinkConnection, mStateFile.c_str());
            }
            SPDLOG_LOGGER_DEBUG(mLogger, "Freeing SEEDLink structure...");
            sl_freeslcd(mSEEDLinkConnection);
            mSEEDLinkConnection = nullptr;
        }
    }
    /// Sends a terminate command to the SEEDLink connection
    void terminate()
    {
        if (mSEEDLinkConnection != nullptr)
        {
            SPDLOG_LOGGER_DEBUG(mLogger, "Issuing terminate command to poller");
            sl_terminate(mSEEDLinkConnection);
        }
    }
    /// Starts the service
    [[nodiscard]] std::future<void> start()
    {
        stop(); // Ensure module is stopped
        if (!mInitialized)
        {
            throw std::runtime_error("SEEDLink client not initialized");
        }
        setRunning(true);
        SPDLOG_LOGGER_DEBUG(mLogger, "Starting the SEEDLink polling thread...");
        mSEEDLinkConnection->terminate = 0;
        auto result = std::async(&SEEDLinkClientImpl::packetToCallback, this);
        return result;
    }
    /// Toggles this as running or not running
    void setRunning(const bool running)
    {
        // Terminate the session
        if (!running && mKeepRunning)
        {
            SPDLOG_LOGGER_DEBUG(mLogger, "Issuing terminate command");
            terminate();
        }
        // Tell the scraping thread to quit if it hasn't already given up
        // because it received a terminate request
        mKeepRunning = running;
    }
    /// Stops the service
    void stop()
    {
        setRunning(false); // Issues terminate command
    }
    /// Initialize
    void initialize(const SEEDLinkClientOptions &options)
    {   
        mHaveOptions = false;
        disconnect();
        mInitialized = false;
        // Create a new instance
        mSEEDLinkConnection
            = sl_initslcd(mClientName.c_str(), Version::getVersion().c_str());
        if (!mSEEDLinkConnection)
        {                   
            throw std::runtime_error("Failed to create client handle");
        }
        // Set the connection string            
        auto host = options.getHost();
        auto port = options.getPort();
        auto seedLinkAddress = host +  ":" + std::to_string(port);
        SPDLOG_LOGGER_INFO(mLogger, "Connecting to SEEDLink server {} ....",
                           seedLinkAddress);
        if (sl_set_serveraddress(
               mSEEDLinkConnection, seedLinkAddress.c_str()) != 0)
        {                                  
            throw std::invalid_argument("Failed to set server address "
                                      + seedLinkAddress);
        }   
        // Set the record size and state file
        //mSEEDRecordSize = options.getSEEDRecordSize();
        if (options.hasStateFile())
        {   
            mStateFile = options.getStateFile();
            if (options.deleteStateFileOnStart())
            {
                if (std::filesystem::exists(mStateFile))
                {
                    if (!std::filesystem::remove(mStateFile))
                    {
                        SPDLOG_LOGGER_WARN(mLogger,
                                           "Failed to remove state file {}",
                                           mStateFile);
                    }
                }
            }
            mStateFileUpdateInterval = options.getStateFileUpdateInterval();
            mUseStateFile = true;
            mDeleteStateFileOnStop = options.deleteStateFileOnStop();
        }
        // If there are selectors then try to use them
        constexpr uint64_t sequenceNumber{SL_UNSETSEQUENCE}; // Start at next data
        const char *timeStamp{nullptr};
        auto streamSelectors = options.getStreamSelectors();
        for (const auto &selector : streamSelectors)
        {
            try
            {
                auto network = selector.getNetwork();
                auto station = selector.getStation();
                auto stationID = network + "_" + station;
                auto streamSelector = selector.getSelector();
                SPDLOG_LOGGER_INFO(mLogger, "Adding: {} {}",
                                   stationID, streamSelector);
                auto returnCode = sl_add_stream(mSEEDLinkConnection,
                                                stationID.c_str(),
                                                streamSelector.c_str(),
                                                sequenceNumber,
                                                timeStamp);
                if (returnCode != 0)
                {
                    throw std::runtime_error("Failed to add selector: "
                                           + network + " "
                                           + station + " "
                                           + streamSelector);
                }
            }
            catch (const std::exception &e)
            {
                SPDLOG_LOGGER_WARN(mLogger,
                                   "Could not add selector because {}",
                                   std::string {e.what()});
            }
        }
        // Configure uni-station mode if no streams were specified
        if (mSEEDLinkConnection->streams == nullptr)
        {
            const char *selectors{nullptr};
            auto returnCode = sl_set_allstation_params(mSEEDLinkConnection,
                                                       selectors,
                                                       sequenceNumber,
                                                       timeStamp);
            if (returnCode != 0)
            {
                SPDLOG_LOGGER_CRITICAL(mLogger,
                    "Could not set SEEDLink uni-station mode");
                throw std::runtime_error(
                    "Failed to create a SEEDLink uni-station client");
            }
        }
        // Preferentially do not block so our thread can check for other
        // commands.
        constexpr bool nonBlock{true};
        if (sl_set_blockingmode(mSEEDLinkConnection, nonBlock) != 0)
        {
            SPDLOG_LOGGER_WARN(mLogger, "Failed to set non-blocking mode");
        }
#ifndef NDEBUG
        assert(mSEEDLinkConnection->noblock == 1);
#endif
        constexpr bool closeConnection{false};
        if (sl_set_dialupmode(mSEEDLinkConnection, closeConnection) != 0)
        {
            SPDLOG_LOGGER_WARN(mLogger, "Failed to set keep-alive connection");
        }
#ifndef NDEBUG
        assert(mSEEDLinkConnection->dialup == 0);
#endif
        // Time out and reconnect delay
        auto networkTimeOut
            = static_cast<int> (options.getNetworkTimeOut().count());
        if (sl_set_idletimeout(mSEEDLinkConnection, networkTimeOut) != 0)
        {
            SPDLOG_LOGGER_WARN(mLogger, "Failed to set idle connection time");
        }
        auto reconnectDelay
            = static_cast<int> (options.getNetworkReconnectDelay().count());
        if (sl_set_reconnectdelay(mSEEDLinkConnection, reconnectDelay) != 0)
        {
            SPDLOG_LOGGER_WARN(mLogger, "Failed to set reconnect delay");
        }
        // Check this worked
#ifndef NDEBUG
        std::string slSite(512, '\0');
        std::string slServerID(512, '\0');
        auto returnCode = sl_ping(mSEEDLinkConnection,
                                  slServerID.data(),
                                  slSite.data());
        if (returnCode != 0)
        {
            if (returnCode ==-1)
            {
                SPDLOG_LOGGER_WARN(mLogger, "Invalid ping response");
            }
            else
            {
                SPDLOG_LOGGER_CRITICAL(mLogger, "Could not connect to server");
                throw std::runtime_error("Failed to connect");
            }
        }
        else
        {
            SPDLOG_LOGGER_INFO(mLogger,
                    "SEEDLink ping successfully returned server {} (site {} )",
                    slServerID, slSite);
        }
#endif
        // All-good
        mOptions = options;
        mInitialized = true;
        mHaveOptions = true;
    }
    /// Scrapes the packets and puts them to the callback
    void packetToCallback()
    {
        constexpr std::chrono::milliseconds timeToSleep{50};
        mConnected = true;
        // Recover state
        if (mUseStateFile)
        {
            if (!sl_recoverstate(mSEEDLinkConnection, mStateFile.c_str()))
            {
                 throw std::runtime_error("Failed to recover state");
            }
        }
        // Now start scraping
        //sl_printslcd(mSEEDLinkConnection); // Useful for debugging
        const SLpacketinfo *seedLinkPacketInfo{nullptr};
        std::array<char, SL_RECV_BUFFER_SIZE> seedLinkBuffer;
        const auto seedLinkBufferSize
            = static_cast<uint32_t> (seedLinkBuffer.size());
        int updateStateFile{1};
        SPDLOG_LOGGER_DEBUG(mLogger,
                            "Thread entering SEEDLink polling loop...");
        while (mKeepRunning)
        {
            // Attempt to collect data but then immediately return.
            auto returnValue = sl_collect(mSEEDLinkConnection,
                                          &seedLinkPacketInfo,
                                          seedLinkBuffer.data(),
                                          seedLinkBufferSize);
            // Deal with packet
            if (returnValue == SLPACKET)
            {
                // I really only care about data packets
                if (seedLinkPacketInfo->payloadformat == SLPAYLOAD_MSEED2 ||
                    seedLinkPacketInfo->payloadformat == SLPAYLOAD_MSEED3)
                {
                    auto payloadLength = seedLinkPacketInfo->payloadlength;
                    try
                    {
                        auto [packets, failedPackets]
                            = ::miniSEEDToPackets(seedLinkBuffer.data(),
                                                  payloadLength);
                        if (failedPackets > 0)
                        {
                            SPDLOG_LOGGER_WARN(mLogger,
                                               "{} packets failed to be unpacked",
                                               failedPackets);
                        }
                        if (packets.empty())
                        {
                            SPDLOG_LOGGER_WARN(mLogger, "No packets created");
                        }
                        else if (packets.size() > 1)
                        {
                            SPDLOG_LOGGER_WARN(mLogger, "Created {} packets",
                                               packets.size());
                        }
                        for (auto &packet : packets)
                        {
                            try
                            {
                                mAddPacketCallback( std::move(packet) );
                            }
                            catch (const std::exception &e)
                            {
                                SPDLOG_LOGGER_WARN(mLogger,
                                       "Failed to propagate packet because {}",
                                       std::string {e.what()});
                            }
                        }
                    }
                    catch (const std::exception &e)
                    {
                        SPDLOG_LOGGER_WARN(mLogger,
                                   "Skipping packet.  Unpacking failed with {}",
                                   std::string(e.what()));
                    }
                    if (mUseStateFile)
                    {
                        if (updateStateFile > mStateFileUpdateInterval)
                        {
                            sl_savestate(mSEEDLinkConnection,
                                         mStateFile.c_str());
                            updateStateFile = 0;
                        }
                        updateStateFile = updateStateFile + 1;
                    }
                }
            }
            else if (returnValue == SLTOOLARGE)
            {
                SPDLOG_LOGGER_WARN(mLogger,
                            "Payload length {} exceeds {}; skipping",
                            seedLinkPacketInfo->payloadlength,
                            seedLinkBufferSize);
                continue;
            }
            else if (returnValue == SLNOPACKET)
            {
                SPDLOG_LOGGER_DEBUG(mLogger, "No data from sl_collect");
                std::this_thread::sleep_for(timeToSleep);
                continue;
            }
            else if (returnValue == SLTERMINATE)
            {
                SPDLOG_LOGGER_INFO(mLogger,
                                   "SEEDLink terminate request received");
                mConnected = false;
                break;
            }
            else
            {
                SPDLOG_LOGGER_WARN(mLogger,
                                   "Unhandled SEEDLink return value {}",
                                   returnValue);
                continue;
            }
        } // Loop on keep running
        // Purge state file
        if (mUseStateFile && mDeleteStateFileOnStop)
        {
            SPDLOG_LOGGER_INFO(mLogger,
                               "Purging state file {}", mStateFile);
            if (std::filesystem::exists(mStateFile))
            {
                if (!std::filesystem::remove(mStateFile))
                {
                    throw std::runtime_error("Failed to purge state file " 
                                           + mStateFile);
                }
            }
        }
        if (mKeepRunning)
        {
            SPDLOG_LOGGER_CRITICAL(mLogger, "Premature end of SEEDLink import");
            throw std::runtime_error("Premature end of SEEDLink import");
        }
        SPDLOG_LOGGER_INFO(mLogger, "Thread leaving SEEDLink polling loop");
        mConnected = false;
    }
//private:
    std::string mClientName{"uSEEDLinkToDataPacketProxy"};
    std::function<void(UDataPacketImportAPI::V1::Packet &&)>
        mAddPacketCallback;
    std::shared_ptr<spdlog::logger> mLogger{nullptr};
    SLCD *mSEEDLinkConnection{nullptr};
    SEEDLinkClientOptions mOptions;
    std::string mStateFile;
    std::atomic<bool> mKeepRunning{true};
    std::atomic<bool> mConnected{false};
    int mStateFileUpdateInterval{100};
    //int mSEEDRecordSize{512};
    bool mHaveOptions{false};
    bool mUseStateFile{false};
    bool mDeleteStateFileOnStop{false};
    bool mInitialized{false};
};


/// Constructor
SEEDLinkClient::SEEDLinkClient(
    const std::function<void (UDataPacketImportAPI::V1::Packet &&)> &callback,
    const SEEDLinkClientOptions &options,
    std::shared_ptr<spdlog::logger> &logger) :
    pImpl(std::make_unique<SEEDLinkClientImpl> (callback, options, logger))
{
    //pImpl->initialize(options);
}

/// Initialized?
bool SEEDLinkClient::isInitialized() const noexcept
{
    return pImpl->mInitialized;
}

/// Start the client
std::future<void> SEEDLinkClient::start()
{
    if (!isInitialized())
    {   
        throw std::runtime_error("SEEDLink client not initialized");
    }   
    return pImpl->start();
}

/// Stop the client
void SEEDLinkClient::stop()
{
    pImpl->stop();
}

/// Destructor
SEEDLinkClient::~SEEDLinkClient() = default;
