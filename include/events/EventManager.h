#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "events/DeviceEvent.h"

class EventManager {
public:
    typedef std::function<void(const DeviceEvent&)> Callback;

    explicit EventManager(size_t capacity = 256);
    ~EventManager();

    bool publish(DeviceEvent event);
    int subscribe(Callback callback, const std::string& deviceFilter = "");
    bool unsubscribe(int id);
    bool waitUntilIdle(int timeoutMs);
    void stop();

    size_t queueSize() const;
    size_t capacity() const;
    unsigned long long droppedCount() const;
    unsigned long long deliveredCount() const;

private:
    struct Subscriber {
        int id;
        std::string filter;
        std::shared_ptr<Callback> callback;
    };

    void dispatchLoop();

    size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable queueCv_;
    std::condition_variable idleCv_;
    std::deque<DeviceEvent> queue_;
    std::vector<Subscriber> subscribers_;
    std::mutex deliveryMutex_;
    std::thread dispatcher_;
    std::thread::id dispatcherId_;
    bool stopping_ = false;
    bool busy_ = false;
    int nextSubscriberId_ = 1;
    unsigned long long sequence_ = 0;
    unsigned long long dropped_ = 0;
    unsigned long long delivered_ = 0;
};
