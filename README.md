# MT6878 Android hardware source — C51

Canonical owner: `LieppOS/android_hardware_mediatek_mt6878`.

`libmtk_regulator_vibrator` is an independently authored Apache-2.0 native backend for the actual MediaTek regulator-vibrator LED-class interface. It is used by the single GQ5012BF1 AIDL adapter in `hardware/ulefone/gq5012bf1/vibrator/`; it is not another HAL service.

## Verified contract, not guessed paths

Read-only evidence in the existing kernel tree (`kernel/ulefone/gq5012bf1/`):

- `drivers/leds/regulator-vibrator.c`, byte SHA-256 `d6cec1e2d6a04d1fd31f8a128ec365999ac1382539395b53d9873a4c85a5ff44`.
- Lines 19, 163–173, 176–177, 231–240: platform driver name `regulator_vibrator`, nonzero/zero LED brightness queues on/off, LED name comes from the DT `label`, LED class registration uses the platform device parent.
- The documented C46 `slotb-dmesg.txt` (SHA-256 `8994a8516f527dd90215452f4354ddbe54d1faee18e4963f13e93f64a9732c81`) records driver enable/disable at lines 170/191. This is historical driver evidence, **not verification of this new HAL**.

The backend enumerates the Linux `/sys/class/leds` ABI and accepts exactly one entry whose `device/driver` symlink names `regulator_vibrator`. It verifies numeric `max_brightness >= 1`, then writes only `1\n` or `0\n` to `brightness`. No DT LED label, voltage, calibration, timed_output path or ioctl is invented. Class/device symlinks are expected sysfs ABI; final attribute symlinks are rejected. The board service fixes the root; Binder clients cannot supply paths.

The driver queues work and does not propagate regulator operation failures through brightness writes. Therefore success means **the kernel accepted the complete attribute write**, not measured motor motion. Missing/ambiguous devices, invalid attributes, permissions and write errors fail explicitly. Partial sysfs writes are errors, never continued as a second command.

## Timing and safety

A monotonic condition-variable worker serializes on/off and timer replacement. Nonpositive durations are rejected before I/O. Failed enables arm a safety stop if needed. Failed explicit/timer stops do not silently disarm an existing deadline; timer stop errors are reported and retried every 250 ms. Shutdown joins the worker and attempts off. No callback/amplitude/effect capability is inferred.

**Deployment blocker:** the verified driver has no kernel timeout in this interface. Process death can leave the actuator enabled until the source service restarts and successfully performs its mandatory initial off, or its kernel suspend/shutdown handlers attempt off. A service restart is not a guaranteed physical timeout. Do not activate without owner-approved crash/restart and power-safety validation or an existing verified kernel fail-safe. No kernel change is made here. Native tests cannot establish this hardware property.

## Builds/tests and dependencies

- Soong definition: root `Android.bp`, C++17 static library; no proprietary userspace library.
- Host behavior suite: `vibrator/tests/ControllerTest.cpp`, 12 substantive cases, compiled/run natively and with AddressSanitizer + UndefinedBehaviorSanitizer by the opt-in C51 tests in `tools/ulefone/gq5012bf1/hal-audit/tests/test_c51_source.py`.
- Native compiler: GCC 16.2.1, Linux x86_64; flags `-std=c++17 -pthread -Wall -Wextra -Werror -O1 -g`, plus sanitizer flags for the second run. Temporary outputs are owned by the audit test runner and removed.
- No full Android/Soong build was possible in the canonical partial checkout. This is not a Binder/VTS or hardware build result.

Origin: **INDEPENDENT**, LieppOS Contributors, Apache-2.0 (`LICENSE`). No kernel implementation bodies, proprietary executable contents, firmware or calibration data were imported. Kernel/hardware support remains outside the scope of this userspace source availability claim.
