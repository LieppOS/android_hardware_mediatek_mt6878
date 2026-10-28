// Copyright 2026 The LieppOS Contributors
// SPDX-License-Identifier: Apache-2.0
#include <mtk_vibrator/Controller.h>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <charconv>
#include <string_view>
#include <utility>

namespace lieppos::vibrator {
namespace {
class Fd {
  public:
    explicit Fd(int fd) : fd_(fd) {}
    ~Fd() { if (fd_ >= 0) close(fd_); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    int get() const { return fd_; }
    int release() { return std::exchange(fd_, -1); }
  private:
    int fd_;
};

struct CloseDirectory {
    void operator()(DIR* directory) const { closedir(directory); }
};

class LedSwitch final : public Switch, public LedAttributes {
  public:
    explicit LedSwitch(int directory) : directory_(directory) {}
    int set(bool enabled) override { return write("brightness", enabled ? "1\n" : "0\n"); }
    int write(const char* name, const std::string& value) override {
        Fd file(openat(directory_.get(), name, O_WRONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
        if (file.get() < 0) return errno;
        struct stat st {};
        if (fstat(file.get(), &st) < 0) return errno;
        if (!S_ISREG(st.st_mode)) return EINVAL;
        ssize_t written;
        do { written = ::write(file.get(), value.data(), value.size()); } while (written < 0 && errno == EINTR);
        if (written < 0) return errno;
        // Sysfs store() expects one entire attribute transaction. Never append a
        // second partial write (which could be interpreted as a new command).
        return static_cast<size_t>(written) == value.size() ? 0 : EIO;
    }
    int read(const char* name, std::string* value) override {
        if (!value) return EINVAL;
        Fd file(openat(directory_.get(), name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
        if (file.get() < 0) return errno;
        struct stat st {};
        if (fstat(file.get(), &st) < 0) return errno;
        if (!S_ISREG(st.st_mode)) return EINVAL;
        char buffer[4097];
        size_t used = 0;
        while (used < sizeof(buffer)) {
            ssize_t count = ::read(file.get(), buffer + used, sizeof(buffer) - used);
            if (count < 0) { if (errno == EINTR) continue; return errno; }
            if (!count) break;
            used += static_cast<size_t>(count);
        }
        if (used == sizeof(buffer)) return EOVERFLOW;
        value->assign(buffer, used);
        return 0;
    }
  private:
    Fd directory_;
};

int validMaximum(int directory) {
    Fd file(openat(directory, "max_brightness", O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
    if (file.get() < 0) return errno;
    struct stat st {};
    if (fstat(file.get(), &st) < 0) return errno;
    if (!S_ISREG(st.st_mode)) return EINVAL;
    char buffer[65];
    size_t used = 0;
    while (used < sizeof(buffer)) {
        ssize_t count = read(file.get(), buffer + used, sizeof(buffer) - used);
        if (count < 0) { if (errno == EINTR) continue; return errno; }
        if (!count) break;
        used += static_cast<size_t>(count);
    }
    if (used == sizeof(buffer)) return EOVERFLOW;
    std::string_view text(buffer, used);
    constexpr std::string_view whitespace = " \t\r\n";
    const auto begin = text.find_first_not_of(whitespace);
    if (begin == std::string_view::npos) return EINVAL;
    const auto end = text.find_last_not_of(whitespace);
    text = text.substr(begin, end - begin + 1);
    unsigned int maximum = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), maximum);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || maximum < 1) return EINVAL;
    return 0;
}
}  // namespace

static std::unique_ptr<LedSwitch> discoverDevice(const std::string& root, int* error) {
    if (!error) return nullptr;
    *error = 0;
    DIR* entries = opendir(root.c_str());
    if (!entries) { *error = errno; return nullptr; }
    std::unique_ptr<DIR, CloseDirectory> directory(entries);
    std::unique_ptr<LedSwitch> selected;
    for (;;) {
        errno = 0;
        const auto* entry = readdir(entries);
        if (!entry) { if (errno) { *error = errno; return nullptr; } break; }
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        // LED class entries intentionally link to real sysfs devices. Only the
        // fixed trusted class root is used by production; no Binder path input.
        Fd led(openat(dirfd(entries), name.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
        if (led.get() < 0) {
            if (errno == ENOENT || errno == ENODEV) continue;  // Hot removal.
            *error = errno; return nullptr;  // Never hide an unreadable second candidate.
        }
        char target[4096];
        const ssize_t count = readlinkat(led.get(), "device/driver", target, sizeof(target));
        if (count < 0) {
            if (errno == ENOENT || errno == ENODEV || errno == EINVAL) continue;
            *error = errno; return nullptr;
        }
        if (count == static_cast<ssize_t>(sizeof(target))) { *error = EOVERFLOW; return nullptr; }
        if (count == 0) continue;
        std::string_view driver(target, static_cast<size_t>(count));
        driver = driver.substr(driver.find_last_of('/') + 1);
        if (driver != "regulator_vibrator") continue;
        if (selected) { *error = EEXIST; return nullptr; }  // Never drive an arbitrary match.
        *error = validMaximum(led.get());
        if (*error) return nullptr;
        selected = std::make_unique<LedSwitch>(led.release());
    }
    if (!selected) *error = ENODEV;
    return selected;
}

std::unique_ptr<Switch> discoverRegulatorLed(const std::string& root, int* error) {
    return discoverDevice(root, error);  // Legacy diagnostic path only.
}

std::unique_ptr<Switch> discoverTimedRegulatorLed(const std::string& root, int* error) {
    auto attributes = discoverDevice(root, error);
    if (!attributes) return nullptr;
    return makeOneShotSwitch(std::move(attributes), error);
}

std::unique_ptr<Switch> discoverDriverTimedRegulatorLed(const std::string& root, int* error) {
    auto attributes = discoverDevice(root, error);
    if (!attributes) return nullptr;
    return makeDriverTimedSwitch(std::move(attributes), error);
}
}  // namespace lieppos::vibrator
