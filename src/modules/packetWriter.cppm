module;

#include <sstream>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <chrono>
#include <spdlog/spdlog.h>
#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>
#ifndef NDEBUG
#include <cassert>
#endif
#include <tbb/concurrent_queue.h>
#include "uDataPacketImportAPI/v1/frontend.grpc.pb.h"
#include "grpcOptions.hpp"

export module PacketWriter;

import ProgramOptions;

namespace USEEDLinkToDataPacketImportProxy::PacketWriter
{

class CustomAuthenticator : public grpc::MetadataCredentialsPlugin
{    
public:    
    CustomAuthenticator(const grpc::string &token) :
        mToken(token)
    {   
    }   
    grpc::Status GetMetadata(
        grpc::string_ref , //serviceURL, 
        grpc::string_ref , //methodName,
        const grpc::AuthContext &, //channelAuthContext,
        std::multimap<grpc::string, grpc::string> *metadata) override
    {   
        metadata->insert(std::make_pair("x-custom-auth-token", mToken));
        return grpc::Status::OK;
    }   
//private:
    grpc::string mToken;
};

export
std::shared_ptr<grpc::Channel>
    createChannel(const USEEDLinkToDataPacketImportProxy::GRPCOptions &options,
                  spdlog::logger *logger,
                  const bool isRetry)
{
#ifndef NDEBUG
    assert(logger);
#endif
    auto address = USEEDLinkToDataPacketImportProxy::makeAddress(options);
    auto serverCertificate = options.getServerCertificate();
    if (serverCertificate)
    {
#ifndef NDEBUG
        assert(!serverCertificate->empty());
#endif
        if (options.getAccessToken())
        {
            auto apiKey = *options.getAccessToken();
#ifndef NDEBUG
            assert(!apiKey.empty());
#endif
            if (!isRetry)
            {
                SPDLOG_LOGGER_INFO(logger,
                                   "Creating secure channel with API key to {}",
                                   address);
            }
            else
            {
                SPDLOG_LOGGER_INFO(logger,
                                   "Recreating secure channel with API key to {}",
                                   address);
            }
            auto callCredentials = grpc::MetadataCredentialsFromPlugin(
                std::unique_ptr<grpc::MetadataCredentialsPlugin> (
                    new CustomAuthenticator(apiKey)));
            grpc::SslCredentialsOptions sslOptions;
            sslOptions.pem_root_certs = *serverCertificate;
            auto channelCredentials
                = grpc::CompositeChannelCredentials(
                      grpc::SslCredentials(sslOptions),
                      callCredentials);
            return grpc::CreateChannel(address, channelCredentials);
        }
        if (!isRetry)
        {
            SPDLOG_LOGGER_INFO(logger,
                               "Creating secure channel without API key to {}",
                               address);
        }
        else
        {
            SPDLOG_LOGGER_INFO(logger,
                               "Recreating secure channel without API key to {}",
                               address);
         }
        grpc::SslCredentialsOptions sslOptions;
        sslOptions.pem_root_certs = *serverCertificate;
        return grpc::CreateChannel(address,
                                   grpc::SslCredentials(sslOptions));
     }
     if (!isRetry)
     {
         SPDLOG_LOGGER_INFO(logger,
                            "Creating non-secure channel to {}",
                             address);
     }
     else
     {
         SPDLOG_LOGGER_INFO(logger,
                            "Recreating non-secure channel to {}", 
                             address);
     }
     return grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
}

export
std::pair<grpc::Status, bool> 
    publishSynchronously(
        const USEEDLinkToDataPacketImportProxy::GRPCOptions &grpcOptions,
        const std::chrono::milliseconds &timeOut,
        const bool isRetry,
        tbb::concurrent_bounded_queue<UDataPacketImportAPI::V1::Packet> *importQueue,
        std::atomic<bool> *keepRunning,
        std::shared_ptr<spdlog::logger> logger)
{
#ifndef NDEBUG
    assert(keepRunning);
    assert(logger != nullptr);
    assert(importQueue != nullptr);
#endif
    auto channel
        = USEEDLinkToDataPacketImportProxy::PacketWriter::createChannel(
            grpcOptions, logger.get(), isRetry);
    auto stub = UDataPacketImportAPI::V1::Frontend::NewStub(channel);
    grpc::ClientContext context;
    context.set_wait_for_ready(false);

    bool hadSuccessfulWrite{false}; 
    UDataPacketImportAPI::V1::PublishResponse publishResponse;
    std::unique_ptr
    <
        grpc::ClientWriter<UDataPacketImportAPI::V1::Packet>
    > writer(stub->Publish(&context,  &publishResponse));
#ifndef NDEBUG
    assert(writer != nullptr);
#endif
    while (keepRunning->load())
    {
        UDataPacketImportAPI::V1::Packet packet;
        if (importQueue->try_pop(packet))
        {
            if (!writer->Write(packet))
            {
                SPDLOG_LOGGER_WARN(logger, "Broken stream");
                break;
            }
            hadSuccessfulWrite = true;
        }
        else
        {
            std::this_thread::sleep_for(timeOut);
        }
    }
    if (!keepRunning->load())
    {
        writer->WritesDone();
    }
    auto status = writer->Finish();
    return std::pair {status, hadSuccessfulWrite};
}

}
