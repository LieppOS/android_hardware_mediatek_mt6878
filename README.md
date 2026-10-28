# MT6878 vibrator backend — C51C, deployment CLOSED

Owner: `LieppOS/android_hardware_mediatek_mt6878`. Continues C51B
`1957904697f7eec67b42c3a46b9b86a5761da779`; native code remains independently
authored Apache-2.0, LieppOS Contributors (`LICENSE`). No proprietary userspace
implementation, firmware, calibration or generated Binder header is imported.

## Current production contract

`discoverDriverTimedRegulatorLed` identifies exactly one `regulator_vibrator` LED
without guessing its DT label. The retained directory fd, bounded reads,
max_brightness checks, attribute no-follow rules, complete-write checks and
propagated discovery failures remain. It requires **driver-owned timeout ABI1**:
`timeout_abi`, `max_duration_ms`, `duration_ms`.

`DriverTimed.cpp` sends one atomic decimal duration request or zero for OFF. It
never selects a generic trigger or writes brightness=1. Missing/unknown ABI,
malformed metadata, inaccessible/disappeared attributes and failed/partial writes
fail explicitly. Kernel-reported maximum propagates through Controller and Binder
validation; oversized requests produce illegal-argument without hardware I/O.
The kernel's current 5000ms ceiling is provisional software policy, not a thermal
rating. Whole hardware safety is not claimed.

The kernel owns expiry; no early competing user deadline is armed. Controller
fault latching, immediate cleanup after failed activation/OFF, bounded retry,
thread serialization, null-init handling and destructor stop remain. These are
best-effort lifecycle handling, not the process-death protection itself.

Legacy `discoverRegulatorLed` boolean and generic `discoverTimedRegulatorLed`
one-shot paths remain regression/diagnostic code only. **Neither is a fallback
from production.** The old oneshot-config-proposal.patch is marked superseded;
CONFIG_LEDS_TRIGGER_ONESHOT stays disabled in the actual unchanged C50 config.

Kernel source/ABI evidence and complete limits: [`vibrator/SAFETY.md`](vibrator/SAFETY.md).
Canonical implementation: kernel `af3ecd85d28614c2ad25c6448cd68634d62c6a33`;
module/private header synchronized canonical-first into the authorized Ulefone
provenance mirror, not a second build root.

## Validation and remaining gates

- HAL: original12 + expanded22 cases = **34/34** normal and ASan/UBSan;
  expanded **22/22 TSan**. Existing 94 checks plus two kernel-policy guards: **96/96 Python checks**.
- Actual kernel shared policy: **12/12** normal/ASan-UBSan/TSan plus four static
  ordering guards; host tests do not execute hrtimer/regulator kernel APIs.
- Actual single-module Kbuild, modpost and linkage PASS with matching Android
  Clang17; configuration unchanged, 32 CRC imports match core, six common
  callback KCFI preambles match, no exports. No kernel Image or phone test.
- Genuine frozen V2 AIDL signatures remain the selected Android contract;
  full Soong link/registration validation is delegated to the active C54 owner,
  whose shared outputs are not used concurrently by C51C.

System UID/domain need only metadata reads and stable duration_ms write, not
trigger/invert/delay/shot access. Actual node labels/ownership, physical duration
rating, regulator fault behavior, kernel installation, the stock Manager contract
and assembled Android compatibility still require owner validation. Do not flip
code/init gates or select the source service. No root/DAC/SELinux shortcut, new
repository, push, flash or phone operation. Exact owner instructions are in the
board README and existing tooling research handoff (C54 research remains owned
by DeepSeek).
