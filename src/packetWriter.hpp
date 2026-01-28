#ifndef PACKET_WRITER_HPP
#define PACKET_WRITER_HPP
#include <sstream>
#include <mutex>
#include <queue>
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
            SPDLOG_LOGGER_INFO(logger,
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
        SPDLOG_LOGGER_INFO(logger,
                           "Creating secure channel without API key to ",
                           address);
        grpc::SslCredentialsOptions sslOptions;
        sslOptions.pem_root_certs = *serverCertificate;
        return grpc::CreateChannel(address,
                                   grpc::SslCredentials(sslOptions));
     }
     SPDLOG_LOGGER_INFO(logger,
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
std::cout << "in" << std::endl;
#ifndef NDEBUG
        assert(mPacketQueue != nullptr);
        assert(mLogger != nullptr);
        assert(mKeepRunning != nullptr);
#endif
        mContext.set_wait_for_ready(true);
        stub->async()->Publish(&mContext, &mResponse, this);
std::cout << "okay" << std::endl;
        //AddHold();
        nextWrite();
        StartCall();
std::cout << "Done" << std::endl;
    }
    void OnWriteDone(bool ok) override
    {
        if (!ok) 
        {
/*
            if (mContext)
            {
                if (mContext->IsCancelled())
                {
                    return Finish(grpc::Status::CANCELLED);
                }
            }
            return Finish(grpc::Status(grpc::StatusCode::UNKNOWN,
                                       "Unexpected failure"));
*/
        }
        //if (mKeepRunning->load()){nextWrite();}
        // Packet is flushed; can now safely write another packet
        mWriteInProgress = false;
        // Start next write
        if (mKeepRunning->load())
        {
            nextWrite();
        }
        else
        {
            StartWritesDone(); 
        }
    }
    void OnDone(const grpc::Status &status) override
    {
#ifndef NDEBUG
        assert(mLogger != nullptr);
#endif
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
        assert(response != nullptr);
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
            lock.unlock();
            if (mDone){*response = mResponse;}
        }
        return std::move(mStatus);
    }
private:
    void nextWrite()
    {
#ifndef NDEBUG
        assert(mKeepRunning != nullptr);
#endif
        while (mKeepRunning->load())
        {
            // Get any remaining packets on the queue on the wire
            if (!mWriteInProgress)
            {
                if (mPacketQueue->try_pop(mPacket))
                {
                    mWriteInProgress = true;
                    StartWrite(&mPacket);
                }
            }
            std::this_thread::sleep_for(mTimeOut);
            if (!mKeepRunning->load())
            {
                StartWritesDone();
            }
        }
        std::stringstream ss;
        ss << std::this_thread::get_id();
        uint64_t id = std::stoull(ss.str());
        SPDLOG_LOGGER_INFO(mLogger, "Writer thread {} exiting queue loop", id);
        StartWritesDone();
        //RemoveHold();
    }
//private:
    grpc::ClientContext mContext;
    tbb::concurrent_bounded_queue<UDataPacketImportAPI::V1::Packet>
        *mPacketQueue{nullptr};
    std::shared_ptr<spdlog::logger> mLogger{nullptr}; 
    std::atomic<bool> *mKeepRunning{nullptr};

    std::mutex mMutex;
    //std::queue<UDataPacketImportAPI::V1::Packet> mWritePacketsQueue;
    std::condition_variable mConditionVariable;
    grpc::Status mStatus;
    UDataPacketImportAPI::V1::Packet mPacket;
    UDataPacketImportAPI::V1::PublishResponse mResponse;
    std::chrono::milliseconds mTimeOut{10};
    bool mWriteInProgress{false};
    bool mDone{false};
};

}
#endif
