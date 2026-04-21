# changes_SRP.md 

This document enumerates every file that was altered or added in order
to support the Stack Resource Policy on top of the EDF scheduler from
Task 1. It also covers the run-time stack-sharing mechanism. Paths are relative to the repository root.

---

## 1. Files altered

### 1.1 `FreeRTOS/FreeRTOS/Source/tasks.c`

All SRP logic lives in this file behind `#if ( configUSE_SRP == 1 )`
guards. Stack-sharing extensions live behind an additional
`#if ( configSRP_STACK_SHARING == 1 )` guard so the two features can be
toggled independently.

**New module-scope static data**

| Symbol | Purpose |
|---|---|
| `xSRPResources[ configMAX_SRP_RESOURCES ]` | Fixed-size resource registry. Each slot holds total/available units, the current holder (if any), the current holder's preemption level, and up to `configMAX_SRP_USERS_PER_RESOURCE` user registrations. |
| `uxSystemCeiling` | Live system ceiling `Π(t)`. Recomputed on every take/give and on drop-late-job cleanup. |

**New structure types**

| Type | Role |
|---|---|
| `SRPUserRegistration_t` | Per-user record: `uxPreemptionLevel`, `uxUnitsNeeded`, `xCriticalSectionTicks`. Populated at `vSRPResourceRegisterUser` time and consulted when computing a resource ceiling. |
| `SRPResourceControl_t` | Multi-unit resource: `uxTotalUnits`, `uxAvailableUnits`, the single current holder (task + level + units), plus the `xUsers[]` table. |

**New TCB_t fields**

| Field | Meaning |
|---|---|
| `uxPreemptionLevel` | `π_i = portMAX_DELAY - D_i`: Baker's preemption level. Higher ⇒ tighter deadline ⇒ higher priority for the SRP gate. |
| `xSRPBlockingBound` | Conservative `B_i` (worst single critical section from any lower-level task that can block this task). Integrated into both LL and DBF admission. |
| `uxSRPHeldResources` | Bitmask of currently-held resource indices. Used by `prvEDFDropLateJob` to release everything on a late-job drop. |
| *(Stack sharing only)* `pxPrivateContextSnapshot` | Heap buffer holding the task's initial register frame so the task can be re-dispatched "fresh" after a same-level peer has trampled the shared buffer. |
| `pxSnapshotTopOfStack` | Pre-recorded `pxTopOfStack` at creation time. Restored on fresh-start dispatch. |
| `uxPrivateContextWords` | Size of the snapshot in stack words. |
| `pxSharedStackBuffer` | Base of the group's shared `.bss` stack buffer. `NULL` for tasks that own a private stack. |
| `uxSharedStackDepth` | Size of `pxSharedStackBuffer` in words. |
| `xSharedStackFreshStart` | `pdTRUE` on the very next dispatch ⇒ PendSV wants the initial frame restored; `pdFALSE` ⇒ live mid-execution context is already on the buffer and must not be overwritten. |

**New private SRP helpers**

| Function | Role |
|---|---|
| `prvFindSRPResource` | Linear scan of `xSRPResources` by handle. |
| `prvSRPRecalculateSystemCeiling` | Scans every resource; for each one, computes `ceiling(r) = max π_i among users whose uxUnitsNeeded > uxAvailableUnits`. Takes the max across all resources into `uxSystemCeiling`. |
| `prvSRPComputeResourceCeilingIfTaken` | Hypothetical ceiling if a candidate took its units, used to compute `B_i` at admission. |
| `prvSRPComputeBlockingBoundForLevel` | For a candidate preemption level `π`, scans every user of every resource and returns the maximum critical-section length belonging to a *lower-level* user (`π_user < π`) whose resource ceiling (if that user took its units) would be `≥ π`. That is the classic SRP `B_i` formula. |
| `prvSRPCanTaskRun` | Implements the SRP gate: allow if `π_i > uxSystemCeiling`, or if the task is the current resource holder (re-entry), or if the task has a live hold. |
| `prvSRPCalculateTaskPreemptionLevel` | `π_i = portMAX_DELAY - D_i`. |
| *(Stack sharing)* `prvSharedBufferHasMidExecutionTask` | Walks `xEDFReadyList` + `pxCurrentTCB` for any TCB that uses the same shared-stack buffer, is at the same preemption level, and has `xSharedStackFreshStart == pdFALSE`. Used by the selector to avoid overwriting a peer's live context. |

**New public SRP API** (declared in `task.h`):

| Function | Role |
|---|---|
| `xSRPResourceCreate(uxMaxUnits)` | Reserves a free slot in `xSRPResources`, sets `uxTotalUnits == uxAvailableUnits = uxMaxUnits`, returns opaque handle. |
| `vSRPResourceRegisterUser(h, uxLevel, uxUnits, xCSTicks)` | Appends a user record to the resource. Must be called before any task that will take the resource is created, because admission reads the registered CS lengths to compute `B_i`. |
| `xSRPResourceTake(h, uxUnits)` | Takes `uxUnits` atomically under `taskENTER_CRITICAL`. Asserts availability (admission guarantees it). Updates holder, decrements `uxAvailableUnits`, recomputes `uxSystemCeiling`, emits `[SRP][TAKE]` trace. Re-entry by the current holder is allowed. |
| `vSRPResourceGive(h, uxUnits)` | Releases `uxUnits`, updates holder on full release, recomputes `uxSystemCeiling`, emits `[SRP][GIVE]` trace. |
| `uxSRPGetSystemCeiling()` | Read the current live ceiling (used by demos). |

**Modified existing functions**

| Function | Change |
|---|---|
| `xTaskCreateEDF` | When `configUSE_SRP == 1`, populates `xCandidate.uxLevel = prvSRPCalculateTaskPreemptionLevel(D)` and `xCandidate.xB = prvSRPComputeBlockingBoundForLevel(level)` **before** calling admission. On accept, stores both values into the TCB (`uxPreemptionLevel`, `xSRPBlockingBound`) and emits `[SRP][task-meta]` trace. |
| `prvEDFAdmissionImplicit` | The LL sum becomes `Σ (C_i + B_i) / T_i`. When SRP is off, `B_i = 0` and the formula degrades cleanly. |
| `prvEDFAdmissionConstrained` | Adds the max `B_i` term at every sweep point: feasibility iff `DBF(t) + B_max ≤ t`. Also, the sweep start moves to `min(D)` because a non-zero `B_max` could wrongly fail at `t=1` otherwise. |
| `taskSELECT_HIGHEST_PRIORITY_TASK` | When SRP is on, selects via `prvEDFSelectRunnableTaskBySRP` instead of "first in list", this walks `xEDFReadyList` head-to-tail and picks the earliest-deadline task that **also passes** the SRP gate. If no EDF task is runnable, fall through to the stock priority path. |
| `prvEDFDropLateJob` | Releases every SRP unit the late task still holds and recomputes the system ceiling, so the ceiling cannot stay latched and starve future jobs. |
| `prvInitialiseNewTask` / `prvCreateTask` | Zero-initialize the new SRP and stack-sharing TCB fields so non-EDF/non-SRP tasks behave as stock FreeRTOS. |
| `vTaskSwitchContext` | New dispatch hook (see §1.1.1 below). |
| `vTaskDelayUntilNextPeriod` | When stack sharing is on and the task owns a private snapshot, sets `xSharedStackFreshStart = pdTRUE` before the blocking `vTaskDelayUntil`, so the **next** dispatch is treated as a fresh job start. |

#### 1.1.1 New public entry point — `xTaskCreateEDFWithStack`

Same signature as `xTaskCreateEDF` plus a caller-owned
`StackType_t * puxStackBuffer`. The heap allocates only the TCB and a
per-task snapshot buffer; the stack itself is the static buffer that
the caller passed in. Implemented alongside the other create helpers
in `tasks.c`.

### 1.2 `FreeRTOS/FreeRTOS/Source/include/task.h`

Added prototypes for `xSRPResourceCreate`, `vSRPResourceRegisterUser`,
`xSRPResourceTake`, `vSRPResourceGive`, `uxSRPGetSystemCeiling`, and
`xTaskCreateEDFWithStack`, each wrapped in `#if ( configUSE_SRP == 1 )`
(with the stack variant also guarded on `configSRP_STACK_SHARING`).
Also added the `SRPResourceHandle_t` opaque typedef so application
code never has to include the private `tasks.c` layout.

### 1.3 `FreeRTOS/FreeRTOS/Source/include/FreeRTOS.h`

Added defaults so non-demo builds stay safe:

```c
#ifndef configUSE_SRP
    #define configUSE_SRP               0
#endif
#ifndef configSRP_STACK_SHARING
    #define configSRP_STACK_SHARING     0
#endif
#ifndef configMAX_SRP_RESOURCES
    #define configMAX_SRP_RESOURCES     8
#endif
#ifndef configMAX_SRP_USERS_PER_RESOURCE
    #define configMAX_SRP_USERS_PER_RESOURCE 16
#endif
```

Plus the `srpTRACE` macro (same shape as `edfTRACE`) so SRP events can
be toggled independently of EDF trace output.

### 1.4 `FreeRTOS/FreeRTOS/Demo/.../Standard/FreeRTOSConfig.h`

```c
#define configUSE_SRP                    1
#define configMAX_SRP_RESOURCES          8
#define configMAX_SRP_USERS_PER_RESOURCE 16
#define configSRP_STACK_SHARING          0   /* flip to 1 for the 100-task demo */
```

All four toggles are wrapped in `#ifndef` so a CMake
`-DconfigSRP_STACK_SHARING=1` override wins without editing the file.

### 1.5 `FreeRTOS/FreeRTOS/Demo/.../Standard/CMakeLists.txt`

Added three SRP targets through the same `add_single_test_bin` helper
used for EDF:

* `main_srp_test`          : basic mutual exclusion + EDF interaction.
* `main_srp_test_dynamic`  : multi-unit, nested, runtime add/reject.
* `main_srp_test_100`      : 100-task quantitative stack-sharing study.

### 1.6 `FreeRTOS/FreeRTOS/Demo/.../Standard/main.c`

No structural change : the SRP tests reuse the `mainCREATE_EDF_TEST`
routing (they still enter through `main_edf_test`), so flashing an
SRP target simply replaces which `main_edf_test.c` file is linked.

---

## 2. Files added

| File | Purpose |
|---|---|
| `.../Standard/main_srp_test.c` | Baseline SRP demo: 4 tasks with two on a shared 1-unit resource, demonstrates the SRP gate and trace. |
| `.../Standard/main_srp_test_dynamic.c` | 3 multi-unit resources, nested re-entrant acquires, cross-resource nesting, one runtime-accept and one runtime-reject admission. |
| `.../Standard/main_srp_test_100.c` | Quantitative study: 100 tasks in 5 preemption-level groups (20 each). Runs once with `configSRP_STACK_SHARING = 0` and once with `= 1`, reporting heap usage + per-group high-water marks + total savings. |

---

## 3. Summary of public API added

```c
/* Resource lifecycle */
SRPResourceHandle_t xSRPResourceCreate( UBaseType_t uxMaxUnits );
void                vSRPResourceRegisterUser( SRPResourceHandle_t xResource,
                                              UBaseType_t uxPreemptionLevel,
                                              UBaseType_t uxUnitsNeeded,
                                              TickType_t  xCriticalSectionTicks );

/* Runtime take/give */
BaseType_t  xSRPResourceTake( SRPResourceHandle_t xResource, UBaseType_t uxUnits );
void        vSRPResourceGive( SRPResourceHandle_t xResource, UBaseType_t uxUnits );

/* Observability */
UBaseType_t uxSRPGetSystemCeiling( void );

/* Stack-sharing task creation */
BaseType_t  xTaskCreateEDFWithStack( TaskFunction_t,  const char *,
                                     configSTACK_DEPTH_TYPE, void *,
                                     TickType_t T, TickType_t D, TickType_t C,
                                     StackType_t * puxStackBuffer,
                                     TaskHandle_t * );
```

All entries are compiled out when `configUSE_SRP = 0`; stack-sharing
entry is further compiled out when `configSRP_STACK_SHARING = 0`.
