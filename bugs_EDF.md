# bugs_EDF.md 

This is the current list of known defects and soft spots in the EDF
implementation. Items are ordered roughly by severity.

---

## 1. Functional bugs

### 1.1 DBF horizon cap can hide true infeasibility

`configEDF_MAX_ANALYSIS_TICKS = 100 000` truncates the LCM horizon for
task sets whose hyperperiod exceeds 100 s. If an infeasibility only
shows up past that horizon, we admit incorrectly. This is a deliberate
trade-off for runtime cost on an M0+, but it is a correctness gap on
paper and worth recording.

**Mitigation.** For the three provided demos, the true LCM is always
≤ horizon, so the cap is never hit. In practice, utilization sums stay
small enough that exact DBF catches every intended reject well before
`t = horizon`.

### 1.2 Registry insertion is not rolled back if TCB allocation fails *after* admission passes

The admission function inserts the candidate into `xEDFTaskRegistryList`
(indirectly, via `prvAddTaskToReadyList`) only *after* `prvCreateTask`
succeeds. But if an allocation inside `prvCreateTask` (stack buffer)
fails, we correctly skip registry insertion, however we do **not**
decrement `uxEDFAcceptedTaskCount`. The counter can therefore drift
upward by at most the number of heap-failure events. No scheduling
consequence, only an observability bug.

### 1.3 Per-tick miss-detection cost is O(N)

`xTaskIncrementTick` scans every running/ready EDF TCB to check
deadlines. With 100 admitted tasks at 1 kHz tick, that's 100 compares
per ms which is measurable but tolerable on an RP2040 (sub-millisecond). Under
`main_edf_test_100`, CPU usage attributable to this scan is around
1–2%. Not a bug per se, but a scalability ceiling.

---

## 2. Behavioural edge cases

### 2.1 Zero-WCET tasks are not rejected at admission

If a user passes `C = 0`, the LL sum contribution is 0 and the DBF sum
contribution is 0; the admission always succeeds. That is technically
correct math, but probably a user error we should guard against.

### 2.2 `vTaskDelayUntilNextPeriod` assumes the caller's initial `xLastWake` matches `xLastReleaseTick`

If the task body sleeps/delays before entering its periodic loop, the
`xLastWake` passed to `vTaskDelayUntilNextPeriod` can drift away from
the TCB's internal `xLastReleaseTick`. The two are kept independent with
`xLastWake` driving the blocking delay, and `xLastReleaseTick` driving the
deadline math. Mismatches produce correct scheduling but misleading
traces (the `[EDF][release]` trace's deadline can lag the analyzer's
first pulse by a few ticks).
