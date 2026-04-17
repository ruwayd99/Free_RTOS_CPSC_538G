# Task 4 — Multiprocessor EDF on RP2040: Implementation Guide

## Scope & Ground Rules (from the assignment)

- Only **implicit-deadline** periodic tasks (`D == T`). The constrained-deadline path used by single-core EDF is **not required** here. Keep it out of Task 4's admission test.
- Two identical cores (SMP). Task set is independent — no resource sharing, no IPC.
- Two modes coexisting in source, one active at a time:
  - `configGLOBAL_EDF_ENABLE`       (default when neither is set)
  - `configPARTITIONED_EDF_ENABLE`
- Required programming interface:
  - Specify the core a task should run on (partitioned: fixed-core; global: affinity hint).
  - Remove a task from a processor.
  - Migrate a task to another processor.
- Admission control with a feasibility test appropriate for each scheme.
- Pick a kernel-placement policy (kernel-on-core-0 pinned vs. kernel-as-interrupt-handler). **We pin the FreeRTOS kernel bookkeeping to the core doing the tick (core 0); task dispatch happens on both cores.**

> Rationale for pinning the tick to core 0: the SMP port already runs one SysTick-like timer that increments `xTickCount` on the tick core. Replicating timers per-core buys nothing when the tick rate is 1 kHz and adds cross-core invariants we don't need. Inter-core preemption is already implemented by the RP2040 port via the SIO FIFO IRQ (see [port.c:273-295](FreeRTOS/FreeRTOS/Source/portable/ThirdParty/GCC/RP2040/port.c#L273-L295)).

---

## Current State — What's Already Present

The repo has the FreeRTOS-SMP machinery wired up, but it's disabled at the config layer. We reuse these primitives; we do **not** re-invent SMP.

Already in [tasks.c](FreeRTOS/FreeRTOS/Source/tasks.c):

| Primitive | Location | Purpose |
| --- | --- | --- |
| `pxCurrentTCBs[configNUMBER_OF_CORES]` | [tasks.c:628](FreeRTOS/FreeRTOS/Source/tasks.c#L628) | Per-core current TCB. Replaces `pxCurrentTCB`. |
| `xTaskRunState` in TCB | [tasks.c:527](FreeRTOS/FreeRTOS/Source/tasks.c#L527) | Which core a task is running on, or `taskTASK_NOT_RUNNING`. |
| `prvYieldCore(xCoreID)` | [tasks.c:435](FreeRTOS/FreeRTOS/Source/tasks.c#L435) | Ask a specific core to reschedule (sends IPI on RP2040). |
| `prvSelectHighestPriorityTask(xCoreID)` | [tasks.c:1479](FreeRTOS/FreeRTOS/Source/tasks.c#L1479) | Per-core task-selection function; we will override/wrap this. |
| `configUSE_CORE_AFFINITY` / `uxCoreAffinityMask` | [task.h:186-187](FreeRTOS/FreeRTOS/Source/include/task.h#L186-L187) | Bitmask of permitted cores for a task. |
| RP2040 core1 bring-up | [port.c:362-368](FreeRTOS/FreeRTOS/Source/portable/ThirdParty/GCC/RP2040/port.c#L362-L368) | `multicore_launch_core1()` + spinlock claim, FIFO IRQ. |

Already-present EDF state in [tasks.c](FreeRTOS/FreeRTOS/Source/tasks.c):

- `xEDFReadyList` — single deadline-sorted list. **Global-EDF uses this directly; partitioned-EDF needs N of these, one per core.**
- `xEDFTaskRegistryList` — accounting list of all admitted EDF tasks.
- `prvEDFAdmissionControl` ([tasks.c:861](FreeRTOS/FreeRTOS/Source/tasks.c#L861)), `prvEDFAdmissionImplicit` ([tasks.c:898](FreeRTOS/FreeRTOS/Source/tasks.c#L898)).
- `xTaskCreateEDF` ([tasks.c:1749](FreeRTOS/FreeRTOS/Source/tasks.c#L1749)) — single-core admission + registry insert.

The single-core selection macro `taskSELECT_HIGHEST_PRIORITY_TASK()` is wrapped in `#if configUSE_EDF_SCHEDULING == 1` at [tasks.c:199-241](FreeRTOS/FreeRTOS/Source/tasks.c#L199-L241). In the SMP branch (`configNUMBER_OF_CORES > 1`) selection goes through `prvSelectHighestPriorityTask(xCoreID)` instead. **We must hook EDF into the SMP path, not the current single-core macro.**

---

## Answering the `??` from `multi_proc_doc.md`

### 1. Interrupt handling / timer: one shared or per-core?

**One shared tick on core 0 is sufficient and is what we'll do.**

- The RP2040 SMP port already routes a single SysTick to core 0; that tick drives `xTickCount`, delayed-list maintenance, and deadline bookkeeping.
- Per-core rescheduling is initiated by **inter-core interrupts** (SIO FIFO IRQ on RP2040, already wired at [port.c:273-329](FreeRTOS/FreeRTOS/Source/portable/ThirdParty/GCC/RP2040/port.c#L273-L329)). `prvYieldCore(coreID)` sends that IPI.
- What we need to add: on each tick (core 0), after releasing jobs / updating deadlines, evaluate whether either core needs to preempt. If so, call `prvYieldCore(otherCore)` to pull the new task onto the peer. This is covered under "Tick Hook" below.

### 2. How to stop/start each core

- **Start:** `vTaskStartScheduler()` → port's `xPortStartScheduler()` already calls `multicore_launch_core1(prvDisableInterruptsAndPortStartSchedulerOnCore)` ([port.c:367-368](FreeRTOS/FreeRTOS/Source/portable/ThirdParty/GCC/RP2040/port.c#L367-L368)). Core 1 enters the same scheduler code path. Nothing new needed here.
- **Stop a core:** for our workloads, "stopping" means running the idle task on that core. Global EDF naturally does this when there is no work. We do not expose an explicit core-shutdown API — it isn't required by the spec.
- **Stop a task on a core (spec requirement "remove a task from a processor"):** implemented in user-facing API `vTaskRemoveFromCore(handle)` below.

### 3. Dispatch & migration

- **Partitioned EDF:** decreasing first-fit at admission time; jobs never cross cores. `uxCoreAffinityMask` is pinned to exactly one core by admission.
- **Global EDF:** single global deadline-sorted ready list. The top **m = configNUMBER_OF_CORES** jobs are dispatched to any available cores; jobs may migrate between cores on preemption/resumption.

### 4. One scheduler or per-core scheduler?

- **Partitioned:** each core runs its own EDF scheduler over its own ready list (`xEDFReadyList_Core[i]`). There is no global view during dispatch. The admission controller is the only global component.
- **Global:** there is logically one scheduler, but the existing SMP machinery still invokes `prvSelectHighestPriorityTask(coreID)` independently on each core. We implement the global decision by having both invocations consult the **same** `xEDFReadyList` and assign the top-`m` jobs to cores based on `xTaskRunState`.

---

## Required Config Changes (`FreeRTOSConfig.h`)

Current state at [FreeRTOSConfig.h:125](FreeRTOS/FreeRTOS/Demo/ThirdParty/Community-Supported-Demos/CORTEX_M0+_RP2040/Standard/FreeRTOSConfig.h#L125):

```c
#define configNUMBER_OF_CORES                   1
#define configTICK_CORE                         0
#define configRUN_MULTIPLE_PRIORITIES           0
```

Change to:

```c
#define configNUMBER_OF_CORES                   2   /* modern name; the FreeRTOS-SMP README calls it configNUM_CORES — we keep NUMBER_OF_CORES because tasks.c already uses it throughout */
#define configTICK_CORE                         0
#define configRUN_MULTIPLE_PRIORITIES           1   /* MANDATORY for EDF on SMP: every EDF job has its own absolute-deadline priority, so without this the lower-priority job never runs on the other core */
#define configUSE_CORE_AFFINITY                 1   /* Needed for partitioned pinning + migration API */

/* Task 4 — multiprocessor EDF strategy.  Exactly one of these should be 1. */
#define configGLOBAL_EDF_ENABLE                 1    /* default */
#define configPARTITIONED_EDF_ENABLE            0

/* Safety: reconcile mutual exclusion + default. */
#if ( configGLOBAL_EDF_ENABLE == 1 ) && ( configPARTITIONED_EDF_ENABLE == 1 )
    #error "Choose exactly one of configGLOBAL_EDF_ENABLE / configPARTITIONED_EDF_ENABLE"
#endif
#if ( configGLOBAL_EDF_ENABLE == 0 ) && ( configPARTITIONED_EDF_ENABLE == 0 )
    #undef  configGLOBAL_EDF_ENABLE
    #define configGLOBAL_EDF_ENABLE             1    /* Spec: global EDF default when neither specified */
#endif
```

Also turn off `configUSE_SRP` and `configUSE_CBS` for the Task-4 demo binaries — the spec explicitly waives resource sharing, and the SRP/CBS code paths assume single-core semantics (system ceiling, shared run-time stack). Keep them compile-guarded so the other tasks' binaries still build with them on.

---

## Data-Structure Changes

### TCB additions (inside `configUSE_EDF_SCHEDULING == 1` and `configNUMBER_OF_CORES > 1`)

```c
BaseType_t  xAssignedCore;      /* Partitioned: pinned core. Global: -1 (any). */
TickType_t  xJobRemainingTicks; /* Optional: for deadline-miss handling and tracing. */
```

`xAssignedCore` seeds `uxCoreAffinityMask` (`1u << xAssignedCore`) for partitioned mode. For global mode all tasks get `uxCoreAffinityMask = (1u << configNUMBER_OF_CORES) - 1u`.

### Partitioned: per-core EDF ready lists

Replace the single `xEDFReadyList` with an array when partitioned is active:

```c
#if ( configPARTITIONED_EDF_ENABLE == 1 )
    PRIVILEGED_DATA static List_t  xEDFReadyList_Core[ configNUMBER_OF_CORES ];
    PRIVILEGED_DATA static uint64_t ullCoreUtilMicro[ configNUMBER_OF_CORES ];  /* sum U_i × 10^6 on each core */
#else
    PRIVILEGED_DATA static List_t xEDFReadyList;   /* existing global list */
#endif
```

The existing `prvAddTaskToReadyList` macro at [tasks.c:339-365](FreeRTOS/FreeRTOS/Source/tasks.c#L339-L365) becomes a dispatch:

- Partitioned → insert into `xEDFReadyList_Core[pxTCB->xAssignedCore]`.
- Global → insert into the single `xEDFReadyList` (unchanged).

### Pending admission list (new, for partitioned)

Per the existing `multi_proc_doc.md` note, we accumulate tasks before the scheduler starts and bin-pack with decreasing first-fit:

```c
#if ( configPARTITIONED_EDF_ENABLE == 1 )
    PRIVILEGED_DATA static List_t xEDFPendingPartitionList;  /* sorted DESC by utilization */
#endif
```

On `vTaskStartScheduler`, iterate the pending list highest-U first and assign each task to the **first** core that still has capacity. Reject (and log) any that do not fit. After partitioning, tasks are pushed to `xEDFReadyList_Core[coreID]` for their first release.

---

## Admission Control

### Global EDF feasibility (implicit deadlines, `m` processors)

The course explicitly shows (via the **Dhall effect**, [l14/l15](l14-multiproc.txt)) that the naive bound `sum(U_i) ≤ m` is **not** sufficient for global EDF — a task set with total utilization arbitrarily close to 1 can miss deadlines under global EDF on any `m`. And l14 says bluntly: *"Global EDF is not optimal … Many sufficient tests (most of them incomparable)"*. The lecture does not prescribe one particular test; we pick one and justify it in `design_MP.md`.

We use these two checks together:

- **Necessary (pruning):** `sum(U_i) ≤ m` and each `U_i ≤ 1`. Reject immediately if either fails.
- **Sufficient (Goossens-Funk-Baruah / GFB):** `sum(U_i) ≤ m − (m − 1) · max(U_i)`. GFB is the standard closed-form sufficient test for global EDF with implicit deadlines (Buttazzo §10; Goossens-Funk-Baruah 2003). It is known to be pessimistic for task sets with one high-utilization task (that's the Dhall case), which is the right conservative behaviour for us — the assignment asks for admission control, not maximum schedulable load.

> Lecture 16 ([l16-global-rta.txt](l16-global-rta.txt)) covers **RTA-style** response-time analysis for global scheduling, but only the fixed-priority variant (RM/DM). It does not map cleanly to global EDF, so we do not use the RTA recurrence for our admission decision.

```c
static BaseType_t prvEDFAdmissionGlobal( const EDFAdmissionTaskParams_t * pxCand,
                                         const char ** ppcReason )
{
    const uint32_t ulScale = 1000000UL;
    uint64_t ullSumU = 0ULL, ullMaxU = 0ULL;
    const ListItem_t * pxIt;

    /* U_candidate */
    uint64_t ullUcand = ( ( uint64_t ) pxCand->xC * ulScale ) / pxCand->xT;
    if( ullUcand > ulScale ) { *ppcReason = "global U_i > 1"; return pdFAIL; }
    ullSumU = ullUcand;
    ullMaxU = ullUcand;

    for( pxIt  = listGET_HEAD_ENTRY( &xEDFTaskRegistryList );
         pxIt != listGET_END_MARKER( &xEDFTaskRegistryList );
         pxIt  = listGET_NEXT( pxIt ) )
    {
        const TCB_t * pxTCB = listGET_LIST_ITEM_OWNER( pxIt );
        uint64_t ullU = ( ( uint64_t ) pxTCB->xWcetTicks * ulScale ) / pxTCB->xPeriod;
        ullSumU += ullU;
        if( ullU > ullMaxU ) { ullMaxU = ullU; }
    }

    /* GFB: sum(U) <= m - (m-1) * max(U) */
    uint64_t ullBound = ( uint64_t ) configNUMBER_OF_CORES * ulScale
                      - ( ( uint64_t ) ( configNUMBER_OF_CORES - 1 ) ) * ullMaxU;
    if( ullSumU <= ullBound ) { *ppcReason = "global GFB ok"; return pdPASS; }

    *ppcReason = "global GFB fails";
    return pdFAIL;
}
```

### Partitioned EDF admission (FFD / decreasing first-fit, per-core `U ≤ 1`)

> This is the exact algorithm from [l14/l15](l15-partitioned.txt): "Most used heuristic: First Fit Decreasing (FFD) — sort jobs in decreasing order of utilizations `U₁ > U₂ > … > Uₙ`; pack `Uᵢ` into the oldest open processor it fits; open a new processor only if none fits; declare UNSCHEDULABLE when all `m` processors are exhausted." The course also gives the utilization guarantee `Ub = (m+1)/2`: any task set with `sum(U_i) ≤ (m+1)/2` is guaranteed schedulable by FFD+EDF (sufficient, not necessary). We do not test `Ub` directly — we run FFD and let the per-core `U ≤ 1` test accept or reject.

Each core independently must satisfy `sum(U_i on core k) ≤ 1` (necessary **and** sufficient for EDF with implicit deadlines on a single core). Two entry points:

1. **Pre-start batching.** Tasks call `xTaskCreateEDF(...)`. Under partitioned mode we *do not* partition at creation time — we append to `xEDFPendingPartitionList` sorted DESC by `U_i`. `vTaskStartScheduler` triggers a one-shot bin-pack pass.
2. **Runtime creation.** Partition the incoming task immediately using first-fit over the live `ullCoreUtilMicro[]`. Accept iff some core can absorb `U_i`.

```c
static BaseType_t prvPartitionTaskFirstFit( TCB_t * pxNewTCB, const char ** ppcReason )
{
    const uint64_t ulScale = 1000000UL;
    uint64_t ullU = ( ( uint64_t ) pxNewTCB->xWcetTicks * ulScale ) / pxNewTCB->xPeriod;

    for( BaseType_t c = 0; c < ( BaseType_t ) configNUMBER_OF_CORES; c++ )
    {
        if( ullCoreUtilMicro[ c ] + ullU <= ulScale )
        {
            pxNewTCB->xAssignedCore      = c;
            pxNewTCB->uxCoreAffinityMask = 1U << c;
            ullCoreUtilMicro[ c ]       += ullU;
            *ppcReason = "partitioned FF fit";
            return pdPASS;
        }
    }
    *ppcReason = "partitioned FF no fit";
    return pdFAIL;
}
```

The decreasing-first-fit wrapper sorts the pending list by `U_i` descending, then loops calling `prvPartitionTaskFirstFit` in order. This yields DFF without a separate algorithm.

---

## Selection / Dispatch

### Global EDF

Override the SMP `prvSelectHighestPriorityTask(xCoreID)` by wrapping it in an EDF branch. Algorithm:

1. Build a conceptual list of the top-`m` jobs in `xEDFReadyList`.
2. If `pxCurrentTCBs[xCoreID]` is still among them → keep it.
3. Otherwise pick the highest-deadline-priority ready job whose `xTaskRunState == taskTASK_NOT_RUNNING`. Set `xTaskRunState = xCoreID`, set previous task to `taskTASK_NOT_RUNNING`.
4. If no EDF job is ready, fall back to the standard priority list (idle / non-EDF tasks).

This is essentially what single-core EDF does today (see [tasks.c:199-241](FreeRTOS/FreeRTOS/Source/tasks.c#L199-L241)) but iterated per core and filtered by `xTaskRunState`.

```c
#if ( configGLOBAL_EDF_ENABLE == 1 )
static void prvEDFSelectGlobal( BaseType_t xCoreID )
{
    /* Walk xEDFReadyList head-first. The list is sorted by absolute deadline.
     * Skip entries already running on the OTHER core. First non-running entry
     * becomes the new current task on this core. */
    const ListItem_t * pxIt  = listGET_HEAD_ENTRY( &xEDFReadyList );
    const ListItem_t * pxEnd = listGET_END_MARKER( &xEDFReadyList );

    for( ; pxIt != pxEnd; pxIt = listGET_NEXT( pxIt ) )
    {
        TCB_t * pxTCB = listGET_LIST_ITEM_OWNER( pxIt );
        if( ( pxTCB->xTaskRunState == taskTASK_NOT_RUNNING ) ||
            ( pxTCB->xTaskRunState == xCoreID ) )
        {
            pxCurrentTCBs[ xCoreID ]->xTaskRunState = taskTASK_NOT_RUNNING;
            pxTCB->xTaskRunState = xCoreID;
            pxCurrentTCBs[ xCoreID ] = pxTCB;
            return;
        }
    }

    /* No EDF job free; fall back to the standard priority-list selection. */
    prvSelectHighestPriorityTask( xCoreID );
}
#endif
```

### Partitioned EDF

Per-core: look at head of `xEDFReadyList_Core[xCoreID]`. No cross-core iteration. If empty, fall back to the standard SMP selector.

```c
#if ( configPARTITIONED_EDF_ENABLE == 1 )
static void prvEDFSelectPartitioned( BaseType_t xCoreID )
{
    List_t * pxReady = &xEDFReadyList_Core[ xCoreID ];
    if( listLIST_IS_EMPTY( pxReady ) == pdFALSE )
    {
        TCB_t * pxTop = listGET_OWNER_OF_HEAD_ENTRY( pxReady );
        pxCurrentTCBs[ xCoreID ]->xTaskRunState = taskTASK_NOT_RUNNING;
        pxTop->xTaskRunState                    = xCoreID;
        pxCurrentTCBs[ xCoreID ]                = pxTop;
        return;
    }
    prvSelectHighestPriorityTask( xCoreID );
}
#endif
```

### Wiring the selectors

Define a new macro `taskSELECT_HIGHEST_PRIORITY_TASK( xCoreID )` that delegates based on the mode. Today the SMP branch in [tasks.c:263](FreeRTOS/FreeRTOS/Source/tasks.c#L263) just calls `prvSelectHighestPriorityTask`. Replace that with:

```c
#if ( configNUMBER_OF_CORES > 1 ) && ( configUSE_EDF_SCHEDULING == 1 )
    #if ( configGLOBAL_EDF_ENABLE == 1 )
        #define taskSELECT_HIGHEST_PRIORITY_TASK( xCoreID )   prvEDFSelectGlobal( xCoreID )
    #elif ( configPARTITIONED_EDF_ENABLE == 1 )
        #define taskSELECT_HIGHEST_PRIORITY_TASK( xCoreID )   prvEDFSelectPartitioned( xCoreID )
    #endif
#elif ( configNUMBER_OF_CORES > 1 )
    #define taskSELECT_HIGHEST_PRIORITY_TASK( xCoreID )       prvSelectHighestPriorityTask( xCoreID )
#endif
```

---

## Tick-Hook Deadline Release + Cross-Core Preempt

The existing `xTaskIncrementTick` already releases delayed tasks and re-inserts them in the ready list. After the existing body, add the **cross-core preempt decision**:

```c
#if ( configNUMBER_OF_CORES > 1 ) && ( configUSE_EDF_SCHEDULING == 1 )
    /* If the new job's deadline is earlier than a currently running task on
     * some core, poke that core.  For partitioned, only poke the assigned core. */
    for( BaseType_t c = 0; c < configNUMBER_OF_CORES; c++ )
    {
        if( pxCurrentTCBs[ c ] == NULL ) continue;
        if( prvShouldPreemptCore( c ) == pdTRUE )
        {
            prvYieldCore( c );
        }
    }
#endif
```

`prvShouldPreemptCore` inspects the head of the corresponding ready list (global or per-core) vs. `pxCurrentTCBs[c]->xAbsoluteDeadline`.

---

## Public API Additions (in [task.h](FreeRTOS/FreeRTOS/Source/include/task.h))

Spec calls out "specify the core a task runs on, remove a task from a processor, or change the processor". Minimum set:

```c
/* Hard-pin an EDF task to a core (partitioned mode or as a user override). */
BaseType_t xTaskCreateEDFOnCore( TaskFunction_t       pxTaskCode,
                                 const char * const   pcName,
                                 configSTACK_DEPTH_TYPE uxStackDepth,
                                 void * const         pvParameters,
                                 TickType_t           xPeriod,
                                 TickType_t           xRelativeDeadline,
                                 TickType_t           xWcet,
                                 BaseType_t           xCoreID,
                                 TaskHandle_t * const pxCreated );

/* Remove a task from its assigned core (deletes under partitioned; drops
 * affinity and lets global re-dispatch under global). */
void vTaskRemoveFromCore( TaskHandle_t xTask );

/* Migrate a task to a new core.
 *   Partitioned: moves between bins; may fail with pdFAIL if the target core
 *                cannot absorb the utilization.
 *   Global:      only changes the affinity hint; actual dispatch stays global. */
BaseType_t xTaskMigrateToCore( TaskHandle_t xTask, BaseType_t xNewCoreID );
```

Internally all three reuse the existing `vTaskCoreAffinitySet` ([task.h:1440](FreeRTOS/FreeRTOS/Source/include/task.h#L1440)) plus our partition/unpartition bookkeeping (updating `ullCoreUtilMicro[]`, `xAssignedCore`, and the ready-list membership).

---

## Kernel Placement Policy (explicit decision)

- **Tick and FreeRTOS bookkeeping (tick hook, delayed-list scan, admission) run on core 0.** Enforced by `configTICK_CORE = 0` (already set).
- **Task execution happens on either core.** `prvSelectHighestPriorityTask` is invoked on both. This is the "kernel pinned to core 0 for timing, dispatcher per-core" hybrid — neither of the two extremes listed in the assignment prose, but it is the natural SMP-port behaviour and simplest to reason about.

Document this explicitly in `design_MP.md`.

---

## Build/Flow Diagram (partitioned)

```
xTaskCreateEDF(...)                       [pre-start]
   |
   v
append to xEDFPendingPartitionList (sorted DESC by U_i)
   |
   v
vTaskStartScheduler()
   |
   v
prvPartitionPendingDecreasingFirstFit():
   for each pending task (high-U first):
      find first core with free capacity
      assign  (xAssignedCore, uxCoreAffinityMask, ullCoreUtilMicro[c])
      push to xEDFReadyList_Core[c]
   |
   v
Port: xPortStartScheduler → multicore_launch_core1 → both cores enter scheduler loop
   |
   v
Per-tick:
   release any delayed task into its core's ready list
   for each core: if head of that core's list has earlier deadline than current, prvYieldCore(c)
```

## Build/Flow Diagram (global)

```
xTaskCreateEDF(...)                       [pre-start or runtime]
   |
   v
prvEDFAdmissionGlobal  (GFB test against live xEDFTaskRegistryList)
   |                   PASS → insert into xEDFTaskRegistryList + xEDFReadyList
   +---- FAIL → rejected, counter bumped
   |
   v
vTaskStartScheduler() → both cores enter scheduler loop
   |
   v
Per-tick on core 0:
   release delayed jobs into xEDFReadyList (sorted by deadline)
   compare xEDFReadyList head-pair vs pxCurrentTCBs[0], pxCurrentTCBs[1]
   prvYieldCore(c) on whichever core is now running a later-deadline job
```

---

## Required Deliverables Mapping

- `changes_MP.md` — list every edited file (`tasks.c`, `task.h`, `FreeRTOS.h`, `FreeRTOSConfig.h`, new `main_mp_test.c`) and the functions changed/added (everything under the prv/x headings above).
- `design_MP.md` — mode flowcharts (global/partitioned), GFB and per-core LL feasibility tests, kernel-placement decision (pinned-tick / per-core dispatch), migration/removal semantics.
- `testing_MP.md` — test cases below.
- `bugs_MP.md` — deferred items (e.g. GFB is sufficient not necessary, so we will over-reject some feasible sets; migration under global is a no-op beyond affinity).
- `future_MP.md` — e.g. plug in DA-LC admission for global EDF, add runtime repartitioning, per-core tick fallback if core 0 is saturated.

---

## Test Plan (for `testing_MP.md`)

1. **Single-core sanity.** Keep `configNUMBER_OF_CORES = 1` and rerun the existing EDF suite to confirm no regression.
2. **Partitioned — clean fit.** 2 tasks, `U = 0.6` + `U = 0.7`. Must admit with one per core. GPIO trace should show one task per core, never both on the same.
3. **Partitioned — DFF fit.** 5 tasks of utilization {0.4, 0.3, 0.3, 0.25, 0.25}. DFF assigns 0.4+0.3+0.25=0.95 to core 0 and 0.3+0.25=0.55 to core 1. First-fit (not decreasing) would mis-pack. Verify the prints match the decreasing variant.
4. **Partitioned — overload rejects.** 3 tasks of `U = 0.8`. Second task admits; third rejected. Admission counter bumps.
5. **Global — GFB pass.** 3 tasks of `U = 0.6`. Sum = 1.8, max = 0.6, bound = 2 − 1·0.6 = 1.4. Rejected by GFB. Add one more `U = 0.5` task instead: sum 1.7, bound 2 − 0.5 = 1.5 → still rejects. Tune until GFB admits and verify.
6. **Global — migration.** Under global, start a task and assert via traces that successive jobs may run on different cores depending on what the other core is doing.
7. **Migration API.** Create a task pinned to core 0, call `xTaskMigrateToCore(task, 1)`, assert subsequent GPIO bursts come from core 1. In partitioned mode, verify the utilization accounting moves with it.
8. **Remove from core.** `vTaskRemoveFromCore(task)` in partitioned mode; assert `ullCoreUtilMicro[]` drops and the task is gone from that core's ready list.
9. **Stress 100 tasks.** Mirror `main_srp_test_100.c` layout but without SRP — 100 implicit-deadline tasks that pass GFB/DFF. Confirm no deadline misses over 15 s.
10. **Mode switch correctness.** Rebuild with `configPARTITIONED_EDF_ENABLE=1` and rerun tests 2-4; rebuild with `configGLOBAL_EDF_ENABLE=1` and rerun tests 5-6. Same source tree.

---

## Order of Work

1. Flip config, resolve build errors (mainly `pxCurrentTCB` references that must become `pxCurrentTCBs[xCoreID]` inside the EDF hot paths — the SMP branch already does this, but the EDF single-core macro at [tasks.c:199-241](FreeRTOS/FreeRTOS/Source/tasks.c#L199-L241) does not).
2. Add `xAssignedCore` to TCB and wire through `xTaskCreateEDFOnCore`.
3. Implement `prvEDFSelectGlobal` and route via the SMP selection macro. Validate with test 5/6.
4. Implement per-core ready lists + `prvEDFSelectPartitioned` + pending list + DFF. Validate with tests 2-4.
5. Implement `vTaskRemoveFromCore` and `xTaskMigrateToCore`. Validate with tests 7-8.
6. Add the 100-task stress binary. Validate with test 9.
7. Write deliverables.
