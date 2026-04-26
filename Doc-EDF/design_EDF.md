# design_EDF.md 

This document describes the scheduler architecture, admission-control
strategy, transient-overload policy, configurability, and runtime flow
for the EDF implementation. Flowcharts are at the end.

---

## 1. Goals and scope

Task 1 asks for:

1. A preemptive EDF scheduler that supports **constrained-deadline**
   periodic tasks (`D ≤ T`).
2. **Exact processor-demand** admission control for the constrained
   case, and the Liu-Layland bound for the implicit case.
3. **Runtime admission**: the system must be able to accept new
   periodic tasks *while the scheduler is already running*.
4. A documented **deadline-miss (overload) policy**.
5. Configurable enable/disable so stock FreeRTOS behaviour is the
   default.
6. A demo that scales to roughly 100 periodic tasks.

---

## 2. Scheduler architecture

### 2.1 Single deadline-sorted ready list

The stock FreeRTOS scheduler keeps `configMAX_PRIORITIES` separate ready
lists and an `uxTopReadyPriority` bitmap. That design is wrong for EDF:
the ready priority of a task changes every time it releases a new job,
so a fixed priority-indexed table is the wrong shape.

We added **one** extra list, `xEDFReadyList`, and an extra macro path in
`taskSELECT_HIGHEST_PRIORITY_TASK()`. When any EDF task is ready, the
head of `xEDFReadyList` is chosen; otherwise the original FreeRTOS
priority path runs so that idle/helper/timer tasks still work.

List ordering is maintained by the stock `vListInsert` plus
`listSET_LIST_ITEM_VALUE( xStateListItem, xAbsoluteDeadline )` with zero new
bookkeeping, and no O(N) sort on every tick.

Why not per-priority lists emulating EDF? An absolute-deadline-keyed
ordered linked list is the simplest correct structure here. Insertion
is O(N) worst case but N stays bounded by admission; selection is O(1).

### 2.2 Registry list

`xEDFTaskRegistryList` is a separate permanent list. A task is added
here once at admission and removed at deletion. It is **not** a ready
list as it exists purely so the DBF admission test can sweep over every
admitted task, including those currently in the delayed list (blocked
waiting for the next release).

### 2.3 Coexistence with stock tasks

`xTaskCreate` still works. A stock task has `xPeriod == 0`, so
`prvAddTaskToReadyList` puts it on the normal priority-indexed list.
EDF tasks and stock tasks coexist, with the kernel preferring EDF
whenever any EDF task is ready. This is why the test binaries use a
stock `xTaskCreate` for helper/orchestrator/monitor tasks running at
`tskIDLE_PRIORITY + 1`: they fill only the CPU time that no EDF task
wants, and never perturb the schedule.

---

## 3. Admission control

### 3.1 Implicit-deadline path (Liu-Layland)

When every admitted task and the candidate satisfy `D == T`, Liu-Layland
`Σ C_i / T_i ≤ 1` is both necessary and sufficient. The check is done in
fixed-point micro-units:

```c
ullUtil += ((uint64_t) (C + B) * 1000000UL) / T;   /* per task */
accept if ullUtil <= 1000000UL
```

No floating-point in kernel.
The `B` term is the SRP blocking bound (always zero in pure Task 1).

### 3.2 Constrained-deadline path (exact DBF)

Any `D < T` anywhere in the set switches to the exact processor-demand
test. The demand-bound function is:

```
dbf_i(t) = max(0, floor((t - D_i) / T_i) + 1) * C_i
```

and the set is feasible iff, for every integer `t` in the evaluation
window, `Σ dbf_i(t) + B_max ≤ t`.

Three design decisions matter here:

1. **Horizon = LCM of all periods**, capped at
   `configEDF_MAX_ANALYSIS_TICKS` (default `100 000`). The hyperperiod
   is the theoretically correct upper bound. Without the cap, ugly
   pairings of periods can make the sweep blow past a minute on an M0+.
2. **Loop start = min(D_i)** across every admitted task and the
   candidate. For `t < min D_i` no task has yet had a deadline, so the
   demand is zero.
3. **Max blocking `B_max` is added at each `t`**. This is the classic
   SRP extension of the Baruah DBF test. With Task 1 alone, all `B`
   values are zero and the term vanishes.

### 3.3 Runtime admission

`xTaskCreateEDF` is callable from any task at any time, not just during
startup. Internally:

1. Build the candidate params on the caller's stack.
2. Take `taskENTER_CRITICAL()`.
3. Run `prvEDFAdmissionControl` against the snapshot of the registry.
4. If the test **passes**, allocate the TCB and stack, populate EDF
   fields, append to `xEDFTaskRegistryList`, and schedule via
   `prvAddTaskToReadyList`.
5. If the test **fails**, increment `uxEDFRejectedTaskCount` and
   return `pdFAIL`. No TCB or stack is allocated and the reject costs
   only the DBF sweep.
6. Exit the critical section.

Admission is serialized inside a critical section so that two
simultaneous admitters cannot both read a stale registry view.

### 3.4 Why not QPA

The Quick Processor-demand Analysis (Baruah/Zhang) would reduce the
sweep to the deadlines of tasks only. I kept the full integer sweep
because:

* The assignment explicitly asks for demonstrating *exact* PD analysis.
* The trace produced by the integer sweep is easier to reason about on
  an MCU than a fixed-point iteration.
* `configEDF_MAX_ANALYSIS_TICKS` caps worst-case cost.

QPA is listed in `future_EDF.md` as a follow-up.

---

## 4. Release, execution, finish

### 4.1 Per-job accounting

* On release (after `vTaskDelayUntilNextPeriod` unblocks), a
  `[EDF][release]` trace is emitted. `xAbsoluteDeadline` already points
  at the next deadline, and `xJobExecTicks = 0`.
* On every tick, if the task is running, `xJobExecTicks++`.
* On job finish, the task calls `vTaskDelayUntilNextPeriod`. This emits
  a `[EDF][finish]` trace, advances the window, and blocks until
  `xLastReleaseTick + xPeriod`.

### 4.2 Why `vTaskDelayUntilNextPeriod` and not `vTaskDelayUntil`

Stock `vTaskDelayUntil` is agnostic to EDF state; it does not update
`xAbsoluteDeadline`. The new helper is a thin wrapper that fixes this
bookkeeping so users only need to call one function at the end of each
period. It also centralizes the finish/release trace so every EDF task
produces uniform logs on the logic analyzer pin and the UART.

---

## 5. Deadline-miss policy: drop-late-job

When `xTaskIncrementTick` sees an EDF task whose
`xAbsoluteDeadline <= xConstTickCount` and whose current job has not
finished, it calls `prvEDFDropLateJob`. The policy is:

1. Emit a `[EDF][drop]` trace containing the missed deadline, the
   consumed time, the declared WCET (and under SRP, the preemption
   level and current system ceiling).
2. If SRP is enabled, release every resource unit the late task
   currently holds and recompute the system ceiling. Without this the
   ceiling could stay latched and block every future job.
3. Advance `xLastReleaseTick += xPeriod` and recompute
   `xAbsoluteDeadline`. `xJobExecTicks = 0`.
4. Remove the TCB from `xEDFReadyList` and reinsert via
   `prvAddCurrentTaskToDelayedList( xPeriod )` so it wakes at the next
   release boundary instead of continuing to run a job that is already
   late.

Why drop-late rather than lower-priority or system-restart:

* **Dropping** preserves the remaining schedule. The other admitted
  tasks, which were the reason the late task could not finish, keep
  running normally. Total impact of one overrun = one missed instance,
  not a cascade.
* **Lowering priority** would leave the late task still competing for
  CPU, extending the overload, and (with SRP) leaving the ceiling
  pinned. That is the bad option in an EDF system.
* **Restart** is only correct when *invariants* (not just a single
  deadline) have been violated. The assignment wanted a design that is
  resilient to a one-off overrun, and drop-late deemed the best choice.

---

## 6. Configuration surface

All toggles live in `FreeRTOSConfig.h`:

| Symbol | Default | Effect |
|---|---|---|
| `configUSE_EDF_SCHEDULING` | `1` | Master switch. `0` compiles every EDF change out and restores stock FreeRTOS. |
| `configEDF_TRACE_ENABLE` | `1` | When `1`, `edfTRACE(...)` expands to `configPRINTF`. When `0`, compiles to nothing, so zero overhead. |
| `configEDF_MAX_ANALYSIS_TICKS` | `100000U` | Hard cap on the DBF horizon; protects the admission path on tiny MCUs. |
| `configPRINTF( x )` | `printf x` | Trace sink. Applications can redirect. |

If a user only needs stock FreeRTOS, setting `configUSE_EDF_SCHEDULING`
to `0` is sufficient; no other code changes are required.

---

## 7. Flowcharts

### 7.1 `xTaskCreateEDF` — admission flow

```
                      xTaskCreateEDF(T, D, C, ...)
                                   |
                                   v
                    taskENTER_CRITICAL()
                                   |
                                   v
             +-----------------------------------+
             | any admitted task has D < T ?     |
             | OR candidate has D < T ?          |
             +-----------------------------------+
               |                               |
               | NO  (implicit)                | YES (constrained)
               v                               v
   +----------------------+         +---------------------------+
   | prvEDFAdmission      |         | prvEDFAdmission           |
   | Implicit (LL bound)  |         | Constrained (exact DBF)   |
   | Sum (C+B)/T <= 1 ?   |         | horizon = LCM, cap        |
   +----------------------+         | for t = min D .. horizon: |
               |                    |   Sum dbf_i(t) + Bmax <= t|
               |                    +---------------------------+
               v                               |
          +-----------+                        v
          | pdPASS ?  |<-----------------------+
          +-----------+
            |       |
          NO|       |YES
            v       v
 uxEDFRejected++   prvInitialiseNewTask, populate EDF fields,
 return pdFAIL     append to xEDFTaskRegistryList,
                   prvAddTaskToReadyList (inserts into
                   xEDFReadyList keyed by xAbsoluteDeadline),
                   uxEDFAccepted++
                              |
                              v
                   taskEXIT_CRITICAL()
                              |
                              v
                       return pdPASS
```

### 7.2 Per-tick scheduler / deadline-miss path

```
                          xTaskIncrementTick()
                                   |
                                   v
           ++ xJobExecTicks for running EDF task
                                   |
                                   v
           +------------------------------------------+
           | For running/ready EDF tasks in SMP loop: |
           | is xAbsoluteDeadline <= now AND          |
           |   job not finished ?                     |
           +------------------------------------------+
             |                              |
             |NO                            |YES
             v                              v
    continue normal tick       prvEDFDropLateJob(tcb, now)
             |                              |
             |                              v
             |               emit [EDF][drop]
             |               release any SRP units held
             |               xLastReleaseTick += xPeriod
             |               xAbsoluteDeadline recomputed
             |               remove from xEDFReadyList
             |               insert into delayed list
             |                              |
             +----------------+-------------+
                              |
                              v
              taskSELECT_HIGHEST_PRIORITY_TASK()
                              |
                              v
           +-----------------------------------+
           | xEDFReadyList non-empty ?         |
           +-----------------------------------+
              |                          |
              |YES                       |NO
              v                          v
     pxCurrentTCB = head        stock priority path
     of xEDFReadyList          (idle, timers, helpers)
```

### 7.3 Periodic task body

```
task body:
    xLastWake = xTaskGetTickCount();
    for (;;) {
        /* release side */
        do_work();                         <-- C ticks of useful execution
        /* finish side */
        vTaskDelayUntilNextPeriod(&xLastWake);
            |
            |  emits [EDF][finish] trace
            |  xLastReleaseTick += xPeriod
            |  xAbsoluteDeadline = xLastReleaseTick + xRelativeDeadline
            |  xJobExecTicks = 0
            |  vTaskDelayUntil( &xLastWake, xPeriod )  -- blocks
            v
        /* next release */
        emits [EDF][release] trace; loop top
    }
```
