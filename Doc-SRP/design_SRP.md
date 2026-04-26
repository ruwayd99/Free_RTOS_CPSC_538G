# design_SRP.md 

This document explains how SRP is implemented on top of the Task 1
EDF scheduler: preemption levels, the dynamic ceiling, the multi-unit
resource model, how `B_i` is computed for admission, and how the **run-time stack-sharing** mechanism works. Flowcharts
are at the end.

---

## 1. Goals and scope

Task 2 asks for:

1. SRP applied to **binary semaphores** (we generalise to multi-unit
   resources because the dynamic-ceiling machinery already supports it).
2. A single `B_i` blocking term per task that is **added to the EDF
   admission test**, so EDF+SRP admission is sound.
3. Support for **runtime** resource creation, user registration, take
   and give.
4. **Run-time stack sharing** across tasks at the same preemption
   level, with a quantitative study on 100 tasks.

What we explicitly do NOT do:

* We do **not** modify `queue.c` or `xSemaphoreTake`. The SRP layer is
  a parallel API (`xSRPResourceTake`) that sits above the EDF scheduler.
  This keeps the stock FreeRTOS semaphore semantics intact for anyone
  who does not enable SRP.

---

## 2. Preemption level — `π_i`

SRP requires that tasks with shorter deadlines have higher preemption
levels. We use Baker's formula:

```
π_i = portMAX_DELAY - D_i
```

This is computed once at `xTaskCreateEDF` time by
`prvSRPCalculateTaskPreemptionLevel` and stored in
`pxTCB->uxPreemptionLevel`. `portMAX_DELAY` is `UINT32_MAX` on this
port, so on a 1 kHz tick the math has ~49 days of headroom. Shorter
`D ⇒ larger π`.

---

## 3. Multi-unit dynamic ceiling

### 3.1 Ceiling of a single resource

```
ceiling(r) = max π_i  over users i  with  units_needed(i, r) > available(r)
```

In plain English: the resource's ceiling is the largest preemption
level of any user that *cannot currently satisfy its full request*.
If the resource is fully available, no user could be blocked, so
`ceiling(r) = 0`. If some units have been taken and one user's
`units_needed` now exceeds the remainder, that user's level contributes.

This is dynamic as the ceiling rises as units are taken and falls as
units are given back. `prvSRPRecalculateSystemCeiling` runs after every
take and every give.

### 3.2 System ceiling

```
uxSystemCeiling = max over all resources r  of  ceiling(r)
```

A task can start running iff `π_i > uxSystemCeiling` (strict). The
strict inequality is what keeps Baker's theorem true: two same-level
tasks can never overlap.

### 3.3 Holder tracking

Each resource keeps a compact array of `SRPHolderRecord_t` entries
(`{xHolder, uxUnitsHeld, uxHolderLevel}`) and a count `uxHolderCount`.
Multiple tasks can hold non-overlapping units of the same resource
simultaneously with each getting its own slot.

`xSRPResourceTake` first scans the array for the calling task:

* **Found (re-entry):** the task already passed the gate; accumulate the
  new units into the existing entry's `uxUnitsHeld`, bypass the gate check.
* **Not found (new take):** the calling task must pass the SRP gate
  (`π > uxSystemCeiling`); if it does, append a new entry.

`vSRPResourceGive` finds the calling task's entry, decrements
`uxUnitsHeld` by the released amount, and removes the entry (compact by
swapping with the last slot) when `uxUnitsHeld` reaches zero.

This is what lets `T1 NESTED` in `main_srp_test_dynamic.c` do
`Take R1(2) → Take R2(1) → Take R1(1 re-entry)`: the third take finds
T1 already in the R1 holders array and simply accumulates. It also lets
two distinct tasks hold disjoint units of the same multi-unit resource at
the same time without corrupting each other's accounting.

---

## 4. Blocking bound `B_i` for admission

At `xTaskCreateEDF` time (Task 1 path), the candidate's params gain two
SRP fields:

```c
xCandidate.uxLevel = portMAX_DELAY - D;
xCandidate.xB      = prvSRPComputeBlockingBoundForLevel( xCandidate.uxLevel );
```

`prvSRPComputeBlockingBoundForLevel(π)` returns the longest critical
section belonging to any *lower-level* user whose resource's
hypothetical ceiling (if that user were holding its units) would be
`≥ π`. This is the standard SRP blocking bound.

`B_i` is then fed into both admission branches:

* **Implicit (LL):**     `Σ (C_i + B_i) / T_i ≤ 1`
* **Constrained (DBF):** `Σ dbf_i(t) + max(B_i) ≤ t` for every `t ∈ [min D, horizon]`.

When `configUSE_SRP = 0`, every `B_i` is zero and the formulas collapse
back to the pure-EDF Task 1 tests — so admission remains sound in
both modes.

---

## 5. Runtime SRP API

### 5.1 `xSRPResourceCreate(units)`

Linear scan of `xSRPResources[]` for a free slot, zero-initialize,
set totals, return handle. Bounded-time, no heap allocation.

### 5.2 `vSRPResourceRegisterUser(h, level, units_needed, cs_ticks)`

Appends a user record to the resource. **Must be called before any
task that will take the resource is admitted**, because the admission
`B_i` computation reads every registered user's CS length. This is
why `main_srp_test_dynamic.c` calls
`vSRPResourceRegisterUser(xR3, prvLevel(T5.D), 1, 300)` *before*
`xTaskCreateEDF(vTaskRuntimeOK, ...)`.

### 5.3 `xSRPResourceTake(h, uxUnits)`

Takes a critical section, then:

1. Asserts `uxUnits ≤ uxAvailableUnits` (admission guarantees this; a
   runtime violation indicates a mismatch between declared CS and
   actual behaviour).
2. Updates holder tracking; handles re-entry by keeping the same
   holder and accumulating units.
3. Decrements `uxAvailableUnits`.
4. Calls `prvSRPRecalculateSystemCeiling()`.
5. Emits `[SRP][TAKE] task=... res=... units=... avail=... sysceil=...`.

### 5.4 `vSRPResourceGive(h, uxUnits)`

Inverse of take: increments availability, clears holder on full
release, recomputes ceiling, emits `[SRP][GIVE] ...`. Under the
drop-late-job path (`prvEDFDropLateJob`), every unit the dying job
still holds is force-released using the same bookkeeping.

---

## 6. Integration with EDF selection

The selector was upgraded from "head of xEDFReadyList" to
`prvEDFSelectRunnableTaskBySRP`:

```
for each task in xEDFReadyList (deadline-sorted):
    if prvSRPCanTaskRun(task):
        return task
return NULL          // fall through to priority-based selection
```

`prvSRPCanTaskRun` is:

* `pdTRUE`  if `π > uxSystemCeiling`
* `pdTRUE`  if `uxSRPHeldResources > 0` — the task holds at least one
   unit and already passed the gate; it may be the currently-executing
   task or a task that was preempted mid-CS by a higher-level preemptor
   and must now resume.
* `pdFALSE` otherwise, and we emit `[SRP][BLOCK]` so the trace shows
   which task was gated and why.

The fall-through keeps the idle task and monitor/orchestrator tasks
runnable even when every EDF task is blocked by ceiling.

---

## 7. Run-time stack sharing

Baker's
theorem gives us the *permission* to share; the implementation has to
make it *actually happen* safely inside the context-switch machinery
that was written assuming each task had its own stack.

### 7.1 Why it's safe

Baker's theorem: two tasks with the same preemption level can never
execute concurrently under SRP. Same level ⇒ same deadline ⇒ they are
admitted with identical `π`. A same-level peer can only start running
when the current holder has **already returned to the EDF delay path**
(blocked via `vTaskDelayUntilNextPeriod`). By that point the current
holder's job is finished, so its register contents on the shared stack
are dead and can be overwritten.

### 7.2 What can go wrong naïvely

If we just point two TCBs at the same `pxStack` and let FreeRTOS's
PendSV save/restore as usual, the following breaks:

* **Fresh-start corruption.** PendSV pushes the outgoing task's live
  registers onto the shared buffer *and moves `pxTopOfStack` down*. On
  the next dispatch of a same-level peer, PendSV pops what it finds at
  `pxTopOfStack`, which is the **previous task's saved registers**,
  not the initial frame the peer needed.
* **Cross-level preemption.** If a higher-level task preempts the
  current holder, the holder's live context is still on the buffer.
  When the holder resumes, PendSV must pop **those live registers**,
  not the initial frame.

The kernel has to distinguish these two situations at dispatch time.
That is what `xSharedStackFreshStart` encodes:

* `pdTRUE`  ⇒ this task is about to begin a **new job**; the top of
  the buffer holds somebody else's garbage; copy the private snapshot
  in and reset `pxTopOfStack` to the initial-frame position.
* `pdFALSE` ⇒ this task was **preempted mid-job** and is now
  resuming; the buffer already contains its live registers; do
  nothing.

### 7.3 The three kernel hooks

The mechanism has three code sites inside `tasks.c`:

#### (1) Snapshot capture at creation: inside `xTaskCreateEDFWithStack`

After `prvInitialiseNewTask` has written the initial register frame to
the top of the shared buffer, the top-of-stack pointer is saved, the
frame size is computed, and a heap buffer `pxPrivateContextSnapshot` is
allocated and filled with a `memcpy`. The TCB stores:

```
pxPrivateContextSnapshot    -- 20-ish words of "initial frame"
pxSnapshotTopOfStack        -- where the frame lives on the buffer
uxPrivateContextWords       -- frame size
pxSharedStackBuffer         -- the static buffer
uxSharedStackDepth          -- buffer size
xSharedStackFreshStart = pdTRUE
```

#### (2) Dispatch hook: inside `vTaskSwitchContext`, right after `taskSELECT_HIGHEST_PRIORITY_TASK()`

```
if ( pxCurrentTCB->pxPrivateContextSnapshot != NULL &&
     pxCurrentTCB->xSharedStackFreshStart   == pdTRUE ) {
    memcpy( pxSnapshotTopOfStack,
            pxPrivateContextSnapshot,
            uxPrivateContextWords * sizeof(StackType_t) );
    pxTopOfStack         = pxSnapshotTopOfStack;
    xSharedStackFreshStart = pdFALSE;
}
```

This runs under PendSV's critical section, so no other core or ISR
can see a half-copied frame. After it runs, the shared buffer looks
byte-identical to what the port's start-of-task trampoline expects.

#### (3) Fresh-start flag: inside `vTaskDelayUntilNextPeriod`

At the end of a job the task calls `vTaskDelayUntilNextPeriod`, which
first flips `xSharedStackFreshStart = pdTRUE` *before* the blocking
`vTaskDelayUntil`. So the **next** time this TCB is selected to run,
the dispatch hook will treat it as a fresh start.

### 7.4 The selector guard

Even with the flag set correctly, we need one more check: if a
**same-level peer** was preempted (`xSharedStackFreshStart == pdFALSE`
on that peer) and *we* are a different same-level task with
`xSharedStackFreshStart == pdTRUE`, running us would overwrite the
peer's live context.

Baker's theorem says this cannot happen under correct SRP gating
but only *after* the peer has given its resources back and lowered the
ceiling. In a multi-resource system that lowering can briefly race
with selection, so we defend with a small scan inside the selector:

```
if ( candidate passes SRP gate ) {
    if ( candidate is fresh-start &&
         prvSharedBufferHasMidExecutionTask(candidate->buffer, candidate->level) )
    {
        emit [SRP][BLOCK-SHARED] trace; keep walking xEDFReadyList
    }
    else return candidate;
}
```

`prvSharedBufferHasMidExecutionTask` walks `xEDFReadyList` + checks
`pxCurrentTCB` for any other TCB sharing the same buffer at the same
level with `xSharedStackFreshStart == pdFALSE`. If it finds one, the
candidate is skipped; the mid-execution peer will get its turn first.

### 7.5 Cost and savings

Per-task overhead with sharing:
* TCB                       ~= 120 bytes
* Private snapshot          ~= 20 words = 80 bytes
* Total per task            ~= 200 bytes

Per-group overhead (once):
* Shared stack buffer       = `SRP100_STACK_WORDS * 4` bytes (in `.bss`)

For 100 tasks × 192-word stacks in 5 groups:

```
   sharing OFF : 100 * 192 * 4 = 76 800 bytes of heap stack
   sharing ON  :   5 * 192 * 4 =  3 840 bytes of .bss stack
                                  + 100 * 80 = 8 000 bytes of snapshots
                                = 11 840 bytes total
   savings     : ~85%
```

`main_srp_test_100.c` measures and prints this directly.

---

## 8. Configuration surface

| Symbol | Default | Effect |
|---|---|---|
| `configUSE_SRP` | `0` | Master switch. When `0`, every SRP change compiles out; pure Task-1 EDF remains. |
| `configMAX_SRP_RESOURCES` | `8` | Size of the `xSRPResources[]` registry. |
| `configMAX_SRP_USERS_PER_RESOURCE` | `16` | Per-resource user table capacity. |
| `configSRP_STACK_SHARING` | `0` | When `1`, enables `xTaskCreateEDFWithStack`, the dispatch hook, and the selector guard. Requires `configUSE_SRP = 1`. |

---

## 9. Flowcharts

### 9.1 Take / give — ceiling update

```
                         xSRPResourceTake(h, n)
                                   |
                                   v
                     taskENTER_CRITICAL()
                                   |
                                   v
           assert(n <= uxAvailableUnits)   /* admission guarantees this */
                                   |
                                   v
              uxAvailableUnits -= n
              scan xHolders[] for calling task:
                found  -> accumulate uxUnitsHeld += n  (re-entry)
                missing -> append new SRPHolderRecord_t entry
              pxTask->uxSRPHeldResources++
                                   |
                                   v
              prvSRPRecalculateSystemCeiling()
                                   |
                                   v
                 uxSystemCeiling = max over r of ceiling(r)
                                   |
                                   v
             emit [SRP][TAKE] task=.. res=.. avail=.. sysceil=..
                                   |
                                   v
                     taskEXIT_CRITICAL()
```

### 9.2 SRP gate inside scheduler selection

```
             taskSELECT_HIGHEST_PRIORITY_TASK()
                                   |
                                   v
                    prvEDFSelectRunnableTaskBySRP()
                                   |
                                   v
       +-----------------------------------------------+
       | for each TCB in deadline-sorted xEDFReadyList |
       +-----------------------------------------------+
                 |
                 v
       prvSRPCanTaskRun(TCB) :
           π > uxSystemCeiling ?            -> yes
           holder of any resource ?         -> yes
           else                             -> no
                 |
         +-------+-------+
         |               |
       no|             yes
         v               v
    emit [SRP][BLOCK]   shared-stack guard:
    continue loop       peer mid-exec on same buffer?
                        |            |
                       yes           no
                        |            |
        emit [BLOCK-SHARED]        return TCB
        continue loop
                 |
                 v
       loop exhausted  ->  return NULL
                          (fall through to stock priority path
                           so idle/helper tasks still run)
```

### 9.3 Stack-sharing dispatch

```
    vTaskSwitchContext()
            |
            v
    taskSELECT_HIGHEST_PRIORITY_TASK()   /* pxCurrentTCB now set */
            |
            v
  +-------------------------------------------------+
  | pxCurrentTCB->pxPrivateContextSnapshot != NULL  |
  | AND xSharedStackFreshStart == pdTRUE  ?         |
  +-------------------------------------------------+
     |                            |
    NO                           YES
     |                            |
     |     memcpy( pxSnapshotTopOfStack,
     |             pxPrivateContextSnapshot,
     |             uxPrivateContextWords );
     |     pxTopOfStack = pxSnapshotTopOfStack;
     |     xSharedStackFreshStart = pdFALSE;
     |                            |
     +-------------+--------------+
                   v
    PendSV pops registers from pxTopOfStack and resumes
```

### 9.4 End-of-job flag flip

```
  task body:
      for (;;) {
          work();
          vTaskDelayUntilNextPeriod(&xLastWake) {
              xSharedStackFreshStart = pdTRUE;   /* before the block */
              vTaskDelayUntil(...);              /* blocks until release */
          }
      }
```
