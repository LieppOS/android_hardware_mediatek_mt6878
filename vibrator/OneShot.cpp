// Copyright 2026 The LieppOS Contributors
// SPDX-License-Identifier: Apache-2.0
#include <mtk_vibrator/Controller.h>

#include <cerrno>
#include <sstream>
#include <utility>

namespace lieppos::vibrator {
namespace {
bool hasToken(const std::string& text, const char* token) {
    std::istringstream input(text);
    std::string word;
    while (input >> word) if (word == token) return true;
    return false;
}

class OneShot final : public Switch {
  public:
    explicit OneShot(std::unique_ptr<LedAttributes> attributes) : attributes_(std::move(attributes)) {}
    bool usesKernelTimerProtocol() const override { return true; }
    int set(bool enabled) override {
        // No duration means no safe activation. OFF also cancels/removes the
        // old kernel trigger, allowing replacement rather than an ignored shot.
        return enabled ? EOPNOTSUPP : attributes_->write("brightness", "0\n");
    }
    int start(int timeoutMs) override {
        if (timeoutMs <= 0) return EINVAL;
        int error = set(false);
        if (error) return error;
        error = attributes_->write("trigger", "oneshot\n");
        if (error) return error;
        std::string active;
        error = attributes_->read("trigger", &active);
        if (error) return error;
        if (!hasToken(active, "[oneshot]")) return EOPNOTSUPP;
        error = attributes_->write("invert", "0\n");
        if (error) return error;
        error = attributes_->write("delay_on", std::to_string(timeoutMs) + "\n");
        if (error) return error;
        // Zero delay_off in led-core means indefinite ON, not immediate OFF!
        error = attributes_->write("delay_off", "1\n");
        if (error) return error;
        // Last transaction: only kernel timer code starts this finite pulse.
        return attributes_->write("shot", "1\n");
    }
  private:
    std::unique_ptr<LedAttributes> attributes_;
};
}  // namespace

std::unique_ptr<Switch> makeOneShotSwitch(std::unique_ptr<LedAttributes> attributes, int* error) {
    if (!error) return nullptr;
    *error = ENODEV;
    if (!attributes) return nullptr;
    std::string triggers;
    *error = attributes->read("trigger", &triggers);
    if (*error) return nullptr;
    if (!hasToken(triggers, "oneshot") && !hasToken(triggers, "[oneshot]")) {
        *error = EOPNOTSUPP;
        return nullptr;
    }
    return std::make_unique<OneShot>(std::move(attributes));
}
}  // namespace lieppos::vibrator
