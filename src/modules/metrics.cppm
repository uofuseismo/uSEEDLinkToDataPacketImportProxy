module;

#include <mutex>
#include <atomic>
#include <string>
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

namespace USEEDLinkToDataPacketImportProxy::Metrics
{

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

        auto startTime = packet.start_time();
        auto startTimeMicroSeconds
            = google::protobuf::util::TimeUtil::TimestampToMicroseconds(startTime);
        auto endTime = startTime;
        auto endTimeNanoSeconds
            = endTime.nanos()
            + std::max(0, (nSamples - 1))*samplingPeriod*1000000000;
        endTime.set_nanos(endTimeNanoSeconds);
        auto endTimeMicroSeconds
            = google::protobuf::util::TimeUtil::TimestampToMicroseconds(endTime);

        // We really don't need an absurd amount of resolution
        auto now 
            = std::chrono::duration_cast<std::chrono::microseconds>
              ((std::chrono::high_resolution_clock::now()).time_since_epoch());
        // Future
        if (endTimeMicroSeconds > now.count() + mMaximumFutureTime.count())
        {

        }
        // Historical
        if (startTimeMicroSeconds < now.count() - mMaximumLatency.count())
        {

        }
    }

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


/*
    [[nodiscard]] int64_t getReceivedPacketsCounter() const noexcept
    {
        return mReceivedPacketsCounter.load();
    }
    [[nodiscard]] std::chrono::microseconds getWindowedAverageLatency() const noexcept
    {
        return mWindowedAverageLatency.load();
    }
*/
private:
    MetricsSingleton() = default;
    ~MetricsSingleton() = default;
    mutable std::mutex mMutex;
    std::map<std::string, int64_t> mReceivedPacketsCounterMap;
    std::map<std::string, int64_t> mExpiredPacketsCounterMap;
    std::map<std::string, int64_t> mFuturePacketsCounterMap;
    std::map<std::string, int64_t> mTotalPacketsCounterMap;
    std::chrono::microseconds mMaximumLatency{std::chrono::days {180}};
    std::chrono::microseconds mMaximumFutureTime{0};
};

export void initializeMetricsSingleton()
{
    MetricsSingleton::getInstance();
}

void observeReceivedPacketsReceived(
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


}
