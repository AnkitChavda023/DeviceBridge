#pragma once
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "transport/VirtualTransport.h"

class SimulatedSource {
public:
    explicit SimulatedSource(std::shared_ptr<VirtualTransport> transport);
    virtual ~SimulatedSource();

    void start(int intervalMs);
    void stop();
    bool isRunning() const;
    void generateOne();

    void injectMalformed(int count);
    void setDelayMs(int ms);
    void disconnect();
    void reconnect();

    std::shared_ptr<VirtualTransport> transport() const { return transport_; }
    unsigned long long generatedCount() const;

protected:
    virtual std::vector<uint8_t> produce() = 0;
    virtual std::vector<uint8_t> corrupt(const std::vector<uint8_t>& frame, int variant);
    virtual void handleWrite(const std::vector<uint8_t>& data);
    void detach();

    std::shared_ptr<VirtualTransport> transport_;
    mutable std::mutex mutex_;

private:
    void runLoop(int intervalMs);

    int malformedLeft_;
    int malformedVariant_;
    unsigned long long generated_;
    std::thread thread_;
    mutable std::mutex threadMutex_;
    std::mutex wakeMutex_;
    std::condition_variable wakeCv_;
    bool stopFlag_;
    bool running_;
};
