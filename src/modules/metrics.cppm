module;

#include <mutex>
#include <atomic>
#include <string>
#include <cmath>
#include <bit>
#include <vector>
#include <map>
#include <algorithm>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/metrics/meter.h>
#include <opentelemetry/metrics/meter_provider.h>
#include <opentelemetry/metrics/provider.h>
#include <opentelemetry/exporters/otlp/otlp_http.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_metric_exporter_options.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h>
#include <opentelemetry/sdk/metrics/meter_context.h>
#include <opentelemetry/sdk/metrics/meter_context_factory.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/meter_provider_factory.h>
#include <opentelemetry/sdk/metrics/provider.h>
#include <opentelemetry/sdk/metrics/view/instrument_selector_factory.h>
#include <opentelemetry/sdk/metrics/view/meter_selector_factory.h>
#include <google/protobuf/util/time_util.h>
#include <uDataPacketImportAPI/v1/packet.pb.h> 

export module Metrics;
import ProgramOptions;
import PacketConverter;

namespace USEEDLinkToDataPacketImportProxy::Metrics
{

#define UPDATE_INTERVAL_SECONDS 120

export void initialize(
    const USEEDLinkToDataPacketImportProxy::ProgramOptions &programOptions)
{
    if (!programOptions.exportMetrics){return;}
    namespace otel = opentelemetry;
    otel::exporter::otlp::OtlpHttpMetricExporterOptions exporterOptions;
    exporterOptions.url = programOptions.otelHTTPMetricsOptions.url
                        + programOptions.otelHTTPMetricsOptions.suffix;
    //exporterOptions.console_debug = debug != "" && debug != "0" && debug != "no";
    exporterOptions.content_type
        = otel::exporter::otlp::HttpRequestContentType::kBinary;

    auto exporter
        = otel::exporter::otlp::OtlpHttpMetricExporterFactory::Create(
             exporterOptions);

    // Initialize and set the global MeterProvider
    otel::sdk::metrics::PeriodicExportingMetricReaderOptions readerOptions;
    readerOptions.export_interval_millis
        = programOptions.otelHTTPMetricsOptions.exportInterval;
    readerOptions.export_timeout_millis
        = programOptions.otelHTTPMetricsOptions.exportTimeOut;

    auto reader
        = otel::sdk::metrics::PeriodicExportingMetricReaderFactory::Create(
             std::move(exporter),
             readerOptions);

    auto context = otel::sdk::metrics::MeterContextFactory::Create();
    context->AddMetricReader(std::move(reader));

    auto metricsProvider
        = otel::sdk::metrics::MeterProviderFactory::Create(
             std::move(context));
    std::shared_ptr<otel::metrics::MeterProvider>
        provider(std::move(metricsProvider));

    otel::sdk::metrics::Provider::SetMeterProvider(provider);
}

export void cleanup()
{
    std::shared_ptr<opentelemetry::metrics::MeterProvider> none;
    opentelemetry::sdk::metrics::Provider::SetMeterProvider(none);
}

/*
template<typename T>
void computeSumAndSumSquared(const std::vector<T> &data,
                             double *packetSum,
                             double *packetSum2)
{
#ifndef NDEBUG
    assert(packetSum != nullptr);
    assert(packetSum2 != nullptr);
#endif
    double sum{0};
    double sum2{0};
    for (int i = 0; i < static_cast<int> (data.size()); ++i)
    {
        auto d = data[i];
        sum = sum + d;
        sum2 = sum2 + d*d;
    }
    *packetSum = sum;
    *packetSum2 = sum2;
}
*/

struct WindowedMetrics
{
    WindowedMetrics() = default;
    explicit WindowedMetrics(const std::chrono::seconds &inputUpdateInterval) :
        updateInterval(inputUpdateInterval)
    {
#ifndef NDEBUG
        assert(updateInterval.count() > 0);
#endif
        windowedAverageLatency.store( 
            static_cast<double> (updateInterval.count()));
    }

    void update(const UDataPacketImportAPI::V1::Packet &packet,
                const std::chrono::microseconds &packetLatency)
    {
        auto nSamples = packet.number_of_samples();
        if (nSamples < 1){return;}
        // Get data
        namespace UDP = UDataPacketImportAPI::V1;
        namespace UPC = USEEDLinkToDataPacketImportProxy::PacketConverter;
        double packetSum{0};
        double packetSum2{0};
        USEEDLinkToDataPacketImportProxy::PacketConverter::
            computeSumAndSumSquared(packet, swapBytes, &packetSum, &packetSum2);
        // Update sums
        {
        std::scoped_lock{mMutex};
        sum = sum + packetSum;
        sumSquared = sumSquared + packetSum2;
        samplesCount = samplesCount + nSamples;
        packetsCount = packetsCount + 1;
        sumLatency = sumLatency + packetLatency;
        }
    }

    [[nodiscard]] bool updateAndReset(const std::chrono::microseconds &now)
    {
        bool wasUpdated{false};
        if (now >= lastUpdate + updateInterval)
        {
            lastUpdate = now;
            double averageLatency
               = static_cast<double> (updateInterval.count());
            double averageCounts{0};
            double stdCounts{0};
            {
            std::scoped_lock lock{mMutex};
            if (samplesCount > 0)
            {
                double besselCorrection{1};
                if (samplesCount > 1)
                {
                    besselCorrection = samplesCount/(samplesCount - 1.0);
                }
                averageLatency = (sumLatency.count()*1.e-6)/packetsCount;
                averageCounts = sum/samplesCount;
                // Var[x] = E[x^2] - E[x]^2
                double varianceOfCounts = sumSquared/samplesCount
                                        - averageCounts*averageCounts;
                stdCounts = besselCorrection
                           *std::sqrt(std::max(0.0, varianceOfCounts));
            }
            // Reset sums
            sumLatency = std::chrono::microseconds{0};
            sum = 0;
            sumSquared = 0;
            samplesCount = 0;
            packetsCount = 0;
            }
            // Update 
            windowedAverageLatency.store(averageLatency);
            windowedAverageCounts.store(averageCounts);
            windowedStdCounts.store(stdCounts);
            // Note this was updated
            wasUpdated = true;
        }
        return wasUpdated;
    }

    double getWindowedAverageLatency() const
    {
        return windowedAverageLatency.load();
    }

    double getWindowedAverageCounts() const
    {
        return windowedAverageCounts.load();
    }

    double getWindowedStdCounts() const
    {
        return windowedStdCounts.load();
    }

    mutable std::mutex mMutex;
    std::chrono::seconds updateInterval{UPDATE_INTERVAL_SECONDS};
    std::chrono::microseconds lastUpdate
    {
        std::chrono::duration_cast<std::chrono::microseconds>
        ((std::chrono::high_resolution_clock::now()).time_since_epoch())
    };
    std::chrono::microseconds sumLatency{0};
    std::atomic<double> windowedAverageLatency
    {
        static_cast<double> (updateInterval.count())
    };
    std::atomic<double> windowedAverageCounts{0};
    std::atomic<double> windowedStdCounts{0};
    double sum{0};
    double sumSquared{0};
    int64_t samplesCount{0};
    int64_t packetsCount{0};
    bool swapBytes
    {
        std::endian::native == std::endian::little ? false : true
    };
};

[[nodiscard]] 
std::string toKeyName(const UDataPacketImportAPI::V1::StreamIdentifier &identifier)
{
     auto network = identifier.network();
     if (network.empty()){throw std::runtime_error("Network is empty");}
     auto station = identifier.station();
     if (station.empty()){throw std::runtime_error("Station is empty");}
     auto channel = identifier.channel();
     if (channel.empty()){throw std::runtime_error("Channel is empty");}
     auto locationCode = identifier.location_code();

     auto result = network + "_"
                 + station + "_"
                 + channel;
     if (!locationCode.empty()){result = result + "_" + locationCode;}
     std::transform(result.begin(), result.end(), result.begin(), ::tolower);
     return result;
}

[[nodiscard]]
std::string toKeyName(const UDataPacketImportAPI::V1::Packet &packet)
{
     return toKeyName(packet.stream_identifier());
}

export class MetricsSingleton
{
public:
    [[maybe_unused]] static MetricsSingleton &getInstance()
    {
        std::mutex mutex;
        std::scoped_lock lock{mutex};
        static MetricsSingleton instance;
        return instance;
    }

    void tabulateMetrics(const UDataPacketImportAPI::V1::Packet &packet)
    {
        auto key = toKeyName(packet); // Throws
        // If it made it this far then we update the total packets received
        incrementTotalPacketsCounter(key);
        // Okay check the times
        int nSamples = packet.number_of_samples();
        if (nSamples <= 0)
        {
            throw std::invalid_argument("Empty packet for " + key);
        }   
        double samplingRate = packet.sampling_rate();
        if (samplingRate <= 0)
        {
            throw std::invalid_argument("Sampling rate must be positive for "
                                      + key);
        }
        auto samplingPeriod = 1./samplingRate;

        // I really don't need an absurd amount of resolution and would
        // rather be resistant to overflow so microseconds are fine.
        namespace gutil = google::protobuf::util;
        auto startTime = packet.start_time();
        auto startTimeMicroSeconds
            = gutil::TimeUtil::TimestampToMicroseconds(startTime);
        auto endTime = startTime;
        auto endTimeNanoSeconds
            = endTime.nanos()
            + std::max(0, (nSamples - 1))*samplingPeriod*1000000000;
        endTime.set_nanos(endTimeNanoSeconds);
        auto endTimeMicroSeconds
            = gutil::TimeUtil::TimestampToMicroseconds(endTime);

        auto now 
            = std::chrono::duration_cast<std::chrono::microseconds>
              ((std::chrono::high_resolution_clock::now()).time_since_epoch());
        auto validStartTimeMuS = now.count() - mMaximumLatency.count();
        auto validEndTimeMuS = now.count() + mMaximumFutureTime.count();
        // Future
        if (endTimeMicroSeconds > validEndTimeMuS)
        {
            incrementFuturePacketsCounter(key);
            return;
        }
        // Historical
        else if (startTimeMicroSeconds < validStartTimeMuS)
        {
            incrementExpiredPacketsCounter(key);
            return;
        }
        // This is a typical good packet, tabulate metrics
        auto latency
            = std::max(std::chrono::microseconds {0},
                       now - std::chrono::microseconds{endTimeMicroSeconds} );
        incrementReceivedPacketsCounter(key);
        {
        std::lock_guard<std::mutex> lock(mMutex);
        auto idx = mWindowedMetricsMap.find(key);
        if (idx == mWindowedMetricsMap.end())
        {
            auto metrics = std::make_unique<WindowedMetrics> (mUpdateInterval);
            metrics->update(packet, latency);
            mWindowedMetricsMap.insert( std::pair{key, std::move(metrics)} );     
        }
        else
        {
            idx->second->update(packet, latency);
        }
        }
    }

    /// Store windowed metrics and reset for next window
    void updateAndResetWindowedMetrics()
    {
        auto now 
            = std::chrono::duration_cast<std::chrono::microseconds>
              ((std::chrono::high_resolution_clock::now()).time_since_epoch());
        std::lock_guard<std::mutex> lock(mMutex);
        for (auto &item : mWindowedMetricsMap)
        {
            auto updated = item.second->updateAndReset(now);
            if (updated)
            {
                auto averageLatency = item.second->getWindowedAverageLatency();
                auto averageCounts = item.second->getWindowedAverageCounts();
                auto stdCounts = item.second->getWindowedStdCounts();
                // Take advantage of our mutex
                mAverageLatencyMap.insert_or_assign(item.first, averageLatency);
                mAverageCountsMap.insert_or_assign(item.first, averageCounts);
                mStdCountsMap.insert_or_assign(item.first, stdCounts);
            }
        }
    }

    /// Average latency
    [[nodiscard]] 
    std::map<std::string, double> getWindowedAverageLatencies() const
    {   
        std::lock_guard<std::mutex> lock(mMutex);
        return mAverageLatencyMap;
    }   

    /// Average counts 
    [[nodiscard]] 
    std::map<std::string, double> getWindowedAverageCounts() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mAverageCountsMap;
    }

    /// Std counts
    [[nodiscard]] 
    std::map<std::string, double> getWindowedStdCounts() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mStdCountsMap;
    }

    /// Received packets
    void incrementReceivedPacketsCounter(const std::string &key)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        auto idx = mReceivedPacketsCounterMap.find(key);
        if (idx == mReceivedPacketsCounterMap.end())
        {
            mReceivedPacketsCounterMap.insert( std::pair {key, 1} );
        }
        else
        {
            idx->second = idx->second + 1;
        }
    }

    [[nodiscard]] std::map<std::string, int64_t> getReceivedPacketsCounters() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mReceivedPacketsCounterMap;
    }

    /// Future counter
    void incrementFuturePacketsCounter(const std::string &key)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        auto idx = mFuturePacketsCounterMap.find(key);
        if (idx == mFuturePacketsCounterMap.end())
        {
            mFuturePacketsCounterMap.insert( std::pair {key, 1} );
        }
        else
        {
            idx->second = idx->second + 1;
        }
    }

    [[nodiscard]] std::map<std::string, int64_t> getFuturePacketsCounters() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mFuturePacketsCounterMap;
    }

    /// Expired counter
    void incrementExpiredPacketsCounter(const std::string &key)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        auto idx = mExpiredPacketsCounterMap.find(key);
        if (idx == mExpiredPacketsCounterMap.end())
        {
            mExpiredPacketsCounterMap.insert( std::pair {key, 1} );
        }
        else
        {
            idx->second = idx->second + 1;
        }
    }   

    [[nodiscard]] std::map<std::string, int64_t> getExpiredPacketsCounters() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mExpiredPacketsCounterMap;
    }

    /// Total packets counter
    void incrementTotalPacketsCounter(const std::string &key)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        auto idx = mTotalPacketsCounterMap.find(key);
        if (idx == mTotalPacketsCounterMap.end())
        {
            mTotalPacketsCounterMap.insert( std::pair {key, 1} );
        }
        else
        {
            idx->second = idx->second + 1;
        }
    }

    [[nodiscard]] std::map<std::string, int64_t> getTotalPacketsCounters() const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mTotalPacketsCounterMap;
    }


    void incrementReceivedPacketsCounter()
    {   
        mReceivedPacketsCounter.fetch_add(1);
    }   

    [[nodiscard]] int64_t getReceivedPacketsCount() const noexcept
    {
        return mReceivedPacketsCounter.load();
    }

    void incrementSentPacketsCounter() 
    {
        mSentPacketsCounter.fetch_add(1);
    }

    [[nodiscard]] int64_t getSentPacketsCount() const noexcept
    {   
        return mReceivedPacketsCounter.load();
    }   

    void setUpdateInterval(const std::chrono::seconds &interval)
    {
        if (interval.count() <= 0)
        {
            throw std::invalid_argument("Update interval must be positive");
        }
        mUpdateInterval = interval;
    }
private:
    MetricsSingleton() = default;
    ~MetricsSingleton() = default;
    mutable std::mutex mMutex;
    std::map<std::string, int64_t> mReceivedPacketsCounterMap;
    std::map<std::string, int64_t> mExpiredPacketsCounterMap;
    std::map<std::string, int64_t> mFuturePacketsCounterMap;
    std::map<std::string, int64_t> mTotalPacketsCounterMap;
    std::map<std::string, double> mAverageLatencyMap;
    std::map<std::string, double> mAverageCountsMap;
    std::map<std::string, double> mStdCountsMap;
    std::map<std::string, std::unique_ptr<WindowedMetrics>> mWindowedMetricsMap;
    std::atomic<int64_t> mReceivedPacketsCounter{0};
    std::atomic<int64_t> mSentPacketsCounter{0};
    std::chrono::seconds mUpdateInterval{UPDATE_INTERVAL_SECONDS};
    std::chrono::microseconds mMaximumLatency{std::chrono::days {180}};
    std::chrono::microseconds mMaximumFutureTime{0};
};

export void initializeMetricsSingleton()
{
    MetricsSingleton::getInstance();
}

export
void observeValidPacketsReceived(
    opentelemetry::metrics::ObserverResult observerResult,
    void *)
{
    if (opentelemetry::nostd::holds_alternative
        <
            opentelemetry::nostd::shared_ptr
            <
                opentelemetry::metrics::ObserverResultT<int64_t>
            >
        > (observerResult))
    {
        auto observer = opentelemetry::nostd::get
        <
            opentelemetry::nostd::shared_ptr
            <
               opentelemetry::metrics::ObserverResultT<int64_t>
            >
        > (observerResult);
        try
        {
            auto &instance = MetricsSingleton::getInstance();
            auto map = instance.getReceivedPacketsCounters();
            for (const auto &item : map)
            {
                try 
                {
                    auto key = item.first;
                    auto value = item.second;
                    std::map<std::string, std::string>
                        attribute{ {"stream", item.first} };
                    observer->Observe(value, attribute);
                }
                catch (...) //const std::exception &e) 
                {   
                    //spdlog::warn(e.what());
                }
            }   
        }
        catch (...)
        {
        }
    }   
}

export
void observeFuturePacketsReceived(
    opentelemetry::metrics::ObserverResult observerResult,
    void *)
{
    if (opentelemetry::nostd::holds_alternative
        <
            opentelemetry::nostd::shared_ptr
            <
                opentelemetry::metrics::ObserverResultT<int64_t>
            >
        > (observerResult))
    {
        auto observer = opentelemetry::nostd::get
        <
            opentelemetry::nostd::shared_ptr
            <
               opentelemetry::metrics::ObserverResultT<int64_t>
            >
        > (observerResult);
        try
        {
            auto &instance = MetricsSingleton::getInstance();
            auto map = instance.getFuturePacketsCounters();
            for (const auto &item : map)
            {
                try 
                {
                    auto key = item.first;
                    auto value = item.second;
                    std::map<std::string, std::string>
                        attribute{ {"stream", item.first} };
                    observer->Observe(value, attribute);
                }
                catch (...) //const std::exception &e) 
                {   
                    //spdlog::warn(e.what());
                }
            }   
        }
        catch (...)
        {
        }
    }   
}

export
void observeExpiredPacketsReceived(
    opentelemetry::metrics::ObserverResult observerResult,
    void *)
{
    if (opentelemetry::nostd::holds_alternative
        <
            opentelemetry::nostd::shared_ptr
            <
                opentelemetry::metrics::ObserverResultT<int64_t>
            >
        > (observerResult))
    {
        auto observer = opentelemetry::nostd::get
        <
            opentelemetry::nostd::shared_ptr
            <
               opentelemetry::metrics::ObserverResultT<int64_t>
            >
        > (observerResult);
        try
        {
            auto &instance = MetricsSingleton::getInstance();
            auto map = instance.getExpiredPacketsCounters();
            for (const auto &item : map)
            {
                try 
                {
                    auto key = item.first;
                    auto value = item.second;
                    std::map<std::string, std::string>
                        attribute{ {"stream", item.first} };
                    observer->Observe(value, attribute);
                }
                catch (...) //const std::exception &e) 
                {   
                    //spdlog::warn(e.what());
                }
            }   
        }
        catch (...)
        {
        }
    }
}

export
void observeTotalPacketsReceived(
    opentelemetry::metrics::ObserverResult observerResult,
    void *)
{
    if (opentelemetry::nostd::holds_alternative
        <
            opentelemetry::nostd::shared_ptr
            <
                opentelemetry::metrics::ObserverResultT<int64_t>
            >   
        > (observerResult))
    {
        auto observer = opentelemetry::nostd::get
        <
            opentelemetry::nostd::shared_ptr
            <
               opentelemetry::metrics::ObserverResultT<int64_t>
            >
        > (observerResult);
        try
        {
            auto &instance = MetricsSingleton::getInstance();
            auto map = instance.getTotalPacketsCounters();
            for (const auto &item : map)
            {
                try
                {
                    auto key = item.first;
                    auto value = item.second;
                    std::map<std::string, std::string>
                        attribute{ {"stream", item.first} };
                    observer->Observe(value, attribute);
                }
                catch (...) //const std::exception &e) 
                {
                    //spdlog::warn(e.what());
                }
            }
        }
        catch (...)
        {
        }
    }
}

export void observeTotalPacketsSent(
    opentelemetry::metrics::ObserverResult observerResult,
    void *)
{
    if (opentelemetry::nostd::holds_alternative
        <
            opentelemetry::nostd::shared_ptr
            <
                opentelemetry::metrics::ObserverResultT<int64_t>
            >
        > (observerResult))
    {   
        auto observer = opentelemetry::nostd::get
        <
            opentelemetry::nostd::shared_ptr
            <
               opentelemetry::metrics::ObserverResultT<int64_t>
            >
        > (observerResult);
        try
        {
            auto &instance = MetricsSingleton::getInstance();
            auto value = instance.getSentPacketsCount();
            observer->Observe(value);
        }
        catch (const std::exception &e) 
        {

        }
    }   
}

export void observeWindowedAverageLatency(
    opentelemetry::metrics::ObserverResult observerResult,
    void *)
{
    if (opentelemetry::nostd::holds_alternative
        <
            opentelemetry::nostd::shared_ptr
            <
                opentelemetry::metrics::ObserverResultT<double>
            >
        > (observerResult))
    {
        auto observer = opentelemetry::nostd::get
        <
            opentelemetry::nostd::shared_ptr
            <
               opentelemetry::metrics::ObserverResultT<double>
            >
        > (observerResult);
        try
        {
            auto &instance = MetricsSingleton::getInstance();
            auto map = instance.getWindowedAverageLatencies();
            for (const auto &item : map)
            {
                try
                {
                    auto key = item.first;
                    auto value = item.second;
                    std::map<std::string, std::string>
                        attribute{ {"stream", item.first} };
                    observer->Observe(value, attribute);
                }
                catch (...) //const std::exception &e) 
                {
                    //spdlog::warn(e.what());
                }
            }
        }
        catch (...)
        {
        }
    }
}

export void observeWindowedAverageCounts(
    opentelemetry::metrics::ObserverResult observerResult,
    void *)
{
    if (opentelemetry::nostd::holds_alternative
        <
            opentelemetry::nostd::shared_ptr
            <
                opentelemetry::metrics::ObserverResultT<double>
            >
        > (observerResult))
    {   
        auto observer = opentelemetry::nostd::get
        <
            opentelemetry::nostd::shared_ptr
            <
               opentelemetry::metrics::ObserverResultT<double>
            >
        > (observerResult);
        try
        {
            auto &instance = MetricsSingleton::getInstance();
            auto map = instance.getWindowedAverageCounts();
            for (const auto &item : map)
            {
                try
                {
                    auto key = item.first;
                    auto value = item.second;
                    std::map<std::string, std::string>
                        attribute{ {"stream", item.first} };
                    observer->Observe(value, attribute);
                }
                catch (...) //const std::exception &e) 
                {
                    //spdlog::warn(e.what());
                }
            }
        }
        catch (...)
        {
        }
    }
}

export void observeWindowedStdCounts(
    opentelemetry::metrics::ObserverResult observerResult,
    void *)
{
    if (opentelemetry::nostd::holds_alternative
        <
            opentelemetry::nostd::shared_ptr
            <
                opentelemetry::metrics::ObserverResultT<double>
            >
        > (observerResult))
    {
        auto observer = opentelemetry::nostd::get
        <
            opentelemetry::nostd::shared_ptr
            <
               opentelemetry::metrics::ObserverResultT<double>
            >
        > (observerResult);
        try
        {
            auto &instance = MetricsSingleton::getInstance();
            auto map = instance.getWindowedStdCounts();
            for (const auto &item : map)
            {
                try
                {
                    auto key = item.first;
                    auto value = item.second;
                    std::map<std::string, std::string>
                        attribute{ {"stream", item.first} };
                    observer->Observe(value, attribute);
                }
                catch (...) //const std::exception &e) 
                {
                    //spdlog::warn(e.what());
                }
            }
        }
        catch (...)
        {
        }
    }
}


}
