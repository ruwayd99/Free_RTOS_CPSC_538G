# SMP (Multiprocessor EDF) - Testing Document

## 1. Testing Methodology

The SMP test suite is split into four discrete executables. Two tests exercise global EDF behavior and two tests exercise partitioned EDF behavior. The tests are intentionally small and focused so each one isolates a single scheduler capability instead of mixing multiple behaviors into one run.

Each test uses the same observation pattern:

1. GPIO pins show which task is currently executing.
2. `printf()` traces record admission results, core changes, and timing.
3. `xTaskGetTickCount()` is used to anchor migration and removal events in time.
4. The RP2040 dual-core setup validates the SMP-specific code paths rather than a single-core fallback.

## 2. Global EDF Tests

### Test 1: Global admission control
**File:** [main_smp_global_test_admission.c](FreeRTOS/FreeRTOS/Demo/ThirdParty/Community-Supported-Demos/CORTEX_M0+_RP2040/Standard/main_smp_global_test_admission.c)

**What it tests:**
This program verifies that the global EDF admission path accepts a schedulable task set and rejects an oversized task. It is the basic smoke test for the global EDF admission controller.

**Program setup:**
- `G_A` on GPIO 10 with 80 ticks of work.
- `G_B` on GPIO 11 with 100 ticks of work.
- `G_C` on GPIO 12 with 120 ticks of work.
- `G_REJECT` on GPIO 13 with 900 ticks of work.
- All tasks use a 1000-tick period and identical deadlines.

**Expected result:**
The first three tasks are admitted and continue to run periodically. The final task is rejected because it makes the task set unschedulable under the global EDF admission test.

**Pass criterion:**
- `uxTaskGetEDFAdmittedCount()` reaches 3.
- `uxTaskGetEDFRejectedCount()` increments for the rejection case.
- The admitted tasks keep producing periodic GPIO activity without kernel instability.

---

### Test 2: Global migration and remove-from-core flow
**File:** [main_smp_global_test_migration.c](FreeRTOS/FreeRTOS/Demo/ThirdParty/Community-Supported-Demos/CORTEX_M0+_RP2040/Standard/main_smp_global_test_migration.c)

**What it tests:**
This test validates the runtime migration and release APIs in global EDF mode. A controller task changes the placement of one task and removes another task from a fixed core assignment.

**Program setup:**
- `G_HINT` is created with `xTaskCreateEDFOnCore(..., 0, ...)` so it starts with a core-0 hint.
- `G_PEER` is created with `xTaskCreateEDF(...)` so it starts as a normal global EDF task.
- The worker logs any observed core changes through `get_core_num()`.
- A controller waits 3 seconds, migrates `G_HINT` to core 1, waits another 3 seconds, and then calls `vTaskRemoveFromCore()` on `G_PEER`.

**Expected result:**
The migration request should succeed and the hinted task should become eligible to run on the new core. Removing the peer task from its core assignment should release it back into the global pool.

**Pass criterion:**
- The controller prints a successful migration result.
- The worker trace eventually shows `G_HINT` on core 1.
- The remove-from-core operation completes without a crash.

## 3. Partitioned EDF Tests

### Test 3: Partitioned fit/reject assignment
**File:** [main_smp_partition_test_fit.c](FreeRTOS/FreeRTOS/Demo/ThirdParty/Community-Supported-Demos/CORTEX_M0+_RP2040/Standard/main_smp_partition_test_fit.c)

**What it tests:**
This program validates the partitioned EDF admission path using a fit/reject scenario. It checks that the partitioning heuristic accepts a task set that fits across the available cores and rejects a task that does not.

**Program setup:**
- `P_U40` with 60 ticks of work and utilization 0.40.
- `P_U30A` with 60 ticks of work and utilization 0.30.
- `P_U30B` with 60 ticks of work and utilization 0.30.
- `P_U25A` with 60 ticks of work and utilization 0.25.
- `P_U25B` with 60 ticks of work and utilization 0.25.
- `P_U80_REJECT` with 800 ticks of work and utilization 0.80.

**Expected result:**
The five moderate tasks are admitted and placed across the two cores. The oversized task is rejected because no core has enough remaining capacity.

**Pass criterion:**
- Five tasks are admitted.
- One task is rejected.
- The summary print shows 5 admitted and 1 rejected.

---

### Test 4: Partitioned migration and capacity release
**File:** [main_smp_partition_test_migration.c](FreeRTOS/FreeRTOS/Demo/ThirdParty/Community-Supported-Demos/CORTEX_M0+_RP2040/Standard/main_smp_partition_test_migration.c)

**What it tests:**
This test validates the partitioned migration and remove-from-core APIs. It demonstrates that a task cannot be migrated onto an overloaded core, then shows that the migration succeeds after another task is removed and capacity becomes available.

**Program setup:**
- `P_A_U70` is created on core 0 with 120 ticks of work and utilization 0.70.
- `P_B_U40` is created on core 1 with 120 ticks of work and utilization 0.40.
- A controller waits 3 seconds, tries to move `P_B_U40` to core 0, removes `P_A_U70` from core assignment, waits 1 second, and then retries the migration.
- The worker task logs any observed core changes through `get_core_num()`.

**Expected result:**
The first migration should fail because core 0 is already carrying a large task. After `P_A_U70` is removed from its fixed core, the second migration should succeed.

**Pass criterion:**
- The first migration request returns failure.
- The controller reports that `P_A_U70` was removed from core assignment.
- The second migration request returns success.

## 4. Coverage Summary

The four attached SMP tests cover the externally visible behavior of the implementation:

- global EDF admission control,
- global EDF migration and removal,
- partitioned EDF fit/reject assignment,
- partitioned EDF migration after capacity is released.

Each test is discrete and intentionally limited in scope so the behavior under test is easy to interpret.
