# CPSC 538G - FreeRTOS Real-Time Kernel Assignment Guide

## Table of Contents

1. [What Is This Assignment About?](#1-what-is-this-assignment-about)
2. [Background: What Is FreeRTOS?](#2-background-what-is-freertos)
3. [Understanding the FreeRTOS Repository](#3-understanding-the-freertos-repository)
4. [FreeRTOS Kernel Internals - The Deep Dive](#4-freertos-kernel-internals---the-deep-dive)
5. [Assignment 1: Earliest Deadline First (EDF) - 15%](#5-assignment-1-earliest-deadline-first-edf---15)
6. [Assignment 2: Stack Resource Policy (SRP) - 15%](#6-assignment-2-stack-resource-policy-srp---15)
7. [Assignment 3: Constant Bandwidth Server (CBS) - 15%](#7-assignment-3-constant-bandwidth-server-cbs---15)
8. [Assignment 4: Multiprocessor Support - 15%](#8-assignment-4-multiprocessor-support---15)
9. [Testing and Debugging Tips](#9-testing-and-debugging-tips)
10. [Build System and CMake](#10-build-system-and-cmake)

---

## 1. What Is This Assignment About?

You have four programming assignments, each worth 15%. They all involve modifying the **FreeRTOS real-time operating system kernel** to support different real-time scheduling algorithms and features:

| # | Assignment | Weight | What You're Doing |
|---|-----------|--------|-------------------|
| 1 | **Earliest Deadline First (EDF)** | 15% | Replace FreeRTOS's fixed-priority scheduler with a dynamic-priority scheduler that always runs the task whose deadline is closest |
| 2 | **Stack Resource Policy (SRP)** | 15% | Implement a real-time locking protocol that prevents priority inversion and deadlocks when tasks share resources |
| 3 | **Constant Bandwidth Server (CBS)** | 15% | Add a mechanism that lets non-real-time (aperiodic) tasks run alongside real-time (periodic) tasks without breaking deadlines |
| 4 | **Multiprocessor Support** | 15% | Extend your kernel to run on both cores of the Raspberry Pi Pico's RP2040 chip |

**Hardware:** Raspberry Pi Pico H (RP2040 microcontroller, dual ARM Cortex-M0+ cores)

**What you submit:** A fully functional artifact (your modified FreeRTOS code) with a README explaining your design and testing. You also present a demo.

---

## 2. Background: What Is FreeRTOS?

### In Plain English

Imagine you have a tiny computer (the Raspberry Pi Pico) and you want it to do multiple things "at the same time" -- like blink an LED, read a sensor, and send data. But the computer only has one processor (for now). How does it juggle all these things?

That's what **FreeRTOS** does. It's a **Real-Time Operating System (RTOS)** -- a tiny piece of software that manages multiple "tasks" on a microcontroller. It rapidly switches between tasks so fast that it looks like they're running simultaneously.

### Key Concepts

**Task:** A function that runs in an infinite loop. Think of it like a mini-program. Each task has:
- A **priority** (how important it is)
- A **stack** (its own private memory for local variables)
- A **state** (Running, Ready, Blocked, or Suspended)

**Scheduler:** The part of FreeRTOS that decides which task runs next. By default, FreeRTOS uses **fixed-priority preemptive scheduling** -- the highest-priority ready task always runs, and if a higher-priority task becomes ready, it immediately takes over (preempts) the current task.

**Tick Interrupt:** A hardware timer that fires at a regular interval (by default every 1 ms). Every time it fires, FreeRTOS checks if it needs to switch tasks. This is the heartbeat of the system.

**Context Switch:** When FreeRTOS swaps one task out and another task in. It saves the registers and stack pointer of the old task, and restores the registers and stack pointer of the new task.

**Task States:**
```
                    ┌──────────────┐
           ┌───────│   RUNNING    │───────┐
           │       └──────────────┘       │
           │ (selected by scheduler)      │ (preempted or yielded)
           │                              │
    ┌──────────────┐              ┌──────────────┐
    │    READY     │◄─────────────│   BLOCKED    │
    └──────────────┘  (event or   └──────────────┘
           ▲          timeout)          │
           │                            │ (waiting for event,
           │       ┌──────────────┐     │  delay, or resource)
           └───────│  SUSPENDED   │     │
                   └──────────────┘     │
                                        │
                   (vTaskDelay, waiting  ◄┘
                    on queue/semaphore)
```

- **Running:** Currently executing on the CPU.
- **Ready:** Wants to run, but a higher-priority task is running.
- **Blocked:** Waiting for something (a timer delay, a queue message, a semaphore).
- **Suspended:** Explicitly paused by the application.

---

## 3. Understanding the FreeRTOS Repository

### Top-Level Structure

```
FreeRTOS/                           ← Root of the repo
├── FreeRTOS/
│   ├── Source/                     ← THE KERNEL (this is where you'll work!)
│   │   ├── tasks.c                ← Task management & scheduler (★ MOST IMPORTANT FILE)
│   │   ├── list.c                 ← Linked list implementation (used everywhere)
│   │   ├── queue.c                ← Queues, semaphores, mutexes
│   │   ├── timers.c               ← Software timers
│   │   ├── event_groups.c         ← Event groups
│   │   ├── stream_buffer.c        ← Stream buffers
│   │   ├── croutine.c             ← Co-routines (legacy, ignore)
│   │   ├── include/               ← Header files
│   │   │   ├── FreeRTOS.h         ← Main header, includes config
│   │   │   ├── task.h             ← Task API declarations
│   │   │   ├── list.h             ← List data structure declarations
│   │   │   ├── queue.h            ← Queue API declarations
│   │   │   ├── semphr.h           ← Semaphore API wrapper
│   │   │   ├── timers.h           ← Timer API declarations
│   │   │   └── portable.h         ← Port abstraction layer
│   │   └── portable/              ← Hardware-specific code
│   │       ├── MemMang/           ← Heap implementations
│   │       │   ├── heap_1.c       ← Simple, no-free allocator
│   │       │   ├── heap_2.c       ← Best-fit, no-coalescing
│   │       │   ├── heap_3.c       ← Wraps standard malloc/free
│   │       │   ├── heap_4.c       ← First-fit with coalescing (★ COMMONLY USED)
│   │       │   └── heap_5.c       ← Like heap_4 but for non-contiguous memory
│   │       └── ThirdParty/GCC/RP2040/  ← Raspberry Pi Pico port
│   │           ├── port.c         ← RP2040 port (context switch, SysTick, etc.)
│   │           ├── include/
│   │           │   └── portmacro.h ← RP2040 port macros and types
│   │           ├── library.cmake   ← CMake for Pico SDK integration
│   │           └── FreeRTOS_Kernel_import.cmake ← Import helper for projects
│   ├── Demo/                      ← Example applications
│   │   └── ThirdParty/Community-Supported-Demos/
│   │       └── CORTEX_M0+_RP2040/ ← Demos for our Pico!
│   │           ├── Standard/      ← Single-core demo (★ YOUR STARTING POINT)
│   │           │   ├── main.c
│   │           │   ├── main_blinky.c
│   │           │   ├── main_full.c
│   │           │   ├── FreeRTOSConfig.h  ← Configuration file
│   │           │   └── CMakeLists.txt
│   │           └── Standard_smp/  ← Multi-core (SMP) demo
│   └── Test/                      ← Test infrastructure
└── FreeRTOS-Plus/                 ← Extra modules (TCP, Trace, etc.) -- probably not needed
```

### Which Files You'll Mainly Work With

| File | Why You Care |
|------|-------------|
| `FreeRTOS/Source/tasks.c` | This is the scheduler. All 4 assignments modify this file. |
| `FreeRTOS/Source/include/task.h` | Task API -- you may add new API functions here. |
| `FreeRTOS/Source/list.c` | The linked list -- you need to understand how it works. |
| `FreeRTOS/Source/include/list.h` | List data structures and macros. |
| `FreeRTOS/Source/queue.c` | For SRP, you'll modify mutex/semaphore behavior. |
| `Demo/.../Standard/FreeRTOSConfig.h` | Configuration settings for your build. |
| `Demo/.../Standard/main.c` | Your application entry point. You'll create test tasks here. |
| `Demo/.../Standard/CMakeLists.txt` | Build configuration. |

---

## 4. FreeRTOS Kernel Internals - The Deep Dive

This section explains how FreeRTOS works under the hood. You MUST understand this before modifying anything.

**Before we start:** forget about code for a moment. Let's build a mental model first, then connect it to the actual code.

---

### 4.0 The Big Picture First (Read This Before Anything Else)

Imagine you're a teacher in a classroom with 5 students. You have ONE chair at the front (the CPU). Only ONE student can sit in the chair at a time. Your job is to decide who sits next.

- **Tasks** = Students
- **The CPU** = The single chair
- **The Scheduler** = You, the teacher
- **Priority** = How important each student is (some students have higher priority)
- **The Tick Interrupt** = An alarm clock that rings every 1 millisecond, and every time it rings, you (the teacher) get a chance to re-evaluate who should be sitting in the chair

Now, how do you organize the students? You need some kind of system. Here's what FreeRTOS does:

**It uses lists (like queues or lines) to organize tasks.** Every task is always in exactly ONE list at any time, and that list tells you the task's current state:

```
 ┌─────────────────────────────────────────────────────────────────────┐
 │                        THE BIG PICTURE                             │
 │                                                                    │
 │  ┌──────────────┐                                                  │
 │  │   RUNNING    │  ← Only 1 task. This is pxCurrentTCB.           │
 │  │  (the chair) │    Not in any list -- it's actively on the CPU. │
 │  └──────┬───────┘                                                  │
 │         │ tick interrupt fires, scheduler runs                     │
 │         ▼                                                          │
 │  ┌──────────────────────────────────────────────┐                  │
 │  │           READY LISTS (the waiting lines)    │                  │
 │  │                                              │                  │
 │  │  Priority 3: [TaskA] → [TaskD] → ...        │ ← checked 1st   │
 │  │  Priority 2: [TaskB]                        │ ← checked 2nd   │
 │  │  Priority 1: [TaskC] → [TaskE] → ...        │ ← checked 3rd   │
 │  │  Priority 0: [Idle]                         │ ← checked last  │
 │  └──────────────────────────────────────────────┘                  │
 │         │ task calls vTaskDelay() or waits on a queue              │
 │         ▼                                                          │
 │  ┌──────────────────────────────────────────────┐                  │
 │  │         DELAYED LIST (the sleeping area)     │                  │
 │  │                                              │                  │
 │  │  Sorted by wake-up time:                    │                  │
 │  │  [TaskF wakes@tick50] → [TaskG wakes@tick120]│                  │
 │  └──────────────────────────────────────────────┘                  │
 │                                                                    │
 │  When a delayed task's wake-up time arrives, it moves              │
 │  back to its Ready List.                                           │
 └─────────────────────────────────────────────────────────────────────┘
```

**The fundamental rule:** The scheduler always picks the task from the **highest non-empty priority list**. If there are multiple tasks at that priority, it rotates between them (round-robin). Lower priority tasks **never run** as long as a higher priority task is ready.

Now let's learn the actual code that makes this happen.

---

### 4.1 The Linked List (`list.h` / `list.c`)

#### Why Do We Need Lists?

FreeRTOS needs to organize tasks into groups: "which tasks are ready to run?", "which tasks are sleeping?", "which tasks are waiting for a queue?". It uses **linked lists** for all of these.

Think of a linked list like a chain of paperclips. Each paperclip is connected to the next one and the previous one. You can walk forward or backward along the chain. You can insert a new paperclip anywhere, or remove one from the middle without breaking the chain.

#### ListItem_t - A Hook on Each Task

Here's the critical insight: **a ListItem_t is physically embedded INSIDE each task's TCB (Task Control Block).** It's not a separate object -- it's literally part of the task's identity structure.

Think of it like this: every student in the classroom has a name tag sewn into their shirt. The name tag has a clip that can hook onto a rope (a list). The student doesn't carry a separate name tag around -- it's part of them. When you want to put a student into a line, you clip their built-in name tag onto the rope.

```c
struct xLIST_ITEM {
    TickType_t xItemValue;          // A number used for sorting (means different things in different contexts)
    struct xLIST_ITEM *pxNext;      // Points to the next item in the chain
    struct xLIST_ITEM *pxPrevious;  // Points to the previous item in the chain
    void *pvOwner;                  // Points BACK to the task (TCB) that owns this item
    struct xLIST *pxContainer;      // Which list is this item currently in?
};
```

**The `xItemValue` field is the most important.** Its meaning changes depending on which list the item is in:

| When the task is in... | `xItemValue` means... |
|------------------------|----------------------|
| A ready list | Not used for sorting (all tasks in one ready list have the same priority) |
| The delayed list | The tick count when this task should wake up (e.g., 5000 means "wake me at tick 5000") |
| An event list (waiting on a queue) | `configMAX_PRIORITIES - priority` (so higher priority tasks sort first) |
| (EDF -- your assignment) | The absolute deadline |

**The `pvOwner` field** points back to the TCB. This is how FreeRTOS goes from "I found an item in a list" to "this item belongs to Task A." It's a two-way connection:

```
  ┌──────────────────────────────────────┐
  │           TCB for "Task A"           │
  │                                      │
  │  pxTopOfStack: 0x2000_1000          │
  │                                      │
  │  xStateListItem: ──────────┐        │   The ListItem is INSIDE the TCB.
  │    xItemValue: 5000        │        │   It's not a separate object.
  │    pxNext: ─────────────── │ ──→ [next item in the list]
  │    pxPrevious: ─────────── │ ──→ [previous item in the list]
  │    pvOwner: ───────────────│──┐  │
  │    pxContainer: ───────────│──│──→ [the list this item is in]
  │  ◄─────────────────────────┘  │  │
  │                               │  │
  │  xEventListItem: ...          │  │
  │  uxPriority: 2                │  │
  │  pcTaskName: "Task A"         │  │
  │  ...                          │  │
  └───────────────────────────────┘  │
           ▲                          │
           └──────────────────────────┘
              pvOwner points back to the TCB!
```

**Each TCB has TWO list items** (two hooks):
- **`xStateListItem`** -- used to put the task into its current state list (ready list, delayed list, or suspended list). A task can only be in ONE state list at a time.
- **`xEventListItem`** -- used to put the task into an event list (e.g., when waiting on a queue or semaphore). A task can be in BOTH a state list AND an event list simultaneously (e.g., blocked on a queue with a timeout: `xStateListItem` is in the delayed list, `xEventListItem` is in the queue's wait list).

#### List_t - The List Itself (The Rope)

If `ListItem_t` is a hook on each task, then `List_t` is the rope that hooks clip onto:

```c
struct xLIST {
    UBaseType_t uxNumberOfItems;    // How many items are currently in this list
    ListItem_t *pxIndex;            // A "cursor" -- remembers where we left off (for round-robin)
    MiniListItem_t xListEnd;        // A special sentinel/marker at the "end" of the list
};
```

The list is **circular** (the last item connects back to the first). The `xListEnd` sentinel has `xItemValue = portMAX_DELAY` (0xFFFFFFFF -- the biggest possible number), so when items are sorted by `xItemValue`, the sentinel is always "last."

Here's what a list with 3 items looks like:

```
  List_t
  ┌─────────────────────┐
  │ uxNumberOfItems: 3  │
  │ pxIndex: ───────────┼──→ (points to one of the items -- the "cursor")
  │ xListEnd:           │
  │   xItemValue: MAX   │
  │   pxNext: ──────────┼──→ Item A
  │   pxPrevious: ──────┼──→ Item C
  └─────────────────────┘
          ▲                       │
          │                       ▼
      ┌───┴───┐           ┌───────────┐           ┌───────────┐
      │Item C │◄──────────│  Item A   │──────────→│  Item B   │
      │val:30 │           │  val:10   │           │  val:20   │
      │next:──┼──→xListEnd│  next:────┼──→Item B  │  next:────┼──→Item C
      │prev:──┼──→Item B  │  prev:────┼──→xListEnd│  prev:────┼──→Item A
      │owner:TaskC        │  owner:TaskA          │  owner:TaskB
      └───────┘           └───────────┘           └───────────┘
          ▲                                                │
          └────────────────────────────────────────────────┘
                         (circular -- C wraps back to xListEnd)
```

Items are sorted by ascending `xItemValue`: A(10) → B(20) → C(30) → xListEnd(MAX) → back to A.

#### Key List Operations (With Examples)

**`vListInsert(pxList, pxNewItem)` -- Sorted insert:**

Imagine the list currently has: `[A val:10] → [C val:30] → [xListEnd val:MAX]`

You insert Item B with `xItemValue = 20`. The function walks through the list, finds that 20 fits between 10 and 30, and inserts it there:

Result: `[A val:10] → [B val:20] → [C val:30] → [xListEnd val:MAX]`

If you insert Item D also with `xItemValue = 20`, it goes **after** B (equal values go after existing ones):

Result: `[A val:10] → [B val:20] → [D val:20] → [C val:30] → [xListEnd val:MAX]`

**Where is `vListInsert` used?**
- Inserting tasks into the **delayed list** (sorted by wake-up time -- the task that wakes up soonest is first).
- Inserting tasks into **event lists** (sorted so higher-priority tasks are first).
- In your EDF assignment: inserting tasks into the ready list sorted by **deadline**.

**`vListInsertEnd(pxList, pxNewItem)` -- Unsorted insert (at the "cursor"):**

This does NOT sort. It inserts the new item right before `pxIndex` (the cursor). This effectively puts the new item at the "back of the line."

**Where is `vListInsertEnd` used?**
- Inserting tasks into the **ready list**. Why no sorting? Because all tasks in a given ready list have the **same priority** (remember, there's a separate list for each priority level). So there's nothing to sort by. You just add the task to the back of the line.

**`listGET_OWNER_OF_NEXT_ENTRY(pxTCB, pxList)` -- Get the next task (round-robin):**

This is how round-robin works. The list remembers where it left off (via `pxIndex`). Every time this macro is called, it advances `pxIndex` to the next item and returns the owner (TCB) of that item.

Example with 3 tasks at priority 2:

```
Ready list at priority 2: [TaskA] → [TaskB] → [TaskC] → (back to xListEnd → TaskA)
                                       ▲
                                    pxIndex (cursor was here last)

Call listGET_OWNER_OF_NEXT_ENTRY:
  → Move pxIndex to TaskC
  → Return TaskC's TCB as the next task to run

Next call:
  → Move pxIndex to xListEnd... skip it (it's the sentinel)... move to TaskA
  → Return TaskA's TCB

Next call:
  → Move pxIndex to TaskB
  → Return TaskB's TCB

... and so on, cycling through A → B → C → A → B → C ...
```

This is **round-robin** -- each task gets a turn, then the next one goes, cycling through them endlessly. **But this only matters when multiple tasks share the same priority.** If only one task is at the highest priority, it always runs (no round-robin needed).

**`uxListRemove(pxItem)` -- Remove an item from its list.** Unlinks the item from the chain and decrements `uxNumberOfItems`.

**`listGET_OWNER_OF_HEAD_ENTRY(pxList)`** -- Gets the owner (TCB) of the **first** item in the list (the item right after `xListEnd`). In a sorted list, this is the item with the smallest `xItemValue`. Used to check: "which delayed task wakes up next?" or "which task has the earliest deadline?"

#### Concrete Example: A List Item's Life Journey

Let's trace one task's `xStateListItem` through its entire lifecycle:

```
1. Task "Blinky" is created with priority 2.
   → Its xStateListItem is inserted into pxReadyTasksLists[2]
   → xStateListItem is now "hooked" onto the priority-2 ready list rope

2. Blinky starts running (scheduler picks it).
   → xStateListItem stays in pxReadyTasksLists[2]
   → pxCurrentTCB = Blinky's TCB

3. Blinky calls vTaskDelay(100) -- "sleep for 100 ticks."
   → xStateListItem is REMOVED from pxReadyTasksLists[2]
   → xStateListItem.xItemValue = current_tick + 100 (e.g., 5100)
   → xStateListItem is INSERTED (sorted) into the delayed list
   → A different task starts running

4. 100 ticks later, tick interrupt fires, xTaskIncrementTick() runs.
   → It checks the delayed list head: "Blinky wakes at 5100, current tick is 5100. Wake up!"
   → xStateListItem is REMOVED from the delayed list
   → xStateListItem is INSERTED back into pxReadyTasksLists[2]
   → If Blinky has the highest priority, scheduler switches back to it
```

**The same `xStateListItem` is reused** -- it moves from list to list as the task changes state. A task can only be in ONE state list at a time because it only has one `xStateListItem`. This is an elegant design: you don't allocate/free memory for list nodes; they're always there, embedded in the TCB.

---

### 4.2 The Task Control Block (TCB) - `tasks.c`

Every task in FreeRTOS has a **Task Control Block (TCB)**. Think of it as the task's "identity card" -- it stores everything FreeRTOS needs to know about a task.

```c
typedef struct tskTaskControlBlock {
    // ★ MUST be first: pointer to top of the task's stack
    volatile StackType_t *pxTopOfStack;

    // ★ Used to put this task into ready/blocked/suspended lists
    // The xItemValue of this list item holds the wake-up time when the task is delayed
    ListItem_t xStateListItem;

    // ★ Used to put this task into event lists (queue/semaphore wait lists)
    // The xItemValue = configMAX_PRIORITIES - uxPriority (so higher priority = lower value = sorted first)
    ListItem_t xEventListItem;

    // ★ The task's priority. 0 = lowest (idle), higher number = higher priority
    UBaseType_t uxPriority;

    // Pointer to the start of the task's stack memory
    StackType_t *pxStack;

    // Task name (for debugging)
    char pcTaskName[configMAX_TASK_NAME_LEN];

    // (if mutexes enabled) Base priority before priority inheritance
    UBaseType_t uxBasePriority;
    UBaseType_t uxMutexesHeld;

    // ... other optional fields (notifications, TLS, runtime stats, etc.)
} tskTCB;
```

**Key things to understand:**

1. Each TCB has **two list items**: `xStateListItem` and `xEventListItem`. The `xStateListItem` is used to place the task in one of the state lists (ready, delayed, suspended). The `xEventListItem` is used when the task is waiting on a queue or semaphore.

2. `uxPriority` is the task's priority. **Higher number = higher priority.** The idle task has priority 0 (`tskIDLE_PRIORITY`). The max is `configMAX_PRIORITIES - 1`.

3. For your EDF assignment, you'll add a **deadline field** to this struct (e.g., `TickType_t xAbsoluteDeadline`).

#### How the TCB, ListItems, and Lists All Connect -- A Visual

Let's say you have 3 tasks: LED (priority 2), Sensor (priority 2), and Idle (priority 0).

```
  ┌─────────────────────────────┐
  │    TCB: "LED" (priority 2)  │
  │    xStateListItem ──────────┼──┐
  │    xEventListItem           │  │ This hook is clipped onto
  │    uxPriority: 2            │  │ the priority-2 ready list
  └─────────────────────────────┘  │
                                   │
  ┌─────────────────────────────┐  │
  │  TCB: "Sensor" (priority 2)│  │
  │    xStateListItem ──────────┼──┤ This hook is ALSO on the
  │    xEventListItem           │  │ priority-2 ready list
  │    uxPriority: 2            │  │
  └─────────────────────────────┘  │
                                   │
         ┌─────────────────────────┘
         │
         ▼
  pxReadyTasksLists[2]:  ──→ [LED's xStateListItem] ←→ [Sensor's xStateListItem] ←→ (back to sentinel)
  pxReadyTasksLists[1]:  ──→ (empty)
  pxReadyTasksLists[0]:  ──→ [Idle's xStateListItem]
```

The tasks' `xStateListItem`s (their hooks) are literally linked together in the list. Through `pvOwner`, the scheduler can go from any list item back to the TCB to get the task's stack pointer, priority, name, etc.

---

### 4.3 The Ready Lists - How FreeRTOS Organizes Ready Tasks

This is the most important data structure for understanding the scheduler.

```c
static List_t pxReadyTasksLists[configMAX_PRIORITIES];
```

This is an **array of linked lists**, one for each priority level. If `configMAX_PRIORITIES = 32`, there are 32 lists (indexed 0 to 31). Most of them will be empty at any given time.

```
pxReadyTasksLists[31] → (empty)                    ← checked first
pxReadyTasksLists[30] → (empty)
...
pxReadyTasksLists[3]  → [High-priority tasks]
pxReadyTasksLists[2]  → [TaskA] → [TaskB]          ← two tasks share priority 2
pxReadyTasksLists[1]  → (empty)
pxReadyTasksLists[0]  → [Idle task]                 ← checked last (always has idle)
```

**Why an array of lists instead of just one list?** It makes finding the highest-priority task very fast. Instead of searching through ALL tasks, the scheduler just starts at the top index and walks down until it finds a non-empty list. That's the priority-based scheduling algorithm.

#### When Does a Task Go Into a Ready List?

A task is in a ready list when it **wants to run but isn't currently running**. Tasks enter a ready list when:
- They are first created (`xTaskCreate`)
- They wake up from a delay (`vTaskDelay` timer expires)
- They receive data from a queue they were waiting on
- They are resumed from suspension

#### Round-Robin Within a Ready List

**When does round-robin happen?** ONLY when there are **multiple tasks at the SAME priority level** and they are ALL ready to run. This is the ONLY time round-robin matters.

**Why does round-robin exist?** Fairness. If Task A and Task B both have priority 2, it would be unfair to always run Task A. Round-robin gives each task a turn.

**How does round-robin work?** Every time the tick interrupt fires (every 1 ms), FreeRTOS checks: "Are there other tasks at the same priority as the currently running task?" If yes, it switches to the next one in the list. Each task gets one **time slice** (one tick period, 1 ms) before the next task gets a turn.

**Concrete example of round-robin:**

```
Suppose configTICK_RATE_HZ = 1000 (1 tick = 1 ms)
Tasks: TaskA (priority 2), TaskB (priority 2), TaskC (priority 2)

pxReadyTasksLists[2]: [A] → [B] → [C]    (pxIndex starts at A)

Tick 0: Scheduler picks A (pxIndex advances to A). A runs for 1 ms.
Tick 1: Time slice! Scheduler advances pxIndex to B. B runs for 1 ms.
Tick 2: Time slice! Scheduler advances pxIndex to C. C runs for 1 ms.
Tick 3: Time slice! Scheduler wraps around, pxIndex to A. A runs for 1 ms.
Tick 4: Time slice! B again.
...

Each task gets 1 ms, then the next one goes. That's round-robin.
```

**What if tasks have DIFFERENT priorities?** Round-robin does NOT apply. The highest-priority task runs until it blocks (delays, waits on a queue, etc.). Lower-priority tasks get NO CPU time at all while a higher-priority task is ready.

```
Tasks: TaskA (priority 3), TaskB (priority 2), TaskC (priority 2)

TaskA runs and runs and runs... B and C never get a chance.
UNTIL TaskA calls vTaskDelay() or blocks on a queue.
Then: B and C take turns via round-robin.
When TaskA wakes up: B and C stop immediately. A runs again.
```

This is called **preemption** -- a higher-priority task immediately takes over when it becomes ready, even if a lower-priority task is in the middle of something.

**Adding a task to its ready list** (the `prvAddTaskToReadyList` macro):

```c
#define prvAddTaskToReadyList(pxTCB)                                                         \
    do {                                                                                     \
        taskRECORD_READY_PRIORITY((pxTCB)->uxPriority);                                      \
        listINSERT_END(&(pxReadyTasksLists[(pxTCB)->uxPriority]), &((pxTCB)->xStateListItem));\
    } while(0)
```

This does two things:
1. `taskRECORD_READY_PRIORITY` -- Updates `uxTopReadyPriority` if this task's priority is higher than the current highest. This is an optimization so the scheduler doesn't have to search from index 31 every time.
2. `listINSERT_END` -- Inserts the task at the back of the line for its priority level. Uses `vListInsertEnd` (NOT sorted) because all tasks in this list have the same priority, so sorting is meaningless. Inserting at the end ensures a newly ready task goes to the back of the round-robin line (it waits its turn).

### 4.4 The Delayed Task Lists

When a task calls `vTaskDelay(100)` (sleep for 100 ticks), it can't stay in the ready list. FreeRTOS moves it to a **delayed list**.

```c
static List_t xDelayedTaskList1;
static List_t xDelayedTaskList2;
static List_t *pxDelayedTaskList;              // Currently active delayed list
static List_t *pxOverflowDelayedTaskList;      // For tick-count overflow
```

Why two lists? Because the tick counter is a 32-bit integer that eventually wraps around from 0xFFFFFFFF to 0. Tasks that should wake up after the overflow go into the overflow list. When the tick counter wraps, the two lists swap roles.

Delayed tasks are sorted by their **wake-up time** (stored in `xStateListItem.xItemValue`). The task at the head of the list is the next one to wake up.

**Example:**

```
Current tick: 1000

TaskA calls vTaskDelay(200): wake at tick 1200
TaskB calls vTaskDelay(50):  wake at tick 1050
TaskC calls vTaskDelay(500): wake at tick 1500

Delayed list (sorted by wake-up time):
  [TaskB wake@1050] → [TaskA wake@1200] → [TaskC wake@1500]

At tick 1050: TaskB wakes up, moves to ready list.
At tick 1200: TaskA wakes up, moves to ready list.
At tick 1500: TaskC wakes up, moves to ready list.
```

The scheduler only needs to check the **head** of the delayed list each tick. If the head's wake-up time hasn't arrived yet, none of the others have either (because the list is sorted). This is very efficient.

### 4.5 The Scheduler - `taskSELECT_HIGHEST_PRIORITY_TASK`

This is the macro that picks the next task to run. For single-core (which is what you start with):

```c
#define taskSELECT_HIGHEST_PRIORITY_TASK()                                       \
do {                                                                             \
    UBaseType_t uxTopPriority = uxTopReadyPriority;                              \
                                                                                 \
    /* Find the highest priority queue that contains ready tasks. */             \
    while(listLIST_IS_EMPTY(&(pxReadyTasksLists[uxTopPriority])))               \
    {                                                                            \
        --uxTopPriority;                                                         \
    }                                                                            \
                                                                                 \
    /* Round-robin among tasks of the same priority. */                          \
    listGET_OWNER_OF_NEXT_ENTRY(pxCurrentTCB, &(pxReadyTasksLists[uxTopPriority]));\
    uxTopReadyPriority = uxTopPriority;                                          \
} while(0)
```

**How it works, step by step with an example:**

Let's say `configMAX_PRIORITIES = 5` and we have:
- TaskA and TaskB at priority 3
- TaskC at priority 1
- Idle at priority 0
- `uxTopReadyPriority = 3` (the highest known priority with ready tasks)

```
Step 1: uxTopPriority = 3
Step 2: Is pxReadyTasksLists[3] empty? NO -- it has TaskA and TaskB.
Step 3: Call listGET_OWNER_OF_NEXT_ENTRY on pxReadyTasksLists[3].
        Last time we picked TaskA, so this time pxIndex advances to TaskB.
        pxCurrentTCB = TaskB's TCB.
Step 4: uxTopReadyPriority = 3.
Done! TaskB will run next.
```

Now suppose both TaskA and TaskB call `vTaskDelay()` and move to the delayed list. The ready lists now look like:

```
pxReadyTasksLists[3]: (empty)
pxReadyTasksLists[2]: (empty)
pxReadyTasksLists[1]: [TaskC]
pxReadyTasksLists[0]: [Idle]
```

Next time the scheduler runs:
```
Step 1: uxTopPriority = 3 (still remembered from last time)
Step 2: Is pxReadyTasksLists[3] empty? YES → decrement: uxTopPriority = 2
        Is pxReadyTasksLists[2] empty? YES → decrement: uxTopPriority = 1
        Is pxReadyTasksLists[1] empty? NO -- it has TaskC.
Step 3: listGET_OWNER_OF_NEXT_ENTRY → picks TaskC.
        pxCurrentTCB = TaskC's TCB.
Step 4: uxTopReadyPriority = 1.
Done! TaskC will run.
```

**Where is this called?** Inside `vTaskSwitchContext()` (around line 5178 in `tasks.c`), which is called whenever a context switch is triggered (by the tick interrupt, a yield, etc.).

### 4.6 The Tick Interrupt - `xTaskIncrementTick`

Every 1 ms (by default), the hardware timer fires and calls `xTaskIncrementTick()`. This is the "heartbeat" of FreeRTOS. Here's what it does:

```
xTaskIncrementTick():
    1. Increment xTickCount by 1
    2. If tick count wrapped to 0: swap the two delayed lists
    3. Check the delayed list: if any tasks should wake up now:
       a. Remove them from the delayed list
       b. Add them to the ready list (prvAddTaskToReadyList)
       c. If the woken task has higher priority than the current task,
          set xSwitchRequired = TRUE
    4. If time slicing is enabled and there are multiple tasks at the
       current priority: set xSwitchRequired = TRUE   ← THIS is the round-robin trigger!
    5. Return xSwitchRequired (tells the port layer to do a context switch)
```

**Step 4 is where round-robin is triggered.** Let's zoom in:

```c
#if ( ( configUSE_PREEMPTION == 1 ) && ( configUSE_TIME_SLICING == 1 ) )
{
    if (listCURRENT_LIST_LENGTH(&(pxReadyTasksLists[pxCurrentTCB->uxPriority])) > 1U)
    {
        xSwitchRequired = pdTRUE;
    }
}
#endif
```

Translation: "If preemption and time slicing are enabled, check the ready list for the currently running task's priority. If there are more than 1 task at that priority, force a context switch." This context switch triggers `taskSELECT_HIGHEST_PRIORITY_TASK`, which calls `listGET_OWNER_OF_NEXT_ENTRY`, which advances to the next task. That's round-robin.

**So to summarize where round-robin fits into the big picture:**

```
 Tick interrupt fires (every 1 ms)
       │
       ▼
 xTaskIncrementTick()
       │
       ├── Wake up any sleeping tasks whose alarm went off
       │
       ├── Did a higher-priority task just wake up?
       │   YES → context switch needed (preemption!)
       │
       └── Are there other tasks at MY priority?      ← ROUND-ROBIN CHECK
           YES → context switch needed (time slicing!)
                 │
                 ▼
           vTaskSwitchContext()
                 │
                 ▼
           taskSELECT_HIGHEST_PRIORITY_TASK()
                 │
                 ├── Find highest non-empty priority list
                 │
                 └── listGET_OWNER_OF_NEXT_ENTRY()    ← ROUND-ROBIN: pick next task in the list
                           │
                           ▼
                     pxCurrentTCB = next task
                           │
                           ▼
                 CPU starts running the new task
```

### 4.7 Task Creation - `xTaskCreate`

When you call `xTaskCreate()`:

```c
xTaskCreate(
    vMyTaskFunction,    // The function this task will run
    "MyTask",           // Name (for debugging)
    256,                // Stack size in words
    NULL,               // Parameters passed to the task function
    2,                  // Priority (0 = lowest, configMAX_PRIORITIES-1 = highest)
    &xTaskHandle        // Output: handle to the created task
);
```

Internally:
1. Allocate memory for the TCB and stack.
2. Call `prvInitialiseNewTask()` to set up the TCB:
   - Set `uxPriority`, initialize list items
   - Set `xEventListItem.xItemValue = configMAX_PRIORITIES - uxPriority`
   - Set up the stack with initial register values
3. Call `prvAddNewTaskToReadyList()` to add the task to the scheduler:
   - Add to the appropriate ready list
   - If scheduler is running and the new task has higher priority, trigger a context switch

### 4.8 `vTaskDelay` and `vTaskDelayUntil`

**`vTaskDelay(xTicksToDelay)`** -- Relative delay:
- "Sleep for N ticks from now."
- Internally calls `prvAddCurrentTaskToDelayedList(xTicksToDelay, pdFALSE)`:
  1. Remove task from its ready list
  2. Calculate wake time: `xTimeToWake = xTickCount + xTicksToDelay`
  3. Set `xStateListItem.xItemValue = xTimeToWake`
  4. Insert into the delayed list (sorted by wake time via `vListInsert`)

**`vTaskDelayUntil(pxPreviousWakeTime, xTimeIncrement)`** -- Absolute delay:
- "Sleep until my next periodic wake-up time."
- Calculates: `xTimeToWake = *pxPreviousWakeTime + xTimeIncrement`
- Updates `*pxPreviousWakeTime = xTimeToWake` for the next period
- Used for **periodic tasks** -- this is important for real-time systems!

**Why are there two kinds of delay?**

`vTaskDelay(100)` says "sleep for 100 ticks starting NOW." But "now" depends on when the task actually reaches this line of code. If the task got delayed by other tasks, "now" might be later than expected, so the period drifts.

`vTaskDelayUntil` says "sleep until tick X." No matter how long the task took to run or how much it was delayed, the wake-up time is always exact. This keeps periodic tasks perfectly regular.

```
vTaskDelay(100) -- drifts over time:

  |--Task runs--|--delay 100--|--Task runs-----|--delay 100--|
  0            10            110              125            225
                                  ↑ took 15 ticks this time, not 10
                                    so the "period" is 115, not 110

vTaskDelayUntil(_, 100) -- stays precise:

  |--Task runs--|--delay--|--Task runs-----|--delay--|
  0            10    →  100              115    →  200
               exact!    ↑ even though it took 15 ticks,
                           wake-up is still at 200 (100+100)
```

### 4.9 Full Walkthrough: An Entire Scenario From Start to Finish

Let's trace everything with a concrete example. We have 3 user tasks plus the idle task:

```
TaskA: priority 3, blinks a red LED, runs briefly then delays 500 ticks
TaskB: priority 2, blinks a green LED, runs briefly then delays 300 ticks
TaskC: priority 2, blinks a yellow LED, runs briefly then delays 300 ticks
Idle:  priority 0, always ready, does nothing (built-in)
```

**Startup (before `vTaskStartScheduler`):**

```
xTaskCreate(TaskA, "Red",   256, NULL, 3, NULL);  → TCB_A created, added to pxReadyTasksLists[3]
xTaskCreate(TaskB, "Green", 256, NULL, 2, NULL);  → TCB_B created, added to pxReadyTasksLists[2]
xTaskCreate(TaskC, "Yellow",256, NULL, 2, NULL);  → TCB_C created, added to pxReadyTasksLists[2]

Ready lists:
  [3]: [A]
  [2]: [B] → [C]
  [1]: (empty)
  [0]: [Idle]

Delayed list: (empty)
```

**`vTaskStartScheduler()` is called:**

- Creates the Idle task at priority 0 (already shown above).
- Calls `taskSELECT_HIGHEST_PRIORITY_TASK()`:
  - `uxTopPriority = 3`, list[3] not empty → picks TaskA.
  - `pxCurrentTCB = TaskA`
- **TaskA starts running. Red LED blinks.**

**Tick 0-9: TaskA runs, does its work.**

Each tick, `xTaskIncrementTick()` fires:
- No delayed tasks to wake up.
- Round-robin check: `listCURRENT_LIST_LENGTH(pxReadyTasksLists[3])` = 1. Only one task at priority 3. No round-robin needed.
- `xSwitchRequired = FALSE`. TaskA keeps running.

**Tick 10: TaskA calls `vTaskDelay(500)`:**

- TaskA is removed from `pxReadyTasksLists[3]`.
- `xStateListItem.xItemValue = 10 + 500 = 510` (wake at tick 510).
- TaskA inserted into delayed list.

```
Ready lists:
  [3]: (empty!!)
  [2]: [B] → [C]
  [0]: [Idle]

Delayed list: [A wakes@510]
```

- `vTaskDelay` triggers a context switch → `taskSELECT_HIGHEST_PRIORITY_TASK()`:
  - `uxTopPriority = 3`, list[3] is empty → decrement to 2.
  - list[2] not empty → picks TaskB (pxIndex advances to B).
  - `pxCurrentTCB = TaskB`
- **TaskB starts running. Green LED blinks.**

**Tick 11: Time slicing check (round-robin!):**

- `xTaskIncrementTick()` fires.
- No delayed tasks to wake (next wakeup is at 510).
- Round-robin check: `listCURRENT_LIST_LENGTH(pxReadyTasksLists[2])` = 2. Two tasks at priority 2!
- `xSwitchRequired = TRUE` → context switch!
- `taskSELECT_HIGHEST_PRIORITY_TASK()`:
  - list[2] not empty → `listGET_OWNER_OF_NEXT_ENTRY` → advances pxIndex from B to C.
  - `pxCurrentTCB = TaskC`
- **TaskC starts running. Yellow LED blinks.**

**Tick 12: Round-robin again:**

- `listCURRENT_LIST_LENGTH(pxReadyTasksLists[2])` = 2 → switch!
- `listGET_OWNER_OF_NEXT_ENTRY` → wraps around back to B.
- **TaskB runs again.**

**Tick 13: TaskB, Tick 14: TaskC, Tick 15: TaskB... they alternate every 1 ms.**

**Tick 20: TaskB calls `vTaskDelay(300)` (while it has the CPU):**

- TaskB removed from ready list[2], added to delayed list with wake@320.
- Only TaskC at priority 2 now. Round-robin stops (only one task).

```
Ready lists:
  [2]: [C]      (only one task -- no round-robin!)
  [0]: [Idle]

Delayed list: [B wakes@320] → [A wakes@510]
```

**TaskC runs exclusively until it delays or a higher-priority task wakes up.**

**Tick 30: TaskC calls `vTaskDelay(300)`:**

```
Ready lists:
  [2]: (empty)
  [0]: [Idle]

Delayed list: [C wakes@330] → [B wakes@320]
  Wait -- sorted! B wakes first: [B wakes@320] → [C wakes@330] → [A wakes@510]
```

- No tasks at priority 2 or 3 anymore. Scheduler picks Idle (priority 0).
- **Idle task runs (does nothing useful).**

**Tick 320: xTaskIncrementTick() wakes TaskB:**

- `xTickCount = 320`, delayed list head = B@320. Match! Wake up B.
- Remove B from delayed list, add to `pxReadyTasksLists[2]`.
- B's priority (2) > Idle's priority (0) → `xSwitchRequired = TRUE`.
- **TaskB preempts Idle and runs. Green LED blinks.**

**Tick 330: xTaskIncrementTick() wakes TaskC:**

- Remove C from delayed list, add to `pxReadyTasksLists[2]`.
- Now list[2] has both B and C again → round-robin resumes!

**Tick 510: TaskA wakes up:**

- Remove A from delayed list, add to `pxReadyTasksLists[3]`.
- A's priority (3) > B/C's priority (2) → **TaskA preempts immediately**.
- B and C stop. A runs. Red LED blinks.
- When A delays again, B and C resume their round-robin.

**And the cycle repeats forever.**

### 4.10 Summary: The Scheduling Flow

```
1. Tick interrupt fires (every 1 ms)
   │
   ▼
2. xTaskIncrementTick()
   - Increment tick count
   - Wake up any delayed tasks whose time has come
   - Check if context switch needed (preemption or round-robin)
   │
   ▼
3. If context switch needed → vTaskSwitchContext()
   │
   ▼
4. taskSELECT_HIGHEST_PRIORITY_TASK()
   - Find highest non-empty priority list
   - Pick next task from that list (round-robin if multiple tasks)
   - Set pxCurrentTCB = selected task
   │
   ▼
5. Port layer restores the new task's context
   (loads its stack pointer, registers, resumes execution)
```

### 4.11 Glossary: Connecting Every Term

| Term | What It Is | How It Relates |
|------|-----------|----------------|
| **Task** | A function that runs in an infinite loop (your code) | Represented internally by a TCB |
| **TCB** | The task's identity card (struct in memory) | Contains the list items, priority, stack pointer, name |
| **ListItem_t** | A "hook" embedded inside the TCB | Used to attach the task to a list |
| **xStateListItem** | The TCB's hook for state lists | Puts the task in a ready list, delayed list, or suspended list (one at a time) |
| **xEventListItem** | The TCB's hook for event lists | Puts the task in a queue/semaphore wait list (can be simultaneous with a state list) |
| **xItemValue** | A number on the hook used for sorting | Means wake-up time in delayed list; means deadline in EDF; unused for sorting in ready lists |
| **List_t** | A circular doubly-linked list (the rope) | Holds multiple ListItems linked together |
| **pxReadyTasksLists** | Array of 32 lists, one per priority | Index = priority. Tasks waiting to run go here |
| **pxDelayedTaskList** | One sorted list | Tasks sleeping via vTaskDelay go here, sorted by wake-up time |
| **pxCurrentTCB** | Pointer to the TCB of the running task | THE task currently on the CPU |
| **uxPriority** | Number (0 to 31) stored in TCB | Higher = more important. Decides which ready list the task goes in |
| **Round-robin** | Taking turns | ONLY happens when 2+ tasks share the same priority. Each gets 1 tick (1 ms) |
| **Preemption** | Kicking out the running task | Happens when a higher-priority task becomes ready (wakes up, unblocks) |
| **Time slicing** | Round-robin triggered by the tick | Every tick, if there are peers at the same priority, switch to the next one |
| **Tick interrupt** | Hardware timer, fires every 1 ms | The heartbeat. Triggers xTaskIncrementTick, which decides if a context switch is needed |
| **Context switch** | Swapping one task for another | Save old task's registers/stack, load new task's registers/stack |
| **vTaskDelay** | "Sleep for N ticks" | Moves task from ready list to delayed list |
| **vTaskDelayUntil** | "Sleep until tick X" | Same, but with precise periodic timing |
| **taskSELECT_HIGHEST_PRIORITY_TASK** | The scheduler's brain | Finds highest non-empty ready list, picks next task via round-robin within it |
| **prvAddTaskToReadyList** | Put a task into its ready list | Uses listINSERT_END (unsorted, at the back of the round-robin line) |
| **vListInsert** | Insert into a list in sorted order | Used for delayed list (sorted by wake time) and EDF list (sorted by deadline) |
| **vListInsertEnd** | Insert into a list without sorting | Used for ready lists (no need to sort -- all same priority) |
| **listGET_OWNER_OF_NEXT_ENTRY** | Get the next task in round-robin | Advances the pxIndex cursor and returns the next task's TCB |

---

## 5. Assignment 1: Earliest Deadline First (EDF) - 15%

### 5.1 What Is EDF?

In the default FreeRTOS scheduler, each task has a **fixed priority** set at creation time. The highest-priority ready task always runs. This is called **Fixed-Priority Preemptive Scheduling (FPS)**.

**EDF (Earliest Deadline First)** is different. Instead of fixed priorities, each task has a **deadline** -- the absolute time by which it must finish its current job. The scheduler always picks the task whose deadline is the **earliest** (closest to now).

**Why EDF?** EDF is provably optimal for single-processor systems -- it can schedule any task set that is schedulable. Fixed-priority scheduling (like Rate Monotonic) cannot. EDF achieves up to **100% CPU utilization**, while Rate Monotonic caps at ~69%.

#### Understanding Periodic Tasks and Deadlines

Before we go further, let's make sure we understand what a "periodic task" is, because EDF is designed for them.

A **periodic task** repeats the same job over and over on a fixed schedule. It has three key numbers:

- **Period (T):** How often the task repeats. E.g., "every 500 ms."
- **Execution time (C):** How long the task takes to do its job each period. E.g., "50 ms of actual computation."
- **Relative Deadline (D):** How much time after the start of each period the task has to finish. Often D = T (must finish before the next period starts).

```
Period = 500ms, Execution = 50ms, Deadline = 500ms

  |--50ms work--|--------450ms idle---------|--50ms work--|-------...
  0            50                          500           550
  ↑                                        ↑
  Job 1 starts                             Job 2 starts
  Deadline for Job 1 is at tick 500        Deadline for Job 2 is at tick 1000
```

The **absolute deadline** is the actual tick count by which the job must finish. It changes every period:
- Job 1's absolute deadline = 0 + 500 = 500
- Job 2's absolute deadline = 500 + 500 = 1000
- Job 3's absolute deadline = 1000 + 500 = 1500
- ...

**EDF says: always run the task whose absolute deadline is the smallest (the closest one).**

#### EDF Example: Step by Step

Imagine two periodic tasks:
- Task A: Period = 5 ticks, Execution time = 2 ticks
- Task B: Period = 7 ticks, Execution time = 3 ticks

```
Time 0: Both tasks start their first job.
  Task A: absolute deadline = 0 + 5 = 5
  Task B: absolute deadline = 0 + 7 = 7
  EDF picks Task A (deadline 5 < 7). A runs.

Time 2: Task A finishes its 2 ticks of work. A sleeps until tick 5.
  Only Task B is ready. B runs.

Time 5: Task A's second period starts. New deadline = 5 + 5 = 10.
  Task B's deadline is still 7 (B hasn't finished yet).
  EDF keeps running B (deadline 7 < 10).

Time 5: Task B finishes its 3 ticks of work (ran from tick 2 to 5). B sleeps until tick 7.
  Only Task A is ready (deadline 10). A runs.

Time 7: Task A finishes its 2 ticks (ran from tick 5 to 7). A sleeps until tick 10.
  Task B's second period starts. New deadline = 7 + 7 = 14.
  B runs (it's the only ready task).

... and so on.
```

The key insight: **the task with the nearest deadline dynamically gets the highest effective priority**. Unlike fixed-priority where Task A is always "priority 3," in EDF the task that is most urgent RIGHT NOW gets to run.

### 5.2 EDF Design: How to Map It onto FreeRTOS

Remember from Section 4 how FreeRTOS scheduling works:
1. An **array of 32 ready lists** (`pxReadyTasksLists[0..31]`), one per priority
2. The scheduler finds the **highest non-empty list** and picks a task from it
3. Tasks are added to their priority's list using `listINSERT_END` (no sorting)

For EDF, we need a fundamentally different approach:
1. A **single ready list** where tasks are sorted by their absolute deadline
2. The scheduler picks the task at the **head** of this list (smallest deadline = most urgent)
3. Tasks are added using `vListInsert` (sorted by deadline using `xItemValue`)

We keep the old priority lists around for non-EDF tasks (like the Idle task), but EDF tasks go into their own sorted list.

### 5.3 Files to Modify

| File | What to Change | Why |
|------|---------------|-----|
| `FreeRTOS/Source/tasks.c` | TCB structure, ready list management, scheduler macro, tick handler | This is the core of the scheduler |
| `FreeRTOS/Source/include/task.h` | New API functions | So user code can create EDF tasks |
| `FreeRTOS/Source/include/FreeRTOS.h` | New config macros | On/off switch for EDF |
| `Demo/.../Standard/FreeRTOSConfig.h` | Enable your new config macros | Turn on EDF for your build |
| `Demo/.../Standard/main.c` or new test file | Test application with periodic tasks | Prove it works |

### 5.4 Step-by-Step Implementation

#### Step 1: Add New Configuration Macro

**What:** A compile-time switch that lets you turn EDF on or off. If it's 0 (off), FreeRTOS behaves normally. If it's 1, your EDF code activates.

**Why:** This way you don't break the original FreeRTOS. You can test with EDF on or off without changing code.

**File:** `Demo/.../Standard/FreeRTOSConfig.h` -- add this line:
```c
/* Set to 1 to enable EDF scheduling. Set to 0 for normal fixed-priority. */
#define configUSE_EDF_SCHEDULING    1
```

**File:** `FreeRTOS/Source/include/FreeRTOS.h` -- add a default so it doesn't break if someone forgets to define it:
```c
/* If the user didn't define configUSE_EDF_SCHEDULING in their config,
 * default to 0 (disabled) so the kernel behaves like standard FreeRTOS. */
#ifndef configUSE_EDF_SCHEDULING
    #define configUSE_EDF_SCHEDULING    0
#endif
```

#### Step 2: Add Deadline Fields to the TCB

**What:** Add three new fields to every task's identity card (TCB) so we can track its period, relative deadline, and current absolute deadline.

**File:** `FreeRTOS/Source/tasks.c` -- inside the `tskTaskControlBlock` struct (around line 371), add after the existing fields:

```c
#if ( configUSE_EDF_SCHEDULING == 1 )
    TickType_t xRelativeDeadline;
    TickType_t xPeriod;
    TickType_t xAbsoluteDeadline;
#endif
```

**What each field means:**

| Field | Type | Changes? | Meaning | Example |
|-------|------|----------|---------|---------|
| `xPeriod` | `TickType_t` (unsigned 32-bit int) | Never changes after creation | How many ticks between repetitions of this task | 500 (for a 500ms period with 1ms ticks) |
| `xRelativeDeadline` | `TickType_t` | Never changes after creation | How many ticks after each period start the task must finish | 500 (if deadline = period) |
| `xAbsoluteDeadline` | `TickType_t` | Updated every period | The actual tick count by which the current job must finish. **This is what the scheduler sorts by.** | At tick 0: absolute deadline = 500. At tick 500: absolute deadline = 1000. Etc. |

**Example:** A task with period=500 and relative deadline=500:
```
Tick    0: xAbsoluteDeadline = 0 + 500 = 500
Tick  500: xAbsoluteDeadline = 500 + 500 = 1000
Tick 1000: xAbsoluteDeadline = 1000 + 500 = 1500
```

#### Step 3: Create an EDF Task Creation Function

**What:** A new function (like `xTaskCreate`, but for EDF tasks) that the user calls to create a periodic task with a period and deadline.

**File:** `FreeRTOS/Source/include/task.h` -- add the declaration:

```c
#if ( configUSE_EDF_SCHEDULING == 1 )
    BaseType_t xTaskCreateEDF(
        TaskFunction_t pxTaskCode,
        const char * const pcName,
        const configSTACK_DEPTH_TYPE uxStackDepth,
        void * const pvParameters,
        TickType_t xPeriod,
        TickType_t xRelativeDeadline,
        TaskHandle_t * const pxCreatedTask
    );
#endif
```

**What each parameter means:**

| Parameter | Type | What It Is | Example |
|-----------|------|-----------|---------|
| `pxTaskCode` | `TaskFunction_t` (function pointer) | The function this task will run. Must be `void myFunc(void *params)` with an infinite loop inside. | `vBlinkRedLED` |
| `pcName` | `const char *` | A human-readable name for the task, only used for debugging. Max length is `configMAX_TASK_NAME_LEN` (default 16 chars). | `"RedLED"` |
| `uxStackDepth` | `configSTACK_DEPTH_TYPE` (usually `uint16_t`) | How many **words** (not bytes!) of stack space to give this task. Each word is 4 bytes on RP2040. So 256 words = 1024 bytes. Used for local variables, function calls, etc. | `256` |
| `pvParameters` | `void *` | A pointer to anything you want to pass into the task function. The task receives this as its parameter. Pass `NULL` if the task doesn't need any input. | `NULL` or `&myConfig` |
| `xPeriod` | `TickType_t` (unsigned 32-bit int) | The task's period in ticks. Use `pdMS_TO_TICKS(ms)` to convert milliseconds to ticks. | `pdMS_TO_TICKS(500)` for 500ms |
| `xRelativeDeadline` | `TickType_t` | The task's relative deadline in ticks. Often equals the period (must finish before next period). | `pdMS_TO_TICKS(500)` |
| `pxCreatedTask` | `TaskHandle_t *` | Output: FreeRTOS writes the task's handle here so you can refer to the task later (e.g., to delete it). Pass `NULL` if you don't need the handle. | `NULL` or `&xMyTaskHandle` |

**Return value:** `pdPASS` (success) or `errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY` (failed -- not enough heap memory).

**File:** `FreeRTOS/Source/tasks.c` -- implement the function:

```c
#if ( configUSE_EDF_SCHEDULING == 1 )
BaseType_t xTaskCreateEDF( TaskFunction_t pxTaskCode,
                           const char * const pcName,
                           const configSTACK_DEPTH_TYPE uxStackDepth,
                           void * const pvParameters,
                           TickType_t xPeriod,
                           TickType_t xRelativeDeadline,
                           TaskHandle_t * const pxCreatedTask )
{
    TCB_t * pxNewTCB;
    BaseType_t xReturn;

    /* prvCreateTask is an internal FreeRTOS function that allocates memory
     * for the TCB and stack, then calls prvInitialiseNewTask to set up
     * the TCB fields. We pass tskIDLE_PRIORITY + 1 as the priority
     * because EDF tasks don't use priority for scheduling -- they use
     * deadlines instead. We just need a priority above idle (0) so
     * FreeRTOS doesn't treat them as idle tasks. */
    pxNewTCB = prvCreateTask( pxTaskCode, pcName, uxStackDepth, pvParameters,
                              tskIDLE_PRIORITY + 1, pxCreatedTask );

    if( pxNewTCB != NULL )
    {
        /* Set the EDF-specific fields in the TCB. */

        /* xPeriod: how often this task repeats (constant, never changes). */
        pxNewTCB->xPeriod = xPeriod;

        /* xRelativeDeadline: how many ticks after each period start the
         * task must finish (constant, never changes). */
        pxNewTCB->xRelativeDeadline = xRelativeDeadline;

        /* xAbsoluteDeadline: the actual tick time the current job must
         * finish by. For the very first job, the task starts at tick 0,
         * so its first absolute deadline = 0 + xRelativeDeadline. */
        pxNewTCB->xAbsoluteDeadline = xRelativeDeadline;

        /* Add the task to the ready list so the scheduler knows about it. */
        prvAddNewTaskToReadyList( pxNewTCB );

        xReturn = pdPASS;
    }
    else
    {
        /* Memory allocation failed. */
        xReturn = errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
    }

    return xReturn;
}
#endif
```

#### Step 4: Add the EDF Ready List and Modify How Tasks Are Added

**What:** Instead of putting EDF tasks into `pxReadyTasksLists[priority]`, we put them into a single new list sorted by deadline. Non-EDF tasks (like Idle) still go into the original priority lists.

**File:** `FreeRTOS/Source/tasks.c` -- add a new global variable near the other list declarations (around line 476):

```c
#if ( configUSE_EDF_SCHEDULING == 1 )
    /* A single sorted list for all EDF tasks. Tasks are sorted by
     * xAbsoluteDeadline (stored in xStateListItem.xItemValue).
     * The task at the HEAD of the list has the earliest deadline
     * and should run next. */
    PRIVILEGED_DATA static List_t xEDFReadyList;
#endif
```

**Now modify the `prvAddTaskToReadyList` macro** (around line 285). This macro is called every time a task becomes ready (created, woken from delay, unblocked from a queue, etc.):

```c
#if ( configUSE_EDF_SCHEDULING == 1 )
    #define prvAddTaskToReadyList( pxTCB )                                             \
    do {                                                                               \
        traceMOVED_TASK_TO_READY_STATE( pxTCB );                                       \
        /* Check if this is an EDF task (has a period > 0) or a                        \
         * non-EDF task (like Idle, which has period 0). */                             \
        if( ( pxTCB )->xPeriod > 0 )                                                   \
        {                                                                              \
            /* EDF task: set xItemValue to the absolute deadline so                    \
             * vListInsert will sort it in ascending deadline order.                    \
             * The task with the smallest deadline ends up at the head. */              \
            listSET_LIST_ITEM_VALUE( &( ( pxTCB )->xStateListItem ),                   \
                                     ( pxTCB )->xAbsoluteDeadline );                   \
            vListInsert( &xEDFReadyList, &( ( pxTCB )->xStateListItem ) );             \
        }                                                                              \
        else                                                                           \
        {                                                                              \
            /* Non-EDF task (Idle, Timer, etc.): use the original                      \
             * priority-based ready list, exactly like standard FreeRTOS. */            \
            taskRECORD_READY_PRIORITY( ( pxTCB )->uxPriority );                        \
            listINSERT_END( &( pxReadyTasksLists[ ( pxTCB )->uxPriority ] ),           \
                            &( ( pxTCB )->xStateListItem ) );                          \
        }                                                                              \
        tracePOST_MOVED_TASK_TO_READY_STATE( pxTCB );                                  \
    } while( 0 )
#else
    /* Original FreeRTOS macro (unchanged when EDF is disabled). */
    #define prvAddTaskToReadyList( pxTCB )                                                                     \
    do {                                                                                                       \
        traceMOVED_TASK_TO_READY_STATE( pxTCB );                                                               \
        taskRECORD_READY_PRIORITY( ( pxTCB )->uxPriority );                                                    \
        listINSERT_END( &( pxReadyTasksLists[ ( pxTCB )->uxPriority ] ), &( ( pxTCB )->xStateListItem ) );     \
        tracePOST_MOVED_TASK_TO_READY_STATE( pxTCB );                                                          \
    } while( 0 )
#endif
```

**Note:** If you later implement CBS (Assignment 3), you will update this macro to add tie-breaking in favor of CBS server tasks. See CBS Step 6.

**What's happening here visually:**

```
Before (standard FreeRTOS):
  pxReadyTasksLists[3]: [TaskA] → [TaskD]       (all priority 3, unsorted)
  pxReadyTasksLists[2]: [TaskB]                  (priority 2)
  pxReadyTasksLists[0]: [Idle]                   (priority 0)

After (with EDF):
  xEDFReadyList: [TaskB dl:500] → [TaskA dl:800] → [TaskD dl:1200]    (sorted by deadline!)
  pxReadyTasksLists[0]: [Idle]                                         (non-EDF stays here)
```

#### Step 5: Modify the Scheduler to Pick the Earliest Deadline

**What:** Change `taskSELECT_HIGHEST_PRIORITY_TASK` so that instead of finding the highest-priority list and round-robining, it simply picks the head of the EDF sorted list (the task with the smallest deadline).

**File:** `FreeRTOS/Source/tasks.c` -- modify the `taskSELECT_HIGHEST_PRIORITY_TASK` macro (around line 195):

```c
#if ( configUSE_EDF_SCHEDULING == 1 )
    #define taskSELECT_HIGHEST_PRIORITY_TASK()                                    \
    do {                                                                          \
        if( listLIST_IS_EMPTY( &xEDFReadyList ) == pdFALSE )                     \
        {                                                                         \
            /* There are EDF tasks ready to run. Pick the one at the              \
             * head of the sorted list -- it has the earliest (smallest)          \
             * absolute deadline, so it's the most urgent. */                     \
            pxCurrentTCB = ( TCB_t * ) listGET_OWNER_OF_HEAD_ENTRY(               \
                                           &xEDFReadyList );                      \
        }                                                                         \
        else                                                                      \
        {                                                                         \
            /* No EDF tasks are ready (all blocked/delayed).                      \
             * Fall back to the idle task in pxReadyTasksLists[0].               \
             * The idle task is always ready (it never blocks). */                 \
            pxCurrentTCB = ( TCB_t * ) listGET_OWNER_OF_HEAD_ENTRY(               \
                               &( pxReadyTasksLists[ tskIDLE_PRIORITY ] ) );      \
        }                                                                         \
    } while( 0 )
#else
    /* Original macro (unchanged). */
    #define taskSELECT_HIGHEST_PRIORITY_TASK()                                       \
    do {                                                                             \
        UBaseType_t uxTopPriority = uxTopReadyPriority;                              \
        while( listLIST_IS_EMPTY( &( pxReadyTasksLists[ uxTopPriority ] ) ) )       \
        {                                                                            \
            --uxTopPriority;                                                         \
        }                                                                            \
        listGET_OWNER_OF_NEXT_ENTRY( pxCurrentTCB,                                  \
                                     &( pxReadyTasksLists[ uxTopPriority ] ) );      \
        uxTopReadyPriority = uxTopPriority;                                          \
    } while( 0 )
#endif
```

**Notice the difference:**
- Original: searches through an array of 32 lists, then does round-robin with `listGET_OWNER_OF_NEXT_ENTRY`.
- EDF: simply grabs the head of ONE sorted list with `listGET_OWNER_OF_HEAD_ENTRY`. Much simpler! The sorting was done at insertion time.

#### Step 6: Initialize the EDF Ready List

**What:** The new list needs to be initialized before it's used (set up its sentinel, pointers, etc.).

**File:** `FreeRTOS/Source/tasks.c` -- in `prvInitialiseTaskLists()` (around line 6082), add after the existing loop that initializes ready lists:

```c
#if ( configUSE_EDF_SCHEDULING == 1 )
    /* Initialize the EDF ready list. This sets up the sentinel node
     * (xListEnd) and clears the item count. Must be done before any
     * EDF tasks are created. */
    vListInitialise( &xEDFReadyList );
#endif
```

#### Step 7: Handle Periodic Task Re-activation (Deadline Update)

**What:** When a periodic EDF task finishes its work for the current period, it needs to:
1. Update its `xAbsoluteDeadline` for the next period (push it forward by one period).
2. Block (sleep) until the next period starts.

We create a helper function that each periodic task calls at the end of every loop iteration:

**File:** `FreeRTOS/Source/include/task.h` -- add the declaration:
```c
#if ( configUSE_EDF_SCHEDULING == 1 )
    /* Call this at the end of each period in your periodic EDF task.
     * It updates the task's deadline and blocks until the next period.
     * pxPreviousWakeTime: pointer to a variable that tracks wake times
     *                     (initialize it with xTaskGetTickCount() before
     *                     your task's main loop). */
    void vTaskDelayUntilNextPeriod( TickType_t * pxPreviousWakeTime );
#endif
```

**File:** `FreeRTOS/Source/tasks.c` -- implement it:
```c
#if ( configUSE_EDF_SCHEDULING == 1 )
void vTaskDelayUntilNextPeriod( TickType_t * pxPreviousWakeTime )
{
    /* Get a pointer to the currently running task's TCB. */
    TCB_t * pxCurrentTCBLocal = pxCurrentTCB;

    /* Push the absolute deadline forward by one period.
     * Example: if xAbsoluteDeadline was 500 and xPeriod is 500,
     * the new deadline becomes 1000. */
    pxCurrentTCBLocal->xAbsoluteDeadline += pxCurrentTCBLocal->xPeriod;

    /* Block this task until the next period starts.
     * vTaskDelayUntil handles the precise timing -- it calculates
     * the exact tick to wake up at, independent of how long the
     * task actually took to execute. This prevents period drift.
     *
     * Internally, this removes the task from the EDF ready list
     * and puts it in the delayed list. When it wakes up,
     * prvAddTaskToReadyList is called, which re-inserts it into
     * the EDF ready list with the updated deadline. */
    vTaskDelayUntil( pxPreviousWakeTime, pxCurrentTCBLocal->xPeriod );
}
#endif
```

**How a periodic EDF task uses this:**
```c
void vMyPeriodicTask( void * pvParameters )
{
    /* Initialize the wake time tracker. */
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for( ;; )  /* Infinite loop -- one iteration = one "job" */
    {
        /* === Do the task's actual work here === */
        doSomethingUseful();

        /* === Done with this period's work. Sleep until next period. === */
        vTaskDelayUntilNextPeriod( &xLastWakeTime );
        /* When we wake up, xAbsoluteDeadline has been updated
         * and we're back in the EDF ready list with the new deadline. */
    }
}
```

#### Step 8: Modify the Tick Handler for EDF Preemption

**What:** In the original FreeRTOS, when a task wakes up from the delayed list, the kernel checks if it should preempt the current task by comparing **priorities**. For EDF, we compare **deadlines** instead.

**File:** `FreeRTOS/Source/tasks.c` -- in `xTaskIncrementTick()` (around line 4736)

Find the code that checks for preemption after unblocking a delayed task. In the original code it looks like:
```c
if( pxTCB->uxPriority > pxCurrentTCB->uxPriority )
{
    xSwitchRequired = pdTRUE;
}
```

Replace it with:
```c
#if ( configUSE_EDF_SCHEDULING == 1 )
    /* EDF preemption check: the newly woken task should preempt
     * the currently running task if it has an EARLIER deadline.
     *
     * IMPORTANT: we also check xPeriod to handle the idle task.
     * The idle task has xPeriod == 0 and xAbsoluteDeadline == 0
     * (never initialized for EDF). Without the xPeriod check,
     * any EDF task's deadline (e.g. 1000) would appear "later"
     * than idle's 0, and EDF tasks would never preempt idle
     * after waking from a delay. */
    if( pxTCB->xPeriod > 0 )
    {
        if( ( pxCurrentTCB->xPeriod == 0 ) ||
            ( pxTCB->xAbsoluteDeadline < pxCurrentTCB->xAbsoluteDeadline ) )
        {
            xSwitchRequired = pdTRUE;
        }
    }
#else
    /* Standard FreeRTOS: preempt if higher priority. */
    if( pxTCB->uxPriority > pxCurrentTCB->uxPriority )
    {
        xSwitchRequired = pdTRUE;
    }
#endif
```

Also **disable time slicing** for EDF tasks. Time slicing is the round-robin mechanism that switches between tasks of equal priority every tick. EDF doesn't use priority-based round-robin -- it uses deadlines. Find the time-slicing code and wrap it:

```c
#if ( configUSE_EDF_SCHEDULING == 0 )
    /* Time slicing only makes sense for fixed-priority scheduling.
     * Under EDF, we don't round-robin -- we always run the task
     * with the earliest deadline. */
    #if ( ( configUSE_PREEMPTION == 1 ) && ( configUSE_TIME_SLICING == 1 ) )
    {
        if( listCURRENT_LIST_LENGTH(
                &( pxReadyTasksLists[ pxCurrentTCB->uxPriority ] ) ) > 1U )
        {
            xSwitchRequired = pdTRUE;
        }
    }
    #endif
#endif
```

#### Step 9: Write a Test Application

**File:** `Demo/.../Standard/main_edf_test.c` (new file) or modify `main_blinky.c`

```c
#include "FreeRTOS.h"
#include "task.h"
#include "pico/stdlib.h"      /* Pico SDK: GPIO, stdio */

/* Each task gets its own GPIO pin. When the task is running,
 * its pin is HIGH. When it's done (sleeping), its pin is LOW.
 * Connect LEDs or a logic analyzer to these pins to see the schedule. */
#define LED_PIN_TASK_A  2
#define LED_PIN_TASK_B  3

/* Task A: runs every 500ms, does ~100ms of work each time.
 * Deadline = period = 500ms (must finish before next period). */
void vTaskA( void * pvParameters )
{
    /* Initialize the wake-time tracker to the current tick.
     * vTaskDelayUntilNextPeriod uses this to calculate exact
     * periodic wake-up times. */
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for( ;; )
    {
        /* Signal that Task A is running (turn on LED / set GPIO high). */
        gpio_put( LED_PIN_TASK_A, 1 );

        /* Simulate doing ~100ms of work by busy-waiting.
         * In a real application, this would be actual computation
         * (reading a sensor, processing data, etc.). */
        TickType_t xStart = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xStart ) < pdMS_TO_TICKS( 100 ) )
        {
            /* Busy wait -- the task is "working." */
        }

        /* Signal that Task A is done with this period's work. */
        gpio_put( LED_PIN_TASK_A, 0 );

        /* Sleep until the next period. This also updates the deadline.
         * The task will wake up at xLastWakeTime + period (500ms).
         * While sleeping, other tasks can use the CPU. */
        vTaskDelayUntilNextPeriod( &xLastWakeTime );
    }
}

/* Task B: runs every 1000ms, does ~200ms of work each time.
 * Deadline = period = 1000ms. */
void vTaskB( void * pvParameters )
{
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for( ;; )
    {
        gpio_put( LED_PIN_TASK_B, 1 );

        TickType_t xStart = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xStart ) < pdMS_TO_TICKS( 200 ) )
        {
            /* Busy wait. */
        }

        gpio_put( LED_PIN_TASK_B, 0 );

        vTaskDelayUntilNextPeriod( &xLastWakeTime );
    }
}

void main_edf_test( void )
{
    /* Initialize GPIO pins for LEDs. */
    gpio_init( LED_PIN_TASK_A );
    gpio_set_dir( LED_PIN_TASK_A, GPIO_OUT );
    gpio_init( LED_PIN_TASK_B );
    gpio_set_dir( LED_PIN_TASK_B, GPIO_OUT );

    /* Create Task A: period=500ms, deadline=500ms.
     * pdMS_TO_TICKS() converts milliseconds to ticks.
     * With configTICK_RATE_HZ=1000, pdMS_TO_TICKS(500) = 500 ticks. */
    xTaskCreateEDF( vTaskA,               /* Task function */
                    "TaskA",              /* Name (for debugging) */
                    256,                  /* Stack size in words */
                    NULL,                 /* No parameters to pass */
                    pdMS_TO_TICKS( 500 ), /* Period: 500ms */
                    pdMS_TO_TICKS( 500 ), /* Deadline: 500ms (= period) */
                    NULL );               /* Don't need the handle */

    /* Create Task B: period=1000ms, deadline=1000ms. */
    xTaskCreateEDF( vTaskB,
                    "TaskB",
                    256,
                    NULL,
                    pdMS_TO_TICKS( 1000 ),
                    pdMS_TO_TICKS( 1000 ),
                    NULL );

    /* Start the scheduler. This function never returns.
     * From this point on, FreeRTOS is in control. It will
     * create the Idle task, start the tick timer, and begin
     * running the task with the earliest deadline. */
    vTaskStartScheduler();

    /* We should never get here. If we do, it means the scheduler
     * failed to start (probably out of memory). */
    for( ;; ) { }
}
```

**What you should see on the logic analyzer:**

```
Time (ms):  0    100   200   500  600   700  1000  1100  1200
            |     |     |     |    |     |    |     |     |
Task A:     ██████░░░░░░░░░░░██████░░░░░░░░░██████░░░░░░░░░░
Task B:     ░░░░░░████████████░░░░░░░░░░░░░░░░░░░░░████████████
Idle:       ░░░░░░░░░░░░░░░░░░░░░░██████████░░░░░░░░░░░░░░░░

█ = running    ░ = sleeping/idle

- At tick 0: A's deadline=500, B's deadline=1000. A runs first (earlier deadline).
- At tick 100: A finishes, sleeps until 500. B runs (deadline 1000).
- At tick 300: B finishes, sleeps until 1000. Idle runs.
- At tick 500: A wakes up, new deadline=1000. B still sleeping.
  A and B both have deadline 1000, but A woke up first, so A runs.
- At tick 600: A finishes. Idle runs until tick 1000.
- At tick 1000: Both wake up. A's deadline=1500, B's deadline=2000.
  A runs first (earlier deadline). Then B. Then Idle. ...
```

### 5.5 Summary of All Changes for EDF

| File | Change | Lines (approx.) |
|------|--------|-----------------|
| `tasks.c` | Add `xRelativeDeadline`, `xPeriod`, `xAbsoluteDeadline` to TCB struct | ~line 450 |
| `tasks.c` | Add `xEDFReadyList` global variable | ~line 480 |
| `tasks.c` | Modify `prvAddTaskToReadyList` macro (EDF tasks → sorted list, non-EDF → priority list) | ~line 285 |
| `tasks.c` | Modify `taskSELECT_HIGHEST_PRIORITY_TASK` macro (pick head of sorted EDF list) | ~line 195 |
| `tasks.c` | Modify `xTaskIncrementTick` (compare deadlines instead of priorities for preemption) | ~line 4849 |
| `tasks.c` | Disable time-slicing for EDF in `xTaskIncrementTick` | ~line 4880 |
| `tasks.c` | Initialize `xEDFReadyList` in `prvInitialiseTaskLists` | ~line 6090 |
| `tasks.c` | Implement `xTaskCreateEDF()` function | new function |
| `tasks.c` | Implement `vTaskDelayUntilNextPeriod()` function | new function |
| `task.h` | Declare `xTaskCreateEDF` and `vTaskDelayUntilNextPeriod` | near other API declarations |
| `FreeRTOS.h` | Add `configUSE_EDF_SCHEDULING` with default value 0 | near other config defaults |
| `FreeRTOSConfig.h` | Set `configUSE_EDF_SCHEDULING` to 1 | anywhere in the file |
| Demo test file | Create test tasks with known periods/deadlines | new file |

### 5.6 Build, Flash, and Visualize the EDF Test

#### Prerequisites

- **Pico SDK** installed (see `FreeRTOS_on_RPi_Pico.pdf` setup guide)
- **Raspberry Pi Pico H** (or Pico W) board
- **Micro USB cable** for flashing
- **Optional: LEDs + 220Ω resistors** (or a logic analyzer) for visualization

#### Hardware Wiring

| GPIO | Connect to | Purpose |
|------|------------|---------|
| **GPIO 2** | LED + 220Ω resistor → GND | Task A: ON when running, OFF when sleeping |
| **GPIO 3** | LED + 220Ω resistor → GND | Task B: ON when running, OFF when sleeping |

The Pico has built-in LED on pin 25 (PICO_DEFAULT_LED_PIN), but the test uses GPIO 2 and 3 for separate task visibility.

#### Build Steps

1. Open a terminal in the Standard demo directory:
   ```
   cd FreeRTOS\FreeRTOS\Demo\ThirdParty\Community-Supported-Demos\CORTEX_M0+_RP2040\Standard
   ```

2. Create and enter a build directory:
   ```
   mkdir build
   cd build
   ```

3. Run CMake (set `FREERTOS_DEMO_PATH` if needed):
   ```
   cmake ..
   ```

4. Build the EDF test target:
   ```
   cmake --build . --target main_edf_test
   ```

   Or on Windows with Visual Studio: `cmake --build . --config Release --target main_edf_test`

5. The output `.uf2` file will be at:
   ```
   build\main_edf_test\main_edf_test.uf2
   ```

#### Flash to the Pico

1. Hold the **BOOTSEL** button on the Pico.
2. Plug in the USB cable (or release and reconnect while holding BOOTSEL).
3. The Pico appears as a USB drive (e.g., `RPI-RP2`).
4. Drag and drop `main_edf_test.uf2` onto the drive.
5. The Pico reboots and runs the EDF test.

#### How to Visualize (Verify It Works)

**Option A: LEDs (simplest)**

- Connect GPIO 2 and 3 to LEDs (with 220Ω resistors to GND).
- **Task A (GPIO 2):** Blinks every ~500 ms, stays ON for ~100 ms.
- **Task B (GPIO 3):** Blinks every ~1000 ms, stays ON for ~200 ms.
- When both run: **Task A preempts Task B** when A’s deadline is closer (e.g., A runs briefly at 500 ms even if B was running).
- If LEDs blink in this pattern, EDF scheduling is behaving correctly.

**Option B: Logic analyzer**

- Connect probes to GPIO 2 and 3 (and optionally GND).
- Use Saleae Logic, Digilent WaveForms, or a cheap clone.
- Capture at 1 kHz or higher.
- You should see:
  - Task A: ~100 ms HIGH every 500 ms.
  - Task B: ~200 ms HIGH every 1000 ms.
  - Interleaving when A preempts B (e.g., A’s pulse at 500 ms even if B was running).
- Measure exact durations to confirm deadlines are met.

**Option C: Serial output (debug only)**

- Add `printf` in the task loops (e.g., when turning LED on).
- Enable USB serial in `CMakeLists.txt`:
  ```cmake
  pico_enable_stdio_usb(main_edf_test 1)
  pico_enable_stdio_uart(main_edf_test 0)
  ```
- Open a serial terminal (e.g., PuTTY, minicom) at 115200 baud.
- You’ll see periodic messages when each task runs. **Note:** `printf` is slow and can distort timing; use only for debugging.

#### Expected Timeline (approximate)

```
Time (ms):  0    100   200   500  600   700  1000  1100  1200
            |     |     |     |    |     |    |     |     |
Task A:     ██████░░░░░░░░░░░██████░░░░░░░░░██████░░░░░░░░░░
Task B:     ░░░░░░████████████░░░░░░░░░░░░░░░░░░░░░████████████
Idle:       ░░░░░░░░░░░░░░░░░░░░░░██████████░░░░░░░░░░░░░░░░

█ = running    ░ = sleeping/idle

- 0–100 ms:   A runs (deadline 500 ms)
- 100–300 ms: B runs (deadline 1000 ms)
- 300–500 ms: Idle
- 500 ms:     A wakes up, preempts; A runs for ~100 ms
- 600–1000 ms: Idle or B (if B was running)
- 1000 ms:    Both wake; A runs first (deadline 1500 < 2000)
```

---

## 6. Assignment 2: Stack Resource Policy (SRP) - 15%

### 6.1 What Is a Mutex? (Background)

Before we talk about SRP, let's make sure you understand **mutexes** and **why they exist**.

Imagine two tasks both want to write to the same global variable `sharedCounter`:

```c
int sharedCounter = 0;  /* Shared between TaskA and TaskB */

void vTaskA(void *p) {
    for (;;) {
        sharedCounter++;  /* Read, increment, write back */
        vTaskDelay(100);
    }
}

void vTaskB(void *p) {
    for (;;) {
        sharedCounter++;  /* Read, increment, write back */
        vTaskDelay(100);
    }
}
```

This looks innocent, but `sharedCounter++` is actually three steps: (1) read the value, (2) add 1, (3) write the new value back. If FreeRTOS switches tasks between steps 1 and 3, you get data corruption:

```
sharedCounter starts at 5.

TaskA reads 5. (about to add 1...)
  -- TICK! Context switch to TaskB --
TaskB reads 5. Adds 1. Writes 6.
  -- TICK! Context switch back to TaskA --
TaskA adds 1 to its stale value of 5. Writes 6.

sharedCounter is 6, but it should be 7! We lost an increment.
```

A **mutex** (MUTual EXclusion) solves this. It's like a key to a bathroom -- only one person (task) can hold the key at a time:

```c
SemaphoreHandle_t xMutex = xSemaphoreCreateMutex();

void vTaskA(void *p) {
    for (;;) {
        xSemaphoreTake(xMutex, portMAX_DELAY);  /* Lock: "I'm using the counter" */
        sharedCounter++;
        xSemaphoreGive(xMutex);                  /* Unlock: "I'm done" */
        vTaskDelay(100);
    }
}
```

Now if TaskA holds the mutex and TaskB tries to take it, TaskB **blocks** (sleeps) until TaskA gives (releases) the mutex. Only one task can be "inside" the mutex at a time.

### 6.2 What Is Priority Inversion? (The Problem)

Mutexes work great... until you hit **priority inversion**:

```
Three tasks: A (high priority), B (medium), C (low).
C takes the mutex. C is running (A and B are waiting for other things).

  Time → → → → → → → → → → → → → → → → → → →
  Task C (Low):     [==takes mutex==|              |==gives mutex==]
  Task B (Medium):                  [===runs=runs=runs=runs===]
  Task A (High):                    [====BLOCKED on mutex=====|===finally runs===]

What happened:
1. C takes the mutex. C runs.
2. A wakes up and wants the mutex. A can't get it (C has it). A blocks.
3. B wakes up. B has higher priority than C, so B PREEMPTS C.
4. B runs for a long time. C can't run (B is higher priority).
5. C can't release the mutex because C isn't running.
6. A is stuck waiting for C to release the mutex.
7. A (the HIGHEST priority task!) is effectively blocked by B (MEDIUM priority).
   This is WRONG. A should never be blocked by B.
```

This is called **unbounded priority inversion** because B could run for any amount of time, and A would be stuck the entire time. This actually caused a real problem on the Mars Pathfinder mission in 1997!

**FreeRTOS's solution: Priority Inheritance Protocol (PIP)**

When A blocks on a mutex held by C, FreeRTOS temporarily raises C's priority to match A's. So B can no longer preempt C. C finishes its critical section quickly, releases the mutex, drops back to its normal priority, and A runs.

**PIP limitations:**
- Doesn't prevent **deadlocks** (A holds Mutex1, wants Mutex2. B holds Mutex2, wants Mutex1. Both stuck forever.)
- **Chained blocking:** A task might be blocked multiple times by different lower-priority tasks, each holding different mutexes.

### 6.3 What Is SRP?

**Stack Resource Policy (SRP)** is a smarter protocol. Its key insight is: **don't let a task start running at all if it might get blocked later.**

Instead of letting a task run and then blocking it when it tries to take a locked mutex (like PIP does), SRP checks *before* the task even gets the CPU: "If this task were to run, could it possibly get blocked by any currently held mutex?" If yes, don't let it run yet.

This has amazing properties:
1. **No deadlocks** -- ever. A task only starts running if it can access all the resources it might need.
2. **At most one block** per task instance -- the blocking happens once, before the task starts, not multiple times during execution.
3. **Stack sharing** -- since a task can't be blocked mid-execution, tasks can share a single stack (they never overlap on the stack).

#### SRP Concepts

**Preemption Level (pi):** A number assigned to each task. Think of it as "how powerful is this task at interrupting others." Higher = more powerful.

- For fixed-priority scheduling: preemption level = priority (they're the same thing).
- For EDF: preemption level = inversely proportional to relative deadline. A task with a shorter deadline has a higher preemption level (it's more urgent, so it can preempt more things).

```
Example for EDF:
  Task A: relative deadline = 100  →  preemption level = 10 (most urgent)
  Task B: relative deadline = 200  →  preemption level = 5
  Task C: relative deadline = 500  →  preemption level = 2  (least urgent)

(The exact numbers don't matter, just the ordering. You could use
 preemption_level = MAX_DEADLINE / relative_deadline, or simply
 assign levels manually.)
```

**Resource Ceiling:** Each mutex gets a "ceiling" number. The ceiling = the **highest preemption level** of any task that uses that mutex. You calculate this at design time (you know which tasks use which mutexes).

```
Example:
  Task A (preemption level 10) uses Mutex1
  Task B (preemption level 5)  uses Mutex1 and Mutex2
  Task C (preemption level 2)  uses Mutex2

  Mutex1 ceiling = max(10, 5) = 10   (A and B use it, A has highest level)
  Mutex2 ceiling = max(5, 2)  = 5    (B and C use it, B has highest level)
```

**System Ceiling (Omega):** At any moment, this is the **maximum ceiling of all currently locked mutexes**. If no mutexes are locked, system ceiling = 0.

```
Example:
  No mutexes locked:     system ceiling = 0
  Only Mutex2 locked:    system ceiling = 5  (Mutex2's ceiling)
  Both mutexes locked:   system ceiling = 10 (max of 10, 5)
  Only Mutex1 locked:    system ceiling = 10
```

**The SRP Rule:** A task is allowed to **preempt** (start running by interrupting the current task) only if its preemption level is **strictly greater than** the current system ceiling.

```
Example:
  C is running. C locks Mutex2.
  System ceiling = 5 (Mutex2's ceiling).

  B wakes up (preemption level 5). Can B preempt?
    B's level (5) > system ceiling (5)?  NO (not strictly greater).
    B is BLOCKED. B cannot run.

  A wakes up (preemption level 10). Can A preempt?
    A's level (10) > system ceiling (5)?  YES.
    A PREEMPTS C and runs.
    A can safely take Mutex1 because no one else has it.

  Why was B blocked? Because if B had run, B would have tried to take
  Mutex2 (which C holds). That would cause a deadlock. SRP prevents
  this by not letting B run in the first place!
```

#### SRP Full Walkthrough

```
Tasks:
  A (preemption level 10): uses Mutex1
  B (preemption level 5):  uses Mutex1, Mutex2
  C (preemption level 2):  uses Mutex2

Mutex1 ceiling = 10, Mutex2 ceiling = 5

Time 0: C runs. System ceiling = 0.
Time 1: C takes Mutex2. System ceiling = max(0, 5) = 5.
Time 2: B wakes up. B's level (5) > system ceiling (5)? NO. B waits.
Time 3: A wakes up. A's level (10) > system ceiling (5)? YES. A preempts C.
Time 4: A takes Mutex1. System ceiling = max(5, 10) = 10.
Time 5: A does its work with Mutex1.
Time 6: A gives Mutex1. System ceiling = max(5) = 5. (only Mutex2 still held)
Time 7: A finishes and sleeps.
Time 8: B's level (5) > system ceiling (5)? Still NO. B still waits.
Time 9: C resumes (no one else can run). C finishes with Mutex2.
Time 10: C gives Mutex2. System ceiling = 0.
Time 11: B's level (5) > system ceiling (0)? YES. B runs.
Time 12: B takes Mutex2 (it's free!). Takes Mutex1 (it's free!).
          SRP guarantees the resources are free when B gets to run.
```

Notice: **B was blocked only ONCE** (before it even started). No deadlocks. No chained blocking.

#### Important: Binary Semaphores, Not Mutexes

The instructor specifies: _"extend the semaphore implementation of FreeRTOS to use SRP. You only need to extend FreeRTOS with SRP for **binary semaphores**."_

In FreeRTOS, binary semaphores (`xSemaphoreCreateBinary`) are simpler than mutexes (`xSemaphoreCreateMutex`):
- **Mutexes** have ownership tracking and Priority Inheritance Protocol (PIP).
- **Binary semaphores** have no ownership and no PIP.

Since SRP **replaces** PIP entirely (SRP prevents priority inversion by blocking tasks before they run, instead of boosting priorities after the fact), the simpler binary semaphore is the right foundation. We add a ceiling to each binary semaphore instead of relying on priority inheritance.

Both binary semaphores and mutexes are implemented using `Queue_t` internally, so the code changes to `queue.c` are similar -- we just create with `xQueueGenericCreate` (binary semaphore) instead of `xQueueCreateMutex`.

#### Run-time Stack Sharing (Required by Instructor)

The instructor explicitly requires: _"In your implementation of SRP, provide support for sharing the run-time stack among the running tasks."_

**Why this works under SRP:** Under SRP, if two tasks have the **same preemption level**, they can never execute simultaneously. SRP enforces that a task can only start running if its preemption level is _strictly greater than_ the system ceiling. Two tasks at the same level can never preempt each other. Therefore, they never need stack space at the same time, and can **share the same physical stack memory**.

```
Without stack sharing (normal FreeRTOS):
  Task X (level 5, stack 512 words): [====512 words====]
  Task Y (level 5, stack 256 words): [==256 words==]
  Task Z (level 5, stack 384 words): [===384 words===]
  Total allocated: 512 + 256 + 384 = 1152 words

With stack sharing (SRP):
  Tasks X, Y, Z all share ONE stack sized to max(512, 256, 384) = 512 words:
  Shared stack (level 5):            [====512 words====]
  Total allocated: 512 words
  Savings: 640 words (55.6%)
```

The savings grow as more tasks share the same preemption level. With 100 tasks at the same level, you only allocate ONE stack instead of 100.

**Implementation approach:** Use `xTaskCreateStatic` (which lets you supply a pre-allocated stack buffer) instead of `xTaskCreate` (which allocates a private stack per task). A shared stack table maps preemption levels to shared stack buffers. When creating a task, look up whether a shared stack already exists for its preemption level. If yes, reuse it (growing it if needed). If no, allocate a new one.

**Constraint:** Task functions sharing a stack must not rely on local variables persisting across periods. Use global/static variables or TCB fields for state that spans periods (e.g., `xLastWakeTime` should be stored in the TCB rather than as a local). The implementation below stores it in a new TCB field `xSavedWakeTime`.

**Quantitative study:** The instructor requires: _"carry out a quantitative study with stack sharing vs. no stack sharing. Report the gains in terms of the maximum run-time stack storage used. It is sufficient to run 100 tasks simultaneously."_ Section Step 10 below provides a reporting function for this.

### 6.4 Files to Modify

| File | What to Change | Why |
|------|---------------|-----|
| `FreeRTOS/Source/tasks.c` | Add preemption level to TCB, add system ceiling global, modify preemption checks | Core scheduler changes |
| `FreeRTOS/Source/tasks.c` | Add SRP resource registry (`SRPResourceEntry_t`), dynamic ceiling computation, resource API (`xSRPResourceCreate`, `xSRPResourceTake`, `vSRPResourceGive`, `vSRPResourceRegisterUser`) | Counting resources with dynamic ceilings |
| `FreeRTOS/Source/tasks.c` | Add shared stack table, `prvGetOrCreateSharedStack`, `vSRPReportStackUsage`, `xTaskCreateEDFWithSharedStack` | Run-time stack sharing |
| `FreeRTOS/Source/tasks.c` | Add admission control function with blocking times | EDF+SRP schedulability |
| `FreeRTOS/Source/include/task.h` | Declare SRP resource API, preemption level API, shared-stack creation, stack usage report | User-facing API |
| `FreeRTOS/Source/include/FreeRTOS.h` | Config macros `configUSE_SRP`, `configSRP_STACK_SHARING`, `configMAX_SRP_RESOURCES`, `configMAX_RESOURCE_USERS` | On/off switches and sizing |
| `Demo/.../FreeRTOSConfig.h` | Enable SRP and stack sharing | Turn them on |

### 6.5 Step-by-Step Implementation

#### Step 1: Add Configuration

**`FreeRTOSConfig.h`:**
```c
#define configUSE_SRP              1    /* Enable Stack Resource Policy */
#define configSRP_STACK_SHARING    1    /* Enable run-time stack sharing for SRP */
```

**`FreeRTOS.h`:**
```c
#ifndef configUSE_SRP
    #define configUSE_SRP    0
#endif

#ifndef configSRP_STACK_SHARING
    #define configSRP_STACK_SHARING    0
#endif
```

#### Step 2: Add Preemption Level to TCB

**File:** `tasks.c`, inside the TCB struct:
```c
#if ( configUSE_SRP == 1 )
    /* The task's preemption level. Higher = more powerful (can preempt
     * more things). For EDF: inversely proportional to relative deadline.
     * For fixed-priority: same as uxPriority. Set during task creation. */
    UBaseType_t uxPreemptionLevel;
#endif
```

#### Step 3: Add System Ceiling and Resource Registry

**How dynamic ceilings work:** Under the generalized SRP, each resource can have **multiple units** (a counting resource). The resource ceiling is NOT fixed at creation time -- it changes dynamically based on how many units are currently available.

```
Example:
  Resource R1 has 3 total units.
  Task 3 (preemption level 10) needs 1 unit of R1.
  Task 2 (preemption level 5)  needs 2 units of R1.
  Task 1 (preemption level 2)  needs 1 unit of R1.

  Time 0: R1 has 3 units available. Ceiling = 0.
    - Task 3 needs 1, 3 available >= 1 → NOT blocked.
    - Task 2 needs 2, 3 available >= 2 → NOT blocked.
    - Task 1 needs 1, 3 available >= 1 → NOT blocked.
    - No one would be blocked → ceiling = 0.

  Task 2 takes 2 units. R1 now has 3 - 2 = 1 unit available.
  Recompute ceiling:
    - Task 3 needs 1, 1 available >= 1 → NOT blocked.
    - Task 2 needs 2, 1 available <  2 → WOULD be blocked. Level = 5.
    - Task 1 needs 1, 1 available >= 1 → NOT blocked.
    - Ceiling = max preemption level of blocked tasks = 5.

  System ceiling = max ceiling of all resources = 5.
  Task 3 (level 10 > 5) CAN preempt. ✓
  Task 1 (level 2  ≤ 5) CANNOT preempt. ✓ (If Task 1 ran, it could
    take 1 unit, leaving 0. Then Task 3 would be blocked. SRP prevents this.)
```

The **rule**: ceiling(R) = max preemption level among all tasks whose unit requirement **exceeds** the currently available units of R. If no task would be blocked, ceiling = 0.

**File:** `tasks.c`, with other global variables:
```c
#if ( configUSE_SRP == 1 )

    /* The system ceiling = max dynamic ceiling across all resources.
     * A task can only preempt if its preemption level > uxSystemCeiling. */
    PRIVILEGED_DATA static volatile UBaseType_t uxSystemCeiling = 0;

    /*----------------------------------------------------------
     * Resource Registry -- tracks all SRP-managed resources
     *----------------------------------------------------------*/

    /* Max number of distinct SRP resources in the system. */
    #ifndef configMAX_SRP_RESOURCES
        #define configMAX_SRP_RESOURCES    16
    #endif

    /* Max number of tasks that can be registered as users of one resource. */
    #ifndef configMAX_RESOURCE_USERS
        #define configMAX_RESOURCE_USERS   16
    #endif

    /* One entry per task that uses a resource. */
    typedef struct
    {
        UBaseType_t uxPreemptionLevel;  /* The task's preemption level */
        UBaseType_t uxUnitsNeeded;      /* How many units this task needs */
    } SRPResourceUser_t;

    /* One entry per SRP resource. */
    typedef struct
    {
        SemaphoreHandle_t xHandle;      /* The underlying FreeRTOS semaphore */
        UBaseType_t uxTotalUnits;       /* Total units of this resource (n) */
        volatile UBaseType_t uxAvailableUnits; /* Units currently free */
        SRPResourceUser_t xUsers[ configMAX_RESOURCE_USERS ];
        UBaseType_t uxUserCount;        /* Number of registered users */
        BaseType_t xInUse;              /* pdTRUE if this slot is active */
    } SRPResourceEntry_t;

    PRIVILEGED_DATA static SRPResourceEntry_t
        xSRPResources[ configMAX_SRP_RESOURCES ] = { { 0 } };

    /*----------------------------------------------------------
     * Ceiling Computation
     *----------------------------------------------------------*/

    /* Compute the current dynamic ceiling of one resource.
     * = max preemption level among tasks whose unit requirement
     *   EXCEEDS the currently available units. */
    static UBaseType_t prvComputeResourceCeiling(
        const SRPResourceEntry_t * pxRes )
    {
        UBaseType_t uxCeiling = 0;
        UBaseType_t i;

        for( i = 0; i < pxRes->uxUserCount; i++ )
        {
            if( pxRes->xUsers[ i ].uxUnitsNeeded > pxRes->uxAvailableUnits )
            {
                /* This task WOULD be blocked if it tried to acquire. */
                if( pxRes->xUsers[ i ].uxPreemptionLevel > uxCeiling )
                {
                    uxCeiling = pxRes->xUsers[ i ].uxPreemptionLevel;
                }
            }
        }

        return uxCeiling;
    }

    /* Recompute the system ceiling from scratch.
     * System ceiling = max ceiling across ALL resources. */
    static void prvRecalculateSystemCeiling( void )
    {
        UBaseType_t uxNewCeiling = 0;
        UBaseType_t r;

        for( r = 0; r < configMAX_SRP_RESOURCES; r++ )
        {
            if( xSRPResources[ r ].xInUse == pdTRUE )
            {
                UBaseType_t uxResCeiling =
                    prvComputeResourceCeiling( &xSRPResources[ r ] );

                if( uxResCeiling > uxNewCeiling )
                {
                    uxNewCeiling = uxResCeiling;
                }
            }
        }

        uxSystemCeiling = uxNewCeiling;
    }

    /* Look up a resource entry by its semaphore handle.
     * Returns NULL if the handle is not registered. */
    static SRPResourceEntry_t * prvFindResource( SemaphoreHandle_t xHandle )
    {
        UBaseType_t r;

        for( r = 0; r < configMAX_SRP_RESOURCES; r++ )
        {
            if( ( xSRPResources[ r ].xInUse == pdTRUE ) &&
                ( xSRPResources[ r ].xHandle == xHandle ) )
            {
                return &xSRPResources[ r ];
            }
        }

        return NULL;
    }

#endif /* configUSE_SRP */
```

#### Step 4: SRP Resource API (Create, Register, Take, Give)

Instead of modifying the `Queue_t` struct directly, we maintain a **separate resource registry** that sits alongside FreeRTOS's counting semaphores. This avoids touching internal FreeRTOS data structures and keeps SRP logic in `tasks.c`.

**File:** `task.h` (or a new `srp.h`):
```c
#if ( configUSE_SRP == 1 )

    /* Create an SRP-managed counting resource.
     *
     * uxMaxUnits: total number of units (e.g., 3 means 3 identical copies
     *             of this resource exist). All units start available.
     *
     * Internally creates a FreeRTOS counting semaphore and registers
     * it in the SRP resource table.
     *
     * Returns: a SemaphoreHandle_t you use with xSRPResourceTake/Give. */
    SemaphoreHandle_t xSRPResourceCreate( UBaseType_t uxMaxUnits );

    /* Register a task as a user of an SRP resource. Call this BEFORE
     * the scheduler starts, for every (task, resource) pair.
     *
     * xResource:         the handle returned by xSRPResourceCreate.
     * uxPreemptionLevel: the task's preemption level.
     * uxUnitsNeeded:     how many units this task acquires at once.
     *
     * This information is used to compute the dynamic ceiling. */
    void vSRPResourceRegisterUser( SemaphoreHandle_t xResource,
                                   UBaseType_t uxPreemptionLevel,
                                   UBaseType_t uxUnitsNeeded );

    /* Take (acquire) uxUnits units from the SRP resource.
     * Under SRP, this should NEVER block -- SRP guarantees the units
     * are available when a task is allowed to run.
     * Updates available count and recomputes system ceiling. */
    BaseType_t xSRPResourceTake( SemaphoreHandle_t xResource,
                                 UBaseType_t uxUnits );

    /* Give (release) uxUnits units back to the SRP resource.
     * Updates available count and recomputes system ceiling. */
    void vSRPResourceGive( SemaphoreHandle_t xResource,
                           UBaseType_t uxUnits );

#endif
```

**File:** `tasks.c` -- implement:
```c
#if ( configUSE_SRP == 1 )

SemaphoreHandle_t xSRPResourceCreate( UBaseType_t uxMaxUnits )
{
    UBaseType_t r;
    SRPResourceEntry_t * pxEntry = NULL;

    /* Find a free slot in the resource table. */
    for( r = 0; r < configMAX_SRP_RESOURCES; r++ )
    {
        if( xSRPResources[ r ].xInUse == pdFALSE )
        {
            pxEntry = &xSRPResources[ r ];
            break;
        }
    }
    configASSERT( pxEntry != NULL ); /* Out of resource slots */

    /* Create a FreeRTOS counting semaphore with uxMaxUnits.
     * Initial count = uxMaxUnits (all units start available). */
    SemaphoreHandle_t xSem = xSemaphoreCreateCounting( uxMaxUnits, uxMaxUnits );
    configASSERT( xSem != NULL );

    pxEntry->xHandle = xSem;
    pxEntry->uxTotalUnits = uxMaxUnits;
    pxEntry->uxAvailableUnits = uxMaxUnits;
    pxEntry->uxUserCount = 0;
    pxEntry->xInUse = pdTRUE;

    return xSem;
}

void vSRPResourceRegisterUser( SemaphoreHandle_t xResource,
                               UBaseType_t uxPreemptionLevel,
                               UBaseType_t uxUnitsNeeded )
{
    SRPResourceEntry_t * pxRes = prvFindResource( xResource );
    configASSERT( pxRes != NULL );
    configASSERT( pxRes->uxUserCount < configMAX_RESOURCE_USERS );
    configASSERT( uxUnitsNeeded <= pxRes->uxTotalUnits );

    pxRes->xUsers[ pxRes->uxUserCount ].uxPreemptionLevel = uxPreemptionLevel;
    pxRes->xUsers[ pxRes->uxUserCount ].uxUnitsNeeded = uxUnitsNeeded;
    pxRes->uxUserCount++;

    /* Recompute ceiling now that a new user is registered.
     * (Before any units are taken the ceiling is 0 because
     *  all units are available and nobody would be blocked.
     *  But it doesn't hurt to recompute.) */
    prvRecalculateSystemCeiling();
}

BaseType_t xSRPResourceTake( SemaphoreHandle_t xResource,
                             UBaseType_t uxUnits )
{
    SRPResourceEntry_t * pxRes = prvFindResource( xResource );
    configASSERT( pxRes != NULL );
    configASSERT( uxUnits <= pxRes->uxAvailableUnits );

    taskENTER_CRITICAL();
    {
        /* Take uxUnits from the underlying counting semaphore.
         * Each call to xSemaphoreTake takes 1 unit, so loop. */
        UBaseType_t i;
        for( i = 0; i < uxUnits; i++ )
        {
            BaseType_t xTaken = xSemaphoreTake( xResource, 0 );
            configASSERT( xTaken == pdTRUE ); /* SRP guarantees no blocking */
        }

        /* Update available count. */
        pxRes->uxAvailableUnits -= uxUnits;

        /* Recompute the system ceiling. Fewer available units means
         * more tasks could potentially be blocked → ceiling may rise. */
        prvRecalculateSystemCeiling();
    }
    taskEXIT_CRITICAL();

    return pdTRUE;
}

void vSRPResourceGive( SemaphoreHandle_t xResource,
                       UBaseType_t uxUnits )
{
    SRPResourceEntry_t * pxRes = prvFindResource( xResource );
    configASSERT( pxRes != NULL );
    configASSERT( pxRes->uxAvailableUnits + uxUnits <= pxRes->uxTotalUnits );

    taskENTER_CRITICAL();
    {
        /* Give uxUnits back to the underlying counting semaphore. */
        UBaseType_t i;
        for( i = 0; i < uxUnits; i++ )
        {
            xSemaphoreGive( xResource );
        }

        /* Update available count. */
        pxRes->uxAvailableUnits += uxUnits;

        /* Recompute the system ceiling. More available units means
         * fewer tasks would be blocked → ceiling may drop. */
        prvRecalculateSystemCeiling();
    }
    taskEXIT_CRITICAL();
}

#endif /* configUSE_SRP */
```

**Key SRP insight:** Under SRP, a task is only allowed to start running if its preemption level is above the system ceiling. This guarantees that by the time it tries to acquire units, enough units are **always available**. So `xSRPResourceTake` should **never fail** -- if it does, something is wrong with the user registrations.

#### Step 5: Dynamic Ceiling Walkthrough

To make sure the dynamic ceiling is clear, here's a full walkthrough:

```
Setup:
  Resource R1: 3 total units.
  Task 3 (level 10): needs 1 unit of R1.
  Task 2 (level 5):  needs 2 units of R1.
  Task 1 (level 2):  needs 1 unit of R1.

Registration (before scheduler starts):
  vSRPResourceRegisterUser( R1, 10, 1 );   /* Task 3 */
  vSRPResourceRegisterUser( R1, 5,  2 );   /* Task 2 */
  vSRPResourceRegisterUser( R1, 2,  1 );   /* Task 1 */

Time 0: All 3 units available. Ceiling computation:
  - Task 3 needs 1, 3 >= 1 → not blocked.
  - Task 2 needs 2, 3 >= 2 → not blocked.
  - Task 1 needs 1, 3 >= 1 → not blocked.
  → ceiling(R1) = 0. System ceiling = 0.
  All tasks can preempt freely.

Time 1: Task 1 (level 2) runs and takes 1 unit.
  Available = 3 - 1 = 2. Recompute ceiling:
  - Task 3 needs 1, 2 >= 1 → not blocked.
  - Task 2 needs 2, 2 >= 2 → not blocked.
  - Task 1 needs 1, 2 >= 1 → not blocked.
  → ceiling(R1) = 0. System ceiling = 0.

Time 2: Task 2 (level 5) preempts, takes 2 units.
  Available = 2 - 2 = 0. Recompute ceiling:
  - Task 3 needs 1, 0 < 1 → BLOCKED. Level 10.
  - Task 2 needs 2, 0 < 2 → BLOCKED. Level 5.
  - Task 1 needs 1, 0 < 1 → BLOCKED. Level 2.
  → ceiling(R1) = max(10, 5, 2) = 10. System ceiling = 10.
  Task 3 (level 10) CANNOT preempt (10 > 10 is false).

Time 3: Task 2 gives back 2 units.
  Available = 0 + 2 = 2. Recompute ceiling:
  - Task 3 needs 1, 2 >= 1 → not blocked.
  - Task 2 needs 2, 2 >= 2 → not blocked.
  - Task 1 needs 1, 2 >= 1 → not blocked.
  → ceiling(R1) = 0. System ceiling = 0.
  Task 3 CAN preempt now.

Time 4: Task 1 gives back 1 unit. Available = 3.
  Ceiling unchanged (already 0).
```

The old "ceiling stack" approach is replaced by `prvRecalculateSystemCeiling()` which iterates all resources and computes the ceiling from their current state. This is correct for counting resources where the ceiling changes with every take/give.

#### Step 6: Modify the Preemption Check

**File:** `tasks.c` -- wherever preemption is decided

In `xTaskIncrementTick`, when a task wakes up from the delayed list and would normally preempt the current task, add the SRP gate:

```c
#if ( configUSE_SRP == 1 )
    /* SRP check: the woken task can only preempt if its preemption
     * level is strictly GREATER than the system ceiling.
     * This prevents the task from running if it might need a
     * resource that's currently held by someone else. */
    if( pxTCB->uxPreemptionLevel > uxSystemCeiling )
    {
        /* SRP allows preemption. Now also check the normal condition. */
        #if ( configUSE_EDF_SCHEDULING == 1 )
            if( pxTCB->xPeriod > 0 )
            {
                /* The woken task is an EDF task. It should preempt if:
                 * 1. The current task is NOT an EDF task (e.g. idle), OR
                 * 2. The current task IS EDF but has a later deadline. */
                if( ( pxCurrentTCB->xPeriod == 0 ) ||
                    ( pxTCB->xAbsoluteDeadline < pxCurrentTCB->xAbsoluteDeadline ) )
                {
                    xSwitchRequired = pdTRUE;
                }
            }
        #else
            if( pxTCB->uxPriority > pxCurrentTCB->uxPriority )
            {
                xSwitchRequired = pdTRUE;
            }
        #endif
    }
    /* else: SRP blocks this task, even if it has a higher priority
     * or earlier deadline. It will run later when the system ceiling drops. */
#else
    /* No SRP -- use standard or EDF preemption check. */
    #if ( configUSE_EDF_SCHEDULING == 1 )
        if( pxTCB->xPeriod > 0 )
        {
            if( ( pxCurrentTCB->xPeriod == 0 ) ||
                ( pxTCB->xAbsoluteDeadline < pxCurrentTCB->xAbsoluteDeadline ) )
            {
                xSwitchRequired = pdTRUE;
            }
        }
    #else
        if( pxTCB->uxPriority > pxCurrentTCB->uxPriority )
        {
            xSwitchRequired = pdTRUE;
        }
    #endif
#endif
```

**Important note about the idle task:** When comparing deadlines (`xAbsoluteDeadline`), always check whether the current task is actually an EDF task (`xPeriod > 0`). The idle task has `xPeriod == 0` and `xAbsoluteDeadline == 0` (never initialized). Without this check, any EDF task's deadline (e.g., 1000) would appear "later" than idle's deadline (0), and EDF tasks would never preempt idle after waking from a delay. This was a bug we caught during EDF testing.

Also modify `taskSELECT_HIGHEST_PRIORITY_TASK` to skip tasks whose preemption level is not above the system ceiling:

```c
#if ( configUSE_EDF_SCHEDULING == 1 ) && ( configUSE_SRP == 1 )
    #define taskSELECT_HIGHEST_PRIORITY_TASK()                                    \
    do {                                                                          \
        /* Walk the EDF list from the head (earliest deadline).                   \
         * Skip any task whose preemption level <= system ceiling. */              \
        ListItem_t * pxIterator = listGET_HEAD_ENTRY( &xEDFReadyList );           \
        ListItem_t const * pxEnd = listGET_END_MARKER( &xEDFReadyList );          \
        pxCurrentTCB = NULL;                                                      \
        while( pxIterator != ( ListItem_t * ) pxEnd )                             \
        {                                                                         \
            TCB_t * pxCandidate = ( TCB_t * ) listGET_LIST_ITEM_OWNER( pxIterator );\
            if( pxCandidate->uxPreemptionLevel > uxSystemCeiling )                \
            {                                                                     \
                pxCurrentTCB = pxCandidate;                                       \
                break;                                                            \
            }                                                                     \
            pxIterator = listGET_NEXT( pxIterator );                              \
        }                                                                         \
        if( pxCurrentTCB == NULL )                                                \
        {                                                                         \
            /* No EDF task can run (all blocked by SRP). Run idle. */              \
            pxCurrentTCB = ( TCB_t * ) listGET_OWNER_OF_HEAD_ENTRY(               \
                               &( pxReadyTasksLists[ tskIDLE_PRIORITY ] ) );      \
        }                                                                         \
    } while( 0 )
#endif
```

#### Step 7: API for Setting Preemption Level

**File:** `task.h`:
```c
#if ( configUSE_SRP == 1 )
    /* Set a task's SRP preemption level. Call after xTaskCreate/xTaskCreateEDF.
     * xTask: the task handle (or NULL for the calling task).
     * uxLevel: the preemption level (higher = can preempt more). */
    void vTaskSetPreemptionLevel( TaskHandle_t xTask, UBaseType_t uxLevel );

    /* Get a task's SRP preemption level. */
    UBaseType_t uxTaskGetPreemptionLevel( TaskHandle_t xTask );
#endif
```

**File:** `tasks.c`:
```c
#if ( configUSE_SRP == 1 )
void vTaskSetPreemptionLevel( TaskHandle_t xTask, UBaseType_t uxLevel )
{
    /* If xTask is NULL, use the currently running task. */
    TCB_t * pxTCB = prvGetTCBFromHandle( xTask );

    taskENTER_CRITICAL();
    {
        pxTCB->uxPreemptionLevel = uxLevel;
    }
    taskEXIT_CRITICAL();
}

UBaseType_t uxTaskGetPreemptionLevel( TaskHandle_t xTask )
{
    TCB_t * pxTCB = prvGetTCBFromHandle( xTask );
    return pxTCB->uxPreemptionLevel;
}
#endif
```

#### Step 8: Run-time Stack Sharing Implementation

**File:** `tasks.c` -- add the shared stack infrastructure with the other globals:

```c
#if ( configUSE_SRP == 1 ) && ( configSRP_STACK_SHARING == 1 )

    /* Maximum number of distinct preemption levels that use stack sharing. */
    #ifndef configMAX_PREEMPTION_LEVELS
        #define configMAX_PREEMPTION_LEVELS    32
    #endif

    /* Each entry tracks a shared stack buffer for one preemption level. */
    typedef struct
    {
        UBaseType_t uxPreemptionLevel;   /* Which preemption level */
        StackType_t * pxSharedStack;     /* The shared stack buffer */
        configSTACK_DEPTH_TYPE uxMaxStackDepth; /* Sized to the largest task */
        UBaseType_t uxTaskCount;         /* How many tasks share this stack */
        BaseType_t xInUse;               /* pdTRUE if this slot is taken */
    } SharedStackEntry_t;

    PRIVILEGED_DATA static SharedStackEntry_t
        xSharedStackTable[ configMAX_PREEMPTION_LEVELS ] = { 0 };

    /* Also track the total "without sharing" allocation for reporting. */
    PRIVILEGED_DATA static size_t xTotalStackWithoutSharing = 0;

    /* Look up or create a shared stack for the given preemption level.
     * If one already exists but is too small, it is reallocated to
     * the larger size. MUST be called before the scheduler starts
     * (i.e., during task creation). */
    static StackType_t * prvGetOrCreateSharedStack(
        UBaseType_t uxPreemptionLevel,
        configSTACK_DEPTH_TYPE uxStackDepth )
    {
        UBaseType_t i;
        SharedStackEntry_t * pxEntry = NULL;

        /* Track what we WOULD allocate without sharing. */
        xTotalStackWithoutSharing += ( size_t ) uxStackDepth * sizeof( StackType_t );

        /* Search for an existing entry at this preemption level. */
        for( i = 0; i < configMAX_PREEMPTION_LEVELS; i++ )
        {
            if( ( xSharedStackTable[ i ].xInUse == pdTRUE ) &&
                ( xSharedStackTable[ i ].uxPreemptionLevel == uxPreemptionLevel ) )
            {
                pxEntry = &xSharedStackTable[ i ];
                break;
            }
        }

        if( pxEntry != NULL )
        {
            /* Shared stack exists. Grow it if this task needs more space. */
            if( uxStackDepth > pxEntry->uxMaxStackDepth )
            {
                vPortFree( pxEntry->pxSharedStack );
                pxEntry->pxSharedStack = ( StackType_t * ) pvPortMalloc(
                    ( size_t ) uxStackDepth * sizeof( StackType_t ) );
                configASSERT( pxEntry->pxSharedStack != NULL );
                pxEntry->uxMaxStackDepth = uxStackDepth;
            }
            pxEntry->uxTaskCount++;
            return pxEntry->pxSharedStack;
        }

        /* No entry yet -- find a free slot. */
        for( i = 0; i < configMAX_PREEMPTION_LEVELS; i++ )
        {
            if( xSharedStackTable[ i ].xInUse == pdFALSE )
            {
                pxEntry = &xSharedStackTable[ i ];
                break;
            }
        }
        configASSERT( pxEntry != NULL );

        pxEntry->xInUse = pdTRUE;
        pxEntry->uxPreemptionLevel = uxPreemptionLevel;
        pxEntry->uxMaxStackDepth = uxStackDepth;
        pxEntry->uxTaskCount = 1;
        pxEntry->pxSharedStack = ( StackType_t * ) pvPortMalloc(
            ( size_t ) uxStackDepth * sizeof( StackType_t ) );
        configASSERT( pxEntry->pxSharedStack != NULL );
        return pxEntry->pxSharedStack;
    }
#endif
```

**File:** `tasks.c` -- add a task creation function that uses shared stacks:

```c
#if ( configUSE_EDF_SCHEDULING == 1 ) && ( configUSE_SRP == 1 ) && ( configSRP_STACK_SHARING == 1 )

    BaseType_t xTaskCreateEDFWithSharedStack(
        TaskFunction_t pxTaskCode,
        const char * const pcName,
        const configSTACK_DEPTH_TYPE uxStackDepth,
        void * const pvParameters,
        TickType_t xPeriod,
        TickType_t xRelativeDeadline,
        UBaseType_t uxPreemptionLevel,
        TaskHandle_t * const pxCreatedTask )
    {
        /* Look up or create the shared stack for this preemption level. */
        StackType_t * pxSharedStack = prvGetOrCreateSharedStack(
            uxPreemptionLevel, uxStackDepth );

        /* We need a StaticTask_t for each task (the TCB is always private,
         * only the STACK memory is shared). Allocate dynamically. */
        StaticTask_t * pxTaskBuffer = ( StaticTask_t * ) pvPortMalloc(
            sizeof( StaticTask_t ) );
        configASSERT( pxTaskBuffer != NULL );

        /* Create the task using the static API, providing the shared stack. */
        TaskHandle_t xHandle = xTaskCreateStatic(
            pxTaskCode,
            pcName,
            uxStackDepth,
            pvParameters,
            tskIDLE_PRIORITY + 1,   /* Priority doesn't matter for EDF */
            pxSharedStack,          /* <-- SHARED stack buffer */
            pxTaskBuffer );

        if( xHandle != NULL )
        {
            /* Set EDF fields. */
            TCB_t * pxTCB = ( TCB_t * ) xHandle;
            pxTCB->xPeriod = xPeriod;
            pxTCB->xRelativeDeadline = xRelativeDeadline;
            pxTCB->xAbsoluteDeadline = xRelativeDeadline;

            /* Set SRP preemption level. */
            pxTCB->uxPreemptionLevel = uxPreemptionLevel;

            if( pxCreatedTask != NULL )
            {
                *pxCreatedTask = xHandle;
            }
            return pdPASS;
        }

        return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
    }

#endif
```

**Important constraint:** Because tasks at the same preemption level share a physical stack, their local variables can be overwritten by each other between periods. Task functions should store any state that must persist across periods in:
- **Global/static variables**, or
- **TCB fields** (e.g., a new `xSavedWakeTime` field for `vTaskDelayUntilNextPeriod`)

For the periodic wake time, add a field to the TCB:
```c
#if ( configSRP_STACK_SHARING == 1 )
    TickType_t xSavedWakeTime;
#endif
```

And modify `vTaskDelayUntilNextPeriod` to use it when stack sharing is enabled:
```c
#if ( configSRP_STACK_SHARING == 1 )
void vTaskDelayUntilNextPeriod( void )
{
    TCB_t * pxTCBLocal = pxCurrentTCB;
    pxTCBLocal->xAbsoluteDeadline += pxTCBLocal->xPeriod;

    /* Use TCB-stored wake time instead of a local variable. */
    TickType_t xWake = pxTCBLocal->xSavedWakeTime;
    vTaskDelayUntil( &xWake, pxTCBLocal->xPeriod );
    pxTCBLocal->xSavedWakeTime = xWake;
}
#endif
```

**File:** `task.h` -- declare:
```c
#if ( configUSE_EDF_SCHEDULING == 1 ) && ( configUSE_SRP == 1 ) && ( configSRP_STACK_SHARING == 1 )
    BaseType_t xTaskCreateEDFWithSharedStack(
        TaskFunction_t pxTaskCode,
        const char * const pcName,
        const configSTACK_DEPTH_TYPE uxStackDepth,
        void * const pvParameters,
        TickType_t xPeriod,
        TickType_t xRelativeDeadline,
        UBaseType_t uxPreemptionLevel,
        TaskHandle_t * const pxCreatedTask
    );
#endif
```

#### Step 9: Admission Control with Blocking Times (EDF + SRP)

The instructor requires: _"extend the admission control tests to include blocking times and test schedulability using EDF + SRP."_

Under EDF + SRP, a task set is schedulable if for all deadlines D in the task set:

```
For all L = D_i (each task's relative deadline):
    sum over tasks with D_i <= L of:
        floor((L + T_i - D_i) / T_i) * C_i   +   max blocking B_i   <=   L
```

Where `B_i` is the worst-case blocking time for task i under SRP. This is the longest critical section of any task with a **lower** preemption level that uses a semaphore with ceiling >= task i's preemption level.

**File:** `tasks.c`:
```c
#if ( configUSE_EDF_SCHEDULING == 1 ) && ( configUSE_SRP == 1 )

    /* Check if the task set is schedulable under EDF + SRP.
     * This uses the processor demand test with blocking times.
     * Returns pdTRUE if schedulable, pdFALSE otherwise. */
    BaseType_t xSRPAdmissionControl( void )
    {
        /* You will need to iterate over all tasks and compute:
         *
         * 1. For each task i, compute its worst-case blocking time B_i:
         *    B_i = max over all semaphores S where ceiling(S) >= pi_i
         *           of the longest critical section among tasks with
         *           preemption level < pi_i that use S.
         *
         * 2. For each task i with deadline D_i, check the processor
         *    demand criterion:
         *    sum_j [ floor((D_i + T_j - D_j) / T_j) * C_j ] + B_i <= D_i
         *    for all tasks j with D_j <= D_i.
         *
         * The WCET (C_i) and critical section lengths need to be
         * provided by the application. You can add these as new TCB
         * fields (e.g., xWCET, xMaxCriticalSection) or pass them as
         * parameters.
         *
         * Design choice: how to provide critical section lengths to
         * the scheduler is up to you (instructor's note). Options:
         *   a) New fields in the TCB set at task creation.
         *   b) A separate configuration table.
         *   c) Parameters to xTaskCreateEDF.
         */

        /* Placeholder -- implement the full test as described above. */
        return pdTRUE;
    }

#endif
```

**Note:** The instructor says _"it is up to you to decide how the worst-case estimates of the lengths of the critical sections are given to the scheduler."_ Add a `xWCET` field to the TCB and an additional parameter to `xTaskCreateEDF` (or a setter function) for the worst-case execution time.

#### Step 10: Stack Usage Reporting

**File:** `tasks.c`:
```c
#if ( configUSE_SRP == 1 ) && ( configSRP_STACK_SHARING == 1 )

    void vSRPReportStackUsage( void )
    {
        UBaseType_t i;
        size_t xWithSharing = 0;

        printf( "=== SRP Stack Sharing Report ===\n" );

        for( i = 0; i < configMAX_PREEMPTION_LEVELS; i++ )
        {
            if( xSharedStackTable[ i ].xInUse == pdTRUE )
            {
                printf( "  Preemption level %u: %u tasks sharing %u words\n",
                    ( unsigned ) xSharedStackTable[ i ].uxPreemptionLevel,
                    ( unsigned ) xSharedStackTable[ i ].uxTaskCount,
                    ( unsigned ) xSharedStackTable[ i ].uxMaxStackDepth );

                xWithSharing += ( size_t ) xSharedStackTable[ i ].uxMaxStackDepth
                                * sizeof( StackType_t );
            }
        }

        printf( "\n  Without sharing: %u bytes (each task gets its own stack)\n",
                ( unsigned ) xTotalStackWithoutSharing );
        printf( "  With sharing:    %u bytes (one stack per preemption level)\n",
                ( unsigned ) xWithSharing );

        if( xTotalStackWithoutSharing > 0 )
        {
            printf( "  Savings:         %u bytes (%.1f%%)\n",
                ( unsigned ) ( xTotalStackWithoutSharing - xWithSharing ),
                100.0 * ( double ) ( xTotalStackWithoutSharing - xWithSharing )
                    / ( double ) xTotalStackWithoutSharing );
        }

        printf( "================================\n" );
    }

#endif
```

**File:** `task.h`:
```c
#if ( configUSE_SRP == 1 ) && ( configSRP_STACK_SHARING == 1 )
    void vSRPReportStackUsage( void );
#endif
```

#### Step 11: Test Application

This test demonstrates dynamic ceilings with a counting resource. Resource R1 has 3 units. Three tasks use it with different unit requirements.

```c
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "pico/stdlib.h"
#include <stdio.h>

#define LED_A  2
#define LED_B  3
#define LED_C  4

/* Counting resource with 3 units. */
SemaphoreHandle_t xResource1;

TaskHandle_t xTaskAHandle, xTaskBHandle, xTaskCHandle;

/* Task A (highest preemption level 10): needs 1 unit of R1. */
void vTaskA( void * p )
{
    TickType_t xLastWake = xTaskGetTickCount();
    for( ;; )
    {
        gpio_put( LED_A, 1 );

        /* Take 1 unit. Under SRP, this should NEVER block.
         * When Task A is allowed to run, >=1 unit is guaranteed free. */
        xSRPResourceTake( xResource1, 1 );

        /* Simulate short critical section (10ms). */
        TickType_t xStart = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xStart ) < pdMS_TO_TICKS( 10 ) ) { }

        vSRPResourceGive( xResource1, 1 );

        gpio_put( LED_A, 0 );
        vTaskDelayUntil( &xLastWake, pdMS_TO_TICKS( 500 ) );
    }
}

/* Task B (medium preemption level 5): needs 2 units of R1. */
void vTaskB( void * p )
{
    TickType_t xLastWake = xTaskGetTickCount();
    for( ;; )
    {
        gpio_put( LED_B, 1 );

        /* Take 2 units at once. */
        xSRPResourceTake( xResource1, 2 );

        /* Simulate critical section (30ms). */
        TickType_t xStart = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xStart ) < pdMS_TO_TICKS( 30 ) ) { }

        vSRPResourceGive( xResource1, 2 );

        gpio_put( LED_B, 0 );
        vTaskDelayUntil( &xLastWake, pdMS_TO_TICKS( 800 ) );
    }
}

/* Task C (lowest preemption level 2): needs 1 unit of R1. */
void vTaskC( void * p )
{
    TickType_t xLastWake = xTaskGetTickCount();
    for( ;; )
    {
        gpio_put( LED_C, 1 );

        xSRPResourceTake( xResource1, 1 );

        /* Simulate longer critical section (50ms). */
        TickType_t xStart = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xStart ) < pdMS_TO_TICKS( 50 ) ) { }

        vSRPResourceGive( xResource1, 1 );

        gpio_put( LED_C, 0 );
        vTaskDelayUntil( &xLastWake, pdMS_TO_TICKS( 1000 ) );
    }
}

void main_srp_test( void )
{
    stdio_init_all();
    gpio_init( LED_A ); gpio_set_dir( LED_A, GPIO_OUT );
    gpio_init( LED_B ); gpio_set_dir( LED_B, GPIO_OUT );
    gpio_init( LED_C ); gpio_set_dir( LED_C, GPIO_OUT );

    /* Create a counting resource with 3 units. */
    xResource1 = xSRPResourceCreate( 3 );

    /* Register which tasks use R1 and how many units each needs.
     * This MUST be done before the scheduler starts. */
    vSRPResourceRegisterUser( xResource1, 10, 1 );  /* Task A: level 10, needs 1 */
    vSRPResourceRegisterUser( xResource1, 5,  2 );  /* Task B: level 5,  needs 2 */
    vSRPResourceRegisterUser( xResource1, 2,  1 );  /* Task C: level 2,  needs 1 */

    /* Create tasks. */
    xTaskCreate( vTaskA, "A", 256, NULL, 3, &xTaskAHandle );
    xTaskCreate( vTaskB, "B", 256, NULL, 2, &xTaskBHandle );
    xTaskCreate( vTaskC, "C", 256, NULL, 1, &xTaskCHandle );

    /* Assign preemption levels. */
    vTaskSetPreemptionLevel( xTaskAHandle, 10 );  /* Highest */
    vTaskSetPreemptionLevel( xTaskBHandle, 5 );   /* Medium */
    vTaskSetPreemptionLevel( xTaskCHandle, 2 );   /* Lowest */

    /* Print stack usage report (for quantitative study). */
    #if ( configSRP_STACK_SHARING == 1 )
        vSRPReportStackUsage();
    #endif

    vTaskStartScheduler();
    for( ;; ) { }
}
```

**What to verify with the counting resource:**

```
Scenario: Task C takes 1 unit first, then Task B arrives.

1. All 3 units available. System ceiling = 0.
2. Task C runs, takes 1 unit. Available = 2.
   Ceiling: A needs 1 (2>=1 ok), B needs 2 (2>=2 ok), C needs 1 (2>=1 ok).
   → ceiling = 0. System ceiling = 0.
3. Task B preempts (level 5 > ceiling 0). Takes 2 units. Available = 0.
   Ceiling: A needs 1 (0<1 BLOCKED, lv10), B needs 2 (0<2, lv5), C needs 1 (0<1, lv2).
   → ceiling = 10. System ceiling = 10.
4. Task A arrives (level 10). Can A preempt? 10 > 10? NO.
   Correct! Only 0 units left and A needs 1.
5. Task B gives 2 units. Available = 2.
   Ceiling: A needs 1 (2>=1 ok), B needs 2 (2>=2 ok), C needs 1 (2>=1 ok).
   → ceiling = 0. System ceiling = 0.
6. Task A preempts (10 > 0). Takes 1 unit. Available = 1.
   Ceiling: A needs 1 (1>=1 ok), B needs 2 (1<2 BLOCKED, lv5), C needs 1 (1>=1 ok).
   → ceiling = 5.
   Task B cannot preempt A (5 > 5? NO). ✓
```

- **Stack sharing test (100 tasks):** For the quantitative study, create 100 tasks across a few preemption levels and call `vSRPReportStackUsage()` before starting the scheduler.

### 6.6 Summary of All Changes for SRP

| File | Change |
|------|--------|
| `tasks.c` | Add `uxPreemptionLevel` to TCB |
| `tasks.c` | Add `xSavedWakeTime` to TCB (for stack sharing) |
| `tasks.c` | Add `uxSystemCeiling` global |
| `tasks.c` | Add `SRPResourceUser_t`, `SRPResourceEntry_t` structs and `xSRPResources[]` registry |
| `tasks.c` | Add `prvComputeResourceCeiling()`, `prvRecalculateSystemCeiling()`, `prvFindResource()` |
| `tasks.c` | Implement `xSRPResourceCreate()`, `vSRPResourceRegisterUser()` |
| `tasks.c` | Implement `xSRPResourceTake()`, `vSRPResourceGive()` (with dynamic ceiling recomputation) |
| `tasks.c` | Add `SharedStackEntry_t xSharedStackTable[]`, `xTotalStackWithoutSharing` globals |
| `tasks.c` | Add `prvGetOrCreateSharedStack()` for shared stack allocation |
| `tasks.c` | Modify preemption checks in `xTaskIncrementTick` (add SRP gate + idle task fix) |
| `tasks.c` | Modify `taskSELECT_HIGHEST_PRIORITY_TASK` (skip tasks blocked by SRP) |
| `tasks.c` | Implement `vTaskSetPreemptionLevel` and `uxTaskGetPreemptionLevel` |
| `tasks.c` | Implement `xTaskCreateEDFWithSharedStack()` |
| `tasks.c` | Implement `vSRPReportStackUsage()` |
| `tasks.c` | Implement `xSRPAdmissionControl()` (processor demand + blocking times) |
| `task.h` | Declare SRP resource API (`xSRPResourceCreate`, `vSRPResourceRegisterUser`, `xSRPResourceTake`, `vSRPResourceGive`) |
| `task.h` | Declare preemption level API, shared-stack creation, stack reporting |
| `FreeRTOS.h` | Add `configUSE_SRP`, `configSRP_STACK_SHARING`, `configMAX_SRP_RESOURCES`, `configMAX_RESOURCE_USERS` defaults |
| `FreeRTOSConfig.h` | Enable `configUSE_SRP` and `configSRP_STACK_SHARING` |
| Demo test file | Test SRP with counting resources, dynamic ceilings, stack sharing, report savings |

---

## 7. Assignment 3: Constant Bandwidth Server (CBS) - 15%

### 7.1 What Is the Problem CBS Solves?

Real-time systems often have two types of work:

**Periodic tasks (hard real-time):** Happen on a fixed schedule. Must meet deadlines. Examples:
- Read sensor every 10ms
- Update motor control every 5ms
- Refresh display every 16ms

**Aperiodic tasks (soft real-time):** Happen unpredictably, triggered by events. No strict deadlines, but we'd like them to respond reasonably fast. Examples:
- User presses a button -- handle it
- A network packet arrives -- process it
- A log message needs to be written to flash

**The problem:** How do you run aperiodic tasks alongside periodic tasks without breaking periodic deadlines?

If you give aperiodic tasks a high priority/early deadline, they could steal CPU time from periodic tasks and cause missed deadlines. If you give them a low priority, they might never run (starvation).

**Analogy:** Imagine you have a strict daily schedule (periodic tasks): meetings, meals, exercise. But random phone calls keep coming in (aperiodic tasks). You can't just answer every call immediately -- you'd miss your meetings. You also can't ignore all calls -- some are important. What you need is a **budget**: "I'll spend at most 15 minutes per hour on phone calls." That's CBS.

### 7.2 What Is CBS?

**Constant Bandwidth Server (CBS)** assigns a "CPU time budget" to aperiodic tasks. It guarantees:
- The aperiodic task gets a fixed fraction of CPU time (its "bandwidth").
- It can NEVER take more than its allocated bandwidth, no matter how much work arrives.
- Periodic tasks are completely isolated -- they always meet their deadlines.

**CBS works on top of EDF.** The CBS gives each aperiodic task a "virtual deadline" and the task participates in EDF scheduling with that deadline. When the budget runs out, the deadline is pushed further into the future, so EDF naturally deprioritizes the task.

#### CBS Parameters

| Parameter | Symbol | What It Means | Example |
|-----------|--------|--------------|---------|
| **Maximum Budget** | Q_s | How many ticks of CPU time the server gets per period. Think of it as "how much time per billing cycle." | 20 ticks |
| **Server Period** | T_s | How often the budget replenishes. Think of it as "the billing cycle length." | 100 ticks |
| **Bandwidth** | U_s = Q_s / T_s | The fraction of CPU reserved for this server. | 20/100 = 0.20 = 20% |

**Example:** If Q_s = 20 ticks, T_s = 100 ticks:
- The aperiodic task gets at most 20 ticks of CPU time every 100 ticks.
- That's 20% of the CPU, guaranteed.
- The remaining 80% is available for periodic tasks.

#### CBS Rules Explained

CBS manages a **virtual deadline** and a **budget counter** for the aperiodic task:

**Rule 1: When a new aperiodic job arrives (task becomes ready):**
```
if budget is exhausted OR the current deadline has already passed:
    Start fresh:
    server_deadline = current_time + T_s     (new deadline = now + period)
    current_budget = Q_s                      (full budget)
else:
    Keep the existing deadline and remaining budget.
    (The task can use whatever budget is left from the last job.)
```

**Rule 2: Every tick the aperiodic task is running:**
```
current_budget = current_budget - 1
```

**Rule 3: When the budget hits 0 (exhausted):**
```
server_deadline = server_deadline + T_s    (push deadline further into the future)
current_budget = Q_s                        (replenish budget)
Force a reschedule.                         (EDF may pick a different task now)
```

When the deadline is pushed forward, the task's EDF priority effectively drops. Periodic tasks with earlier deadlines get to run. The aperiodic task will run again later when its new deadline becomes the earliest.

#### CBS Walkthrough

```
Setup:
  Periodic Task P: period=100, execution=40, deadline=100
  CBS Server for Aperiodic Task A: Q_s=20, T_s=100 (20% bandwidth)

Time 0:
  P starts. P's deadline = 100. A is idle (no aperiodic work yet).
  EDF ready list: [P dl:100]
  P runs.

Time 20:
  An aperiodic job arrives for A! Budget is full (Q_s=20), no old deadline.
  CBS assigns: A's deadline = 20 + 100 = 120. Budget = 20.
  EDF ready list: [P dl:100] → [A dl:120]
  P has earlier deadline, P keeps running.

Time 40:
  P finishes its 40 ticks of work. P sleeps until tick 100.
  EDF ready list: [A dl:120]
  A starts running. Budget = 20.

Time 41: A runs. Budget = 19.
Time 42: A runs. Budget = 18.
...
Time 59: A runs. Budget = 1.
Time 60: A runs. Budget = 0. BUDGET EXHAUSTED!
  CBS postpones: A's new deadline = 120 + 100 = 220. Budget = 20.
  EDF ready list: [A dl:220]
  But no one else is ready, so A keeps running (with new deadline 220).

Time 61-79: A runs (still has budget from the replenishment).
Time 80: Suppose A finishes its aperiodic work and blocks (waits for next event).

Time 100:
  P wakes up. P's deadline = 200.
  EDF ready list: [P dl:200]
  P runs.

Time 140: P finishes. Idle runs.

Key insight: A used 40 ticks of CPU between tick 40 and 80. But it was
only ALLOWED 20 ticks per 100-tick period. The CBS rule ensured that after
the first 20 ticks (budget exhaustion at tick 60), the deadline was pushed
to 220, making it the lowest-priority task in EDF. If P had been ready at
that point, P would have preempted A.
```

#### CBS Tie-Breaking Rule (Required by Instructor)

The instructor explicitly states: _"Remember that priority ties are always broken in favor of the server."_

This means: when a CBS task and a periodic task have the **same deadline** (same `xAbsoluteDeadline`), the CBS task must run first.

**Why?** If the periodic task runs first, the CBS server's budget keeps ticking down while it waits, wasting bandwidth. Running the server first is better because:
1. The server finishes quickly (bounded by its budget), then the periodic task runs.
2. If the server exhausts its budget, its deadline is pushed forward, naturally giving the periodic task priority.

**How to implement:** When inserting a CBS task into the EDF ready list, use a slightly adjusted sort key so it appears before periodic tasks with the same deadline. Specifically, subtract 1 from the `xItemValue` used for sorting. Since `vListInsert` places items in ascending order, the CBS task (with value `deadline - 1`) will sort just before a periodic task (with value `deadline`).

This is implemented in Step 4 below by modifying `prvAddTaskToReadyList`.

### 7.3 How CBS Builds on Top of EDF

CBS **requires** EDF. The aperiodic task participates in EDF scheduling with a virtual deadline managed by the CBS rules. From EDF's perspective, it's just another task with a deadline -- EDF doesn't know or care that the deadline is "fake."

This means you need to have completed Assignment 1 (EDF) before implementing CBS. CBS adds fields to the TCB and logic to the tick handler, but it uses EDF's sorted ready list and deadline-based scheduling.

### 7.4 Files to Modify

| File | What to Change | Why |
|------|---------------|-----|
| `FreeRTOS/Source/tasks.c` | Add CBS fields to TCB, budget tracking in tick handler, deadline postponement, job arrival check | Core CBS logic |
| `FreeRTOS/Source/include/task.h` | CBS task creation API | User needs to create CBS tasks |
| `FreeRTOS/Source/include/FreeRTOS.h` | Config macro `configUSE_CBS` | On/off switch |
| `Demo/.../FreeRTOSConfig.h` | Enable CBS | Turn it on |

### 7.5 Step-by-Step Implementation

#### Step 1: Add Configuration

**`FreeRTOSConfig.h`:**
```c
#define configUSE_CBS    1   /* Enable Constant Bandwidth Server */
```

**`FreeRTOS.h`:**
```c
#ifndef configUSE_CBS
    #define configUSE_CBS    0
#endif
```

#### Step 2: Add CBS Fields to the TCB

**File:** `tasks.c`, inside the TCB struct:

```c
#if ( configUSE_CBS == 1 )
    /* pdTRUE if this task is an aperiodic task managed by a CBS.
     * pdFALSE for regular periodic tasks. */
    BaseType_t xIsCBSTask;

    /* Q_s: Maximum budget per server period. This is the max number
     * of ticks the CBS task can use before its deadline is postponed.
     * Constant -- set at creation time, never changes. */
    TickType_t xCBSMaxBudget;

    /* Remaining budget. Starts at Q_s, decremented by 1 each tick
     * the task runs. When it hits 0, the deadline is postponed and
     * the budget is replenished back to Q_s. */
    TickType_t xCBSCurrentBudget;

    /* T_s: Server period. The deadline advances by this amount
     * whenever the budget is exhausted. Constant -- set at creation. */
    TickType_t xCBSPeriod;

    /* The current virtual deadline managed by CBS. This value is
     * used as the task's xAbsoluteDeadline for EDF scheduling.
     * It gets pushed forward by T_s every time the budget runs out. */
    TickType_t xCBSDeadline;
#endif
```

#### Step 3: Create a CBS Task Creation Function

**File:** `task.h` -- declaration:
```c
#if ( configUSE_CBS == 1 )
    /* Create an aperiodic task managed by a Constant Bandwidth Server.
     *
     * pxTaskCode:      The task function (same as xTaskCreate).
     * pcName:          Task name for debugging.
     * uxStackDepth:    Stack size in words.
     * pvParameters:    Parameters to pass to the task function.
     * xServerBudget:   Q_s -- max CPU ticks per server period.
     * xServerPeriod:   T_s -- the server's replenishment period.
     * pxCreatedTask:   Output: handle to the created task (or NULL).
     *
     * Bandwidth = xServerBudget / xServerPeriod.
     * Example: xServerBudget=20, xServerPeriod=100 → 20% of CPU.
     *
     * The total bandwidth of all CBS servers plus all periodic tasks
     * must be <= 1.0 (100%) for EDF schedulability to hold. */
    BaseType_t xTaskCreateCBS(
        TaskFunction_t pxTaskCode,
        const char * const pcName,
        const configSTACK_DEPTH_TYPE uxStackDepth,
        void * const pvParameters,
        TickType_t xServerBudget,
        TickType_t xServerPeriod,
        TaskHandle_t * const pxCreatedTask
    );
#endif
```

**File:** `tasks.c` -- implementation:
```c
#if ( configUSE_CBS == 1 )
BaseType_t xTaskCreateCBS( TaskFunction_t pxTaskCode,
                           const char * const pcName,
                           const configSTACK_DEPTH_TYPE uxStackDepth,
                           void * const pvParameters,
                           TickType_t xServerBudget,
                           TickType_t xServerPeriod,
                           TaskHandle_t * const pxCreatedTask )
{
    TCB_t * pxNewTCB;
    BaseType_t xReturn;

    /* Create the task with a basic priority (above idle).
     * Actual scheduling is done by EDF using the CBS deadline. */
    pxNewTCB = prvCreateTask( pxTaskCode, pcName, uxStackDepth, pvParameters,
                              tskIDLE_PRIORITY + 1, pxCreatedTask );

    if( pxNewTCB != NULL )
    {
        /* Mark this task as a CBS-managed aperiodic task. */
        pxNewTCB->xIsCBSTask = pdTRUE;

        /* Set the server parameters (constant for the task's lifetime). */
        pxNewTCB->xCBSMaxBudget = xServerBudget;
        pxNewTCB->xCBSPeriod = xServerPeriod;

        /* Initialize the budget to full. */
        pxNewTCB->xCBSCurrentBudget = xServerBudget;

        /* Set the initial CBS deadline. The first deadline is
         * at time 0 + T_s = T_s. */
        pxNewTCB->xCBSDeadline = xServerPeriod;

        /* Set the EDF deadline to match the CBS deadline.
         * This is what the EDF scheduler actually sorts by. */
        pxNewTCB->xAbsoluteDeadline = xServerPeriod;

        /* The CBS task needs a non-zero xPeriod so prvAddTaskToReadyList
         * puts it in the EDF ready list (not the priority ready list).
         * We use the CBS period for this. */
        pxNewTCB->xPeriod = xServerPeriod;
        pxNewTCB->xRelativeDeadline = xServerPeriod;

        prvAddNewTaskToReadyList( pxNewTCB );
        xReturn = pdPASS;
    }
    else
    {
        xReturn = errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
    }

    return xReturn;
}
#endif
```

#### Step 4: Modify the Tick Handler for Budget Tracking

**File:** `tasks.c`, in `xTaskIncrementTick()` -- add after incrementing the tick count:

```c
#if ( configUSE_CBS == 1 )
    /* If the currently running task is a CBS task, decrement its budget. */
    if( pxCurrentTCB->xIsCBSTask == pdTRUE )
    {
        if( pxCurrentTCB->xCBSCurrentBudget > 0 )
        {
            /* Each tick the CBS task runs costs 1 unit of budget. */
            pxCurrentTCB->xCBSCurrentBudget--;
        }

        if( pxCurrentTCB->xCBSCurrentBudget == 0 )
        {
            /* Budget exhausted! The CBS task has used up its
             * allocation for this period. Three things happen:
             *
             * 1. Postpone the deadline by one server period.
             *    This makes the task less urgent in EDF,
             *    allowing periodic tasks with earlier deadlines to run.
             *
             * 2. Replenish the budget back to Q_s (full).
             *    The task gets a fresh allocation.
             *
             * 3. Re-sort the task in the EDF ready list with the
             *    new (later) deadline, and force a reschedule. */

            /* 1. Postpone deadline. */
            pxCurrentTCB->xCBSDeadline += pxCurrentTCB->xCBSPeriod;
            pxCurrentTCB->xAbsoluteDeadline = pxCurrentTCB->xCBSDeadline;

            /* 2. Replenish budget. */
            pxCurrentTCB->xCBSCurrentBudget = pxCurrentTCB->xCBSMaxBudget;

            /* 3. Re-insert into the EDF ready list with the new deadline.
             *    Remove from current position, update sort key, re-insert. */
            ( void ) uxListRemove( &( pxCurrentTCB->xStateListItem ) );
            listSET_LIST_ITEM_VALUE( &( pxCurrentTCB->xStateListItem ),
                                     pxCurrentTCB->xAbsoluteDeadline );
            vListInsert( &xEDFReadyList, &( pxCurrentTCB->xStateListItem ) );

            /* Force a context switch so the scheduler re-evaluates
             * who should run next (a periodic task might now have
             * an earlier deadline than the postponed CBS task). */
            xSwitchRequired = pdTRUE;
        }
    }
#endif
```

#### Step 5: Handle Job Arrival for CBS Tasks

When an aperiodic event occurs (e.g., button press) and the CBS task transitions from blocked to ready, we need to check if the current budget/deadline is still valid.

**File:** `tasks.c` -- add this check in **two places**:
1. In `xTaskIncrementTick`, right **before** `prvAddTaskToReadyList( pxTCB )` is called for tasks unblocked from the delayed list.
2. In `xTaskRemoveFromEventList`, right **before** the task is added to the ready list (or pending ready list).

```c
#if ( configUSE_CBS == 1 )
    /* CBS job arrival check: when a CBS task wakes up (becomes ready),
     * check if its current budget and deadline are still valid. */
    if( pxTCB->xIsCBSTask == pdTRUE )
    {
        TickType_t xCurrentTime = xTickCount;

        if( ( pxTCB->xCBSCurrentBudget == 0 ) ||
            ( xCurrentTime >= pxTCB->xCBSDeadline ) )
        {
            /* Budget exhausted or deadline passed while the task was
             * sleeping. Assign a fresh deadline and full budget.
             * This is like "starting a new billing cycle." */
            pxTCB->xCBSDeadline = xCurrentTime + pxTCB->xCBSPeriod;
            pxTCB->xCBSCurrentBudget = pxTCB->xCBSMaxBudget;
        }
        /* else: the task still has budget left and the deadline hasn't
         * passed. Keep the existing deadline and remaining budget.
         * This is efficient -- the task can use leftover budget. */

        /* Update the EDF deadline to match the CBS deadline. */
        pxTCB->xAbsoluteDeadline = pxTCB->xCBSDeadline;
    }
#endif
```

#### Step 6: CBS Tie-Breaking in `prvAddTaskToReadyList`

The instructor requires: _"priority ties are always broken in favor of the server."_ When a CBS task and a periodic task have the same deadline, the CBS task must be selected to run first.

**File:** `tasks.c` -- Modify the `prvAddTaskToReadyList` macro (the one you created in EDF Step 4). When inserting a CBS task into the EDF ready list, subtract 1 from its sort key so it sorts just before a periodic task at the same deadline:

```c
#if ( configUSE_EDF_SCHEDULING == 1 )
    #define prvAddTaskToReadyList( pxTCB )                                             \
    do {                                                                               \
        traceMOVED_TASK_TO_READY_STATE( pxTCB );                                       \
        if( ( pxTCB )->xPeriod > 0 )                                                   \
        {                                                                              \
            TickType_t xSortKey = ( pxTCB )->xAbsoluteDeadline;                        \
            /* CBS tie-breaking: if this is a CBS task, use deadline - 1              \
             * as the sort key so it is placed BEFORE any periodic task               \
             * with the same deadline. vListInsert sorts ascending, so               \
             * a smaller value means earlier position in the list.                    \
             * This implements the rule: "ties broken in favor of server." */          \
            if( ( configUSE_CBS == 1 ) && ( ( pxTCB )->xIsCBSTask == pdTRUE ) &&       \
                ( xSortKey > 0 ) )                                                     \
            {                                                                          \
                xSortKey--;                                                            \
            }                                                                          \
            listSET_LIST_ITEM_VALUE( &( ( pxTCB )->xStateListItem ), xSortKey );       \
            vListInsert( &xEDFReadyList, &( ( pxTCB )->xStateListItem ) );             \
        }                                                                              \
        else                                                                           \
        {                                                                              \
            taskRECORD_READY_PRIORITY( ( pxTCB )->uxPriority );                        \
            listINSERT_END( &( pxReadyTasksLists[ ( pxTCB )->uxPriority ] ),           \
                            &( ( pxTCB )->xStateListItem ) );                          \
        }                                                                              \
        tracePOST_MOVED_TASK_TO_READY_STATE( pxTCB );                                  \
    } while( 0 )
#else
    /* Original FreeRTOS macro (unchanged when EDF is disabled). */
    ...
#endif
```

**Also update the budget exhaustion re-insertion in Step 4** (tick handler) to use the same tie-breaking:

```c
/* When re-inserting after budget exhaustion, use xSortKey - 1 for CBS. */
TickType_t xSortKey = pxCurrentTCB->xAbsoluteDeadline;
if( xSortKey > 0 ) xSortKey--;
listSET_LIST_ITEM_VALUE( &( pxCurrentTCB->xStateListItem ), xSortKey );
vListInsert( &xEDFReadyList, &( pxCurrentTCB->xStateListItem ) );
```

#### Step 7: Test Application

```c
#include "FreeRTOS.h"
#include "task.h"
#include "pico/stdlib.h"

#define LED_PERIODIC_A  2   /* GPIO for periodic task A */
#define LED_PERIODIC_B  3   /* GPIO for periodic task B */
#define LED_APERIODIC   4   /* GPIO for CBS aperiodic task */
#define BUTTON_PIN      5   /* GPIO for button (triggers aperiodic work) */

/* Periodic Task A: period=200ms, execution=50ms. Hard real-time. */
void vPeriodicA( void * p )
{
    TickType_t xLastWake = xTaskGetTickCount();
    for( ;; )
    {
        gpio_put( LED_PERIODIC_A, 1 );
        TickType_t xStart = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xStart ) < pdMS_TO_TICKS( 50 ) ) { }
        gpio_put( LED_PERIODIC_A, 0 );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

/* Periodic Task B: period=500ms, execution=100ms. Hard real-time. */
void vPeriodicB( void * p )
{
    TickType_t xLastWake = xTaskGetTickCount();
    for( ;; )
    {
        gpio_put( LED_PERIODIC_B, 1 );
        TickType_t xStart = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xStart ) < pdMS_TO_TICKS( 100 ) ) { }
        gpio_put( LED_PERIODIC_B, 0 );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

/* Aperiodic CBS task: handles button presses. Gets 20% of CPU. */
void vAperiodicHandler( void * p )
{
    for( ;; )
    {
        /* Wait for a notification (sent when button is pressed). */
        ulTaskNotifyTake( pdTRUE, portMAX_DELAY );

        /* Process the button press (simulate 80ms of work). */
        gpio_put( LED_APERIODIC, 1 );
        TickType_t xStart = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xStart ) < pdMS_TO_TICKS( 80 ) ) { }
        gpio_put( LED_APERIODIC, 0 );
    }
}

/* Button interrupt handler. Notifies the aperiodic task. */
TaskHandle_t xAperiodicHandle;

void gpio_callback( uint gpio, uint32_t events )
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR( xAperiodicHandle, &xHigherPriorityTaskWoken );
    portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
}

void main_cbs_test( void )
{
    gpio_init( LED_PERIODIC_A ); gpio_set_dir( LED_PERIODIC_A, GPIO_OUT );
    gpio_init( LED_PERIODIC_B ); gpio_set_dir( LED_PERIODIC_B, GPIO_OUT );
    gpio_init( LED_APERIODIC );  gpio_set_dir( LED_APERIODIC, GPIO_OUT );
    gpio_init( BUTTON_PIN );     gpio_set_dir( BUTTON_PIN, GPIO_IN );
    gpio_pull_up( BUTTON_PIN );
    gpio_set_irq_enabled_with_callback( BUTTON_PIN, GPIO_IRQ_EDGE_FALL,
                                        true, &gpio_callback );

    /* Periodic tasks (hard real-time, created with EDF). */
    xTaskCreateEDF( vPeriodicA, "PerA", 256, NULL,
                    pdMS_TO_TICKS( 200 ), pdMS_TO_TICKS( 200 ), NULL );
    xTaskCreateEDF( vPeriodicB, "PerB", 256, NULL,
                    pdMS_TO_TICKS( 500 ), pdMS_TO_TICKS( 500 ), NULL );

    /* CBS aperiodic task: budget=20 ticks, period=100 ticks → 20% CPU.
     * Utilization check: PerA = 50/200 = 25%, PerB = 100/500 = 20%,
     * CBS = 20/100 = 20%. Total = 65% < 100%. Schedulable! */
    xTaskCreateCBS( vAperiodicHandler, "CBS", 256, NULL,
                    pdMS_TO_TICKS( 20 ),  /* Q_s: 20ms budget per period */
                    pdMS_TO_TICKS( 100 ), /* T_s: 100ms period */
                    &xAperiodicHandle );

    vTaskStartScheduler();
    for( ;; ) { }
}
```

**What to verify:**
- Periodic tasks A and B always meet their deadlines, regardless of how many times you press the button.
- The CBS task responds to button presses and does its work.
- If you press the button rapidly (flooding the CBS task with work), it gets throttled to 20% of CPU. Its LED might flash in bursts (20ms on, 80ms off, 20ms on, ...) as the budget runs out and gets replenished.
- The periodic task deadlines are never affected by the aperiodic load.

### 7.6 Summary of All Changes for CBS

| File | Change |
|------|--------|
| `tasks.c` | Add `xIsCBSTask`, `xCBSMaxBudget`, `xCBSCurrentBudget`, `xCBSPeriod`, `xCBSDeadline` to TCB |
| `tasks.c` | Budget tracking in `xTaskIncrementTick` (decrement budget, postpone deadline on exhaustion) |
| `tasks.c` | Job arrival check when CBS task unblocks (refresh deadline/budget if expired) — in `xTaskIncrementTick` and `xTaskRemoveFromEventList` |
| `tasks.c` | CBS tie-breaking in `prvAddTaskToReadyList` (CBS tasks sort before periodic tasks at same deadline) |
| `tasks.c` | Implement `xTaskCreateCBS()` function |
| `task.h` | Declare `xTaskCreateCBS` with full documentation |
| `FreeRTOS.h` | Add `configUSE_CBS` with default value 0 |
| `FreeRTOSConfig.h` | Enable `configUSE_CBS` |
| Demo test file | Periodic tasks + aperiodic CBS task + button interrupt, verify bandwidth isolation |

---

## 8. Assignment 4: Multiprocessor Support - 15%

### 8.1 What Is This About?

The Raspberry Pi Pico has **two** ARM Cortex-M0+ cores. FreeRTOS already has SMP (Symmetric Multi-Processing) support for RP2040, but you need to understand how it works and potentially extend it with your EDF/SRP/CBS changes.

There are two approaches to multiprocessor real-time scheduling:

**AMP (Asymmetric Multi-Processing):**
- Each core runs its own independent scheduler.
- Tasks are statically assigned to cores.
- Simpler but may not balance load well.

**SMP (Symmetric Multi-Processing):**
- A single scheduler manages all cores.
- Tasks can run on any core (or be pinned to specific cores).
- FreeRTOS already supports this for RP2040!

### 8.2 How FreeRTOS SMP Works on RP2040

**Key configuration:**
```c
#define configNUMBER_OF_CORES    2    // Enable dual-core
#define configTICK_CORE          0    // Core 0 handles tick interrupts
#define configUSE_CORE_AFFINITY  1    // Allow pinning tasks to cores
```

**What changes in SMP mode:**
- `pxCurrentTCB` becomes `pxCurrentTCBs[configNUMBER_OF_CORES]` (one per core).
- `taskSELECT_HIGHEST_PRIORITY_TASK(xCoreID)` becomes a function rather than a macro, considering which tasks are already running on other cores.
- Each core can independently enter/exit critical sections.
- Inter-core interrupts are used to signal context switches on the other core.

### 8.3 Files to Modify

| File | What to Change |
|------|---------------|
| `FreeRTOSConfig.h` | Set `configNUMBER_OF_CORES=2`, enable SMP features |
| `tasks.c` | Extend EDF/SRP/CBS for multi-core scheduling decisions |
| Port files (`port.c`, `portmacro.h`) | Already support SMP (review but likely minimal changes) |
| Demo test file | Test tasks running on both cores |

### 8.4 Key Considerations

1. **Global EDF on SMP:** Run the two tasks with the earliest deadlines on the two cores. When a new task with a very early deadline arrives, it might need to preempt a task on one of the cores.

2. **SRP on SMP:** Each core may need its own system ceiling, or you need a global system ceiling protected by a spinlock. Locking decisions become more complex.

3. **CBS on SMP:** Budget tracking needs to be core-aware. A CBS task's budget should be decremented only when it's actually running.

4. **Core Affinity:** FreeRTOS's `uxCoreAffinityMask` in the TCB lets you pin tasks to specific cores. You might use this for partitioned scheduling.

### 8.5 Step-by-Step Implementation

#### Step 1: Enable SMP in Configuration

**`FreeRTOSConfig.h`:**
```c
#define configNUMBER_OF_CORES           2
#define configTICK_CORE                 0
#define configUSE_CORE_AFFINITY         1
#define configRUN_MULTIPLE_PRIORITIES   1
#define configUSE_PASSIVE_IDLE_HOOK     0
```

#### Step 2: Extend `taskSELECT_HIGHEST_PRIORITY_TASK` for SMP+EDF

In SMP mode, FreeRTOS uses `prvSelectHighestPriorityTask(xCoreID)` instead of the single-core macro. You need to modify this function to use EDF logic (pick the ready task with the earliest deadline that isn't already running on another core).

#### Step 3: Handle Inter-Core Preemption

When a task becomes ready with an earlier deadline than a task running on another core, you need to send an inter-processor interrupt (IPI) to that core to trigger a context switch. FreeRTOS's RP2040 port already has `vYieldCore(xCoreID)` for this.

#### Step 4: Test

Create test scenarios with tasks running on both cores. Use GPIO pins and a logic analyzer to verify timing on both cores independently.

---

## 9. Testing and Debugging Tips

### 9.1 Using LEDs

The simplest way to visualize task scheduling:
- Assign each task a GPIO pin connected to an LED.
- Turn the LED ON when the task starts executing, OFF when it finishes.
- You can visually see which tasks are running and in what order.

### 9.2 Using a Logic Analyzer

For precise timing:
- Connect GPIO pins to a logic analyzer (Saleae, Digilent, or a cheap clone).
- Toggle GPIOs using FreeRTOS **Trace Hook Macros**.
- The logic analyzer captures exact timing of task switches.

### 9.3 Trace Hook Macros

FreeRTOS provides macros that are called at key points. Define them in `FreeRTOSConfig.h`:

```c
// Called when a task is switched in (starts running)
#define traceTASK_SWITCHED_IN()     gpio_put(current_task_gpio_pin, 1)

// Called when a task is switched out (stops running)
#define traceTASK_SWITCHED_OUT()    gpio_put(current_task_gpio_pin, 0)
```

For more sophisticated tracing, you can identify which task by checking `pxCurrentTCB`:
```c
#define traceTASK_SWITCHED_IN()                             \
    do {                                                    \
        if (pxCurrentTCB == xTaskAHandle) gpio_put(2, 1);   \
        else if (pxCurrentTCB == xTaskBHandle) gpio_put(3, 1); \
    } while(0)
```

### 9.4 Printf Debugging (Use Sparingly!)

`printf` is very slow on embedded systems and can disrupt real-time behavior. Use it only during development, not for timing-sensitive measurements.

```c
// Enable USB serial output in CMakeLists.txt:
pico_enable_stdio_usb(your_target 1)
pico_enable_stdio_uart(your_target 0)

// In your code:
printf("Task A: tick=%lu, deadline=%lu\n", xTaskGetTickCount(), pxTCB->xAbsoluteDeadline);
```

### 9.5 General Testing Strategy

1. **Start simple:** One task, verify it runs periodically.
2. **Add a second task:** Verify correct scheduling order.
3. **Add stress cases:** Overlapping deadlines, budget exhaustion, resource contention.
4. **Edge cases:** What happens at tick overflow? What if a task misses its deadline? What if all CBS budget is used?

---

## 10. Build System and CMake

### 10.1 How the Build Works

The RP2040 demo uses CMake with the Pico SDK. The key files are:

1. **`pico_sdk_import.cmake`** -- Finds and imports the Pico SDK.
2. **`FreeRTOS_Kernel_import.cmake`** -- Finds and imports the FreeRTOS kernel.
3. **`CMakeLists.txt`** -- Your project's build configuration.

### 10.2 Building Your Project

From the demo directory:
```bash
mkdir build
cd build
cmake ..
make main_blinky   # or whatever your target is named
```

This produces a `.uf2` file that you flash onto the Pico.

### 10.3 Flashing the Pico

1. Hold the **BOOTSEL** button on the Pico while plugging it in via USB.
2. It appears as a USB mass storage device.
3. Drag and drop the `.uf2` file onto it.
4. The Pico reboots and runs your firmware.

Alternatively, if you have the debug probe set up, you can use OpenOCD/GDB for debugging and flashing.

### 10.4 Adding Your Test Files to CMake

To add a new test executable (e.g., `main_edf_test.c`), add to `CMakeLists.txt`:

```cmake
add_executable(main_edf_test
    main.c
    main_edf_test.c
)

target_compile_definitions(main_edf_test PRIVATE
    mainCREATE_SIMPLE_BLINKY_DEMO_ONLY=0
    # Add any other defines you need
)

target_link_libraries(main_edf_test
    pico_stdlib
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap4
)

pico_add_extra_outputs(main_edf_test)
```

---

## Quick Reference: Key Locations in tasks.c

| What | Approximate Line | Description |
|------|-----------------|-------------|
| TCB struct | 371–453 | Task Control Block definition |
| `taskSELECT_HIGHEST_PRIORITY_TASK` | 195–210 | Scheduler task selection macro |
| `prvAddTaskToReadyList` | 285–292 | Macro to add task to ready list |
| `taskSWITCH_DELAYED_LISTS` | 264–276 | Swap delayed lists on tick overflow |
| Ready lists declaration | 476–480 | `pxReadyTasksLists`, delayed lists |
| `uxTopReadyPriority` | 505 | Tracks highest priority with ready tasks |
| `xTaskCreate` | 1741–1772 | Create a new task |
| `prvInitialiseNewTask` | 1816–1955 | Initialize TCB fields |
| `prvAddNewTaskToReadyList` | 2052–2127 | Add newly created task to scheduler |
| `vTaskDelay` | 2469–2513 | Relative delay |
| `vTaskDelayUntil` | 2380–2462 | Absolute periodic delay |
| `vTaskStartScheduler` | 3700–3802 | Start the scheduler |
| `prvCreateIdleTasks` | 3551–3696 | Create idle task(s) |
| `xTaskIncrementTick` | 4736–4983 | Tick interrupt handler |
| `vTaskSwitchContext` | ~5170 | Context switch (calls `taskSELECT_HIGHEST_PRIORITY_TASK`) |
| `prvInitialiseTaskLists` | 6078–6107 | Initialize all lists |
| Priority inheritance | ~6690–6810 | Mutex priority inheritance logic |
| `prvAddCurrentTaskToDelayedList` | 8590–8706 | Move task to delayed list |
