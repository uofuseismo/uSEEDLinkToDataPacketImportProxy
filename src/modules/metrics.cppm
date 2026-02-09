module;

#include <mutex>
#include <atomic>
#include <string>
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

export class MetricsSingleton
{
public:
    static MetricsSingleton &getInstance()
    {
        std::mutex mutex;
        std::scoped_lock lock{mutex};
        static MetricsSingleton instance;
        return instance;
    }   
private:
    MetricsSingleton() = default;
    ~MetricsSingleton() = default;
//    std::atomic<int64_t> mReceivedPacketsCounter{0};
//    std::atomic<int64_t> mSentPacketsCounter{0};
//    std::atomic<double> mPublisherUtilization{0};
//    std::atomic<double> mSubscriberUtilization{0};
};


}
