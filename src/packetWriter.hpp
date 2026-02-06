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
                  std::shared_ptr<spdlog::logger> &logger,
                  const bool isRetry)
{
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
                    new ::CustomAuthenticator(apiKey)));
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

/*

class AsynchronousWriter :
    public grpc::ClientWriteReactor<UDataPacketImportAPI::V1::Packet>
{
public:
    AsynchronousWriter(
        UDataPacketImportAPI::V1::Frontend::Stub *stub,
        tbb::concurrent_bounded_queue<UDataPacketImportAPI::V1::Packet> *packetQueue,
        std::shared_ptr<spdlog::logger> logger,
        std::atomic<bool> *keepRunning) :
        mPacketQueue(packetQueue),
        mLogger(logger),
        mKeepRunning(keepRunning)
    {
std::cout << "in " << mKeepRunning->load() << std::endl;
#ifndef NDEBUG
        assert(stub != nullptr);
        assert(mPacketQueue != nullptr);
        assert(mLogger != nullptr);
        assert(mKeepRunning != nullptr);
#endif
        mContext.set_wait_for_ready(true);
        stub->async()->Publish(&mContext, &mSummary, this);
std::cout << "okay" << std::endl;
        //AddHold();
        nextWrite();
        StartCall();
std::cout << "RPC Done" << std::endl;
    }
    void OnWriteDone(bool ok) override
    {
#ifndef NDEBUG
        assert(mLogger != nullptr);
#endif
        if (!ok)
        {
            SPDLOG_LOGGER_INFO(mLogger, "Write problem detected");
        } 
std::cout << "on wr done" << std::endl;
        //if (mKeepRunning->load()){nextWrite();}
        // Packet is flushed; can now safely write another packet
        mWriteInProgress = false;
        // Start next write
        nextWrite();
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
            if (mDone){*response = mSummary;}
        }
        return std::move(mStatus);
    }
private:
    void nextWrite()
    {
#ifndef NDEBUG
        assert(mKeepRunning != nullptr);
#endif
std::cout << "nr" << std::endl;
        while (mKeepRunning->load())
        {
            // Get any remaining packets on the queue on the wire
            if (!mWriteInProgress)
            {
std::cout << "not writing" << std::endl;
                if (mPacketQueue->try_pop(mPacketToWrite))
                {
std::cout << "start write " << (*mPacketToWrite.mutable_stream_identifier()).network() << "."
                            << (*mPacketToWrite.mutable_stream_identifier()).station() << "."
                            << (*mPacketToWrite.mutable_stream_identifier()).channel() << "."
                            << (*mPacketToWrite.mutable_stream_identifier()).location_code() << "."
                            << (*mPacketToWrite.mutable_start_time()).seconds() << " " 
                            <<  mPacketToWrite.number_of_samples() << " " << mPacketToWrite.data().size() << " " << mPacketToWrite.sampling_rate() << std::endl;
                    mWriteInProgress = true;
                    StartWrite(&mPacketToWrite);
std::cout << "okay" << std::endl;
                }
            }
            std::this_thread::sleep_for(mTimeOut);
        }
        std::stringstream ss;
        ss << std::this_thread::get_id();
        uint64_t id = std::stoull(ss.str());
        SPDLOG_LOGGER_INFO(mLogger, "Writer thread {} exiting queue loop", id);
        if (!mWriteInProgress)
        {
            StartWritesDone();
            //RemoveHold();
        }
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
    UDataPacketImportAPI::V1::Packet mPacketToWrite;
    UDataPacketImportAPI::V1::PublishResponse mSummary;
    std::chrono::milliseconds mTimeOut{10};
    bool mWriteInProgress{false};
    bool mDone{false};
};
*/

}
#endif
