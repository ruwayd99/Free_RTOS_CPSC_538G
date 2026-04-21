# CBS (Constant Bandwidth Server) - Changes Made

## Overview
This document records all modifications made to FreeRTOS to support Constant Bandwidth Server (CBS) scheduling. CBS allows aperiodic, soft real-time tasks to execute alongside hard real-time periodic tasks while maintaining EDF schedulability guarantees.

## Modified Files

### 1. FreeRTOS/FreeRTOS/Source/include/FreeRTOS.h
**Changes:**
- Added default configuration for `configUSE_CBS` (default: 0)

**Lines affected:**
- Added configuration check for `configUSE_CBS` to disable CBS by default and allow user override

---

### 2. FreeRTOS/FreeRTOS/Source/include/task.h

**Functions added:**
- `BaseType_t xTaskCreateCBS()` — Creates a CBS-managed aperiodic task with specified server budget and period. Parameters include:
    - `pxTaskCode`: Task function pointer
    - `pcName`: Descriptive name for debugging
    - `uxStackDepth`: Stack size in words
    - `pvParameters`: Task parameters
    - `xServerBudget`: Q_s (max CPU ticks per server period)
    - `xServerPeriod`: T_s (server replenishment period)
    - `pxCreatedTask`: Output handle to created task

---

### 3. FreeRTOS/FreeRTOS/Source/tasks.c

#### a) Task Control Block (TCB) Modifications
**Lines affected:** ~517-545

**New fields added to TCB_t structure:**
```c
BaseType_t xIsCBSTask                    // Flag: is this a CBS task?
TickType_t xCBSMaxBudget                 // Q_s: max budget per period (constant)
TickType_t xCBSCurrentBudget             // Remaining budget this period
TickType_t xCBSPeriod                    // T_s: server period (constant)
TickType_t xCBSDeadline                  // Current CBS deadline (virtual)
```

**Purpose:** Store CBS-specific state for each task managed by a constant bandwidth server.

---

#### b) Ready List Tie-Breaking for CBS
**Lines affected:** ~344-350

**New logic:** 
- When inserting tasks into EDF ready list with identical deadlines, CBS tasks are given priority over regular periodic tasks
- Implements "priority ties broken in favor of the server" requirement

---

#### c) Budget Tracking in Tick Handler
**Lines affected:** ~6499-6550

**New logic:**
- On each tick, if a CBS task is running:
  - Decrement `xCBSCurrentBudget` by 1 tick
  - When budget reaches 0:
    - Postpone deadline: `xCBSDeadline += xCBSPeriod`
    - Update EDF's `xAbsoluteDeadline` to match
    - Replenish budget: `xCBSCurrentBudget = xCBSMaxBudget`
    - Re-insert task into EDF ready list with new deadline
    - Trigger preemption if needed

**Purpose:** Enforce CBS bandwidth reservation and automatically postpone deadlines when budget exhausted.

---

#### d) Server Refresh on Task Wakeups
**Lines affected:** Multiple locations
- ~6613-6630: Delayed list wake
- ~7334-7360: Event list wake  
- ~10475-10490: Task notify wake
- ~10623-10640: Task notify-from-ISR wake
- ~10763-10780: Task notify-give-from-ISR wake

**New logic:**
When a CBS task transitions from blocked/delayed to ready state:
- Check if a new server period has started since the task was blocked
- If `xTaskGetTickCount()` ≥ next expected refresh time, refresh server state:
  - Update `xCBSDeadline`
  - Replenish `xCBSCurrentBudget`
  - Re-insert into EDF ready list with refreshed deadline

**Purpose:** Handle server period boundaries for CBS tasks that resume from blocking, ensuring deadline and budget are current.

---

#### e) CBS Task Creation Function
**Lines affected:** ~11372-11450

**Function:** `xTaskCreateCBS()`

**Implementation:**
1. Create task with basic priority (above idle)
2. Mark task as CBS-managed (`xIsCBSTask = pdTRUE`)
3. Initialize CBS fields:
   - `xCBSMaxBudget = xServerBudget`
   - `xCBSPeriod = xServerPeriod`
   - `xCBSCurrentBudget = xServerBudget`
   - `xCBSDeadline = xServerPeriod`
4. Set EDF fields to match CBS parameters (for EDF scheduler)
5. Add to ready list (goes into EDF ready list, not priority-based list)

**Purpose:** Provide a clean API for creating aperiodic tasks with CBS guarantees.

---

## Configuration Options

### `configUSE_CBS`
- **Default:** 0 (disabled)
- **Effect:** When 1, enables all CBS functionality
- **Location:** FreeRTOSConfig.h or FreeRTOS.h

### Related EDF Configuration
- **Prerequisite:** `configUSE_EDF_SCHEDULING` must be 1
- CBS builds on top of EDF for deadline ordering and preemption

---

## Bandwidth Calculation

For a CBS task:
- **Bandwidth** = `xServerBudget / xServerPeriod`
- Example: Budget=200 ticks, Period=1000 ticks → 20% CPU

**Schedulability requirement:** Total bandwidth of all CBS servers plus all periodic EDF tasks must be ≤ 1.0 (100%).
