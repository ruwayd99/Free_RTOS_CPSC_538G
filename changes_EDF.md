# changes_EDF.md

This document enumerates every file that was altered or added in order to
support Earliest-Deadline-First scheduling, constrained-deadline admission
control, and transient-overload handling in FreeRTOS on the Raspberry Pi
Pico (RP2040). Paths are relative to the repository root.

---

## 1. Files altered

### 1.1 `FreeRTOS/FreeRTOS/Source/tasks.c`

This is the single largest change site. All EDF logic lives in this file
behind `#if ( configUSE_EDF_SCHEDULING == 1 )` guards so the default
fixed-priority FreeRTOS behaviour is preserved when EDF is disabled.

**New module-scope static data**

| Symbol | Purpose |
|---|---|
| `xEDFReadyList` | Single deadline-sorted ready list. Every runnable EDF task lives here, sorted by `xAbsoluteDeadline` (via `listSET_LIST_ITEM_VALUE`). |
| `xEDFTaskRegistryList` | Long-lived registry of every admitted EDF task. Needed so the DBF admission test can sweep the whole admitted set (not just ready tasks). |
| `uxEDFAcceptedTaskCount` | Live count of admitted EDF tasks (incremented on accept, decremented on delete). |
| `uxEDFRejectedTaskCount` | Lifetime count of rejected `xTaskCreateEDF` calls. |

**Modified core scheduling macros**

| Macro | Change |
|---|---|
| `taskSELECT_HIGHEST_PRIORITY_TASK()` | When `xEDFReadyList` is non-empty, picks `listGET_OWNER_OF_HEAD_ENTRY( &xEDFReadyList )` instead of the fixed-priority lists. Falls back to the original FreeRTOS macro when the EDF list is empty (so the idle task and non-EDF helpers still run). |
| `prvAddTaskToReadyList( pxTCB )` | If the task was created via `xTaskCreateEDF` (detected by `pxTCB->xPeriod > 0`), inserts it into `xEDFReadyList` with `listSET_LIST_ITEM_VALUE( xStateListItem, xAbsoluteDeadline )` so `vListInsert` keeps the list deadline-ordered. Non-EDF tasks keep the original priority-list insertion. |

**New TCB_t fields**

Added inside `struct tskTaskControlBlock` (declared unconditionally so every
call site compiles even when EDF is off, but only written when EDF is on):

| Field | Meaning |
|---|---|
| `xPeriod` | Task period `T` (ticks). Zero for non-EDF tasks. |
| `xRelativeDeadline` | Relative deadline `D` (ticks). `D ≤ T` enforced at admission. |
| `xAbsoluteDeadline` | Deadline of the currently-released job (tick count). |
| `xWcetTicks` | Declared WCET `C` (ticks). Used both for admission and tick-based budget accounting. |
| `xLastReleaseTick` | Absolute release tick of the current job. |
| `xJobExecTicks` | Ticks consumed by the current job (accumulated inside `xTaskIncrementTick`). |
| `xSRPBlockingBound` | SRP blocking term `B_i`. Always zero when SRP is disabled. |
| `uxEDFFlags` | Bitfield reserved for EDF-state bookkeeping (e.g. drop-pending). |
| `xEDFRegistryListItem` | `ListItem_t` used to link the TCB into `xEDFTaskRegistryList`. |

**New private admission helpers**

| Function | Role |
|---|---|
| `prvEDFAdmissionControl` | Entry point. Scans `xEDFTaskRegistryList`; if any admitted task has `D < T` (or the candidate does), dispatches to the exact DBF test. Otherwise uses LL. |
| `prvEDFAdmissionImplicit` | Liu-Layland utilization test. Uses fixed-point micro-units (`scale = 1 000 000`) so the kernel never calls floating-point. Checks `Σ (C_i+B_i)/T_i ≤ 1`. |
| `prvEDFAdmissionConstrained` | Exact processor-demand (DBF) sweep. Horizon = LCM of all task periods, capped at `configEDF_MAX_ANALYSIS_TICKS`; sweep starts at `min(D_i)` to avoid spurious rejections on tiny `t` when SRP blocking is non-zero. Accepts iff `DBF(t) + B_max ≤ t` for every integer `t ∈ [min D, horizon]`. |
| `prvGCD` / `prvLCM` | Integer helpers used by `prvEDFAdmissionConstrained` to compute the hyperperiod. |

**New public EDF API**

| Function | Role |
|---|---|
| `xTaskCreateEDF` | Builds an `EDFAdmissionTaskParams_t` from `(T, D, C)`, runs `prvEDFAdmissionControl`, and — only if `pdPASS` — calls the regular `prvInitialiseNewTask` / `prvAddTaskToReadyList` path with the EDF TCB fields populated. On reject, no TCB is allocated and `uxEDFRejectedTaskCount` is bumped. |
| `xTaskCreateEDFWithStack` | Same as above, but the caller owns the stack buffer. Used by SRP stack-sharing demo (same preemption level ⇒ shared static buffer). |
| `vTaskDelayUntilNextPeriod` | Called at the *end* of every periodic job. Advances `xLastReleaseTick += xPeriod`, recomputes `xAbsoluteDeadline`, zeroes `xJobExecTicks`, and then calls the stock `vTaskDelayUntil` to block until the next release boundary. Emits `[EDF][finish]` and `[EDF][release]` traces. |
| `uxTaskGetEDFAdmittedCount` | Critical-section-wrapped reader for the live admitted count. |
| `uxTaskGetEDFRejectedCount` | Critical-section-wrapped reader for the lifetime reject count. |

**New private overload-management helper**

| Function | Role |
|---|---|
| `prvEDFDropLateJob` | Chosen transient-overload policy. When `xTaskIncrementTick` detects `xTaskGetTickCount() >= xAbsoluteDeadline` and the job is not yet finished, this function emits a `[EDF][drop]` trace, releases any SRP units the task still holds, advances the release/deadline window, removes the TCB from `xEDFReadyList`, and re-inserts it into the delayed list via `prvAddCurrentTaskToDelayedList` until the *next* release tick. |

**Modified existing functions**

| Function | Change |
|---|---|
| `xTaskIncrementTick` | Added per-tick deadline-miss check. For every EDF task that is currently running or ready with `xAbsoluteDeadline <= xConstTickCount`, invokes `prvEDFDropLateJob`. For tasks that have ticked but not yet finished, increments `xJobExecTicks`. |
| `prvDeleteTCB` | Unlinks the TCB from `xEDFTaskRegistryList` and decrements `uxEDFAcceptedTaskCount` if the task was EDF. |
| `vTaskStartScheduler` / `prvInitialiseTaskLists` | Calls `vListInitialise( &xEDFReadyList )` and `vListInitialise( &xEDFTaskRegistryList )` alongside the stock ready/delayed lists. |
| `vTaskSwitchContext` (or the equivalent SMP selector) | Tie-breaks in favour of CBS servers by subtracting 1 from the deadline key when inserting a CBS TCB. EDF tasks alone are unaffected. |

### 1.2 `FreeRTOS/FreeRTOS/Source/include/task.h`

Added the public prototypes for the API listed above, each wrapped in
`#if ( configUSE_EDF_SCHEDULING == 1 )`. No existing declaration was
changed and the additions are strictly additive so applications that never
call any `*EDF*` symbol see no diff.

### 1.3 `FreeRTOS/FreeRTOS/Source/include/FreeRTOS.h`

Added two conditional macros:

```c
#if ( configEDF_TRACE_ENABLE == 1 )
    #define edfTRACE( ... )    configPRINTF( ( __VA_ARGS__ ) )
#else
    #define edfTRACE( ... )
#endif
```

`edfTRACE` is the single trace hook used by `xTaskCreateEDF`,
`vTaskDelayUntilNextPeriod`, `prvEDFDropLateJob`, and the per-tick miss
detector. It reuses the existing `configPRINTF` hook so applications can
redirect output to any serial backend.

### 1.4 `FreeRTOS/FreeRTOS/Demo/ThirdParty/Community-Supported-Demos/CORTEX_M0+_RP2040/Standard/FreeRTOSConfig.h`

Added the user-visible toggles and bounds:

```c
#define configUSE_EDF_SCHEDULING        1
#define configEDF_TRACE_ENABLE          1
#define configPRINTF( x )               printf x
#define configEDF_MAX_ANALYSIS_TICKS    100000U
```

Plus the SRP and CBS toggles which are deliberately wrapped in
`#ifndef` so a per-target `target_compile_definitions` override wins.
When `configUSE_EDF_SCHEDULING` is set to `0`, every change above compiles
out and FreeRTOS reverts to stock fixed-priority scheduling.

### 1.5 `FreeRTOS/FreeRTOS/Demo/ThirdParty/Community-Supported-Demos/CORTEX_M0+_RP2040/Standard/main.c`

Added routing for `mainCREATE_EDF_TEST`: when that symbol is defined the
extern `main_edf_test()` is called from `vLaunch()` in place of the stock
blinky/full demos. All three EDF test binaries route through this single
hook.

### 1.6 `FreeRTOS/FreeRTOS/Demo/ThirdParty/Community-Supported-Demos/CORTEX_M0+_RP2040/Standard/CMakeLists.txt`

Added the helper `add_single_test_bin()` and three EDF test targets:

* `main_edf_test`            — basic 4-task scenario plus runtime add.
* `main_edf_test_dynamic`    — 8-task dynamic admission scenario.
* `main_edf_test_100`        — 100-task scaling/stress scenario.

Each target sets `mainCREATE_EDF_TEST=1` via
`target_compile_definitions` so `main.c` knows which demo entry to call.

---

## 2. Files added

| File | Purpose |
|---|---|
| `.../Standard/main_edf_test.c` | Basic EDF demo. Two periodic tasks created before the scheduler starts, plus a runtime creator that adds two more tasks while the scheduler is running (one expected accept, one expected reject). Drives GPIO 10–13 for logic-analyzer observation. |
| `.../Standard/main_edf_test_dynamic.c` | 8-task dynamic admission demo. Six tasks created at startup, then a non-EDF orchestrator task adds two more at runtime (one guaranteed-reject `IMPL4_OVERLOAD`, one guaranteed-accept `CONS4_LATE_OK`). Drives GPIO 10–13 and 18–21. |
| `.../Standard/main_edf_test_100.c` | 100-task scaling demo required by the README. 50 implicit-deadline + 50 constrained-deadline periodic tasks admitted before scheduler start. Eight monitored indices drive GPIO; a non-EDF monitor task prints admitted/rejected counts every 2 s. |

---

## 3. Summary of interfaces exposed to user code

```c
/* Creation */
BaseType_t xTaskCreateEDF( TaskFunction_t pxTaskCode,
                           const char * const pcName,
                           configSTACK_DEPTH_TYPE uxStackDepth,
                           void * const pvParameters,
                           TickType_t xPeriod,            /* T */
                           TickType_t xRelativeDeadline,  /* D */
                           TickType_t xWcetTicks,         /* C */
                           TaskHandle_t * const pxCreatedTask );

/* Per-period body helper */
void        vTaskDelayUntilNextPeriod( TickType_t * pxPreviousWakeTime );

/* Observability */
UBaseType_t uxTaskGetEDFAdmittedCount( void );
UBaseType_t uxTaskGetEDFRejectedCount( void );
```

All of these are available only when `configUSE_EDF_SCHEDULING == 1`.