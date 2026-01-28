#ifndef PACKET_WRITER_HPP
#define PACKET_WRITER_HPP
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>
#ifndef NDEBUG
#include <cassert>
#endif
#include <tbb/concurrent_queue.h>
#include "uDataPacketImportAPI/v1/frontend.grpc.pb.h"
#include "grpcOptions.hpp"
namespace
{

class CustomAuthenticator : public grpc::MetadataCredentialsPlugin
{           
public:         
    CustomAuthenticator(const grpc::string &token) :
        mToken(token)
    {   
    }   
    grpc::Status GetMetadata(
        grpc::string_ref serviceURL, 
        grpc::string_ref methodName,
        const grpc::AuthContext &channelAuthContext,
        std::multimap<grpc::string, grpc::string> *metadata) override
    {   
        metadata->insert(std::make_pair("x-custom-auth-token", mToken));
        return grpc::Status::OK;
    }   
//private:
    grpc::string mToken;
};

std::shared_ptr<grpc::Channel>
    createChannel(const USEEDLinkToDataPacketImportProxy::GRPCOptions &options,
                  std::shared_ptr<spdlog::logger> &logger)
{
    auto address = USEEDLinkToDataPacketImportProxy::makeAddress(options);
    auto serverCertificate = options.getServerCertificate();
    if (serverCertificate)
    {
#ifndef NDEBUG
        assert(!serverCertificate->empty());
#endif
        auto apiKey = options.getAccessToken();
        if (apiKey)
        {
#ifndef NDEBUG
            assert(!apiKey->empty());
#endif
            SPDLOG_LOGGER_DEBUG(logger,
                                "Creating secure channel with API key to {}",
                                address);
            auto callCredentials = grpc::MetadataCredentialsFromPlugin(
                std::unique_ptr<grpc::MetadataCredentialsPlugin> (
                    new ::CustomAuthenticator(*apiKey)));
            grpc::SslCredentialsOptions sslOptions;
            sslOptions.pem_root_certs = *serverCertificate;
            auto channelCredentials
                = grpc::CompositeChannelCredentials(
                      grpc::SslCredentials(sslOptions),
                      callCredentials);
            return grpc::CreateChannel(address, channelCredentials);
        }
        SPDLOG_LOGGER_DEBUG(logger,
                            "Creating secure channel without API key to ",
                            address);
        grpc::SslCredentialsOptions sslOptions;
        sslOptions.pem_root_certs = *serverCertificate;
        return grpc::CreateChannel(address,
                                   grpc::SslCredentials(sslOptions));
     }
     SPDLOG_LOGGER_DEBUG(logger,
                         "Creating non-secure channel to {}", address);
     return grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
}

class AsynchronousWriter final :
    public grpc::ClientWriteReactor<UDataPacketImportAPI::V1::Packet>
{
public:
    AsynchronousWriter(
        UDataPacketImportAPI::V1::Frontend::Stub *stub,
        tbb::concurrent_bounded_queue<UDataPacketImportAPI::V1::Packet> *packetQueue,
        std::shared_ptr<spdlog::logger> &logger,
        std::atomic<bool> *keepRunning) :
        mPacketQueue(packetQueue),
        mLogger(logger),
        mKeepRunning(keepRunning)
    {
#ifndef NDEBUG
        assert(mPacketQueue != nullptr);
        assert(mLogger != nullptr);
        assert(mKeepRunning != nullptr);
#endif
        stub->async()->Publish(&mContext, &mResponse, this);
        AddHold();
        nextWrite();
        StartCall();
    }
    void OnWriteDone(bool ok) override
    {
        if (mKeepRunning->load()){nextWrite();}
    }
    void OnDone(const grpc::Status &status) override
    {
        SPDLOG_LOGGER_INFO(mLogger, "Writer finished");
        std::unique_lock<std::mutex> lock(mMutex);
        mStatus = status;
        mDone = true;
        mConditionVariable.notify_one();
    }
    [[nodiscard]] grpc::Status
        await(UDataPacketImportAPI::V1::PublishResponse *response)
    {
#ifndef NDEBUG
        assert(mKeepRunning != nullptr);
#endif
        while (mKeepRunning->load())
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mConditionVariable.wait_for(lock,
                                        std::chrono::milliseconds {50},
                                        [this]
                                        {
                                            return mDone;
                                        });
            if (mDone && response){*response = mResponse;} 
            lock.unlock();
        }
        return std::move(mStatus);
/*
        std::unique_lock<std::mutex> lock(mMutex);
        mConditionVariable.wait(lock, [this] {return mDone;});
        *response = mResponse; 
        return std::move(mStatus);
*/
    }
    void nextWrite()
    {
#ifndef NDEBUG
        assert(mKeepRunning != nullptr);
#endif
        while (mKeepRunning->load())
        {
            if (mPacketQueue->try_pop(mPacket))
            {
                StartWrite(&mPacket);
            }
            else
            {
                std::this_thread::sleep_for(mTimeOut);
                nextWrite();
            }
        }
        SPDLOG_LOGGER_INFO(mLogger, "Writer exiting queue loop");
        StartWritesDone();
        RemoveHold();
    }
//private:
    tbb::concurrent_bounded_queue<UDataPacketImportAPI::V1::Packet>
        *mPacketQueue{nullptr};
    std::shared_ptr<spdlog::logger> mLogger{nullptr}; 
    std::atomic<bool> *mKeepRunning{nullptr};

    grpc::ClientContext mContext;
    std::mutex mMutex;
    std::condition_variable mConditionVariable;
    grpc::Status mStatus;
    UDataPacketImportAPI::V1::Packet mPacket;
    UDataPacketImportAPI::V1::PublishResponse mResponse;
    std::chrono::milliseconds mTimeOut{15};
    bool mDone{false};
};

}
#endif
