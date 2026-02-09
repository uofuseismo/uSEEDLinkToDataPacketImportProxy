module;
/*
 * Copyright The OpenTelemetry Authors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <opentelemetry/logs/severity.h>
#include <opentelemetry/logs/provider.h>
#include <opentelemetry/semconv/code_attributes.h>
#include <opentelemetry/semconv/incubating/thread_attributes.h>
#include <opentelemetry/version.h>

#include <spdlog/details/null_mutex.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/version.h>

#include <mutex>

export module otelspdlog;

namespace spdlog::sinks
{

export
template <typename Mutex>
class OpenTelemetrySink : public spdlog::sinks::base_sink<Mutex>
{
public:
  static const std::string &libraryVersion()
  {
    static const std::string kLibraryVersion = std::to_string(SPDLOG_VER_MAJOR) + "." +
                                               std::to_string(SPDLOG_VER_MINOR) + "." +
                                               std::to_string(SPDLOG_VER_PATCH);
    return kLibraryVersion;
  }

  static inline opentelemetry::logs::Severity levelToSeverity(int level) noexcept
  {
    namespace Level = spdlog::level;
    using opentelemetry::logs::Severity;

    switch (level)
    {
      case Level::critical:
        return Severity::kFatal;
      case Level::err:
        return Severity::kError;
      case Level::warn:
        return Severity::kWarn;
      case Level::info:
        return Severity::kInfo;
      case Level::debug:
        return Severity::kDebug;
      case Level::trace:
        return Severity::kTrace;
      case Level::off:
      default:
        return Severity::kInvalid;
    }
  }

protected:
  void sink_it_(const spdlog::details::log_msg &msg) override
  {
    static constexpr auto kLibraryName = "spdlog";

    auto provider   = opentelemetry::logs::Provider::GetLoggerProvider();
    auto logger     = provider->GetLogger(msg.logger_name.data(), kLibraryName, libraryVersion());
    auto log_record = logger->CreateLogRecord();

    if (log_record)
    {
      using namespace opentelemetry::semconv::code;
      using namespace opentelemetry::semconv::thread;

      log_record->SetSeverity(levelToSeverity(msg.level));
      log_record->SetBody(opentelemetry::nostd::string_view(msg.payload.data(), msg.payload.size()));
      log_record->SetTimestamp(msg.time);
      if (!msg.source.empty())
      {   
        log_record->SetAttribute(kCodeFilePath, msg.source.filename);
        log_record->SetAttribute(kCodeLineNumber, msg.source.line);
      }   
      log_record->SetAttribute(kThreadId, msg.thread_id);
      logger->EmitLogRecord(std::move(log_record));
    }
  }
  void flush_() override {}
};

/*
using opentelemetry_sink_mt = OpenTelemetrySink<std::mutex>;
using opentelemetry_sink_st = OpenTelemetrySink<spdlog::details::null_mutex>;
*/

}  // namespace spdlog::sinks
