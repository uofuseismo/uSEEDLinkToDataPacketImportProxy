module;
#include <utility>
#include <string>
#include <mutex>
#include <filesystem>
#include <memory>
#ifndef NDEBUG
#include <cassert>
#endif
#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/common.h>
#include <opentelemetry/exporters/otlp/otlp_http_log_record_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_http_log_record_exporter_options.h>
#ifdef WITH_OTLP_GRPC
#include <opentelemetry/exporters/otlp/otlp_grpc_log_record_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_grpc_log_record_exporter_options.h>
#endif
#include <opentelemetry/logs/provider.h>
#include <opentelemetry/logs/logger_provider.h>
#include <opentelemetry/sdk/logs/logger_provider_factory.h>
#include <opentelemetry/sdk/logs/simple_log_record_processor_factory.h>


export module Logger;
import ProgramOptions;
import OTelSpdLog;

namespace USEEDLinkToDataPacketImportProxy::Logger
{

// NOLINTBEGIN(misc-include-cleaner)
std::shared_ptr<opentelemetry::sdk::logs::LoggerProvider> loggerProvider{nullptr};
// NOLINTEND(misc-include-cleaner)

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

std::shared_ptr<spdlog::logger>
    initializeHTTP(const int verbosity,
                   const bool exportLogs,
                   const OTelHTTPLogOptions &options)
{
    std::shared_ptr<spdlog::logger> logger{nullptr};
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt> ();
    if (exportLogs)
    {
        namespace otel = opentelemetry;
        otel::exporter::otlp::OtlpHttpLogRecordExporterOptions httpOptions;
        httpOptions.url = options.url + options.suffix;
        auto exporter
            = otel::exporter::otlp::OtlpHttpLogRecordExporterFactory::Create(httpOptions);
        auto processor
            = otel::sdk::logs::SimpleLogRecordProcessorFactory::Create(
                 std::move(exporter));
        loggerProvider
            = otel::sdk::logs::LoggerProviderFactory::Create(
                std::move(processor));
        const std::shared_ptr<opentelemetry::logs::LoggerProvider>
            apiProvider = loggerProvider;
        otel::logs::Provider::SetLoggerProvider(apiProvider);

        // NOLINTBEGIN(misc-include-cleaner)
        auto otelLogger
            = std::make_shared<spdlog::sinks::OpenTelemetrySink<std::mutex>> ();
        // NOLINTEND(misc-include-cleaner)

        logger
            = std::make_shared<spdlog::logger>
              (spdlog::logger ("OTelLogger", {otelLogger, consoleSink}));
    }
    else
    {
        logger
            = std::make_shared<spdlog::logger>
              (spdlog::logger ("", {consoleSink}));
    }
    // Verbosity
    setVerbosityForSPDLOG(verbosity, &*logger);
    return logger;
}

#ifdef WITH_OTLP_GRPC
std::shared_ptr<spdlog::logger>
    initializeGRPC(const int verbosity,
                   const bool exportLogs,
                   const OTelGRPCLogOptions &options)
{
    std::shared_ptr<spdlog::logger> logger{nullptr};
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt> ();
    if (exportLogs)
    {
        namespace otel = opentelemetry;
        otel::exporter::otlp::OtlpGrpcLogRecordExporterOptions grpcOptions;
        grpcOptions.endpoint = options.url;
        //grpcOptions.timeout = duration?
        grpcOptions.use_ssl_credentials = false;
        if (!options.certificatePath.empty() &&
            std::filesystem::exists(options.certificatePath))
        {
            grpcOptions.use_ssl_credentials = true;
            grpcOptions.ssl_credentials_cacert_path = options.certificatePath;
        }
        auto exporter
            = otel::exporter::otlp::OtlpGrpcLogRecordExporterFactory::Create(grpcOptions);
        auto processor
            = otel::sdk::logs::SimpleLogRecordProcessorFactory::Create(
                 std::move(exporter));
        loggerProvider
            = otel::sdk::logs::LoggerProviderFactory::Create(
                std::move(processor));
        const std::shared_ptr<opentelemetry::logs::LoggerProvider>
            apiProvider = loggerProvider;
        otel::logs::Provider::SetLoggerProvider(apiProvider);

        auto otelLogger
            = std::make_shared<spdlog::sinks::OpenTelemetrySink<std::mutex>> ();

        logger
            = std::make_shared<spdlog::logger>
              (spdlog::logger ("OTelLogger", {otelLogger, consoleSink}));
    }
    else
    {
        logger
            = std::make_shared<spdlog::logger>
              (spdlog::logger ("", {consoleSink}));
    }
    // Verbosity
    setVerbosityForSPDLOG(verbosity, &*logger);
    return logger;
}
#endif

export std::shared_ptr<spdlog::logger>
initialize(const USEEDLinkToDataPacketImportProxy::ProgramOptions &options)
{
    if (options.exportLogsWithHTTP)
    {
        return initializeHTTP(options.verbosity,
                              options.exportLogs,
                              options.otelHTTPLogOptions);
    }
    else
    {
#ifdef WITH_OTLP_GRPC
        return initializeGRPC(options.verbosity,
                              options.exportLogs,
                              options.otelGRPCLogOptions);
#else
        throw std::runtime_error("Recompile with Conan and OTLP_GRPC");
#endif
    }
}
/*
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
        logger
            = std::make_shared<spdlog::logger>
              (spdlog::logger ("", {consoleSink}));
    }
    // Verbosity
    setVerbosityForSPDLOG(programOptions.verbosity, &*logger);
    return logger;
}
*/

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
