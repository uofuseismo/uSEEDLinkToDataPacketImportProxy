#include <iostream>
#ifndef NDEBUG
#include <cassert>
#endif
#include <string>
#include <algorithm>
#include "streamSelector.hpp"

using namespace USEEDLinkToDataPacketImportProxy;

class StreamSelector::StreamSelectorImpl
{
public:
    std::string mNetwork; //{"??"};
    std::string mStation{"*"};
    std::string mChannel;
    std::string mLocationCode;
    StreamSelector::Type mType{StreamSelector::Type::All};
};

/// Constructor
StreamSelector::StreamSelector() :
    pImpl(std::make_unique<StreamSelectorImpl> ())
{
}

/// Copy constructor
StreamSelector::StreamSelector(const StreamSelector &selector)
{
    *this = selector;
}

/// Move constructor
StreamSelector::StreamSelector(StreamSelector &&selector) noexcept
{
    *this = std::move(selector);
}

/// Copy assignment
StreamSelector& StreamSelector::operator=(const StreamSelector &selector)
{
    if (&selector == this){return *this;}
    pImpl = std::make_unique<StreamSelectorImpl> (*selector.pImpl);
    return *this;
}

/// Move assignment
StreamSelector& StreamSelector::operator=(StreamSelector &&selector) noexcept
{
    if (&selector == this){return *this;}
    pImpl = std::move(selector.pImpl);
    return *this;
}

/// Reset class
void StreamSelector::clear() noexcept
{
    pImpl = std::make_unique<StreamSelectorImpl> ();
}

/// Destructor
StreamSelector::~StreamSelector() = default;

/// Network
void StreamSelector::setNetwork(const std::string &network)
{
    if (network.size() != 2)
    {
        throw std::invalid_argument("Network size must 2");
    }
    pImpl->mNetwork = network;
    std::transform(pImpl->mNetwork.begin(),
                   pImpl->mNetwork.end(),
                   pImpl->mNetwork.begin(), ::toupper);
}

std::string StreamSelector::getNetwork() const
{
    if (!hasNetwork()){throw std::runtime_error("Network not set");}
    return pImpl->mNetwork;
}

bool StreamSelector::hasNetwork() const noexcept
{
    return !pImpl->mNetwork.empty();
}

/// Station
void StreamSelector::setStation(const std::string &station)
{
    if (station.empty())
    {
        throw std::invalid_argument("Station is empty");
    }
    pImpl->mStation = station;
    std::transform(pImpl->mStation.begin(),
                   pImpl->mStation.end(),
                   pImpl->mStation.begin(), ::toupper);
}

std::string StreamSelector::getStation() const noexcept
{
    return pImpl->mStation;
}

/// Set selector
void StreamSelector::setSelector(
   const std::string &channel,
   const StreamSelector::Type type)
{
    pImpl->mChannel = channel;
    pImpl->mLocationCode.clear();
    pImpl->mType = type;
}

/// Set the selector
void StreamSelector::setSelector(
    const std::string &channel, const std::string &locationCode,
    const StreamSelector::Type type)
{
    pImpl->mChannel = channel;
    pImpl->mLocationCode = locationCode;
    pImpl->mType = type;
}

/// Build the selector
std::string StreamSelector::getSelector() const noexcept
{
    std::string selector;
    std::string locationChannel;
    if (pImpl->mLocationCode.empty() && pImpl->mChannel.empty())
    {
        locationChannel = "";
    }
    else
    {
        if (pImpl->mLocationCode.empty())
        {
            locationChannel = "??";
        }
        else
        {
            locationChannel = pImpl->mLocationCode;
        }
        if (pImpl->mChannel.empty())
        {
            locationChannel = locationChannel + "*";
        }
        else
        {
            locationChannel = locationChannel + pImpl->mChannel;
        }
    }
    if (pImpl->mType == StreamSelector::Type::All)
    {
        if (locationChannel.empty())
        {
            return selector; // Empty all the way through
        }
        else
        {
            selector = locationChannel + pImpl->mChannel + ".*";
        }
    }
    else if (pImpl->mType == StreamSelector::Type::Data)
    {
        if (locationChannel.empty())
        {
            selector = "*.D";
        }
        else
        {
            selector = locationChannel + ".D";
        }
    }
    else if (pImpl->mType == StreamSelector::Type::Timing)
    {
        if (locationChannel.empty())
        {
            selector = "*.T";
        }
        else
        {
            selector = locationChannel + ".T";
        }
    }
    else if (pImpl->mType == StreamSelector::Type::Log)
    {
        if (locationChannel.empty())
        {
            selector = "*.L";
        }
        else
        {
            selector = locationChannel + ".L";
        }
    }
    else if (pImpl->mType == StreamSelector::Type::Event)
    {   
        if (locationChannel.empty())
        {
            selector = "*.E";
        }
        else
        {
            selector = locationChannel + ".E";
        }
    }
    else if (pImpl->mType == StreamSelector::Type::Blockette)
    {   
        if (locationChannel.empty())
        {
            selector = "*.B";
        }
        else
        {
            selector = locationChannel + ".B";
        }
    }
    else if (pImpl->mType == StreamSelector::Type::Calibration)
    {   
        if (locationChannel.empty())
        {
            selector = "*.C";
        }
        else
        {
            selector = locationChannel + ".C";
        }
    }
    else
    {
#ifndef NDEBUG
        assert(false);
#else 
        std::cerr << "Unhandled type" << std::endl;
#endif
        if (locationChannel.empty())
        {
            return selector; // Return everything
        }
        else
        {
            selector = locationChannel + ".*";
        }
    }
    return selector; 
} 
