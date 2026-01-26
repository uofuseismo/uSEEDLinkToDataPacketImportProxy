#ifndef PACKET_WRITER_HPP
#define PACKET_WRITER_HPP
#include <mutex>
#include <condition_variable>
#include <chrono>
#ifndef NDEBUG
#include <cassert>
#endif
#include <tbb/concurrent_queue.h>
#include "uDataPacketImportAPI/v1/frontend.grpc.pb.h"
namespace
{

class AsynchronousWriter final :
    public grpc::ClientWriteReactor<UDataPacketImportAPI::V1::Packet>
{
public:
    AsynchronousWriter(
        UDataPacketImportAPI::V1::Frontend::Stub *stub,
        tbb::concurrent_bounded_queue<UDataPacketImportAPI::V1::Packet> *packetQueue,
        std::atomic<bool> *keepRunning) :
        mPacketQueue(packetQueue),
        mKeepRunning(keepRunning)
    {
#ifndef NDEBUG
        assert(mPacketQueue != nullptr);
        assert(mKeepRunning != nullptr);
#endif
        stub->async()->Publish(&mContext, &mResponse, this);
        nextWrite();
        StartCall();
    }
    void OnDone(const grpc::Status &status) override
    {
        std::unique_lock<std::mutex> lock(mMutex);
        mStatus = status;
        mDone = true;
        mConditionVariable.notify_one();
    }
    grpc::Status await(UDataPacketImportAPI::V1::PublishResponse *response)
    {
        std::unique_lock<std::mutex> lock(mMutex);
        mConditionVariable.wait(lock, [this] {return mDone;});
        *response = mResponse; 
        return std::move(mStatus);
    }
    void nextWrite()
    {
        while (mKeepRunning->load())
        {
            if (mPacketQueue->try_pop(mPacket))
            {
                StartWrite(&mPacket);
            }
            else
            {
                std::this_thread::sleep_for(mTimeOut);
            }
        }
        StartWritesDone();
    }
    tbb::concurrent_bounded_queue<UDataPacketImportAPI::V1::Packet>
        *mPacketQueue{nullptr};
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
