// Copyright 2026 The LieppOS Contributors
// SPDX-License-Identifier: Apache-2.0
#include <mtk_vibrator/Controller.h>

#include <cerrno>
#include <charconv>
#include <utility>

namespace lieppos::vibrator {
namespace {
int number(LedAttributes& attributes, const char* name, int* value) {
    std::string text;
    const int error = attributes.read(name, &text);
    if (error) return error;
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return EINVAL;
    const auto last = text.find_last_not_of(" \t\r\n");
    const char* begin = text.data() + first;
    const char* end = text.data() + last + 1;
    const auto parsed = std::from_chars(begin, end, *value);
    return parsed.ec == std::errc{} && parsed.ptr == end && *value >= 0 ? 0 : EINVAL;
}

class DriverTimed final : public Switch {
  public:
    DriverTimed(std::unique_ptr<LedAttributes> attributes, int maximum)
        : attributes_(std::move(attributes)), maximum_(maximum) {}
    bool usesKernelTimerProtocol() const override { return true; }
    int maxDurationMs() const override { return maximum_; }
    int set(bool enabled) override {
        return enabled ? EOPNOTSUPP : attributes_->write("duration_ms", "0\n");
    }
    int start(int durationMs) override {
        if (durationMs <= 0 || durationMs > maximum_) return EINVAL;
        // Atomic kernel request: no trigger selection or dynamic attributes.
        return attributes_->write("duration_ms", std::to_string(durationMs) + "\n");
    }
  private:
    std::unique_ptr<LedAttributes> attributes_;
    const int maximum_;
};
}  // namespace

std::unique_ptr<Switch> makeDriverTimedSwitch(std::unique_ptr<LedAttributes> attributes, int* error) {
    if (!error) return nullptr;
    *error = ENODEV;
    if (!attributes) return nullptr;
    int version = 0, maximum = 0, remaining = 0;
    *error = number(*attributes, "timeout_abi", &version);
    if (*error) return nullptr;
    if (version != 1) { *error = EOPNOTSUPP; return nullptr; }
    *error = number(*attributes, "max_duration_ms", &maximum);
    if (*error) return nullptr;
    if (!maximum) { *error = EINVAL; return nullptr; }
    *error = number(*attributes, "duration_ms", &remaining);
    if (*error) return nullptr;
    if (remaining > maximum) { *error = EINVAL; return nullptr; }
    return std::make_unique<DriverTimed>(std::move(attributes), maximum);
}
}  // namespace lieppos::vibrator
