#include "events/EventManager.h"
#include <chrono>
#include "util/Util.h"

EventManager::EventManager(size_t capacity) : capacity_(capacity == 0 ? 1 : capacity) {
    dispatcher_ = std::thread(&EventManager::dispatchLoop, this);
    dispatcherId_ = dispatcher_.get_id();
}

EventManager::~EventManager() {
    stop();
}

void EventManager::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    queueCv_.notify_all();
    if (dispatcher_.joinable() && std::this_thread::get_id() != dispatcherId_) {
        dispatcher_.join();
    }
}

bool EventManager::publish(DeviceEvent event) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            return false;
        }
        if (queue_.size() >= capacity_) {
            dropped_++;
            return false;
        }
        if (event.timestampMs == 0) {
            event.timestampMs = nowMs();
        }
        event.sequence = ++sequence_;
        queue_.push_back(event);
    }
    queueCv_.notify_one();
    return true;
}

int EventManager::subscribe(Callback callback, const std::string& deviceFilter) {
    std::lock_guard<std::mutex> lock(mutex_);
    Subscriber s;
    s.id = nextSubscriberId_++;
    s.filter = deviceFilter;
    s.callback = std::make_shared<Callback>(callback);
    subscribers_.push_back(s);
    return s.id;
}

bool EventManager::unsubscribe(int id) {
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t i = 0; i < subscribers_.size(); i++) {
            if (subscribers_[i].id == id) {
                subscribers_.erase(subscribers_.begin() + i);
                found = true;
                break;
            }
        }
    }
    if (found && std::this_thread::get_id() != dispatcherId_) {
        std::lock_guard<std::mutex> wait(deliveryMutex_);
    }
    return found;
}

bool EventManager::waitUntilIdle(int timeoutMs) {
    std::unique_lock<std::mutex> lock(mutex_);
    return idleCv_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                            [this] { return queue_.empty() && !busy_; });
}

size_t EventManager::queueSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

size_t EventManager::capacity() const {
    return capacity_;
}

unsigned long long EventManager::droppedCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dropped_;
}

unsigned long long EventManager::deliveredCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return delivered_;
}

void EventManager::dispatchLoop() {
    while (true) {
        DeviceEvent event;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            queueCv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty()) {
                break;
            }
            event = queue_.front();
            queue_.pop_front();
            busy_ = true;
        }

        {
            std::lock_guard<std::mutex> delivery(deliveryMutex_);
            std::vector<Subscriber> current;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                current = subscribers_;
            }
            for (size_t i = 0; i < current.size(); i++) {
                if (current[i].filter.empty() || current[i].filter == event.deviceId) {
                    try {
                        (*current[i].callback)(event);
                    } catch (...) {
                    }
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            busy_ = false;
            delivered_++;
            if (queue_.empty()) {
                idleCv_.notify_all();
            }
        }
    }
}
