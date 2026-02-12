module;

#include <string>
#include <array>
#include <bit>
#include <algorithm>
#ifndef NDEBUG
#include <cassert>
#endif
#include <spdlog/spdlog.h>
#include <google/protobuf/util/time_util.h>
#include <boost/algorithm/string/trim.hpp>
#include <libmseed.h>
#include "uDataPacketImportAPI/v1/stream_identifier.pb.h"
#include "uDataPacketImportAPI/v1/packet.pb.h"

export module PacketConverter;

namespace USEEDLinkToDataPacketImportProxy::PacketConverter
{

template<typename T>
std::string pack(const T *data, const int nSamples, const bool swapBytes)
{
    constexpr auto dataTypeSize = sizeof(T);
    std::string result;
    if (nSamples < 1){return result;}
    result.resize(dataTypeSize*nSamples);
    // Pack it up
    union CharacterValueUnion
    {
        unsigned char cArray[dataTypeSize];
        T value;
    };
    CharacterValueUnion cvUnion;
    if (!swapBytes)
    {
        for (int i = 0; i < nSamples; ++i)
        {
            cvUnion.value = data[i];
            std::copy(cvUnion.cArray, cvUnion.cArray + dataTypeSize,
                      result.data() + dataTypeSize*i);
        }
    }
    else
    {
        for (int i = 0; i < nSamples; ++i)
        {
            cvUnion.value = data[i];
            std::reverse_copy(cvUnion.cArray, cvUnion.cArray + dataTypeSize,
                              result.data() + dataTypeSize*i);
        }
    }
    return result;
}

export
template<typename T>
std::vector<T> unpack(const std::string &data, const int nSamples,
                      const bool swapBytes)
{
    constexpr auto dataTypeSize = sizeof(T);
    std::vector<T> result;
    if (nSamples < 1){return result;}
    if (static_cast<size_t> (nSamples)*dataTypeSize != data.size())
    {
        throw std::invalid_argument("Unexpected data size");
    }
    result.resize(nSamples);
    // Pack it up
    union CharacterValueUnion
    {
        unsigned char cArray[dataTypeSize];
        T value;
    };
    CharacterValueUnion cvUnion;
    if (!swapBytes)
    {
        for (int i = 0; i < nSamples; ++i)
        {
            cvUnion.value = data[i];
            auto i1 = i*dataTypeSize;
            auto i2 = i1 + dataTypeSize;
            std::copy(data.data() + i1, data.data() + i2,
                      cvUnion.cArray);
            result[i] = cvUnion.value;
        }
    }
    else
    {
        for (int i = 0; i < nSamples; ++i)
        {
            cvUnion.value = data[i];
            auto i1 = i*dataTypeSize;
            auto i2 = i1 + dataTypeSize;
            std::reverse_copy(data.data() + i1, data.data() + i2,
                              cvUnion.cArray);
            result[i] = cvUnion.value;
        }
    }
    return result;
}

export
template<typename T>
std::vector<T> unpack(const std::string &data, const int nSamples)
{
    const bool swapBytes
    {   
        std::endian::native == std::endian::little ? false : true
    };  
    return unpack<T>(data, nSamples, swapBytes);
}

[[nodiscard]] UDataPacketImportAPI::V1::Packet
    convert(const MS3Record &miniSEEDRecord, const bool swapBytes)
{
    UDataPacketImportAPI::V1::Packet result;
    // Some simple stuff
    if (miniSEEDRecord.samprate <= 0)
    {
        throw std::invalid_argument("Sampling rate must be positive");
    }
    result.set_sampling_rate(miniSEEDRecord.samprate);
    // Number of samples
    auto nSamples = static_cast<int> (miniSEEDRecord.numsamples);
    if (nSamples <= 0)
    {
        throw std::invalid_argument("Empty data packet");
    }
    result.set_number_of_samples(nSamples);
    // Start time
    std::chrono::nanoseconds startTime
    {
        static_cast<int64_t> (miniSEEDRecord.starttime)
    };
    *result.mutable_start_time() 
        = google::protobuf::util::TimeUtil::NanosecondsToTimestamp(
             startTime.count());

    // SNCL
    std::array<char, LM_SIDLEN> networkWork;
    std::array<char, LM_SIDLEN> stationWork;
    std::array<char, LM_SIDLEN> channelWork;
    std::array<char, LM_SIDLEN> locationWork;
    std::fill(networkWork.begin(),  networkWork.end(),  '\0');
    std::fill(stationWork.begin(),  stationWork.end(),  '\0');
    std::fill(channelWork.begin(),  channelWork.end(),  '\0');
    std::fill(locationWork.begin(), locationWork.end(), '\0');
    auto returnCode = ms_sid2nslc_n(miniSEEDRecord.sid,
                                    networkWork.data(),  networkWork.size(),
                                    stationWork.data(),  stationWork.size(),
                                    locationWork.data(), locationWork.size(),
                                    channelWork.data(),  channelWork.size());
    if (returnCode == MS_NOERROR)
    {
        std::string network(networkWork.data());
        std::string station(stationWork.data());
        std::string channel(channelWork.data());
        std::string locationCode(locationWork.data());

        boost::algorithm::trim(network);
        if (network.empty()){throw std::runtime_error("Network is empty");}
        std::transform(network.begin(), network.end(), network.begin(),
                       ::toupper);

        boost::algorithm::trim(station);
        if (station.empty()){throw std::runtime_error("Station is empty");}
        std::transform(station.begin(), station.end(), station.begin(),
                       ::toupper);

        boost::algorithm::trim(channel);
        if (channel.empty()){throw std::runtime_error("Channel is empty");}
        std::transform(channel.begin(), channel.end(), channel.begin(),
                       ::toupper);
        if (channel.empty()){throw std::runtime_error("Channel is empty");}

        if (!locationCode.empty())
        {
            boost::algorithm::trim(locationCode);
            std::transform(locationCode.begin(), locationCode.end(),
                           locationCode.begin(), ::toupper);
        }
        if (locationCode.empty()){locationCode = "--";}
        // Pack it
        UDataPacketImportAPI::V1::StreamIdentifier identifier; 
        identifier.set_network(std::move(network));
        identifier.set_station(std::move(station));
        identifier.set_channel(std::move(channel));
        identifier.set_location_code(std::move(locationCode));

        *result.mutable_stream_identifier() = std::move(identifier);
    }
    else
    {
        throw std::runtime_error("Couldn't unpack station identifier");
    }

    // Now the hard stuff - the data
#ifndef NDEBUG
    assert(nSamples > 0);
#endif
    result.set_data_type(
        UDataPacketImportAPI::V1::DataType::DATA_TYPE_UNKNOWN);
    if (miniSEEDRecord.sampletype == 'i')
    {
        result.set_data_type(
            UDataPacketImportAPI::V1::DataType::DATA_TYPE_INTEGER_32);
        const auto data
            = reinterpret_cast<const int *> (miniSEEDRecord.datasamples);
        auto packedData = pack(data, nSamples, swapBytes);
        result.set_data(std::move(packedData));
    }
    else if (miniSEEDRecord.sampletype == 'f')
    {
        result.set_data_type(
            UDataPacketImportAPI::V1::DataType::DATA_TYPE_FLOAT);
        const auto data
            = reinterpret_cast<const float *> (miniSEEDRecord.datasamples);
        auto packedData = pack(data, nSamples, swapBytes);
        result.set_data(std::move(packedData));
    }
    else if (miniSEEDRecord.sampletype == 'd')
    {
        result.set_data_type(
            UDataPacketImportAPI::V1::DataType::DATA_TYPE_DOUBLE);
        const auto data
           = reinterpret_cast<const double *> (miniSEEDRecord.datasamples);
        auto packedData = pack(data, nSamples, swapBytes); 
        result.set_data(std::move(packedData));
    }
    else if (miniSEEDRecord.sampletype == 't')
    {
        result.set_data_type(
            UDataPacketImportAPI::V1::DataType::DATA_TYPE_TEXT);
        const auto data
           = reinterpret_cast<const char *> (miniSEEDRecord.datasamples);
        std::string packedData;
        packedData.resize(nSamples);
        std::copy(data, data + nSamples, packedData.data());
        result.set_data(std::move(packedData));
    }
    else
    {
        throw std::runtime_error("Unhandled sample type");
    }

    return result;
}

/// @brief Unpacks a miniSEED record.
export
[[nodiscard]]
std::pair<std::vector<UDataPacketImportAPI::V1::Packet>, int>
    miniSEEDToPackets(char *msRecord,
                      const int bufferSize,
                      const bool swapBytes)
{
    if (msRecord == nullptr)
    {
        throw std::invalid_argument("Input mseed paylaod is null");
    }
    std::vector<UDataPacketImportAPI::V1::Packet> dataPackets;
    auto bufferLength = static_cast<uint64_t> (bufferSize);
    uint64_t offset{0};
    int failedPackets{0};
    // Iterate through the consumed buffer
    while (bufferLength - offset > MINRECLEN)
    {   
        // Convert every packet in the buffer
        constexpr int8_t verbose{0};
        constexpr uint32_t flags{MSF_UNPACKDATA};
        UDataPacketImportAPI::V1::Packet dataPacket;
        MS3Record *miniSEEDRecord{nullptr};
        auto returnCode = msr3_parse(msRecord + offset,
                                     static_cast<uint64_t> (bufferSize) - offset,
                                     &miniSEEDRecord, flags,
                                     verbose);
        if (returnCode == MS_NOERROR && miniSEEDRecord)
        {
            try
            {
                auto dataPacket = convert(*miniSEEDRecord, swapBytes);
                dataPackets.push_back(std::move(dataPacket));
            }
            catch (const std::exception &e)
            {
                failedPackets = failedPackets + 1;
            }
            offset = offset + miniSEEDRecord->reclen;
            msr3_free(&miniSEEDRecord);
        }
        else
        {
            if (returnCode != MS_NOERROR)
            {
                if (miniSEEDRecord){msr3_free(&miniSEEDRecord);}
                throw std::runtime_error("libmseed error detected");
            }
            msr3_free(&miniSEEDRecord);
            throw std::runtime_error(
                 "Insufficient data.  Number of additional bytes estimated is "
                + std::to_string(returnCode));
        }
    }
    return std::pair{ std::move(dataPackets), failedPackets };
}

export
[[nodiscard]]
std::pair<std::vector<UDataPacketImportAPI::V1::Packet>, int>
    miniSEEDToPackets(char *msRecord, const int bufferSize)
{
    const bool swapBytes
    {
        std::endian::native == std::endian::little ? false : true
    };
    return miniSEEDToPackets(msRecord, bufferSize, swapBytes);
}

}
