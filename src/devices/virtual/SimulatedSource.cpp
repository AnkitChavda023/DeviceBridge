#include "simulator/SimulatedSource.h"
#include <chrono>

SimulatedSource::SimulatedSource(std::shared_ptr<VirtualTransport> transport)
    : transport_(transport), malformedLeft_(0), malformedVariant_(0), generated_(0), stopFlag_(false), running_(false) {
    transport_->setDataProvider([this] { generateOne(); });
    transport_->setWriteHandler([this](const std::vector<uint8_t>& data) { handleWrite(data); });
}

SimulatedSource::~SimulatedSource() {
    detach();
}

void SimulatedSource::detach() {
    stop();
    transport_->setDataProvider(nullptr);
    transport_->setWriteHandler(nullptr);
}

void SimulatedSource::generateOne() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<uint8_t> frame = produce();
    if (malformedLeft_ > 0) {
        malformedLeft_--;
        frame = corrupt(frame, malformedVariant_++);
    }
    generated_++;
    transport_->feed(frame);
}

std::vector<uint8_t> SimulatedSource::corrupt(const std::vector<uint8_t>& frame, int) {
    std::vector<uint8_t> bad = frame;
    if (!bad.empty()) {
        bad[bad.size() - 1] ^= 0x5A;
    }
    return bad;
}

void SimulatedSource::handleWrite(const std::vector<uint8_t>&) {}

void SimulatedSource::injectMalformed(int count) {
    std::lock_guard<std::mutex> lock(mutex_);
    malformedLeft_ = count;
}

void SimulatedSource::setDelayMs(int ms) {
    transport_->setReadDelayMs(ms);
}

void SimulatedSource::disconnect() {
    transport_->setConnected(false);
}

void SimulatedSource::reconnect() {
    transport_->setConnected(true);
}

unsigned long long SimulatedSource::generatedCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return generated_;
}

void SimulatedSource::start(int intervalMs) {
    std::lock_guard<std::mutex> lock(threadMutex_);
    if (running_) {
        return;
    }
    {
        std::lock_guard<std::mutex> wake(wakeMutex_);
        stopFlag_ = false;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    running_ = true;
    thread_ = std::thread(&SimulatedSource::runLoop, this, intervalMs);
}

void SimulatedSource::stop() {
    std::lock_guard<std::mutex> lock(threadMutex_);
    if (!thread_.joinable()) {
        running_ = false;
        return;
    }
    {
        std::lock_guard<std::mutex> wake(wakeMutex_);
        stopFlag_ = true;
    }
    wakeCv_.notify_all();
    thread_.join();
    running_ = false;
}

bool SimulatedSource::isRunning() const {
    std::lock_guard<std::mutex> lock(threadMutex_);
    return running_;
}

void SimulatedSource::runLoop(int intervalMs) {
    while (true) {
        generateOne();
        std::unique_lock<std::mutex> wake(wakeMutex_);
        wakeCv_.wait_for(wake, std::chrono::milliseconds(intervalMs), [this] { return stopFlag_; });
        if (stopFlag_) {
            break;
        }
    }
}
