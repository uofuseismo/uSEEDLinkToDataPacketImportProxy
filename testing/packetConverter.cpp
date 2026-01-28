#include <cmath>
#include <string>
#include <chrono>
#include <bit>
#include <libmseed.h>
#include "packetConverter.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

namespace
{

static void msRecordHandler(char *record, int recordLength, void *outputBuffer)
{
    auto buffer = reinterpret_cast<std::string *> (outputBuffer);
    buffer->append(record, recordLength);
}

template<typename T>
std::vector<T> unpack(const std::string &data)
{
    const bool swapBytes
    {   
        std::endian::native == std::endian::little ? false : true
    };  
    constexpr auto dataTypeSize = sizeof(T);
    std::vector<T> result;
    REQUIRE(data.size()%dataTypeSize == 0);
    auto nSamples = static_cast<int> (data.size()/dataTypeSize);
    if (nSamples < 1){return result;}
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

template<typename T>
std::string toMiniSEED(
    const std::vector<T> &dataIn,
    const std::string &network = "UU",
    const std::string &station = "CWU",
    const std::string &channel = "HHZ",
    const std::string &locationCode = "01",
    const std::chrono::nanoseconds startTime = std::chrono::nanoseconds (1769631059123321000),
    const double samplingRate = 100.0,
    const bool useMiniSEED3 = true)
{
    auto data = dataIn;
    constexpr int maxRecordLength{512};
    std::string outputBuffer;
    if (data.empty()){return outputBuffer;}

    MS3Record *msRecord{NULL};
    msRecord = msr3_init(msRecord);
    if (msRecord)
    {
        msRecord->starttime = startTime.count();
        msRecord->samprate = samplingRate;
        msRecord->numsamples = data.size();
        auto sidLength
            = ms_nslc2sid(msRecord->sid, LM_SIDLEN, 0,
                          const_cast<char *> (network.c_str()),
                          const_cast<char *>(station.c_str()),
                          const_cast<char *>(locationCode.c_str()),
                          const_cast<char *>(channel.c_str()));
        if (sidLength < 1)
        {
            msr3_free(&msRecord);
            throw std::runtime_error("Failed to pack SID");
        }

        if constexpr (std::is_same<T, int>::value == true)
        {
            msRecord->encoding = DE_INT32;
            msRecord->sampletype = 'i';
            msRecord->datasamples = reinterpret_cast<void *> (data.data());
        }
        else if constexpr (std::is_same<T, float>::value == true)
        {
            msRecord->encoding = DE_FLOAT32;
            msRecord->sampletype = 'f';
            msRecord->datasamples = reinterpret_cast<void *> (data.data());
        }
        else if constexpr (std::is_same<T, double>::value == true)
        {
            msRecord->encoding = DE_FLOAT64;
            msRecord->sampletype = 'd';
            msRecord->datasamples = reinterpret_cast<void *> (data.data());
        }
        else if constexpr (std::is_same<T, char>::value == true)
        {
            msRecord->encoding = DE_TEXT;
            msRecord->sampletype = 't';
            msRecord->datasamples = reinterpret_cast<void *> (data.data());
        }
        else
        {
            REQUIRE(false);
        }
        uint32_t flags{0};
        constexpr int8_t verbose{0};
        flags |= MSF_FLUSHDATA;
        if (!useMiniSEED3){flags |= MSF_PACKVER2;}
        auto nRecordsPacked = msr3_pack(msRecord, 
                                        &msRecordHandler,
                                        &outputBuffer,
                                        nullptr, 
                                        flags,
                                        verbose);
        msRecord->datasamples = nullptr;
        if (nRecordsPacked < 1)
        {
            msr3_free(&msRecord);
            throw std::runtime_error("Failed to pack data");
        }
    }
    else
    {
        throw std::runtime_error("Failed to initialize MS3Record");
    }
    msr3_free(&msRecord);
    return outputBuffer; 

}

}

TEMPLATE_TEST_CASE("USEEDLinkToDataPacketImportProxy::PacketConverter",
                   "[mseed3]", int, float, double)
{
    constexpr bool useMiniSEED3{true};
    const std::string network{"UU"};
    const std::string station{"CWU"};
    const std::string channel{"HHZ"};
    const std::string locationCode{"01"};
    std::vector<TestType> data{1, 2, 3, 4, 5, 6, 7, 8};
    const std::chrono::nanoseconds startTime{1769631059123321000};
    constexpr double samplingRate{99.9995}; 
    auto mseedBuffer = ::toMiniSEED(data,
                                    network, station, channel, locationCode,
                                    startTime,
                                    samplingRate,
                                    useMiniSEED3);
    auto [packets, nFailures]
        = ::miniSEEDToPackets(mseedBuffer.data(), mseedBuffer.size());
    REQUIRE(nFailures == 0);
    REQUIRE(packets.size() == 1);
    auto packet = packets.at(0);
    auto streamIdentifier = packet.stream_identifier();
    REQUIRE(streamIdentifier.network() == network);
    REQUIRE(streamIdentifier.station() == station);
    REQUIRE(streamIdentifier.channel() == channel);
    REQUIRE(streamIdentifier.location_code() == locationCode);
    auto seconds = std::chrono::seconds {packet.start_time().seconds()};
    auto nanoSeconds = std::chrono::nanoseconds {packet.start_time().nanos()};
    std::chrono::nanoseconds startTimeBack = seconds + nanoSeconds;
    REQUIRE(std::abs(samplingRate - packet.sampling_rate()) < 1.e-15);
    REQUIRE(startTime == startTimeBack);
    if (packet.data_type() == UDataPacketImportAPI::V1::DataType::DATA_TYPE_INTEGER_32)
    {
        auto samples = ::unpack<int> (packet.data());
        REQUIRE(samples.size() == data.size());
        for (int i = 0; i < static_cast<int> (data.size()); ++i)
        {
            REQUIRE(samples.at(i) == static_cast<int> (data[i]));
        }
    } 
    else if (packet.data_type() == UDataPacketImportAPI::V1::DataType::DATA_TYPE_FLOAT)
    {
        auto samples = ::unpack<float> (packet.data());
        REQUIRE(samples.size() == data.size());
        for (int i = 0; i < static_cast<int> (data.size()); ++i)
        {
            REQUIRE(std::abs(samples.at(i) - (data[i])) < 1.e-7);
        }
    }
    else if (packet.data_type() == UDataPacketImportAPI::V1::DataType::DATA_TYPE_DOUBLE)
    {   
        auto samples = ::unpack<double> (packet.data());
        REQUIRE(samples.size() == data.size());
        for (int i = 0; i < static_cast<int> (data.size()); ++i)
        {
            REQUIRE(std::abs(samples.at(i) - (data[i])) < 1.e-15);
        }
    }   
}

