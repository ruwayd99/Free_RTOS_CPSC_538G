# future_EDF.md

A list of follow-up items that would make the EDF implementation
faster, more accurate, more ergonomic, or better tested. Roughly
ordered by impact.

---

## 1. Admission-control algorithm

### 1.1 Replace the integer DBF sweep with Quick Processor-demand Analysis (QPA)

The current `prvEDFAdmissionConstrained` walks every integer `t` in
`[min(D), horizon]`. QPA (Zhang & Burns, 2009) proves that only a
finite set of *deadline points* needs to be checked, shrinking the
number of evaluations from `horizon` down to ≈ number of jobs in the
interval. With QPA, admitting 100 tasks at `T = 8 000` would take
microseconds rather than seconds.

### 1.2 Cache the admitted aggregate DBF

Under QPA or the current sweep, every admission recomputes the demand
contribution of every already-admitted task. A small precomputed
"cumulative DBF table" would let us add/remove a single task in O(1)
amortised instead of O(N) per point.

### 1.3 Smarter horizon

For task sets where `U < 1`, the Baruah bound
`L = max{ D_max, (Σ (T_i - D_i) * U_i) / (1 - U) }` is often much
tighter than the hyperperiod. Using it would drop `configEDF_MAX_ANALYSIS_TICKS`
by an order of magnitude for most realistic workloads and remove the
correctness gap documented in `bugs_EDF.md §1.1`.

---

## 2. Scheduler internals

### 2.1 Priority-queue structure instead of linked list

`xEDFReadyList` is O(N) insertion worst case. Swapping it for a
binary heap or a Bounded-Preemption-Level array of linked lists (one
per SRP level, which is the right shape for Task 2) would make
`prvAddTaskToReadyList` O(log N) and reduce jitter.

### 2.2 Absolute-deadline overflow handling

`xAbsoluteDeadline` is a `TickType_t`. On a 32-bit tick at 1 kHz that's
~49 days to wrap. The current list comparator uses plain unsigned
comparison; an explicit wrap-safe compare (`(int32_t)(a - b) < 0`)
would make the scheduler correct across overflow.

### 2.3 Per-tick miss detection → sorted-deadline wakeup

Instead of scanning every EDF TCB on each tick, keep a second list
sorted by `xAbsoluteDeadline` and only check the head. This turns the
O(N) per-tick cost into O(1) amortised.

---

## 3. API and configuration

### 3.1 Pluggable overload policy

`prvEDFDropLateJob` is hard-coded to drop-late-job. A callback hook
(`pxEDFOverloadHandler`) would let applications choose abort /
demote-to-background / skip-to-next-period without recompiling the
kernel.

---

## 4. Testing and tooling

### 4.1 Property-based test for DBF

Generate random `(T, D, C)` tuples, check `prvEDFAdmissionConstrained`
against a slow reference implementation (explicit simulation over
hyperperiod). Target > 1 M random sets before shipping.

### 4.2 Coverage for the miss-detection path

The three provided tests do not ever exercise `prvEDFDropLateJob`
(they are all feasible). A dedicated `main_edf_test_miss` binary that
intentionally overruns one task and verifies the drop trace + the
other tasks' undisturbed schedule would close that gap.

---

## 5. Tracing and observability

### 5.1 Fixed-size ring-buffer trace sink

`edfTRACE` currently calls `configPRINTF` synchronously. On large
tests the UART backpressures the kernel. A ring buffer drained by a
low-priority task would remove that backpressure and preserve event
timing fidelity.

### 5.2 Compact binary trace format

The current `[EDF][finish] task=IMPL4 dl=1234 exec=800` line costs
~60 bytes per event. A binary format (event code + 3 × uint32) would
drop that to 13 bytes and let us trace at 10× the event rate. 

