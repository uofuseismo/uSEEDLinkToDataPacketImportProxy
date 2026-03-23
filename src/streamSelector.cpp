#include <boost/algorithm/string.hpp>
#ifndef NDEBUG
#include <cassert>
#endif
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
        throw std::invalid_argument("Received network "
                                  + network + " which has length "
                                  + std::to_string(network.size())
                                  + " but network size must 2.");
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
    std::transform(pImpl->mChannel.begin(), pImpl->mChannel.end(),
                   pImpl->mChannel.begin(), ::toupper);
    pImpl->mLocationCode.clear();
    pImpl->mType = type;
}

/// Set the selector
void StreamSelector::setSelector(
    const std::string &channel, const std::string &locationCode,
    const StreamSelector::Type type)
{
    pImpl->mChannel = channel;
    std::transform(pImpl->mChannel.begin(), pImpl->mChannel.end(),
                   pImpl->mChannel.begin(), ::toupper);
    pImpl->mLocationCode = locationCode;
    pImpl->mType = type;
}

/// Build the selector
std::string StreamSelector::getSelector() const
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
        throw std::runtime_error("Unhandled data type");
#endif
    }
    return selector; 
} 

StreamSelector StreamSelector::fromString(
    const std::string &streamSelector)
{
    std::vector<std::string> thisSelector; 
    auto splitSelector = streamSelector;
    boost::algorithm::trim(splitSelector);

    // Need to preprocess selector so there's no double spaces
    for (int k = 1; k < static_cast<int> (splitSelector.size()); )
    {
        if (splitSelector[k - 1] == splitSelector[k] &&
            splitSelector[k] == ' ') 
        {
            splitSelector.erase(k, 1);
        }
        else
        {
            ++k;
        }
    }

    boost::split(thisSelector, splitSelector,
                 boost::is_any_of(" \t"));
    StreamSelector selector;
    if (splitSelector.empty())
    {
        throw std::invalid_argument("Empty selector");
    }

    // Require a network
    auto network = thisSelector.at(0);
    boost::algorithm::trim(network);
    selector.setNetwork(network);
    // Add a station?
    if (splitSelector.size() > 1) 
    {
        auto station = thisSelector.at(1);
        boost::algorithm::trim(station);
        selector.setStation(station);
    }
    // Add channel + location code + data type
    std::string channel{"*"};
    std::string locationCode{"??"};
    if (splitSelector.size() > 2) 
    {
        channel = thisSelector[2];
        boost::algorithm::trim(channel);
    }
    if (splitSelector.size() > 3) 
    {
        locationCode = thisSelector[3];
        boost::algorithm::trim(locationCode);
    }
    // Data type
    auto dataType = StreamSelector::Type::All;
    if (thisSelector.size() > 4) 
    {
        boost::algorithm::trim(thisSelector.at(4));
        if (thisSelector.at(4) == "D" || thisSelector.at(4) == "d")
        {
            dataType = StreamSelector::Type::Data;
        }
        else if (thisSelector.at(4) == "A" || thisSelector.at(4) == "a") 
        {
            dataType  = StreamSelector::Type::All;
        }
        // TODO other data types
    }
    selector.setSelector(channel, locationCode, dataType);
    return selector;
}
