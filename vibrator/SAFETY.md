# C51B timeout and lifecycle contract — deployment CLOSED

No phone testing. No kernel change applied. Kernel paths below are relative to
`android/kernel/ulefone/gq5012bf1/`. This is source evidence, not a claim about the
loaded device kernel or a hard real-time physical cutoff.

## Source findings

| Input | SHA-256 | Relevant contract |
|---|---|---|
| drivers/leds/regulator-vibrator.c | d6cec1e2d6a04d1fd31f8a128ec365999ac1382539395b53d9873a4c85a5ff44 | brightness callback 163–173 queues boolean state; no timer initialization/start despite an hrtimer include; regulator failures logged, not returned |
| drivers/leds/trigger/ledtrig-oneshot.c | 8fba950dd67867b11001877621609b71529a6ee976d6e9a12cd3d71c033cfbc0 | shot starts led_blink_set_oneshot; invert=0 ends OFF; delay_on/off are milliseconds; shot payload ignored |
| drivers/leds/led-core.c | c6478281d6cae1ff2552c7c130827413eba5d8eb6f38f46ff6cc1686fc55c828 | led_timer_function runs independently of userspace; non-inverted one-shot ends OFF; delay_off=0 is indefinite ON; a shot during a pending shot is ignored |
| drivers/leds/led-class.c | 7903f7f0a220a69ddafc56495c5dd489775476a298f7409d319361704f359acc | brightness_store(0) removes trigger, queues OFF and flushes LED-core brightness work (not the driver's separate workqueue) |
| drivers/leds/led-triggers.c | 25d85c1976e8b2efde512fcc5d49ba6a21eeddf6c41fbd511a4c199bd0657403 | trigger removal synchronizes LED work/timer and deactivates trigger; must cancel before replacement |
| arch/arm64/configs/gki_defconfig | 6416f396c578a92b57e94b30edefda1629a63ef6baeb9f94bd87832943a8cee5 | selects TIMER/TRANSIENT, not ONESHOT; not proof of final/deployed config |

No hardware auto-off/PMIC watchdog/timed-output guarantee was established by the
inspected driver/DT/regulator contracts. Over-current notification and suspend/
shutdown cleanup are not duration guarantees. A free-running timer trigger is not
one-shot expiry. No guessed register, DT label or voltage is used.

## Existing lower-layer mechanism; smallest kernel-owner proposal

`oneshot-config-proposal.patch` selects the **existing** one-shot trigger, not a
second custom timeout implementation. It is a review-only patch against the
hashed source, dry-run checked using a disposable single-file fixture, never
against the kernel worktree. Owner must use the effective product config and
verify CONFIG_LEDS_CLASS / CONFIG_LEDS_TRIGGERS and built-in ONESHOT availability. The current positive int32 API bound is finite
but is not a verified safe continuous-on duration. Owner must establish and
enforce an evidence-backed physical duration policy before opening deployment.
If module form is chosen instead, module load/provenance/KMI become additional
owner dependencies. Do not assume module autoload is present.

The HAL's timed protocol requires exactly one driver-identified LED with the
advertised `oneshot` trigger. It cancels the old shot with brightness=0, selects
oneshot and verifies `[oneshot]`, writes invert=0, requested positive delay_on,
**delay_off=1**, then shot=1 as the **last** transaction. Never emit brightness=1
in this path. Configuration failure cannot begin a new shot. Cancellation is
serialized with replacement. Each accepted shot has a finite kernel timer even
if userspace subsequently dies. The normal in-process deadline is used only by
the legacy diagnostic switch. It is not armed for a successful kernel-timed shot:
a user deadline measured before the kernel's first tick could shorten the pulse.
The worker still retries failed OFF/transactions; that is best-effort cleanup,
not the claimed process-death protection.

This is a **conditional source-level expiry argument**: correct loaded kernel,
stable bound device, exclusive authorized writer, running scheduler/workqueues
and functioning regulator OFF operation are assumptions. There is no physical
hard-deadline guarantee under kernel hangs, regulator faults or unbind/removal.
There is no deployed trigger/config or hardware proof. The service must remain
hard-gated and init-disabled until all owner gates pass.

## Failure model

| Scenario | Guarantee / remaining boundary |
|---|---|
| Normal completion | kernel one-shot queues OFF independently; no competing early user timer; no motion acknowledgment |
| Graceful shutdown | join worker, best-effort OFF; destructor alone is not fail-safe |
| Restart | mandatory accepted initial OFF before registration; old kernel shot expires independently if present |
| Binder-client death | callback not supported/retained; accepted finite shot continues to expiry; no invented client death ownership |
| Service termination, SIGKILL, userspace crash | legacy brightness timer fails; timed protocol's already-armed kernel shot survives process death under assumptions above |
| Concurrent commands / overlapping on/off | controller mutex serializes entire configuration transaction; cancel old shot before replacement; other sysfs writers must be denied |
| Failed enable / partial write | return errno/EIO, latch fault, arm immediate best-effort OFF; no repeated unsafe activation until explicit successful OFF |
| Failed OFF / active error | latch fault, retry OFF at 250ms; no success claim or unbounded hot loop; kernel expiry remains independent when successfully armed |
| Node disappearance | explicit I/O error; retained directory fd cannot silently discover a different actuator; physical OFF unknown |
| Partial initialization | missing/ambiguous/invalid device or trigger refuses startup; missing switch is ENODEV, not a null dereference |
| Worker termination / whole-process loss | no in-process cleanup guarantee; kernel shot only conditional lower-layer protection |

## Kernel owner review still required

The existing driver allocates a separate workqueue before later probe failures
but does not destroy it on those paths. `vib_remove` cancels its work **before**
LED unregister (which can queue OFF again), never destroys the queue or forces
regulator OFF. Suspend/shutdown can race queued enable work and regulator errors
are only logged. Do not claim hot-unplug/unbind/suspend safety from this code.
Before deployment, owner must review and fix/quiesce all command/notifier/trigger
producers, gate new requests, synchronize LED timer/core work, drain driver work,
perform checked OFF, unregister notifier and destroy the queue before releasing
state. Timer callbacks must never call sleeping regulator operations directly.
Lock ordering must avoid cancel/flush while holding a mutex needed by that work.
No unreviewed driver rewrite is applied or passed off as validated here.

Required owner regression tests: enable/rearm/cancel at expiry boundary; SIGKILL
at each configuration step and after shot; regulator enable/disable failures;
concurrent notifier/expiry; probe unwind at every allocation/registration step;
remove/unbind/suspend/shutdown during queued enable and timeout; KASAN/lockdep;
repeat requests must never resurrect an expired/cancelled generation. Real
hardware current/motion and deadline observations require separate authorization.

Host fault-injection and kill tests can test our transaction ordering and show
the legacy limitation; they **cannot** certify the real kernel or hardware.
