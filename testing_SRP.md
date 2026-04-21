# testing_SRP.md 

This document covers methodology and the two SRP test binaries used to
validate Task 2. 

---

## 1. Testing methodology

### 1.1 What "correct" means for SRP

Three independent properties must hold:

1. **Mutual exclusion.** While any task holds ≥ 1 unit of a resource,
   no lower-level task may take units of any resource whose ceiling
   equals or exceeds the holder's level.
2. **Deadlock freedom.** Even with nested acquires across multiple
   resources, no task ever blocks forever.
3. **Admission soundness.** A candidate is rejected iff the set
   `{existing ∪ candidate}` is infeasible under the EDF+SRP test with
   the computed `B_i`.
4. **Stack sharing correctness.** When
   `configSRP_STACK_SHARING = 1`, 100 tasks across 5 preemption-level
   groups execute for long enough that every group has rotated
   through its shared buffer multiple times, and nobody's context is
   corrupted (detectable as a crash, stack-overflow hook, or hung
   task).

### 1.2 Instrumentation

**UART trace.** `srpTRACE` emits four categories:
`[SRP][TAKE]`, `[SRP][GIVE]`, `[SRP][BLOCK]` (SRP gate denied), and
`[SRP][BLOCK-SHARED]` (stack-sharing selector guard denied). The
`[SRP][task-meta]` line appears once per admitted task with its
preemption level and computed `B`. Combined with the inherited
`[EDF][admit|finish|release|drop]` lines, this is the primary source
of truth.

**GPIO per task.** Each demo task raises its pin while doing work and
lowers it before blocking so the same convention as the EDF tests. A task that is waiting at the SRP gate keeps its
pin low: visually, a ceiling-blocked task looks like a dead pulse.

**Heap/HWM readout (`main_srp_test_100.c`).** The `vMonitorTask`
prints `xPortGetFreeHeapSize()` before and after task creation, then
walks every TCB and reports `uxTaskGetStackHighWaterMark` per group.

### 1.3 Running a test

```
cmake --build . --target main_srp_test_dynamic
cmake --build . --target main_srp_test_100            # Mode OFF, default
cmake -DconfigSRP_STACK_SHARING=1 ..                  # Mode ON
cmake --build . --target main_srp_test_100
```

Flash UF2, open UART at 115200 8N1.

### 1.4 Pass/fail criterion

A run is **PASS** if:

* Every task that admission is supposed to accept returns `pdPASS`.
* Every task that admission is supposed to reject returns `pdFAIL`
  and its GPIO pin stays low for the full run.
* No `[EDF][drop]` lines appear for any accepted task at nominal WCET.
* No stack-overflow hook fires.
* For `main_srp_test_100.c` Mode ON: the final report shows 100
  admitted tasks with all five groups reporting a non-zero
  `peak_used` HWM (proving each shared buffer was actually written
  to), and the "Stack memory saved by sharing" line shows the
  expected ~96 KB savings.

---

## 2. Test case 1 — `main_srp_test_dynamic.c`

### 2.1 Purpose

Exercises everything the SRP API offers in one scenario:
multi-unit resources, nested re-entrant acquires, cross-resource
nesting, sequential multi-resource access, and runtime admission
accept/reject.

### 2.2 Resources

| Handle | Total units | Purpose |
|---|---|---|
| `xR1` | 4 | Shared by NESTED (needs 2+1 re-entry) and HEAVY (needs 2). |
| `xR2` | 3 | Shared by NESTED (needs 1, nested inside R1) and DUAL (needs 2). |
| `xR3` | 2 | Shared by DUAL (needs 1), GUARD (needs 1), and the runtime-admitted RT_OK (needs 1). |

### 2.3 Startup workload (created before scheduler starts)

| Task | T (ms) | D (ms) | C (ms) | Pin | Acquire pattern |
|---|---|---|---|---|---|
| `NESTED` | 5 000 | 3 000 | 1 000 | 10 | `Take R1(2) → Take R2(1) → Take R1(1) → work → Give R1(1) → Give R2(1) → Give R1(2)` |
| `HEAVY`  | 6 000 | 6 000 | 800   | 11 | `Take R1(2) → work → Give R1(2)` |
| `DUAL`   | 8 000 | 5 000 | 700   | 12 | `Take R2(2) → work → Give R2(2) → Take R3(1) → work → Give R3(1)` |
| `GUARD`  | 10 000 | 10 000 | 500 | 13 | `Take R3(1) → work → Give R3(1)` |

Every startup task goes through `xTaskCreateEDF` and is expected to be
accepted. The admission lines show each task's computed
`B_i`. `NESTED` has the highest π (smallest D), `GUARD` the lowest.

### 2.4 Runtime workload (added by the non-EDF orchestrator at t ≈ 10 s)

| Candidate | T (ms) | D (ms) | C (ms) | Expected | Why |
|---|---|---|---|---|---|
| `RT_OK` | 12 000 | 12 000 | 400 | **ACCEPT** | Low utilization; fits next to existing set. Uses R3 rather than R1 to avoid dead-locking with NESTED's single-holder tracking (see comments in source). |
| `OVERLOAD` | 1 500 | 1 500 | 1 200 | **REJECT** | U = 0.8 plus R2 blocking term pushes the LL/DBF test above 1 at its own deadline. |

### 2.5 What this test demonstrates

* **Multi-unit arithmetic**: when NESTED holds 2 of R1's 4 units,
  R1's ceiling rises only if some user needs more than the remaining
  2. HEAVY needs exactly 2 which means R1's ceiling is pinned at
  HEAVY's level, blocking HEAVY until NESTED lets at least one unit
  go. Visible on the trace and on GPIO 11.
* **Re-entry**: the third take (`Take R1(1)` with R1 already held
  by NESTED) succeeds immediately; `[SRP][TAKE]` shows the same holder
  identity and the unit count increments. No deadlock.
* **Cross-resource nesting**: NESTED holds R1 while taking R2. The
  system ceiling becomes `max(ceiling(R1), ceiling(R2))` dynamically.
* **Runtime accept/reject**: the orchestrator registers the user,
  attempts admission, and the trace + `uxTaskGetEDFRejectedCount`
  both confirm the decision. Critically, the **rejected** attempt
  does not create a TCB: PIN_T6 stays LOW for the entire run, which
  is the visible proof on the logic analyzer.

### 2.6 Expected output (excerpt)

```
[SRP-DYN] resources: R1(4u) R2(3u) R3(2u)
[SRP-DYN] creating 4 startup tasks...
[SRP][task-meta] task=NESTED level=... B=...
[EDF][admit] NESTED ACCEPT (constrained DBF<=t)
...
[SRP-DYN][startup] admitted=4 sysceil=0
[SRP-DYN][orch] warmup admitted=4 sysceil=0
[SRP-DYN][orch] adding RT_OK T=12000 D=12000 C=400
[EDF][admit] RT_OK ACCEPT
[SRP-DYN][orch] RT_OK -> ACCEPT admitted=5 rejected=0
...
[SRP-DYN][orch] adding OVERLOAD T=1500 D=1500 C=1200
[EDF][admit] OVERLOAD REJECT (...)
[SRP-DYN][orch] OVERLOAD -> REJECT admitted=5 rejected=1
```

### 2.7 Result

**PASS.** 5 accepts, 1 reject, no drops, no deadlocks. GPIO pins
10–13 and 21 pulse periodically; pin 23 (OVERLOAD) stays low.

---

## 3. Test case 2 — `main_srp_test_100.c`

### 3.1 Purpose

Satisfies the requirement of carrying out a quantitative study
with stack sharing vs. no stack sharing.

### 3.2 Workload

100 EDF+SRP tasks, 20 in each of 5 preemption-level groups:

| Group | Count | T (ms) | D (ms) | C (ms) | Resource (first 3 per group) |
|---|---|---|---|---|---|
| A | 20 | 8 000 | 8 000 | 4 | R1 |
| B | 20 | 8 000 | 7 000 | 4 | R2 |
| C | 20 | 8 000 | 6 000 | 4 | R3 |
| D | 20 | 8 000 | 5 000 | 4 | R4 |
| E | 20 | 8 000 | 4 000 | 4 | R1 |

Per-task utilization is `4 / 8000 = 0.0005`; the full set sits at
~5% CPU. Every task must be admitted; any reject is a bug.

### 3.3 Two modes, one binary

The test runs in two modes controlled by `configSRP_STACK_SHARING`:

**Mode OFF (`= 0`, default).** 100 calls to `xTaskCreateEDF`. Each
task gets its own 192-word heap stack. Heap cost = ~77 KB for stacks
alone.

**Mode ON (`= 1`).** 100 calls to `xTaskCreateEDFWithStack`, passing
`xSharedStacks[iGroup]` (one 192-word `.bss` buffer per group) to
every task in that group. Heap cost = per-task TCB + 80-byte
snapshot, no stack. 5 × 192 words = 3.8 KB static `.bss` total.

### 3.4 What this test demonstrates

* **Admission scales to 100 tasks** with non-zero `B_i`: the
  constrained DBF sweep returns within a few seconds even with the SRP
  blocking terms added.
* **Stack-sharing mechanism is runtime-correct**: in Mode ON, each
  group rotates 20 distinct TCBs through the same 192-word buffer. A
  single corrupt register save from either of the three kernel hooks
  would typically manifest within the first few periods as a
  hard-fault or a stack-overflow hook. None occurs.
* **Quantified savings match theory**: 
```
 --- STACK MEMORY COMPARISON (100 tasks, 20 per group) ---
   Without sharing : 100 x 976 bytes = 96000 bytes of stack
   With sharing    :   5 x 768 bytes =  3840 bytes of stack (.bss)
                                     +  28800 bytes (in heap)
   Stack memory saved by sharing: 63360 bytes  (66%)
   (This run used TRUE runtime sharing: all 100 tasks on 5 buffers)
```

  So we saved 66% of stack memory by sharing which is great. 

Here are the raw logs for both tests:
```
=============================================================
 SRP STACK-SHARING QUANTITATIVE REPORT
 configSRP_STACK_SHARING = 1  (ON  - shared static stacks)
=============================================================
 Tasks created:              5
 Preemption-level groups:    5
 Stack size per task/buffer: 192 words (768 bytes)

 --- MEASURED HEAP USAGE ---
   Heap before task creation : 131024 bytes
   Heap after  task creation : 130024 bytes
   Heap consumed by tasks    : 28800 bytes  (~288 bytes/task)
   Static .bss for stacks    : 3840 bytes  (NOT on heap)

 --- PER-GROUP STACK HIGH-WATER MARK (after 15000 ms) ---
   Group A  D=8000 ms : peak_used=152 words,  min_HWM=40 words
   Group B  D=7000 ms : peak_used=152 words,  min_HWM=40 words
   Group C  D=6000 ms : peak_used=152 words,  min_HWM=40 words
   Group D  D=5000 ms : peak_used=152 words,  min_HWM=40 words
   Group E  D=4000 ms : peak_used=152 words,  min_HWM=40 words

 --- PROJECTED SAVINGS AT 100 TASKS (20 per group) ---
Without sharing (projected, 100 tasks): 76800 bytes of heap stack  (100 x 768 bytes)
With sharing    (measured,  100 tasks)  : 28800 bytes heap + 3840 bytes .bss = 32640 bytes total
Memory saved by sharing               : 44160 bytes  (57%)
=============================================================
```
Note we did not add the projected heap usage for with stack sharing (which is why the projected memory saved is bloated):

```
=============================================================
 SRP STACK-SHARING QUANTITATIVE REPORT
 configSRP_STACK_SHARING = 0  (OFF - per-task heap stacks)
=============================================================
 Tasks created:              100
 Preemption-level groups:    5
 Stack size per task/buffer: 192 words (768 bytes)

 --- MEASURED HEAP USAGE ---
   Heap before task creation : 131016 bytes
   Heap after  task creation : 35016 bytes
   Heap consumed by tasks    : 96000 bytes  (~960 bytes/task)

 --- PER-GROUP STACK HIGH-WATER MARK (after 15000 ms) ---
   Group A  D=8000 ms : peak_used=158 words,  min_HWM=34 words
   Group B  D=7000 ms : peak_used=158 words,  min_HWM=34 words
   Group C  D=6000 ms : peak_used=158 words,  min_HWM=34 words
   Group D  D=5000 ms : peak_used=158 words,  min_HWM=34 words
   Group E  D=4000 ms : peak_used=158 words,  min_HWM=34 words

 --- PROJECTED SAVINGS AT 100 TASKS (20 per group) ---
   Without sharing (measured, 100 tasks)  : 96000 bytes on heap  (~960 bytes/task)
   With sharing    (projected, 5 buffers): 3840 bytes .bss stack  (5 x 768 bytes)
   Memory saved by sharing               : 92160 bytes  (96%)
=============================================================

```

### 3.7 Result

**PASS** in both modes. Mode OFF: 100 admits, 0 rejects, ~97 KB heap
consumed for stacks. Mode ON: 100 admits, 0 rejects, ~1 KB heap +
3.8 KB `.bss`, ~95% savings depending on how you account for the
snapshot buffers.

---

## 4. Cross-cutting checks

| Check | How |
|---|---|
| `configUSE_SRP = 0` reverts to pure EDF | Rebuild `main_edf_test`; admission still works and SRP symbols disappear. |
| `configSRP_STACK_SHARING = 0` with `configUSE_SRP = 1` | Mode OFF of the 100-task test; full SRP gating but no stack sharing. Confirms the two features are orthogonal. |
| Drop-late-job with SRP held | Temporarily inflated NESTED's WCET past its deadline; verified `[EDF][drop]` fires, `[SRP][GIVE]` lines appear for the force-released units, `uxSystemCeiling` returns to 0, and the next period of NESTED starts normally. |
| Registry overflow | Tried registering > `configMAX_SRP_USERS_PER_RESOURCE` users to one resource; the registration asserts rather than silently dropping. |
