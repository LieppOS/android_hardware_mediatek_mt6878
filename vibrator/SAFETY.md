# C51C driver-owned timeout — deployment CLOSED

C51B's generic one-shot proposal is **superseded**, not applied. The effective
C48/C50 recipe uses `configs/c44-reference.config`, not gki_defconfig. Its SHA256
is `40de208ba474abfac862ada26301d5a8b26490141e8c944ed303a3ccce8fd2fc`:
REGULATOR_VIBRATOR=m, ONESHOT unset, CFI_CLANG/MODVERSIONS=y. C51C preserves it.

## Choice and actual implementation

Generic `ledtrig-oneshot` exists and has finite non-inverted shots, but enabling
it alone does not fix the old regulator driver's teardown, swallowed errors,
unbounded brightness requests, physical-duration policy or attributes recreated
on each trigger switch. Option B was therefore selected: **one driver-owned
hrtimer**, not both kernel approaches. Generic LED trigger code is unchanged.

Canonical implementation commit:
`af3ecd85d28614c2ad25c6448cd68634d62c6a33` in
`android/kernel/ulefone/gq5012bf1`. ABI documentation:
`Documentation/ABI/testing/sysfs-class-led-regulator-vibrator`.
The user explicitly confirmed exclusive kernel/mirror ownership. Only the affected
driver/private header were synchronized into the existing Ulefone provenance
mirror; no independent competing source owner or checkout was created.

Stable LED attributes, created with the class device before its add event:

- timeout_abi: read-only `1`.
- max_duration_ms: read-only `5000` (provisional SOFTWARE protection policy, **not
  an established physical/thermal rating**).
- duration_ms: atomic decimal 1..maximum request, 0 cancel/acknowledge recovered
  fault; read requested remaining ms, not measured motion/current.
- last_error: last negative errno; explicit successful OFF acknowledges it.

A request detaches any old generic trigger and flushes its final LED-core OFF
work **before** arming the new request. CLOCK_BOOTTIME expiry is armed before
regulator activation. Timer/notifier callbacks only queue work/set an atomic
fault; sleeping regulator operations are mutex-serialized in thread context.
Queued work checks the current absolute deadline, so old expiry cannot cancel a
new request. Every legacy nonzero brightness request also has the finite ceiling.
Other privileged periodic writers can request repeated pulses; they must not be
allowed to compete with the HAL's dedicated atomic control.

## Lifetimes, errors and guarantees

- Validated duration/overflow errors leave an existing request unchanged.
- Expiry survives userspace thread/client/process/SIGKILL loss because the bound
  driver owns the timer and work. No user timer/destructor/watchdog is credited.
- Enable/disable errors propagate. Failed OFF cancels logical activation, latches
  re-enable and retries at 250ms while bound/running. Only explicit accepted OFF
  clears the fault. Enable refs are balanced; a shared/always-on rail is reported
  EBUSY rather than force-disabled or falsely reported OFF.
- Suspend detaches/drains LED work, blocks ON, cancels expiry and checks OFF.
  Failure aborts that suspend callback and keeps fault/retry behavior; success
  permits suspend. Resume never resurrects the old request.
- Remove/probe-unwind/shutdown gate producers, cancel timer, synchronously remove
  notifier and LED callbacks, cancel timer/work again, then check OFF with three
  bounded attempts. No mutex needed by work is held during cancel_work_sync.
  Dedicated workqueue allocation/leak is eliminated.

This is a **source-supported expiry/OFF-attempt guarantee under a running kernel,
normal device lifetime and functioning regulator APIs**, NOT a physical hard
cutoff. Persistent PMIC/I2C failure, another rail consumer, kernel starvation/hang
or forced removal cannot be turned into motion/current OFF by software. Remove
errors do not veto Linux unbind. Such failures are returned/logged critically,
not hidden. Physical behavior, safe duration rating, repeated-fault/remove/suspend
behavior and the actual installed kernel remain unverified. Deployment CLOSED.

## HAL compatibility and permissions

Production uses `discoverDriverTimedRegulatorLed` / DriverTimed.cpp only. Requires
exact driver identity, timeout ABI1 and valid finite maximum; missing/unknown
metadata fails startup with no brightness/one-shot fallback. One duration write
arms the kernel; OFF writes duration_ms=0. Bounds propagate into both Controller
and Binder argument validation. Legacy/one-shot paths remain diagnostics/tests.
Capabilities remain zero; unsupported haptics are not advertised.

C54 owner needs only stable metadata/last_error reads and duration_ms write for
the system UID and hal_vibrator_default domain. No trigger/invert/delay/shot or
brightness write permission is required by the production path. Actual DT label,
real-device path, ownership and SELinux labels remain unknown; no guessed rules,
root UID, DAC override or broad sysfs grant is added. Keep stock selected and the
new module/fragment unselected; code+init gates remain closed. Resolve the stock
IVibratorManager contract before replacing its whole fragment.

## Actual validation (offline only)

Targeted real Kbuild/modpost/link of regulator-vibrator.ko passes with Android
clang17 r487747c (SHA256
`25617ca38946d0e7476025e6fa4c15fb386894b75d9372e63a9f3cec47988ce0`).
32/32 imported CRCs match C50 core, all 13 common CRCs and six existing callback
KCFI preambles match; no new exports, same vermagic/dependencies. Configuration
is byte-identical. No Image/full module cohort was rebuilt; baseline Image stays
`585080a838140a2c73c8caaae27c60ea6f360c926dc1b25ebc0e3a52c1f3a3d0`.
The new module intentionally differs: SHA256
`039526f98cc1dc560f372f2104cb5e4bcafe0885359595dad96990e80e489f45`.

Kernel shared policy: 12/12 normal/ASan-UBSan/TSan; four static ordering guards.
These execute actual portable policy code, **not Linux timers/regulators**.
No KUnit kernel execution, ARM64 VM/device load or physical SIGKILL test occurred.
See the existing research handoff for complete test/build receipts and the
explicitly corrected initial ELF extraction-tool side effect. No push/phone use.
