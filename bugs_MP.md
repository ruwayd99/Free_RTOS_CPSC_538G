# SMP - Known Bugs and Issues


### 1. **Partitioned Mode Pending Task Assignment Race**
**Status:** Not observed in practice; theoretical concern

**Description:** During system startup, if multiple cores attempt to move pending tasks from `xEDFPendingTasksList` to per-core ready lists simultaneously, the assignment logic might race.

**Trigger Condition:**
- Startup phase with many tasks (100+) pending assignment
- `prvEDFPartitionMovePendingTasksToReadyLists()` called from multiple cores
- Core 0 completes tick, core 1 attempts reschedule concurrently

**Observed Impact:** None observed; protected by `taskENTER_CRITICAL()` sections

**Workaround:** None required; critical sections protect shared list

---

### 2. **Utilization Calculation Rounding Errors**
**Description:** Utilization is calculated in micro-units (0-1,000,000). For very small periods or WCETs, rounding errors might cause slight over-counting.

**Trigger Condition:**
- Task with Period=3, WCET=1: U = 1/3 = 0.333... → rounded to 333,333 micro-units (not exactly 333,333.333...)
- Multiple tasks with periods = 3, 7, 11, ... (primes)
- Cumulative rounding errors

**Observed Impact:** Negligible; error < 0.1% for practical task sets

**Workaround:** Use periods/WCETs that are multiples of 10 or 100 to avoid rounding

---

### 3. **No Validation of Core ID in User APIs (Minor)**
**Description:** `xTaskCreateEDFOnCore()` and `xTaskMigrateToCore()` don't robustly validate core IDs. Passing invalid core ID might cause undefined behavior.

**Trigger Condition:**
```c
xTaskCreateEDFOnCore(..., 5, ...);  // Core 5 doesn't exist on RP2040 (only 0-1)
```

**Observed Impact:** Potential invalid memory access; no crash observed due to luck

**Workaround:** Always use valid core IDs (0-1 for RP2040)
```c
if (!taskVALID_CORE_ID(xCoreID)) return pdFAIL;
```
