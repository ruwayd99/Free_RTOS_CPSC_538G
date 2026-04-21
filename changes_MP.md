# SMP (Multiprocessor EDF) - Changes Made

## Overview
This document records the changes made to FreeRTOS to support multiprocessor EDF scheduling on the RP2040 dual-core platform. The implementation supports both global EDF and partitioned EDF, with compile-time selection through configuration flags.

## Modified Files

### 1. FreeRTOS/FreeRTOS/Source/include/FreeRTOS.h
**Changes:**
- Added default configuration values for SMP EDF mode selection.
- Added guard logic so global EDF becomes the default when neither SMP mode is explicitly enabled.
- Added a compile-time error if both global and partitioned EDF are enabled together.

**Configuration introduced:**
- `configGLOBAL_EDF_ENABLE`
- `configPARTITIONED_EDF_ENABLE`

**Purpose:**
Allow the kernel to compile in either global or partitioned EDF mode without requiring application code changes.

---

### 2. FreeRTOS/FreeRTOS/Source/include/task.h
**Changes:**
- Added public APIs for SMP task creation and core management.

**New functions:**
- `BaseType_t xTaskCreateEDFOnCore(...)`
- `BaseType_t xTaskMigrateToCore(...)`
- `void vTaskRemoveFromCore(...)`

**Purpose:**
Expose the kernel-level interfaces needed by the SMP demo applications to pin tasks, migrate them, and remove fixed core assignments.

---

### 3. FreeRTOS/FreeRTOS/Source/tasks.c

#### a) TCB metadata updates
**Changes:**
- Added task-core assignment metadata for EDF tasks.
- Added per-task affinity and partitioning state.

**New fields:**
- `xAssignedCore`
- `uxCoreAffinityMask`

**Purpose:**
Track whether a task is pinned to a specific core, and whether it may be migrated.

---

#### b) Partitioned EDF data structures
**Changes:**
- Added per-core EDF ready lists.
- Added per-core utilization tracking.
- Added a pending-task list for tasks that must be committed to a core after admission.

**New state:**
- `xEDFReadyListsByCore[]`
- `ullEDFCoreUtilMicro[]`
- `xEDFPendingTasksList`
- `ullEDFPendingTasksUtil`

**Purpose:**
Support static task placement and per-core admission control in partitioned mode.

---

#### c) EDF-aware SMP task selection
**Changes:**
- Added an EDF-aware wrapper around the SMP selection path.
- Added a core-aware helper that selects the best EDF task from either the global ready list or the per-core ready list.

**New helper functions:**
- `prvSelectHighestPriorityTaskEDF(...)`
- `prvSMPSelectEDFTaskForCore(...)`
- `prvSMPRequestRescheduleForEDF(...)`

**Purpose:**
Allow each core to run the correct EDF job while preserving the existing SMP scheduling flow.

---

#### d) Admission control helpers
**Changes:**
- Added a global EDF admission routine for SMP.
- Added a partitioned admission routine based on utilization fitting.
- Added utility conversion helpers for comparing task utilization in fixed-point micro-units.

**New helper functions:**
- `prvEDFUtilToMicro(...)`
- `prvEDFAdmissionGlobalSMP(...)`
- `prvEDFPartitionCanFitPendingSet(...)`
- `prvEDFPartitionAssignRuntimeTask(...)`
- `prvEDFPartitionMovePendingTasksToReadyLists(...)`
- `prvEDFPartitionRemoveTaskUtilization(...)`

**Purpose:**
Provide separate admission logic for global and partitioned EDF so the same kernel code can support both modes.

---

#### e) Task lifecycle integration
**Changes:**
- Updated task creation to route EDF tasks through SMP-aware admission logic.
- Updated task deletion and core-removal paths to release utilization in partitioned mode.
- Added handling for late-job removal so missed deadlines do not remain in the ready set.
- Added startup-time commit of pending partitioned tasks before the scheduler begins running.

**Affected functions include:**
- `xTaskCreateEDF(...)`
- `xTaskCreateEDFOnCore(...)`
- `xTaskMigrateToCore(...)`
- `vTaskRemoveFromCore(...)`
- task tick and ready-list maintenance paths

**Purpose:**
Keep per-core utilization and task placement consistent across create, migrate, delete, and deadline-miss events.

---

### 4. FreeRTOS/FreeRTOS/Demo/ThirdParty/Community-Supported-Demos/CORTEX_M0+_RP2040/Standard/FreeRTOSConfig.h
**Changes:**
- Added SMP control macros for the RP2040 demo build.

**Configuration added:**
- `configNUMBER_OF_CORES`
- `configTICK_CORE`
- `configRUN_MULTIPLE_PRIORITIES`
- `configUSE_CORE_AFFINITY`
- `configGLOBAL_EDF_ENABLE`
- `configPARTITIONED_EDF_ENABLE`

**Purpose:**
Let the demo select global or partitioned EDF and enable the dual-core RP2040 setup.

---

### 5. FreeRTOS/FreeRTOS/Demo/ThirdParty/Community-Supported-Demos/CORTEX_M0+_RP2040/Standard/CMakeLists.txt
**Changes:**
- Added compile definitions for global and partitioned SMP builds.
- Added build targets for the SMP test binaries.
- Preserved separate build-time configuration for EDF, SRP, CBS, and SMP demos.

**Purpose:**
Make the SMP test executables easy to build and switch between without editing kernel source.

---

### 6. FreeRTOS/FreeRTOS/Demo/ThirdParty/Community-Supported-Demos/CORTEX_M0+_RP2040/Standard/main_smp_*.c
**Changes:**
- Added/updated SMP demo programs for global admission, global migration, partitioned fit/reject, and partitioned migration/remove flows.

**Files:**
- `main_smp_global_test_admission.c`
- `main_smp_global_test_migration.c`
- `main_smp_partition_test_fit.c`
- `main_smp_partition_test_migration.c`

**Purpose:**
Provide concrete application-level validation for the kernel changes.

---

## Summary of Kernel Behavior Added

| Area | Result |
|---|---|
| Global EDF | One shared EDF ready queue for all cores |
| Partitioned EDF | One EDF ready queue per core |
| Core management | Task pinning, removal, and migration APIs |
| Admission control | Utilization-based acceptance/rejection |
| Rescheduling | Cross-core reschedule requests via SMP helper path |
| Backward compatibility | Single-core builds remain available when SMP is disabled |

## Key Design Outcome
The kernel now supports two mutually exclusive SMP EDF modes that share the same task API surface, while keeping the default single-core behavior available through configuration.
