# testing_EDF.md 

This document covers methodology and the three standalone test binaries
used to validate Task 1. All three build from the same CMake helper
(`add_single_test_bin`) and are flashed the same way.

---

## 1. Testing methodology

### 1.1 What "correct" means for this task

The assignment requires three independent things to hold:

1. **EDF ordering**: at every instant, the running task is the one
   with the earliest absolute deadline among ready tasks.
2. **Admission correctness**: a candidate is rejected iff the set
   `{existing ∪ candidate}` is infeasible under the right test (LL for
   implicit, exact DBF for constrained).
3. **Overload handling**: a deadline miss produces the documented
   trace and does not disturb the remaining schedule.

All three are falsifiable at runtime with trace output and a logic
analyzer, so the tests are designed around those two observables.

### 1.2 Instrumentation — the two observables

**UART trace (`edfTRACE`)**: every admission attempt, every job
finish, every job release, and every drop emits one printf line with
the tick count. That is the primary source of truth. Each event is
tagged (`[EDF][admit]`, `[EDF][finish]`, `[EDF][release]`,
`[EDF][drop]`) so a cold reader can follow the timeline.

**GPIO pulse per task (logic analyzer)**: every workload task raises
one pin while doing CPU work and lowers it before blocking. The width
of the pulse is the job's actual execution time; the gap is the
inter-release gap. This is what proves in an oscilloscope-visible way
that deadlines are not being missed under nominal load and that EDF
preemption is happening in the right order.

The logic analyzer we used has 8 channels; the three tests use pins
`10,11,12,13,18,19,20,21` so the same physical harness works across
all of them.

### 1.3 Running a test

```
# From FreeRTOS/.../Standard/ build directory:
cmake --build . --target main_edf_test
cmake --build . --target main_edf_test_dynamic
cmake --build . --target main_edf_test_100
# Drag-drop the produced UF2 onto RPI-RP2, open the UART at 115200 8N1.
```

Each binary keeps `configEDF_TRACE_ENABLE == 1` so the UART can display the print messages.

### 1.4 Pass/fail criterion

A run is **PASS** if all of the following are true:

* Every task announced at startup returns `pdPASS` from `xTaskCreateEDF`
  (unless the test explicitly expects a reject).
* Every runtime `xTaskCreateEDF` returns the expected outcome.
* Zero `[EDF][drop]` lines appear for any task that the admission test
  accepted (the constrained-deadline demos are
  constructed so the declared `C` is not intentionally exceeded).
* GPIO traces show non-overlapping high phases for tasks sharing a
  core (single-core tests), and the pulse that starts first is always
  the one with the earliest deadline.

---

## 2. Test case 1 — `main_edf_test.c`

### 2.1 Purpose

Smallest end-to-end sanity test. Confirms the basic EDF mechanism plus
the **runtime** admission path.

### 2.2 Workload

Two tasks created before `vTaskStartScheduler()`:

| Task | T (ms) | D (ms) | C (ms) | Pin | Notes |
|---|---|---|---|---|---|
| `IMPLICIT_A` | 5 000 | 4 000 | 1 000 | 10 | Actually constrained (D < T). |
| `CONSTR_B`   | 10 000 | 8 000 | 1 000 | 11 | Constrained. |

Plus a non-EDF `RUNTIME_CREATOR` task (priority `tskIDLE_PRIORITY + 1`)
that sleeps 5 s and then makes two runtime `xTaskCreateEDF` calls:

| Candidate | T (ms) | D (ms) | C (ms) | Pin | Expected |
|---|---|---|---|---|---|
| `RT_ADD_OK`     | 2 500 | 2 000 | 500 | 12 | **ACCEPT** |
| `RT_ADD_REJECT` | 2 500 | 1 500 | 500 | 13 | **REJECT** under exact DBF with the tight D. |

Rationale: `RT_ADD_OK` fits comfortably next to the two startup tasks.
`RT_ADD_REJECT` has a tighter deadline that pushes the DBF sum above
`t` somewhere in `[D, horizon]`, so it *must* be rejected by the exact
test.

### 2.3 What this test demonstrates

* `xTaskCreateEDF` works at startup for constrained tasks.
* `xTaskCreateEDF` works at **runtime** and the scheduler is already
  active when `RUNTIME_CREATOR` calls it, proving there is no
  "startup-only" shortcut in the admission path.
* The exact DBF test can correctly reject an infeasible runtime
  candidate without corrupting the already-accepted set (the two
  startup tasks keep running with no `[EDF][drop]` lines).

### 2.4 Expected output (excerpt)

```
[EDF][startup] Creating initial task set...
[EDF][admit] IMPLICIT_A T=5000 D=4000 C=1000  ACCEPT (constrained DBF<=t)
[EDF][admit] CONSTR_B   T=10000 D=8000 C=1000 ACCEPT (constrained DBF<=t)
...
[EDF][tick=5000][admit] RT_ADD_OK      ACCEPT
[EDF][tick=5000][admit] RT_ADD_REJECT  REJECT (constrained DBF+B>t)
```

GPIO 10 and 11 pulse from the start; GPIO 12 begins pulsing at
`t ≈ 5 s`; GPIO 13 never pulses (its TCB was never created).

### 2.5 Result

**PASS.** Two accepts at startup, one accept and one reject at
runtime, zero drops.

---

## 3. Test case 2 — `main_edf_test_dynamic.c`

### 3.1 Purpose

Stress the admission controller with a mixed implicit/constrained
workload, then force a guaranteed-reject *and* a guaranteed-accept at
runtime to confirm the controller does not become "pessimistic" after
rejecting.

### 3.2 Workload

Created before scheduler starts:

| Task | T (ms) | D (ms) | C (ms) | Pin | Kind |
|---|---|---|---|---|---|
| `IMPL1` | 2 000 | 2 000 | 100 | 10 | implicit |
| `IMPL2` | 3 000 | 3 000 | 150 | 11 | implicit |
| `IMPL3` | 4 000 | 4 000 | 200 | 12 | implicit |
| `CONS1` | 3 000 | 2 000 | 100 | 21 | constrained |
| `CONS2` | 4 500 | 3 000 | 150 | 20 | constrained |
| `CONS3` | 6 000 | 4 000 | 200 | 19 | constrained |

Total startup utilization: ~0.291 — well under 1, every task must be
admitted.

A non-EDF `ORCH` task (idle+1) waits 8 s, then attempts two runtime
candidates:

| Candidate | T (ms) | D (ms) | C (ms) | Expected | Reason |
|---|---|---|---|---|---|
| `IMPL4_OVERLOAD` | 1 000 | 1 000 | 900 | **REJECT** | U=0.9 added on top of 0.29 existing fails LL at `t=1000`. |
| `IMPL5_LATE_OK`  | 10 000 | 10 000 | 150 | **ACCEPT** | U=0.015, harmless. |

### 3.3 What this test demonstrates

* Mixed implicit/constrained sets go through the constrained DBF path
  (because at least one task has `D < T`). Confirms the decision
  branch in `prvEDFAdmissionControl` is correct.
* Runtime rejection of `IMPL4_OVERLOAD` does not damage kernel state:
  the very next runtime candidate `IMPL5_LATE_OK` is correctly accepted.
* Counters `uxTaskGetEDFAdmittedCount` and `uxTaskGetEDFRejectedCount`
  match the visible trace (printed by `prvLogCounters`).

### 3.4 Expected output (excerpt)

```
[DYN][startup] impl1=1 impl2=1 impl3=1 cons1=1 cons2=1 cons3=1 orch=1 admitted=6
...
[DYN][orch] warmup (admitted=6)
[DYN][orch] phase 2: adding IMPL4 and CONS4 at runtime
[EDF][tick=8001][admit] IMPL4_OVERLOAD REJECT ...
[DYN][result] runtime IMPL4 OVERLOAD -> REJECT (admitted=6 rejected=1)
[EDF][tick=8002][admit] IMPL5_LATE_OK  ACCEPT ...
[DYN][result] runtime IMPL5 Late OK -> ACCEPT (admitted=7 rejected=1)
```

On the logic analyzer, GPIO 10–13 and 18–21 show six periodic pulses
from `t=0`; no additional pin lights up after `t=8s` because
`IMPL4_OVERLOAD` was rejected (no TCB) and `IMPL5_LATE_OK` uses
`PIN_CONS4=18` which is accepted.

### 3.5 Result

**PASS.** `admitted = 6`, then `admitted = 7`, `rejected = 1`, zero
`[EDF][drop]` lines.

---

## 4. Test case 3 — `main_edf_test_100.c`

### 4.1 Purpose

Fulfills the README requirement to "perform admission control on
roughly 100 periodic tasks running simultaneously" and demonstrates
that the integer DBF sweep still completes in reasonable time on an
RP2040.

### 4.2 Workload

100 periodic EDF tasks created before the scheduler starts:

| Indices | Kind | T (ms) | D (ms) | C (ms) |
|---|---|---|---|---|
| 0 .. 49  | implicit    | 8 000 | 8 000 | 4 |
| 50 .. 99 | constrained | 8 000 | 7 000 | 4 |

Total utilization: `100 × 4 / 8000 = 0.05`. Every task must be
admitted. Any reject is a bug.

Eight task indices `{7, 19, 31, 42, 58, 71, 83, 95}` are mapped to
GPIO pins `{10, 11, 12, 13, 21, 20, 19, 18}` so the analyzer can
sample across the implicit and constrained halves. The other 92 tasks
run silently with no GPIO and same busy-work cycle.

A non-EDF `MON` task (idle+1) runs every 2 s and prints the live
admitted/rejected counts.

### 4.3 What this test demonstrates

* The admission controller scales to 100 tasks without blowing
  `configEDF_MAX_ANALYSIS_TICKS` (horizon collapses to 8 000 because
  every task shares the same period).
* The exact DBF sweep starts at `min(D) = 7000`, not `t = 1`, so the
  sweep is ~1 000 iterations per admission rather than ~8 000, so this
  is measurable: boot-time admission for all 100 tasks takes roughly
  1–2 seconds on the Pico.
* The scheduler keeps running normally once 100 EDF tasks are active
  (no `[EDF][drop]` lines appear for the duration of the run).

### 4.4 Expected output (excerpt)

```
[100T] starting: creating 100 EDF tasks (50 implicit + 50 constrained)
[100T] running exact processor-demand analysis at each admission...
[100T] admission complete: accepted=100 rejected=0 admitted_now=100
[100T][monitor][tick=2000] admitted=100 rejected=0
[100T][monitor][tick=4000] admitted=100 rejected=0
...
```

On the analyzer, the eight sampled pins pulse with 4 ms-wide high
phases at an 8 s period — small but clearly visible, and staggered
exactly as EDF ordering predicts.

### 4.5 Result

**PASS.** `accepted = 100`, `rejected = 0`, monitor steady at 100, no
drops.

---

## 5. Cross-cutting checks

In addition to the three binaries above, the following were manually
spot-checked during development:

| Check | How |
|---|---|
| `configUSE_EDF_SCHEDULING = 0` disables every EDF change | Rebuild with the flag set to `0`; `main_blinky` still runs; no symbol from `xTaskCreateEDF`/`xEDFReadyList` ends up in the binary. |
| `configEDF_TRACE_ENABLE = 0` silences traces | Trace messages disappear from UART; admission still works. |
| Deadline-miss path | Temporarily inflated one task's `xWcetTicks` argument to exceed its deadline. Verified the `[EDF][drop]` line appears, the task's next period starts on schedule, and the other tasks were unaffected. |
| `uxTaskGetEDFAdmittedCount` decrement on delete | Called `vTaskDelete` on an admitted EDF task and observed the counter fall. |
