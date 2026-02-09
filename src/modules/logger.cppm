module;

#ifndef NDEBUG
#include <cassert>
#endif
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter_options.h>
#include <opentelemetry/exporters/otlp/otlp_http_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_log_record_exporter_factory.h>
#include <opentelemetry/logs/provider.h>
#include <opentelemetry/sdk/logs/logger_provider_factory.h>
#include <opentelemetry/sdk/logs/simple_log_record_processor_factory.h>

export module Logger;
import ProgramOptions;
import OTelSpdLog;

namespace USEEDLinkToDataPacketImportProxy::Logger
{

std::shared_ptr<opentelemetry::sdk::logs::LoggerProvider> loggerProvider{nullptr};

void setVerbosityForSPDLOG(const int verbosity,
                           spdlog::logger *logger)
{
#ifndef NDEBUG
    assert(logger != nullptr);
#endif
    if (verbosity <= 1)
    {
        logger->set_level(spdlog::level::critical);
    }
    if (verbosity == 2){logger->set_level(spdlog::level::warn);}
    if (verbosity == 3){logger->set_level(spdlog::level::info);}
    if (verbosity >= 4){logger->set_level(spdlog::level::debug);}
}

export std::shared_ptr<spdlog::logger>
    initialize(USEEDLinkToDataPacketImportProxy::ProgramOptions programOptions)
{
    std::shared_ptr<spdlog::logger> logger{nullptr};
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt> ();
    if (programOptions.exportLogs)
    {
        namespace otel = opentelemetry;
        otel::exporter::otlp::OtlpHttpLogRecordExporterOptions httpOptions;
        httpOptions.url = programOptions.otelHTTPLogOptions.url
                        + programOptions.otelHTTPLogOptions.suffix;
        //httpOptions.use_ssl_credentials = false;
        //httpOptions.ssl_credentials_cacert_path = programOptions.otelGRPCOptions.certificatePath;
        //using providerPtr
        //    = otel::nostd::shared_ptr<opentelemetry::logs::LoggerProvider>;
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
            = std::make_shared<spdlog::sinks::OpenTelemetrySink<std::mutex>> ();

        logger
            = std::make_shared<spdlog::logger>
              (spdlog::logger ("OTelLogger", {otelLogger, consoleSink}));
    }
    else
    {
        auto consoleSink
            = std::make_shared<spdlog::sinks::stdout_color_sink_mt> ();
        logger
            = std::make_shared<spdlog::logger>
              (spdlog::logger ("", {consoleSink}));
    }
    // Verbosity
    setVerbosityForSPDLOG(programOptions.verbosity, &*logger);
    return logger;
}


export void cleanup()
{
    if (loggerProvider)
    {   
        loggerProvider->ForceFlush();
        loggerProvider.reset();
        std::shared_ptr<opentelemetry::logs::LoggerProvider> none;
        opentelemetry::logs::Provider::SetLoggerProvider(none);
    }   
}

}
