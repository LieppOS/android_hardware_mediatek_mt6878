// Copyright 2026 The LieppOS Contributors
// SPDX-License-Identifier: Apache-2.0
#include <mtk_vibrator/Controller.h>

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

using lieppos::vibrator::Controller;
using lieppos::vibrator::Switch;
using lieppos::vibrator::discoverRegulatorLed;
namespace fs = std::filesystem;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)

struct State {
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<bool> accepted;
    int failOn = 0;
    int failOff = 0;
    std::thread::id failOffCaller{};
    int errors = 0;
    bool waitForOff() {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(3), [&] {
            return !accepted.empty() && !accepted.back();
        });
    }
};
struct MemorySwitch final : Switch {
    explicit MemorySwitch(std::shared_ptr<State> state) : s(std::move(state)) {}
    int set(bool value) override {
        std::lock_guard lock(s->mutex);
        int& failures = value ? s->failOn : s->failOff;
        if (failures && (value || s->failOffCaller == std::thread::id{} ||
                         s->failOffCaller == std::this_thread::get_id())) {
            --failures; return EACCES;
        }
        s->accepted.push_back(value);
        s->changed.notify_all();
        return 0;
    }
    std::shared_ptr<State> s;
};
std::unique_ptr<Switch> memory(const std::shared_ptr<State>& state) {
    return std::make_unique<MemorySwitch>(state);
}
void put(const fs::path& file, const std::string& value) { std::ofstream(file) << value; }
std::string get(const fs::path& file) {
    std::ifstream in(file);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
fs::path led(const fs::path& root, const std::string& name, const std::string& driver = "regulator_vibrator") {
    const auto entry = root / name;
    fs::create_directories(entry / "device");
    fs::create_symlink("../../drivers/" + driver, entry / "device/driver");
    put(entry / "max_brightness", "255\n");
    put(entry / "brightness", "0\n");
    return entry;
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;  // Caller creates/removes this owned temporary directory.
    const fs::path base = argv[1];
    int passed = 0;
    const auto test = [&](const std::string& name, const std::function<void()>& body) {
        try { body(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { std::cerr << "FAIL " << name << ": " << e.what() << '\n'; throw; }
    };
    try {
        test("invalid timeouts never write", [&] {
            auto s = std::make_shared<State>();
            Controller c(memory(s));
            CHECK(c.on(0) == EINVAL); CHECK(c.on(-1) == EINVAL);
            CHECK(s->accepted.empty());
        });
        test("timed on stops and explicit off is idempotent", [&] {
            auto s = std::make_shared<State>(); Controller c(memory(s));
            CHECK(c.on(20) == 0); CHECK(s->waitForOff());
            CHECK(c.off() == 0); CHECK(c.off() == 0);
        });
        test("replacement duration cannot leave an old timer armed", [&] {
            auto s = std::make_shared<State>(); Controller c(memory(s));
            CHECK(c.on(2000) == 0); CHECK(c.on(20) == 0); CHECK(s->waitForOff());
        });
        test("write errors propagate and an existing stop remains armed", [&] {
            auto s = std::make_shared<State>(); Controller c(memory(s));
            CHECK(c.on(20) == 0);
            { std::lock_guard lock(s->mutex);
              s->failOn = 1; s->failOff = 1;
              // The new immediate safety worker must not consume the explicit
              // caller's injected error nondeterministically.
              s->failOffCaller = std::this_thread::get_id(); }
            CHECK(c.on(2000) == EACCES); CHECK(c.off() == EACCES); CHECK(s->waitForOff());
            auto firstFailure = std::make_shared<State>(); firstFailure->failOn = 1;
            Controller safe(memory(firstFailure));
            CHECK(safe.on(20) == EACCES); CHECK(firstFailure->waitForOff());
        });
        test("timer off failure reports and retries instead of fake success", [&] {
            auto s = std::make_shared<State>(); s->failOff = 1;
            Controller c(memory(s), [s](int error) {
                CHECK(error == EACCES);
                std::lock_guard lock(s->mutex); ++s->errors;
            });
            CHECK(c.on(20) == 0); CHECK(s->waitForOff()); CHECK(s->errors == 1);
        });
        test("destruction joins worker and writes safety off", [&] {
            auto s = std::make_shared<State>();
            { Controller c(memory(s)); CHECK(c.on(200000) == 0); }
            CHECK(s->accepted.size() == 2); CHECK(!s->accepted.back());
        });
        test("concurrent calls serialize and terminate cleanly", [&] {
            auto s = std::make_shared<State>(); Controller c(memory(s));
            std::vector<std::thread> threads;
            for (int i = 0; i < 4; ++i) threads.emplace_back([&] {
                for (int j = 0; j < 25; ++j) { CHECK(c.on(2000) == 0); CHECK(c.off() == 0); }
            });
            for (auto& t : threads) t.join();
            CHECK(c.off() == 0); CHECK(!s->accepted.back());
        });
        test("discovers DT label by exact driver and writes actual attribute", [&] {
            const auto root = base / "discovery"; fs::create_directory(root);
            const auto actual = led(root, "not-a-hardcoded-vibrator-label");
            led(root, "backlight", "different_driver");
            int error = 0; auto device = discoverRegulatorLed(root.string(), &error);
            CHECK(device && error == 0); CHECK(device->set(true) == 0);
            CHECK(get(actual / "brightness") == "1\n");
            CHECK(device->set(false) == 0); CHECK(get(actual / "brightness") == "0\n");
        });
        test("real LED class symlink accepted but leaf symlink rejected", [&] {
            const auto root = base / "links"; fs::create_directory(root);
            const auto target = led(base, "physical-device");
            fs::create_directory_symlink(target, root / "class-link");
            int error = 0; auto device = discoverRegulatorLed(root.string(), &error);
            CHECK(device && error == 0);
            fs::remove(target / "brightness"); put(target / "other", "untouched");
            fs::create_symlink("other", target / "brightness");
            CHECK(device->set(true) == ELOOP); CHECK(get(target / "other") == "untouched");
        });
        test("missing wrong and ambiguous devices fail closed", [&] {
            int error = 0;
            CHECK(!discoverRegulatorLed((base / "missing").string(), &error)); CHECK(error == ENOENT);
            const auto root = base / "ambiguity"; fs::create_directory(root);
            led(root, "wrong", "other");
            CHECK(!discoverRegulatorLed(root.string(), &error)); CHECK(error == ENODEV);
            led(root, "one"); led(root, "two");
            CHECK(!discoverRegulatorLed(root.string(), &error)); CHECK(error == EEXIST);
        });
        test("malformed maximum and disappeared attributes return errors", [&] {
            const auto root = base / "malformed"; fs::create_directory(root);
            const auto entry = led(root, "actuator"); int error = 0;
            for (const auto* value : {"0\n", "-1\n", "", "1\njunk", "99999999999999999999999999999999999999"}) {
                put(entry / "max_brightness", value);
                CHECK(!discoverRegulatorLed(root.string(), &error)); CHECK(error == EINVAL);
            }
            put(entry / "max_brightness", std::string(66, '1'));
            CHECK(!discoverRegulatorLed(root.string(), &error)); CHECK(error == EOVERFLOW);
            put(entry / "max_brightness", "1\n");
            auto device = discoverRegulatorLed(root.string(), &error); CHECK(device);
            fs::remove(entry / "brightness"); CHECK(device->set(false) == ENOENT);
        });
        test("discovery releases descriptors on success and ambiguity", [&] {
            const auto count = [] { return std::distance(fs::directory_iterator("/proc/self/fd"), fs::directory_iterator{}); };
            const auto root = base / "descriptors"; fs::create_directory(root); led(root, "one");
            const auto before = count();
            for (int i = 0; i < 30; ++i) { int error; CHECK(discoverRegulatorLed(root.string(), &error)); }
            led(root, "two");
            for (int i = 0; i < 30; ++i) { int error; CHECK(!discoverRegulatorLed(root.string(), &error)); }
            CHECK(count() == before);
        });
    } catch (...) { return 1; }
    std::cout << "TOTAL " << passed << "/12\n";
    return passed == 12 ? 0 : 1;
}
