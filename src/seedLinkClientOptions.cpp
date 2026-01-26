#include <filesystem>
#include <string>
#include "seedLinkClientOptions.hpp"
#include "streamSelector.hpp"

using namespace USEEDLinkToDataPacketImportProxy;

class SEEDLinkClientOptions::SEEDLinkClientOptionsImpl
{
public:
    std::string mHost{"rtserve.iris.washington.edu"};
    std::filesystem::path mStateFile;
    std::vector<StreamSelector> mSelectors;
    std::chrono::seconds mNetworkTimeOut{300};
    std::chrono::seconds mNetworkDelay{30};
    bool mDeleteStateFileOnStop{false};
    bool mDeleteStateFileOnStart{false};
    uint16_t mStateFileInterval{100};
    uint16_t mPort{18000};
};

/// Constructor 
SEEDLinkClientOptions::SEEDLinkClientOptions() :
    pImpl(std::make_unique<SEEDLinkClientOptionsImpl> ())
{   
}   

/// Copy constructor
SEEDLinkClientOptions::SEEDLinkClientOptions(
    const SEEDLinkClientOptions &options)
{
    *this = options;
}

/// Move constructor
SEEDLinkClientOptions::SEEDLinkClientOptions(
    SEEDLinkClientOptions &&options) noexcept
{
    *this = std::move(options);
}

/// Copy assignment
SEEDLinkClientOptions& SEEDLinkClientOptions::operator=(
    const SEEDLinkClientOptions &options)
{   
    if (&options == this){return *this;}
    pImpl = std::make_unique<SEEDLinkClientOptionsImpl> (*options.pImpl);
    return *this;
}

/// Move assignment
SEEDLinkClientOptions& SEEDLinkClientOptions::operator=(
    SEEDLinkClientOptions &&options) noexcept
{
    if (&options == this){return *this;}
    pImpl = std::move(options.pImpl);
    return *this;
}

/// Destructor
SEEDLinkClientOptions::~SEEDLinkClientOptions() = default;

/// Reset class
void SEEDLinkClientOptions::clear() noexcept
{
    pImpl = std::make_unique<SEEDLinkClientOptionsImpl> ();
}

/// Address
void SEEDLinkClientOptions::setHost(const std::string &hostIn)
{
    auto host = hostIn;
    host.erase(std::remove(host.begin(), host.end(), ' '), host.end());
    std::transform(host.begin(), host.end(), host.begin(), ::tolower);
    if (host.empty()){throw std::invalid_argument("Host is empty");}
    pImpl->mHost = host;
}

std::string SEEDLinkClientOptions::getHost() const noexcept
{
    return pImpl->mHost;
}

/// Port
void SEEDLinkClientOptions::setPort(const uint16_t port) noexcept
{
    pImpl->mPort = port;
}

uint16_t SEEDLinkClientOptions::getPort() const noexcept
{
    return pImpl->mPort;
}

/// Sets a SEEDLink state file
void SEEDLinkClientOptions::setStateFile(const std::filesystem::path &stateFile)
{
    if (stateFile.empty())
    {   
        pImpl->mStateFile.clear();
        return;
    }   
    auto parentPath = stateFile.parent_path();
    if (!parentPath.empty())
    {   
        if (!std::filesystem::exists(parentPath))
        {
            if (!std::filesystem::create_directories(parentPath))
            {
                throw std::runtime_error("Failed to create state file path");
            }
        }
    }   
    pImpl->mStateFile = stateFile;
}

std::filesystem::path SEEDLinkClientOptions::getStateFile() const
{
    if (!hasStateFile()){throw std::runtime_error("State file not set");}
    return pImpl->mStateFile;
}

bool SEEDLinkClientOptions::hasStateFile() const noexcept
{
    return !pImpl->mStateFile.empty();
}

/// State file interval
void SEEDLinkClientOptions::setStateFileUpdateInterval(
    const uint16_t interval) noexcept
{
    pImpl->mStateFileInterval = interval;
}

uint16_t SEEDLinkClientOptions::getStateFileUpdateInterval() const noexcept
{
    return pImpl->mStateFileInterval;
}

void SEEDLinkClientOptions::enableDeleteStateFileOnStop() noexcept
{
    pImpl->mDeleteStateFileOnStop = true;
}

void SEEDLinkClientOptions::disableDeleteStateFileOnStop() noexcept
{
    pImpl->mDeleteStateFileOnStop = false;
}

bool SEEDLinkClientOptions::deleteStateFileOnStop() const noexcept
{
    return pImpl->mDeleteStateFileOnStop;
}

void SEEDLinkClientOptions::enableDeleteStateFileOnStart() noexcept
{
    pImpl->mDeleteStateFileOnStart = true;
}

void SEEDLinkClientOptions::disableDeleteStateFileOnStart() noexcept
{
    pImpl->mDeleteStateFileOnStart = false;
}

bool SEEDLinkClientOptions::deleteStateFileOnStart() const noexcept
{
    return pImpl->mDeleteStateFileOnStart;
}

/// Network timeout
void SEEDLinkClientOptions::setNetworkTimeOut(const std::chrono::seconds &timeOut)
{
    if (timeOut < std::chrono::seconds {0})
    {
        throw std::invalid_argument("Network time-out cannot be negative");
    }
    pImpl->mNetworkTimeOut = timeOut;
}

std::chrono::seconds SEEDLinkClientOptions::getNetworkTimeOut() const noexcept
{
    return pImpl->mNetworkTimeOut;
}

void SEEDLinkClientOptions::setNetworkReconnectDelay(
    const std::chrono::seconds &delay)
{
    if (delay < std::chrono::seconds {0})
    {
        throw std::invalid_argument("Network delay cannot be negative");
    }
    pImpl->mNetworkDelay = delay;
}

std::chrono::seconds 
    SEEDLinkClientOptions::getNetworkReconnectDelay() const noexcept
{
    return pImpl->mNetworkDelay;
}

/// Stream selectors
void SEEDLinkClientOptions::addStreamSelector(
    const StreamSelector &selector)
{
    if (!selector.hasNetwork())
    {
        throw std::invalid_argument("Network not set");
    }
    for (const auto &mySelector : pImpl->mSelectors)
    {
        if (mySelector.getNetwork() == selector.getNetwork() &&
            mySelector.getStation() == selector.getStation() &&
            mySelector.getSelector() == selector.getSelector())
        {
            throw std::invalid_argument("Duplicate selector");
        }
    }
    pImpl->mSelectors.push_back(selector);
}

std::vector<StreamSelector> 
    SEEDLinkClientOptions::getStreamSelectors() const noexcept
{
    return pImpl->mSelectors;
}

