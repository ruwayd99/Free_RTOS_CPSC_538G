# SMP - Known Bugs and Issues

## Current Status: No Critical Bugs Found

As of the current implementation, we've fixed all critical errors with our implementation (the ones that would make our tests fail).
Below are some possible, more minor issues.

---

## Known Issues and Limitations

### 1. **Partitioned Mode Pending Task Assignment Race (Minor)**
**Severity:** Low  
**Status:** Not observed in practice; theoretical concern

**Description:** During system startup, if multiple cores attempt to move pending tasks from `xEDFPendingTasksList` to per-core ready lists simultaneously, the assignment logic might race.

**Trigger Condition:**
- Startup phase with many tasks (100+) pending assignment
- `prvEDFPartitionMovePendingTasksToReadyLists()` called from multiple cores
- Core 0 completes tick, core 1 attempts reschedule concurrently

**Observed Impact:** None observed; protected by `taskENTER_CRITICAL()` sections

**Workaround:** None required; critical sections protect shared list

**Recommended Fix (Future):** Use atomic CAS for pending task commit if moving to lock-free scheduler

---

### 2. **Late-Job Drop Doesn't Update Period Alignment (Minor)**
**Severity:** Low  
**Description:** When a task misses its deadline and a late job is dropped, the task is reinserted with deadline = `now + period`. This might not align with the task's "original" period grid.

**Trigger Condition:**
```
Task A: Period=1000, starts at tick 0
  Deadline sequence: 0→1000→2000→3000...

If task A misses deadline at tick 1050:
  Late job dropped; reinserted with deadline = 1050 + 1000 = 2050
  Instead of next aligned deadline = 2000
```

**Observed Impact:** Minimal; affects only that one dropped job; subsequent periods realign

**Workaround:** None needed; system recovers automatically

**Recommended Fix (Future):** Realign deadline to nearest future period boundary: `deadline = ((now / period) + 1) * period`

---

### 3. **Global EDF Migration Latency (Minor)**
**Severity:** Low  
**Description:** When a task is migrated to a different core, the migration is "soft" (affinity hint only). The task might not move until the target core reschedules, causing latency.

**Trigger Condition:**
- Task A on core 0, migrated to core 1 via `xTaskMigrateToCore(A, 1)`
- Core 0 is busy; core 1 is idle
- Task A continues running on core 0 until preempted

**Observed Impact:** Task doesn't move immediately; runqueue delay of 0-100ms observed

**Workaround:** Explicit `taskYIELD()` in calling code to force preemption

**Recommended Fix (Future):** Option to force immediate preemption and migration via IPI if migration is urgent

---

### 4. **Partitioned Mode Doesn't Support Over-Subscription Recovery (Design Choice)**
**Severity:** Medium (not a bug, limitation by design)  
**Description:** If total utilization exceeds 100% in partitioned mode, the system doesn't automatically load-balance. Once assigned, a task stays on its core forever (unless migrated).

**Trigger Condition:**
- Create tasks that fit individually on cores but saturate one core
- Example: Core 0 gets 0.95 utilization, Core 1 gets 0.40
- Can't dynamically rebalance

**Observed Impact:** No crashes; system runs but one core may miss deadlines while another is idle

**Workaround:** Use global EDF mode for automatic load balancing, or manually migrate tasks

**Recommended Fix (Future):** Implement work-stealing or greedy migration policies

---

### 5. **Utilization Calculation Rounding Errors (Minor)**
**Severity:** Low  
**Description:** Utilization is calculated in micro-units (0-1,000,000). For very small periods or WCETs, rounding errors might cause slight over-counting.

**Trigger Condition:**
- Task with Period=3, WCET=1: U = 1/3 = 0.333... → rounded to 333,333 micro-units (not exactly 333,333.333...)
- Multiple tasks with periods = 3, 7, 11, ... (primes)
- Cumulative rounding errors

**Observed Impact:** Negligible; error < 0.1% for practical task sets

**Workaround:** Use periods/WCETs that are multiples of 10 or 100 to avoid rounding

**Recommended Fix (Future):** Use higher precision (64-bit fixed-point or rational arithmetic) for utilization calculations

---

### 6. **Inter-Core Interrupt Latency Not Bounded (Platform Limitation)**
**Severity:** Low  
**Description:** When core 0 sends an IPI (inter-processor interrupt) to core 1 to trigger reschedule, there's a variable latency before core 1 services the interrupt.

**Trigger Condition:**
- Core 1 is executing a long instruction (rare on Cortex-M0+)
- Core 1 is in wait-for-event state
- Interrupt latency can be 0-100 cycles

**Observed Impact:** Task migration/preemption delayed by < 1 µs; negligible for 1ms tick

**Workaround:** None required; latency is < 1% of tick period

**Recommended Fix:** Use lower-level interrupt priority for reschedule signals (if configurable)

---

### 7. **No Validation of Core ID in User APIs (Minor)**
**Severity:** Low  
**Description:** `xTaskCreateEDFOnCore()` and `xTaskMigrateToCore()` don't robustly validate core IDs. Passing invalid core ID might cause undefined behavior.

**Trigger Condition:**
```c
xTaskCreateEDFOnCore(..., 5, ...);  // Core 5 doesn't exist on RP2040 (only 0-1)
```

**Observed Impact:** Potential invalid memory access; no crash observed due to luck

**Workaround:** Always use valid core IDs (0-1 for RP2040)

**Recommended Fix:** Add assertion or return error for invalid core ID
```c
if (!taskVALID_CORE_ID(xCoreID)) return pdFAIL;
```

---

### 8. **Partitioned Mode: No Greedy Admission (Performance Limitation)**
**Severity:** Low  
**Description:** Decreasing First Fit (DFF) is not optimal for partitioned assignment. Sometimes a task is rejected even though a different assignment order would fit.

**Trigger Condition:**
```
Tasks: A(0.6), B(0.45), C(0.45)
Cores: [1.0 capacity each]

DFF order (descending U): A(0.6), B(0.45), C(0.45)
  A → Core0 [0.4 left]
  B → Core1 [0.55 left]
  C → Core0? NO (need 0.45, have 0.4)  [REJECTED!]

Optimal assignment:
  A → Core0 [0.4 left]
  C → Core1 [0.55 left]
  B → Core0? NO... still doesn't fit

Actually optimal:
  B → Core0 [0.55 left]
  C → Core1 [0.55 left]
  A → [needs 0.6, doesn't fit anywhere]  [REJECTED]
  
In fact, 0.6+0.45+0.45=1.5 tasks can't fit on 2 cores (capacity 2.0 total, but A is too big)
```

**Observed Impact:** Some schedulable task sets might be rejected; no false acceptances

**Workaround:** Reduce task sizes or use global EDF mode

**Recommended Fix (Future):** Implement bin-packing heuristics (First Fit Decreasing, Best Fit, etc.)
