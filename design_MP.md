# SMP (Symmetric Multiprocessor) - Design Document

## 1. Executive Summary

FreeRTOS is extended to support Earliest Deadline First (EDF) scheduling on multi-core systems. Two scheduling modes are provided:

1. **Global EDF:** A single ready list shared across all cores; tasks can migrate freely
2. **Partitioned EDF:** Tasks statically assigned to cores using a bin-packing heuristic

Both modes run on the RP2040's dual ARM Cortex-M0+ cores and maintain real-time guarantees through per-core or global utilization-based admission control.

---

## 2. Architecture Overview

### 2.1 System Components

```
┌─────────────────────────────────────────────────────┐
│           Multi-Core System (RP2040)                │
├──────────────────────┬──────────────────────────────┤
│     Core 0           │        Core 1                │
├──────────────────────┼──────────────────────────────┤
│                      │                              │
│  ✓ SysTick Timer     │   Waits for inter-core IRQ   │
│  ✓ Tick Handler      │                              │
│  ✓ Scheduler         │   ✓ Scheduler (on wake)      │
│  ✓ Task Dispatch     │   ✓ Task Dispatch            │
│                      │                              │
├──────────────────────┴──────────────────────────────┤
│  Shared Memory:                                     │
│  - Global tick count (xTickCount)                   │
│  - EDF ready lists (global or per-core)             │
│  - Per-core current TCB (pxCurrentTCBs[2])          │
│  - Utilization state                                │
│                                                     │
│  Inter-Core Communication:                         │
│  - SIO FIFO interrupt to trigger core 1 reschedule │
└─────────────────────────────────────────────────────┘
```

### 2.2 Data Structures

#### Global Mode
```
xEDFReadyList (sorted by deadline, shared)
├─ Task_A (deadline=500, core_affinity_hint=any)
├─ Task_B (deadline=600, core_affinity_hint=core_0)
├─ Task_C (deadline=800, core_affinity_hint=any)
└─ ...

pxCurrentTCBs[2]
├─ [0] → Task_A (running on core 0)
└─ [1] → Task_B (running on core 1)
```

#### Partitioned Mode
```
xEDFReadyListsByCore[0] (core 0)
├─ Task_A (deadline=500, assigned_core=0)
└─ Task_C (deadline=700, assigned_core=0)

xEDFReadyListsByCore[1] (core 1)
├─ Task_B (deadline=600, assigned_core=1)
└─ Task_D (deadline=900, assigned_core=1)

pxCurrentTCBs[2]
├─ [0] → Task_A (running on core 0)
└─ [1] → Task_B (running on core 1)
```

---

## 3. Global EDF Mode

### 3.1 Concept
A single EDF ready list serves both cores. The highest-deadline task always runs, regardless of which core it was previously on.

### 3.2 Advantages
- Better load balancing (no fragmentation)
- Lower latency for high-priority tasks
- Simpler API (no core assignment needed)

### 3.3 Disadvantages
- Migration overhead (cache, context)
- Requires global synchronization

### 3.4 Admission Control

**Test:** Generalized Liu-Layland bound for multiprocessor EDF

```
For m cores and tasks with utilization U_i:

Check 1: Each task U_i ≤ 1.0
Check 2: ΣU_i ≤ m
Check 3: Generalized LL: ΣU_i + max(U_i) ≤ m + (1 - max(U_i))
         (only needed if max(U_i) > 0.5)
```

**Pseudocode:**
```c
BaseType_t admit_global(U_candidate) {
    if (U_candidate > 1.0) return FAIL;
    
    U_sum = U_candidate;
    U_max = U_candidate;
    
    for (each admitted task) {
        U_sum += U_i;
        U_max = max(U_max, U_i);
    }
    
    if (U_sum > m) return FAIL;
    
    if (U_max > 0.5) {
        if (U_sum + U_max > m + (1 - U_max)) return FAIL;
    }
    
    return PASS;
}
```

### 3.5 Task Migration

Tasks can migrate between cores dynamically:

```c
// User code:
xTaskMigrateToCore(xTaskHandle, 1);  // Move task to core 1
```

**Implementation:**
1. Update core affinity mask to target core
2. If task is running on different core, trigger inter-core interrupt
3. Both cores reschedule; target core will pick up the task

**Constraints:**
- Only works in global EDF mode (partitioned mode prevents migration)
- Prefers core with earliest idle time for affinity

---

## 4. Partitioned EDF Mode

### 4.1 Concept
Tasks are statically assigned to cores at creation time. Each core runs an independent EDF scheduler on its own ready list.

### 4.2 Advantages
- No migration overhead
- Predictable cache behavior
- Simple scheduling (per-core, no contention)

### 4.3 Disadvantages
- Static assignment (no dynamic load balancing)
- Risk of fragmentation (one core overloaded, another idle)

### 4.4 Assignment Algorithm: Decreasing First Fit (DFF)

```
1. Sort all tasks by utilization (descending)
2. For each task:
   a. Find core with most available capacity
   b. If available capacity ≥ task utilization:
      - Assign task to that core
      - Reduce core's available capacity
   c. Else:
      - Reject task (not schedulable)
```

**Example:**
```
Tasks: A (U=0.4), B (U=0.3), C (U=0.3), D (U=0.25), E (U=0.25)
Cores: [1.0 capacity each]

Step 1: Sort by U (desc): A(0.4), B(0.3), C(0.3), D(0.25), E(0.25)

Step 2: Assign
  - A(0.4) → Core0  [Core0: 0.6 left]  [Core1: 1.0 left]
  - B(0.3) → Core1  [Core0: 0.6 left]  [Core1: 0.7 left]
  - C(0.3) → Core0  [Core0: 0.3 left]  [Core1: 0.7 left]
  - D(0.25) → Core1 [Core0: 0.3 left]  [Core1: 0.45 left]
  - E(0.25) → Core0 [Core0: 0.05 left] [Core1: 0.45 left] ✓

Result: All fit! Utilization: Core0=0.95, Core1=0.85
```

### 4.5 Admission Control (Partitioned)

```c
BaseType_t admit_partitioned(U_candidate) {
    // Find best core (most available capacity)
    best_core = -1;
    best_capacity = 0;
    
    for (each core) {
        available = 1.0 - core_utilization[core];
        if (available >= U_candidate && available > best_capacity) {
            best_capacity = available;
            best_core = core;
        }
    }
    
    if (best_core == -1) {
        return FAIL;  // No core has room
    }
    
    // Assign to best core
    task.assigned_core = best_core;
    core_utilization[best_core] += U_candidate;
    return PASS;
}
```

### 4.6 Task Removal and Migration

**Removal:**
```c
vTaskRemoveFromCore(xTaskHandle);
// Task is deleted; its utilization is released from the core
```

**Migration:**
```c
xTaskMigrateToCore(xTaskHandle, new_core_id);
// Check if new_core has capacity
// If yes: update utilization and assignment
// If no: return FAIL
```

---

## 5. Tick Handling and Inter-Core Synchronization

### 5.1 Tick Flow

```
Core 0 (Tick Processor):
  1. Increment xTickCount
  2. Process delayed tasks (move to ready)
  3. Update all EDF deadlines (advance next period)
  4. Call tick hook
  5. Optionally: trigger inter-core reschedule

Core 1 (Waits for Signal):
  1. Runs current task
  2. On SIO FIFO interrupt: check for reschedule
  3. If reschedule needed: select new task
```

### 5.2 Cross-Core Preemption

When a task's deadline changes on core 0's tick:

```c
// In tick handler:
if (edf_deadline_changed) {
    prvSMPRequestRescheduleForEDF();
}

// Implementation:
prvSMPRequestRescheduleForEDF() {
    // Send SIO FIFO interrupt to core 1
    // Core 1 wakes from WFE, triggers reschedule
    sio_hw->fifo_wr = RESCHEDULE_SIGNAL;
}

// In core 1 interrupt handler:
void SIO_IRQHandler() {
    uint32_t msg = sio_hw->fifo_rd;
    if (msg == RESCHEDULE_SIGNAL) {
        // Re-select highest-deadline task for core 1
        prvSelectHighestPriorityTaskEDF(1);
    }
}
```

### 5.3 Utilization Tracking

**Global Mode:**
- Single global utilization counter (informational only)

**Partitioned Mode:**
- Per-core utilization: `ullEDFCoreUtilMicro[core_id]`
- Unit: micro-utilization (0 = 0%, 1,000,000 = 100%)
- Updated on task create/delete/migrate

**Calculation:**
```
U_i = C_i / T_i = WCET / Period
In micro units: ullU_i = (C_i * 1,000,000) / T_i
```

---

## 6. Late-Job Handling

### 6.1 Deadline Miss Detection

If a task is still running when its deadline passes:

```
Current tick ≥ task.xAbsoluteDeadline && task.xTaskRunState != taskTASK_NOT_RUNNING
    ⇒ Deadline miss!
```

### 6.2 Late-Job Drop Strategy

```c
void prvEDFDropLateJob(TCB_t *pxTCB, TickType_t xNow) {
    // Remove task from ready list
    uxListRemove(&pxTCB->xStateListItem);
    
    // Advance deadline to next period
    pxTCB->xAbsoluteDeadline += pxTCB->xPeriod;
    
    // Re-insert with new deadline (will run next)
    prvAddTaskToReadyList(pxTCB);
    
    // Trace the miss
    edfTRACE("[DEADLINE MISS] task=%s tick=%lu deadline=%lu\n", ...);
}
```

### 6.3 Design Rationale

Late-job drop prevents cascading misses:
- Task A misses deadline, blocks task B
- Task B misses deadline, blocks task C
- ...cascade of failures

By dropping late jobs, we allow high-priority future jobs to run.

**Tradeoff:** We lose one job; better than losing many.

---

## 7. Configuration and Mode Selection

### Configuration Flags

```c
// In FreeRTOSConfig.h:

// Enable multiprocessor support
#define configNUMBER_OF_CORES  2
#define configTICK_CORE        0  // Core 0 runs system tick

// Choose scheduling mode
#define configGLOBAL_EDF_ENABLE       1  // Global EDF
#define configPARTITIONED_EDF_ENABLE  0  // Partitioned EDF (exclusive)

// Base requirement
#define configUSE_EDF_SCHEDULING  1  // EDF must be enabled
```

### Compile-Time Selection

```c
#if (configGLOBAL_EDF_ENABLE == 1)
    // Global EDF code paths
    #define SELECT_TASK() prvSMPSelectEDFTaskForCore(core_id, &xEDFReadyList, ...)
#elif (configPARTITIONED_EDF_ENABLE == 1)
    // Partitioned EDF code paths
    #define SELECT_TASK() prvSMPSelectEDFTaskForCore(core_id, &xEDFReadyListsByCore[core_id], ...)
#endif
```

---

## 8. API Reference

### Global EDF Creation
```c
BaseType_t xTaskCreateEDFOnCore(
    TaskFunction_t pxTaskCode,
    const char *pcName,
    configSTACK_DEPTH_TYPE uxStackDepth,
    void *pvParameters,
    TickType_t xPeriod,
    TickType_t xRelativeDeadline,
    TickType_t xWcetTicks,
    BaseType_t xCoreID,              // Core affinity hint (0 or 1)
    TaskHandle_t *pxCreatedTask
);
```

### Migration API
```c
// Global EDF: move task to target core (if it has capacity)
BaseType_t xTaskMigrateToCore(TaskHandle_t xTask, BaseType_t xNewCoreID);

// Partitioned EDF: reassign task to new core (must have capacity)
// Global EDF: update affinity hint

// Remove task from core assignment (allow free scheduling)
void vTaskRemoveFromCore(TaskHandle_t xTask);
```

---

## 9. Example Scenarios

### Scenario 1: Global EDF with 2 Tasks, 2 Cores

```
Task A: Period=1000, WCET=250, Deadline=1000 (U=0.25)
Task B: Period=1000, WCET=300, Deadline=1000 (U=0.30)

Admission: ΣU = 0.55 < 2.0 ✓

Timeline:
Tick 0-249:    A runs on Core0, B runs on Core1
Tick 250:      A finishes; Core0 idle
Tick 250-549:  B continues on Core1; Core0 idle
Tick 550-799:  Both idle
Tick 800-999:  Next period: A and B ready; A deadline=1000, B deadline=1000
               → Same deadline; both run simultaneously on both cores

Result: Both meet deadlines ✓
```

### Scenario 2: Partitioned EDF with 3 Tasks, 2 Cores

```
Task A: U=0.45, Period=1000
Task B: U=0.35, Period=1000
Task C: U=0.30, Period=1000
Total: U=1.10 (exceeds 1.0; not schedulable on 1 core)

Decreasing First Fit:
1. Sort: A(0.45), B(0.35), C(0.30)
2. A → Core0 [0.55 left]
3. B → Core1 [0.65 left]
4. C → Core0 [0.25 left] ✓

Result:
  Core0: A(0.45) + C(0.30) = 0.75
  Core1: B(0.35) = 0.35
  
Both cores individually schedulable (< 1.0) ✓
```

---

## 10. Testing Strategy

### Functional Tests
1. **Admission control:** Accept schedulable sets, reject unschedulable
2. **Deadline ordering:** Tasks run in deadline order
3. **Coexistence:** Multiple cores run independent/simultaneous work
4. **Migration (global):** Task moves between cores smoothly
5. **Removal (partitioned):** Task deletion frees core capacity

### Stress Tests
1. **High utilization:** Run at 90%+ CPU load
2. **Many tasks:** 10+ tasks on 2 cores
3. **Frequent migration:** Tasks migrating every period (global)
4. **Dynamic creation:** Tasks created and deleted at runtime

### Metrics
- Deadline miss rate (should be 0% for schedulable loads)
- Core utilization per core
- Task migration frequency (global mode)
- Admission success rate

---

## 11. Known Limitations

1. **Implicit deadline only:** Partitioned/global modes require D ≤ T (no constrained deadline for SMP)
2. **No resource sharing:** Tasks assumed to be independent (SRP not integrated with SMP modes)
3. **No dynamic mode switching:** Must recompile to change global ↔ partitioned
4. **Single shared tick:** Only core 0 increments tick; no per-core local time

---

## 12. Future Enhancements

- Per-core tick support (for energy efficiency)
- Task migration policies (affinity, load balancing)
- SRP integration with SMP (resource sharing in multiprocessor)
- Energy-aware scheduling (DVFS integration)
- Real-time tracing for multiprocessor systems
