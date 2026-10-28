// Copyright 2026 The LieppOS Contributors
// SPDX-License-Identifier: Apache-2.0
#include <mtk_vibrator/Controller.h>

#include <cerrno>
#include <utility>

namespace lieppos::vibrator {
Controller::Controller(std::unique_ptr<Switch> device, ErrorHandler handler)
    : device_(std::move(device)), onAsyncError_(std::move(handler)), worker_(&Controller::run, this) {}

Controller::~Controller() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        changed_.notify_all();
    }
    worker_.join();
    // Best effort safety stop, including shutdown before the deadline. The board
    // service also fails startup if its initial off() write cannot be accepted.
    const int error = device_->set(false);
    if (error && onAsyncError_) onAsyncError_(error);
}

int Controller::on(int timeoutMs) {
    if (timeoutMs <= 0) return EINVAL;
    std::lock_guard lock(mutex_);
    const int error = device_->set(true);
    if (error) {
        // A partial/failed enable has uncertain physical state. Preserve an
        // existing stop, or arm an immediate safety-off if there was none.
        if (!deadline_) {
            deadline_ = std::chrono::steady_clock::now();
            changed_.notify_all();
        }
        return error;
    }
    deadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    changed_.notify_all();
    return 0;
}

int Controller::off() {
    std::lock_guard lock(mutex_);
    const int error = device_->set(false);
    if (error) return error;  // Keep the safety worker armed for another attempt.
    deadline_.reset();
    changed_.notify_all();
    return 0;
}

void Controller::run() {
    std::unique_lock lock(mutex_);
    while (!stopping_) {
        if (!deadline_) {
            changed_.wait(lock, [this] { return stopping_ || deadline_.has_value(); });
            continue;
        }
        const auto expected = *deadline_;
        if (changed_.wait_until(lock, expected, [this, expected] {
                return stopping_ || !deadline_ || *deadline_ != expected;
            })) continue;
        const int error = device_->set(false);
        if (!error) {
            deadline_.reset();
        } else {
            // A failed timer-off must not become fake success or silently disarm
            // the motor stop. Retry at a bounded rate; report outside the mutex.
            deadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
            lock.unlock();
            if (onAsyncError_) onAsyncError_(error);
            lock.lock();
        }
    }
}
}  // namespace lieppos::vibrator
