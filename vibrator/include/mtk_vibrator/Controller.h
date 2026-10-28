// Copyright 2026 The LieppOS Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace lieppos::vibrator {

// Zero means a complete accepted write; otherwise a positive errno.
class Switch {
  public:
    virtual ~Switch() = default;
    virtual int set(bool enabled) = 0;
    // Legacy bool switches are retained for diagnostics/tests, not production.
    virtual int start(int timeoutMs) { (void)timeoutMs; return set(true); }
    virtual bool usesKernelTimerProtocol() const { return false; }
    virtual int maxDurationMs() const { return std::numeric_limits<int>::max(); }
};

// Internal attribute transport; injectable for explicitly mocked protocol tests.
// Production owns a directory fd and fixed attribute names, not Binder paths.
class LedAttributes {
  public:
    virtual ~LedAttributes() = default;
    virtual int read(const char* name, std::string* value) = 0;
    virtual int write(const char* name, const std::string& value) = 0;
};
std::unique_ptr<Switch> makeOneShotSwitch(std::unique_ptr<LedAttributes> attributes, int* error);
std::unique_ptr<Switch> makeDriverTimedSwitch(std::unique_ptr<LedAttributes> attributes, int* error);

// Uses the real Linux LED class ABI. Never guesses the DT-provided LED name.
// root is fixed by the board service; the argument allows isolated host tests.
std::unique_ptr<Switch> discoverRegulatorLed(const std::string& root, int* error);
// Production: requires advertised oneshot; never falls back to brightness-on.
std::unique_ptr<Switch> discoverTimedRegulatorLed(const std::string& root, int* error);
// C51C enumerating variant: walks EVERY LED class entry, so it needs broad
// sysfs_leds/other-LED access under SELinux. Diagnostics/tests only.
std::unique_ptr<Switch> discoverDriverTimedRegulatorLed(const std::string& root, int* error);
// C51D production path: opens one canonical platform device (O_PATH, no LED
// enumeration), requires its driver binding to be regulator_vibrator, opens
// leds/<label> without following a symlink, then requires timeout ABI v1.
std::unique_ptr<Switch> openDriverTimedRegulatorLed(const std::string& device, const std::string& label,
                                                    int* error);

class Controller final {
  public:
    using ErrorHandler = std::function<void(int)>;
    explicit Controller(std::unique_ptr<Switch> device, ErrorHandler onAsyncError = {});
    ~Controller();
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;

    int on(int timeoutMs);
    int off();
    int maxDurationMs() const;

  private:
    void run();
    std::unique_ptr<Switch> device_;
    ErrorHandler onAsyncError_;
    std::mutex mutex_;
    std::condition_variable changed_;
    bool stopping_ = false;
    int fault_ = 0;  // Re-enable requires an explicit successful off after I/O failure.
    std::optional<std::chrono::steady_clock::time_point> deadline_;
    std::thread worker_;
};
}  // namespace lieppos::vibrator
