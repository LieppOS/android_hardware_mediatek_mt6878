# MT6878 Android hardware source — C51B

Canonical owner: `LieppOS/android_hardware_mediatek_mt6878`. Continues C51
`d8a26a85b00a5db382185150b2be148d8bade307`, not another HAL service or source copy.
Native code is **INDEPENDENT**, LieppOS Contributors, Apache-2.0 (`LICENSE`).
No proprietary userspace library, firmware or calibration payload was imported.

## Two deliberately distinct paths

- `discoverRegulatorLed`: original boolean brightness backend, retained for
  diagnostics/regression tests only. A userspace timer cannot survive process
  death. **Not used by the production entrypoint.**
- `discoverTimedRegulatorLed`: exact same driver-identity discovery plus a required
  Linux LED `oneshot` trigger. It **never falls back** to indefinite brightness-on.
  Source: `vibrator/OneShot.cpp`; complete lower-layer evidence/failure model and
  kernel-owner proposal: [`vibrator/SAFETY.md`](vibrator/SAFETY.md).

Driver `regulator_vibrator` is verified in the authorized kernel source
`drivers/leds/regulator-vibrator.c` (SHA-256
`d6cec1e2d6a04d1fd31f8a128ec365999ac1382539395b53d9873a4c85a5ff44`).
LED label comes from DT. Discovery accepts exactly one matching device, verifies
numeric max_brightness, pins its directory fd, permits normal kernel class links,
and refuses attribute symlinks/nonregular nodes. Unexpected discovery read/open
errors are propagated rather than hiding an unreadable second candidate.
No label, voltage, calibration path, ioctl or PMIC watchdog is guessed.

The timed protocol serializes: brightness=0 (cancel old trigger), trigger=oneshot,
verify `[oneshot]`, invert=0, delay_on=requested milliseconds, delay_off=1,
shot=1 **last**. delay_off=0 would mean indefinite ON in this kernel and is never
used. Repeated requests cancel before rearming; a pending kernel shot otherwise
ignores replacement. Accepted shot expiry is kernel-owned, conditional on the
verified running kernel/trigger and functioning scheduler/workqueue/regulator.
Success means a complete attribute transaction was accepted, not motion feedback.

## Corrections and safety limits

Controller requests are mutex-serialized. Nonpositive duration is EINVAL; the
positive signed-32-bit API range is finite (max 2147483647 ms), **not a claimed
safe physical continuous-vibration limit**. Failed activation arms an immediate
OFF instead of retaining a distant deadline. Failed OFF without a prior deadline
now arms retry. Any I/O failure latches re-enable until an explicit successful OFF;
timer cleanup success alone does not acknowledge/release the fault. Retries are
250 ms apart; null partial initialization returns ENODEV. Shutdown joins its worker
and attempts OFF. These are best-effort behaviors, not SIGKILL safety.

A successful kernel shot has no competing normal userspace deadline: kernel
activation starts on a later tick, so an earlier user timer could shorten the
pulse. The legacy diagnostic path retains its original user deadline.

**Deployment CLOSED:** inspected GKI defconfig does not select ONESHOT; loaded
trigger/config, actual node labels/permissions and physical behavior are unknown.
The review-only `vibrator/oneshot-config-proposal.patch` selects existing kernel
code, not a new driver ABI; fixture-only dry-run passes. Kernel owner must also
resolve/review driver workqueue probe unwind, remove/unbind, notifier, suspend and
shutdown lifetime races described in SAFETY.md. No kernel worktree was changed.

## Tests and build evidence

- Original `vibrator/tests/ControllerTest.cpp`: **12/12** normally and ASan/UBSan.
  Existing error expectations remain; explicit caller fault injection is now
  deterministic against the new immediate safety worker.
- Board-owned `hardware/ulefone/gq5012bf1/vibrator/tests/LifecycleTest.cpp`:
  **17/17** normally, ASan/UBSan and TSan. Uses the actual controller/request
  policy with clearly fake sysfs attributes, including concurrency, every timed
  transaction failure, cancellation, fault recovery, partial initialization and
  SIGTERM/SIGKILL/SIGABRT child-process simulations of the **legacy limitation**.
- Existing audit runner: **94/94 Python checks**, original 90 preserved.
- Genuine platform SDK37 aidl generated frozen V2 headers; Android Clang22/Bionic
  compiled all five backend/adapter/main sources to **arm64 objects** with vendor
  and VNDK macros, no exceptions/RTTI, C++17 and Werror. **Not linked or Soong-built.**
- Targeted Soong invocation reached bootstrap but failed on missing
  `build/soong/config/main.star`; DeepSeek C53 owns recovery. No shared output was
  reused or platform source modified. Compiler/test outputs are removed from the
  audit runner's existing ignored `.tmp/`.

Native tests never access real `/sys`; mocks do not prove kernel or hardware
expiry. Whole kernel/PMIC/firmware support and physical safety are not certified
open or functional by this userspace source claim. No push or phone operation.
