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

### 5.1 Big Picture (What You Are Building)

For this assignment, you are turning FreeRTOS from fixed-priority scheduling into EDF scheduling for periodic real-time tasks.

You must support all of these Task-1 requirements **inside the kernel**:
- EDF dispatch (earliest absolute deadline runs first).
- Admission control when a task is created.
- Two admission tests:
  - implicit deadline task sets (`D_i == T_i`) use utilization test,
  - constrained deadline task sets (`D_i <= T_i`, and at least one `D_i < T_i`) use exact processor-demand test.
- Runtime admission (new tasks can be accepted/rejected after scheduler starts).
- Transient overload policy (if a job misses deadline, drop late job immediately).
- Detailed tracing prints (task list, releases, finishes, preemptions, resumes, admissions, drops).
- UART-based prints (not USB stdio) for more stable timing logs.

Think of each periodic task as repeatedly creating jobs. Every job has:
- release time,
- absolute deadline,
- execution budget (WCET).

EDF always runs the ready job with earliest absolute deadline.

### 5.2 Required EDF Math

#### Implicit deadline case (`D_i == T_i` for every task)

Use utilization check:

$$
U = \sum_i \frac{C_i}{T_i}
$$

Accept task set if $U \le 1$.

#### Constrained deadline case (`D_i <= T_i` and at least one strict `<`)

Use exact processor-demand test with demand bound function (dbf):

$$
dbf_i(t) = \max\left(0,\left\lfloor \frac{t - D_i}{T_i} \right\rfloor + 1\right) C_i
$$

$$
DBF(t) = \sum_i dbf_i(t)
$$

Feasible iff $DBF(t) \le t$ for all tested instants $t$ in the analysis interval.

### 5.3 Files You Will Update

| File | Why it changes |
|------|----------------|
| `FreeRTOS/Source/tasks.c` | Main EDF logic: task metadata, admission, runtime miss handling, scheduler selection, trace logs |
| `FreeRTOS/Source/include/task.h` | Public EDF API declarations |
| `FreeRTOS/Source/include/FreeRTOS.h` | Default compile-time fallback macros |
| `Demo/.../Standard/FreeRTOSConfig.h` | Enable EDF + tracing options |
| `Demo/.../Standard/CMakeLists.txt` | Route stdio to UART for deterministic-ish logs |
| `Demo/.../Standard/main_edf_test.c` | Validation workloads for accept/reject/runtime-add/miss-drop |

### 5.4 Kernel Data Model Updates (`tasks.c`)

Add these EDF fields into `TCB_t` (`tskTaskControlBlock`) so each task carries full timing info.

```c
/* Add this inside tskTaskControlBlock in tasks.c, under EDF compile guard.
 * These fields are needed for admission tests and runtime deadline handling. */
#if ( configUSE_EDF_SCHEDULING == 1 )
    TickType_t xPeriod;             /* T: Job release interval in ticks. */
    TickType_t xRelativeDeadline;   /* D: Relative deadline in ticks from each release. */
    TickType_t xAbsoluteDeadline;   /* Absolute deadline of currently active job. */
    TickType_t xWcetTicks;          /* C: Worst-case execution budget per job (in ticks). */
    TickType_t xSRPBlockingBound;   /* B: SRP blocking bound (0 when SRP disabled). */
    TickType_t xLastReleaseTick;    /* Tick when current job was released. */
    TickType_t xJobExecTicks;       /* Execution consumed by current job (debug/admission stats). */
    UBaseType_t uxEDFFlags;         /* Bit flags (e.g., periodic task, job active, was preempted). */
    ListItem_t xEDFRegistryListItem;/* Dedicated list item for EDF registry membership. */
#endif
```

Add EDF global lists/variables near other scheduler globals.

```c
#if ( configUSE_EDF_SCHEDULING == 1 )
    /* Ready EDF jobs sorted by absolute deadline; head is next to run. */
    PRIVILEGED_DATA static List_t xEDFReadyList;

    /* Registry of all admitted EDF periodic tasks (ready + blocked + delayed).
     * Admission control must consider the whole accepted set, not just ready tasks. */
    PRIVILEGED_DATA static List_t xEDFTaskRegistryList;

    /* Optional counters useful for tracing and demo metrics. */
    PRIVILEGED_DATA static UBaseType_t uxEDFAcceptedTaskCount = 0U;
    PRIVILEGED_DATA static UBaseType_t uxEDFRejectedTaskCount = 0U;
#endif
```

### 5.5 Config + Trace Macros

In `FreeRTOSConfig.h`, add/verify:

```c
/* Enable EDF scheduler path in kernel. */
#define configUSE_EDF_SCHEDULING    1

/* Enable/disable EDF trace prints without deleting print calls. */
#define configEDF_TRACE_ENABLE      1

/* Use the standard FreeRTOS pattern where configPRINTF is configurable.
 * This lets you switch output backends later without editing kernel logic. */
#define configPRINTF( x )           printf x
```

In `FreeRTOS.h`, provide defaults so builds do not break when user config omits them.

```c
#ifndef configUSE_EDF_SCHEDULING
    #define configUSE_EDF_SCHEDULING    0
#endif

#ifndef configEDF_TRACE_ENABLE
    #define configEDF_TRACE_ENABLE      0
#endif
```

In `tasks.c`, add trace helper macros and small trace helpers.

```c
#if ( configUSE_EDF_SCHEDULING == 1 )
    #if ( configEDF_TRACE_ENABLE == 1 )
        #define edfTRACE( ... ) configPRINTF( ( __VA_ARGS__ ) )
    #else
        #define edfTRACE( ... )
    #endif

    /* Emit one-line admission result with enough detail for demo and debugging. */
    static void prvEDFTraceAdmission( const char * pcTaskName,
                                      TickType_t xC,
                                      TickType_t xT,
                                      TickType_t xD,
                                      BaseType_t xAccepted,
                                      const char * pcReason )
    {
        edfTRACE( "[EDF][tick=%lu][admission] task=%s C=%lu T=%lu D=%lu result=%s reason=%s\r\n",
                  ( unsigned long ) xTaskGetTickCount(),
                  pcTaskName,
                  ( unsigned long ) xC,
                  ( unsigned long ) xT,
                  ( unsigned long ) xD,
                  ( xAccepted == pdPASS ) ? "ACCEPT" : "REJECT",
                  pcReason );
    }
#endif
```

### 5.6 EDF API (`task.h`) and Create Path (`tasks.c`)

You need WCET (`C`) for admission control, so extend API to include it.

```c
/* task.h */
#if ( configUSE_EDF_SCHEDULING == 1 )
    /* Create a periodic EDF task.
     * xWcetTicks is required for admission control (C/T/D checks). */
    BaseType_t xTaskCreateEDF( TaskFunction_t pxTaskCode,
                               const char * const pcName,
                               const configSTACK_DEPTH_TYPE uxStackDepth,
                               void * const pvParameters,
                               TickType_t xPeriod,
                               TickType_t xRelativeDeadline,
                               TickType_t xWcetTicks,
                               TaskHandle_t * const pxCreatedTask );

    /* Call at end of each periodic job to release next one and delay to next period. */
    void vTaskDelayUntilNextPeriod( TickType_t * pxPreviousWakeTime );
#endif
```

Add EDF admission helper declarations in `tasks.c` (private/static).

```c
#if ( configUSE_EDF_SCHEDULING == 1 )
    /* Candidate parameters used by admission logic before task is inserted.
     * When SRP is enabled, xB and uxLevel are populated so both admission
     * paths can account for SRP blocking. */
    typedef struct xEDFAdmissionTaskParams
    {
        TickType_t xC;            /* WCET (execution budget) */
        TickType_t xT;            /* Period */
        TickType_t xD;            /* Relative deadline */
        TickType_t xB;            /* SRP blocking bound (0 when SRP disabled) */
        UBaseType_t uxLevel;      /* SRP preemption level (0 when SRP disabled) */
        const char * pcName;      /* Used only for tracing */
    } EDFAdmissionTaskParams_t;

    static BaseType_t prvEDFAdmissionControl( const EDFAdmissionTaskParams_t * pxCandidate,
                                              const char ** ppcReason );
    static BaseType_t prvEDFAdmissionImplicit( const EDFAdmissionTaskParams_t * pxCandidate,
                                               const char ** ppcReason );
    static BaseType_t prvEDFAdmissionConstrained( const EDFAdmissionTaskParams_t * pxCandidate,
                                                  const char ** ppcReason );
#endif
```

Now integrate admission in `xTaskCreateEDF` itself.

```c
#if ( configUSE_EDF_SCHEDULING == 1 )
BaseType_t xTaskCreateEDF( TaskFunction_t pxTaskCode,
                           const char * const pcName,
                           const configSTACK_DEPTH_TYPE uxStackDepth,
                           void * const pvParameters,
                           TickType_t xPeriod,
                           TickType_t xRelativeDeadline,
                           TickType_t xWcetTicks,
                           TaskHandle_t * const pxCreatedTask )
{
    TCB_t * pxNewTCB;
    BaseType_t xReturn = pdFAIL;
    const char * pcReason = "UNKNOWN";
    EDFAdmissionTaskParams_t xCandidate;

    /* Basic parameter validation first so kernel rejects bad API usage early. */
    if( ( xPeriod == 0U ) || ( xRelativeDeadline == 0U ) || ( xWcetTicks == 0U ) )
    {
        prvEDFTraceAdmission( pcName, xWcetTicks, xPeriod, xRelativeDeadline, pdFAIL, "zero parameter" );
        return pdFAIL;
    }

    if( xRelativeDeadline > xPeriod )
    {
        /* Assignment asks constrained-deadline support, which means D <= T. */
        prvEDFTraceAdmission( pcName, xWcetTicks, xPeriod, xRelativeDeadline, pdFAIL, "D > T not supported" );
        return pdFAIL;
    }

    /* Prepare candidate object used by admission functions. */
    xCandidate.xC = xWcetTicks;
    xCandidate.xT = xPeriod;
    xCandidate.xD = xRelativeDeadline;
    #if ( configUSE_SRP == 1 )
        xCandidate.uxLevel = prvSRPCalculateTaskPreemptionLevel( xRelativeDeadline );
        xCandidate.xB = prvSRPComputeBlockingBoundForLevel( xCandidate.uxLevel );
    #else
        xCandidate.uxLevel = 0U;
        xCandidate.xB = 0U;
    #endif
    xCandidate.pcName = pcName;

    /* Admission and insertion must be atomic while scheduler is running. */
    taskENTER_CRITICAL();
    {
        /* This works both pre-start and runtime, so requirement "accept new tasks
         * while system is running" is satisfied by the same API path. */
        if( prvEDFAdmissionControl( &xCandidate, &pcReason ) == pdPASS )
        {
            /* Allocate and initialize the task after admission says OK. */
            pxNewTCB = prvCreateTask( pxTaskCode,
                                      pcName,
                                      uxStackDepth,
                                      pvParameters,
                                      tskIDLE_PRIORITY + 1U,
                                      pxCreatedTask );

            if( pxNewTCB != NULL )
            {
                /* Fill EDF timing metadata for first job. */
                pxNewTCB->xPeriod = xPeriod;
                pxNewTCB->xRelativeDeadline = xRelativeDeadline;
                pxNewTCB->xWcetTicks = xWcetTicks;
                pxNewTCB->xLastReleaseTick = xTaskGetTickCount();
                pxNewTCB->xAbsoluteDeadline = pxNewTCB->xLastReleaseTick + xRelativeDeadline;
                pxNewTCB->xJobExecTicks = 0U;
                pxNewTCB->uxEDFFlags = 0U;

                /* Add task to scheduler and internal EDF registry. */
                prvAddNewTaskToReadyList( pxNewTCB );
                vListInsertEnd( &xEDFTaskRegistryList, &( pxNewTCB->xEventListItem ) );
                uxEDFAcceptedTaskCount++;

                prvEDFTraceAdmission( pcName, xWcetTicks, xPeriod, xRelativeDeadline, pdPASS, "admitted" );
                edfTRACE( "[EDF][tick=%lu][task-add] task=%s mode=%s\r\n",
                          ( unsigned long ) xTaskGetTickCount(),
                          pcName,
                          ( xTaskGetSchedulerState() == taskSCHEDULER_RUNNING ) ? "runtime" : "pre-start" );

                xReturn = pdPASS;
            }
            else
            {
                prvEDFTraceAdmission( pcName, xWcetTicks, xPeriod, xRelativeDeadline, pdFAIL, "allocation failed" );
                xReturn = errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
            }
        }
        else
        {
            uxEDFRejectedTaskCount++;
            prvEDFTraceAdmission( pcName, xWcetTicks, xPeriod, xRelativeDeadline, pdFAIL, pcReason );
            xReturn = pdFAIL;
        }
    }
    taskEXIT_CRITICAL();

    return xReturn;
}
#endif
```

### 5.7 Admission Control Functions (Complete Kernel Logic)

#### 5.7.1 Dispatch function

```c
#if ( configUSE_EDF_SCHEDULING == 1 )
static BaseType_t prvEDFAdmissionControl( const EDFAdmissionTaskParams_t * pxCandidate,
                                          const char ** ppcReason )
{
    const ListItem_t * pxIt;
    BaseType_t xHasConstrained = pdFALSE;

    /* If ANY admitted task has D < T, exact constrained test is required. */
    for( pxIt = listGET_HEAD_ENTRY( &xEDFTaskRegistryList );
         pxIt != listGET_END_MARKER( &xEDFTaskRegistryList );
         pxIt = listGET_NEXT( pxIt ) )
    {
        const TCB_t * pxTCB = ( const TCB_t * ) listGET_LIST_ITEM_OWNER( pxIt );

        if( pxTCB->xRelativeDeadline < pxTCB->xPeriod )
        {
            xHasConstrained = pdTRUE;
            break;
        }
    }

    if( pxCandidate->xD < pxCandidate->xT )
    {
        xHasConstrained = pdTRUE;
    }

    if( xHasConstrained == pdTRUE )
    {
        return prvEDFAdmissionConstrained( pxCandidate, ppcReason );
    }

    return prvEDFAdmissionImplicit( pxCandidate, ppcReason );
}
#endif
```

#### 5.7.2 Implicit path (`U <= 1`)

> **Note:** When SRP is enabled, the formula becomes `sum((C_i + B_i) / T_i) <= 1` to account for SRP blocking. The code below already handles this: `xSRPBlockingBound` is 0 when SRP is disabled, so the formula gracefully degrades to `sum(C_i / T_i) <= 1`.

```c
#if ( configUSE_EDF_SCHEDULING == 1 )
/* Uses fixed-point micro-units to avoid floating-point math in kernel.
 * scale = 1,000,000 means utilization 1.0 == 1,000,000.
 * When SRP is enabled, blocking bound B is added to each task's C. */
static BaseType_t prvEDFAdmissionImplicit( const EDFAdmissionTaskParams_t * pxCandidate,
                                           const char ** ppcReason )
{
    const uint32_t ulScale = 1000000UL;
    uint64_t ullUtil = 0ULL;
    const ListItem_t * pxIt;

    for( pxIt = listGET_HEAD_ENTRY( &xEDFTaskRegistryList );
         pxIt != listGET_END_MARKER( &xEDFTaskRegistryList );
         pxIt = listGET_NEXT( pxIt ) )
    {
        const TCB_t * pxTCB = ( const TCB_t * ) listGET_LIST_ITEM_OWNER( pxIt );
        ullUtil += ( ( uint64_t ) ( pxTCB->xWcetTicks + pxTCB->xSRPBlockingBound ) * ulScale ) / pxTCB->xPeriod;
    }

    ullUtil += ( ( uint64_t ) ( pxCandidate->xC + pxCandidate->xB ) * ulScale ) / pxCandidate->xT;

    if( ullUtil <= ulScale )
    {
        *ppcReason = "implicit U<=1";
        return pdPASS;
    }

    *ppcReason = "implicit U>1";
    return pdFAIL;
}
#endif
```

#### 5.7.3 Constrained path (exact dbf)

> **Note:** This code has been corrected from the original version. See [Section 6.10](#610-admission-control-bug-fixes-and-corrections) for full details on the three bugs that were fixed (horizon computation, loop start point, and SRP blocking bound ceiling check).

```c
/* GCD / LCM helpers for horizon computation. */
#if ( configUSE_EDF_SCHEDULING == 1 )
static TickType_t prvGCD( TickType_t a, TickType_t b )
{
    while( b != 0U )
    {
        TickType_t t = b;
        b = a % b;
        a = t;
    }
    return a;
}

static TickType_t prvLCM( TickType_t a, TickType_t b )
{
    if( ( a == 0U ) || ( b == 0U ) ) return 0U;
    return ( a / prvGCD( a, b ) ) * b;
}
#endif

#if ( configUSE_EDF_SCHEDULING == 1 )
/* Exact processor-demand test (DBF).
 *
 * Horizon: LCM of all task periods, capped at configEDF_MAX_ANALYSIS_TICKS.
 * Loop range: [min(D_i), horizon] -- skips t < min(D_i) where DBF = 0
 *             and the blocking term alone would cause spurious failures.
 * Blocking: max(B_i) across all tasks is added to demand at each t. */
static BaseType_t prvEDFAdmissionConstrained( const EDFAdmissionTaskParams_t * pxCandidate,
                                              const char ** ppcReason )
{
    TickType_t xTTest;
    const ListItem_t * pxIt;

    /* Horizon: LCM of all task periods, capped. */
    TickType_t xHorizon = pxCandidate->xT;
    for( pxIt = listGET_HEAD_ENTRY( &xEDFTaskRegistryList );
         pxIt != listGET_END_MARKER( &xEDFTaskRegistryList );
         pxIt = listGET_NEXT( pxIt ) )
    {
        const TCB_t * pxTCB = ( const TCB_t * ) listGET_LIST_ITEM_OWNER( pxIt );
        xHorizon = prvLCM( xHorizon, pxTCB->xPeriod );
        if( xHorizon > configEDF_MAX_ANALYSIS_TICKS )
        {
            xHorizon = configEDF_MAX_ANALYSIS_TICKS;
            break;
        }
    }

    /* Minimum deadline across all tasks (including candidate). */
    TickType_t xMinDeadline = pxCandidate->xD;
    for( pxIt = listGET_HEAD_ENTRY( &xEDFTaskRegistryList );
         pxIt != listGET_END_MARKER( &xEDFTaskRegistryList );
         pxIt = listGET_NEXT( pxIt ) )
    {
        const TCB_t * pxTCB = ( const TCB_t * ) listGET_LIST_ITEM_OWNER( pxIt );
        if( pxTCB->xRelativeDeadline < xMinDeadline )
            xMinDeadline = pxTCB->xRelativeDeadline;
    }

    /* Evaluate demand at each integer t in [min(D_i), horizon]. */
    for( xTTest = xMinDeadline; xTTest <= xHorizon; xTTest++ )
    {
        uint64_t ullDemand = 0ULL;
        TickType_t xMaxBlocking = pxCandidate->xB;

        /* Demand from already admitted tasks. */
        for( pxIt = listGET_HEAD_ENTRY( &xEDFTaskRegistryList );
             pxIt != listGET_END_MARKER( &xEDFTaskRegistryList );
             pxIt = listGET_NEXT( pxIt ) )
        {
            const TCB_t * pxTCB = ( const TCB_t * ) listGET_LIST_ITEM_OWNER( pxIt );

            /* dbf_i(t) = max(0, floor((t-D)/T)+1) * C */
            if( xTTest >= pxTCB->xRelativeDeadline )
            {
                uint64_t ullJobs = ( ( uint64_t ) ( xTTest - pxTCB->xRelativeDeadline ) / pxTCB->xPeriod ) + 1ULL;
                ullDemand += ullJobs * pxTCB->xWcetTicks;
            }

            if( pxTCB->xSRPBlockingBound > xMaxBlocking )
                xMaxBlocking = pxTCB->xSRPBlockingBound;
        }

        /* Demand from candidate task. */
        if( xTTest >= pxCandidate->xD )
        {
            uint64_t ullCandidateJobs = ( ( uint64_t ) ( xTTest - pxCandidate->xD ) / pxCandidate->xT ) + 1ULL;
            ullDemand += ullCandidateJobs * pxCandidate->xC;
        }

        /* If demand + blocking exceeds interval length, set is infeasible. */
        if( ( ullDemand + ( uint64_t ) xMaxBlocking ) > ( uint64_t ) xTTest )
        {
            *ppcReason = "constrained DBF+B>t";
            return pdFAIL;
        }
    }

    *ppcReason = "constrained DBF<=t";
    return pdPASS;
}
#endif
```

### 5.8 Ready List + Scheduler Selection

Update task insertion macro so EDF tasks go to sorted EDF ready list.

```c
#if ( configUSE_EDF_SCHEDULING == 1 )
    #define prvAddTaskToReadyList( pxTCB )                                              \
    do {                                                                                 \
        traceMOVED_TASK_TO_READY_STATE( pxTCB );                                         \
                                                                                         \
        if( ( pxTCB )->xPeriod > 0U )                                                    \
        {                                                                                \
            /* Store absolute deadline in list item so vListInsert sorts by deadline. */ \
            listSET_LIST_ITEM_VALUE( &( ( pxTCB )->xStateListItem ),                    \
                                     ( pxTCB )->xAbsoluteDeadline );                     \
                                                                                         \
            /* EDF queue is always deadline-sorted in ascending order. */                \
            vListInsert( &xEDFReadyList, &( ( pxTCB )->xStateListItem ) );               \
        }                                                                                \
        else                                                                             \
        {                                                                                \
            /* Non-EDF tasks (idle/timer) keep default behavior. */                     \
            taskRECORD_READY_PRIORITY( ( pxTCB )->uxPriority );                          \
            listINSERT_END( &( pxReadyTasksLists[ ( pxTCB )->uxPriority ] ),            \
                            &( ( pxTCB )->xStateListItem ) );                            \
        }                                                                                \
                                                                                         \
        tracePOST_MOVED_TASK_TO_READY_STATE( pxTCB );                                    \
    } while( 0 )
#endif
```

Update task pick macro to choose EDF head.

```c
#if ( configUSE_EDF_SCHEDULING == 1 )
    #define taskSELECT_HIGHEST_PRIORITY_TASK()                                        \
    do {                                                                              \
        if( listLIST_IS_EMPTY( &xEDFReadyList ) == pdFALSE )                         \
        {                                                                             \
            /* Head owner has earliest deadline -> run it now. */                    \
            pxCurrentTCB = ( TCB_t * ) listGET_OWNER_OF_HEAD_ENTRY( &xEDFReadyList );\
        }                                                                             \
        else                                                                          \
        {                                                                             \
            /* Fall back to idle when no EDF task is ready. */                       \
            pxCurrentTCB = ( TCB_t * ) listGET_OWNER_OF_HEAD_ENTRY(                  \
                &( pxReadyTasksLists[ tskIDLE_PRIORITY ] ) );                        \
        }                                                                             \
    } while( 0 )
#endif
```

Initialize EDF lists in `prvInitialiseTaskLists()`.

```c
#if ( configUSE_EDF_SCHEDULING == 1 )
    /* Must initialize both EDF lists before any EDF task creation call. */
    vListInitialise( &xEDFReadyList );
    vListInitialise( &xEDFTaskRegistryList );
#endif
```

### 5.9 Tick Processing: Preemption + Deadline-Miss Drop Policy

#### 5.9.1 Preemption check (deadline-based)

In `xTaskIncrementTick()`, replace priority comparison with EDF comparison:

```c
#if ( configUSE_EDF_SCHEDULING == 1 )
    /* If a newly readied EDF task has earlier deadline than current EDF task,
     * request a context switch immediately. */
    if( pxTCB->xPeriod > 0U )
    {
        if( ( pxCurrentTCB->xPeriod == 0U ) ||
            ( pxTCB->xAbsoluteDeadline < pxCurrentTCB->xAbsoluteDeadline ) )
        {
            xSwitchRequired = pdTRUE;
            edfTRACE( "[EDF][tick=%lu][preempt] in=%s dl=%lu out=%s dl=%lu\r\n",
                      ( unsigned long ) xTickCount,
                      pxTCB->pcTaskName,
                      ( unsigned long ) pxTCB->xAbsoluteDeadline,
                      pxCurrentTCB->pcTaskName,
                      ( unsigned long ) pxCurrentTCB->xAbsoluteDeadline );
        }
    }
#else
    if( pxTCB->uxPriority > pxCurrentTCB->uxPriority )
    {
        xSwitchRequired = pdTRUE;
    }
#endif
```

Disable time-slicing in EDF mode:

```c
#if ( configUSE_EDF_SCHEDULING == 0 )
    #if ( ( configUSE_PREEMPTION == 1 ) && ( configUSE_TIME_SLICING == 1 ) )
    {
        if( listCURRENT_LIST_LENGTH( &( pxReadyTasksLists[ pxCurrentTCB->uxPriority ] ) ) > 1U )
        {
            xSwitchRequired = pdTRUE;
        }
    }
    #endif
#endif
```

#### 5.9.2 Drop-late-job policy helper

```c
#if ( configUSE_EDF_SCHEDULING == 1 )
/* Drop current late job immediately and move task to next job release.
 * This is your chosen transient-overload policy. */
static void prvEDFDropLateJob( TCB_t * pxTCB, TickType_t xNow )
{
    /* Emit trace before state update for easier debugging timeline. */
    edfTRACE( "[EDF][tick=%lu][drop] task=%s missed_deadline=%lu consumed=%lu wcet=%lu\r\n",
              ( unsigned long ) xNow,
              pxTCB->pcTaskName,
              ( unsigned long ) pxTCB->xAbsoluteDeadline,
              ( unsigned long ) pxTCB->xJobExecTicks,
              ( unsigned long ) pxTCB->xWcetTicks );

    /* Advance to next job window. */
    pxTCB->xLastReleaseTick += pxTCB->xPeriod;
    pxTCB->xAbsoluteDeadline = pxTCB->xLastReleaseTick + pxTCB->xRelativeDeadline;
    pxTCB->xJobExecTicks = 0U;

    /* Remove from ready state if needed, then delay until next period. */
    if( listIS_CONTAINED_WITHIN( &xEDFReadyList, &( pxTCB->xStateListItem ) ) != pdFALSE )
    {
        ( void ) uxListRemove( &( pxTCB->xStateListItem ) );
    }

    /* Reinsert through delayed path for next release boundary. */
    prvAddCurrentTaskToDelayedList( pxTCB->xPeriod, pdFALSE );
}
#endif
```

#### 5.9.3 Where to call drop check

Add this in tick path (after tick increments and before/around schedule decision) for current EDF task:

```c
#if ( configUSE_EDF_SCHEDULING == 1 )
    if( ( pxCurrentTCB->xPeriod > 0U ) && ( xTickCount > pxCurrentTCB->xAbsoluteDeadline ) )
    {
        /* Deadline passed while job still active -> immediate drop policy. */
        prvEDFDropLateJob( pxCurrentTCB, xTickCount );
        xSwitchRequired = pdTRUE;
    }
#endif
```

### 5.10 Period Boundary Helper

Keep `vTaskDelayUntilNextPeriod()` but make it update per-job metadata and trace release/finish.

```c
#if ( configUSE_EDF_SCHEDULING == 1 )
void vTaskDelayUntilNextPeriod( TickType_t * pxPreviousWakeTime )
{
    TCB_t * pxTCB = pxCurrentTCB;
    TickType_t xNow = xTaskGetTickCount();

    /* Job finished point for tracing. */
    edfTRACE( "[EDF][tick=%lu][finish] task=%s dl=%lu exec=%lu\r\n",
              ( unsigned long ) xNow,
              pxTCB->pcTaskName,
              ( unsigned long ) pxTCB->xAbsoluteDeadline,
              ( unsigned long ) pxTCB->xJobExecTicks );

    /* Move release/deadline to next job. */
    pxTCB->xLastReleaseTick += pxTCB->xPeriod;
    pxTCB->xAbsoluteDeadline = pxTCB->xLastReleaseTick + pxTCB->xRelativeDeadline;
    pxTCB->xJobExecTicks = 0U;

    /* Delay until next release to preserve periodic behavior. */
    vTaskDelayUntil( pxPreviousWakeTime, pxTCB->xPeriod );

    /* New job release point for tracing after wake. */
    edfTRACE( "[EDF][tick=%lu][release] task=%s new_dl=%lu\r\n",
              ( unsigned long ) xTaskGetTickCount(),
              pxTCB->pcTaskName,
              ( unsigned long ) pxTCB->xAbsoluteDeadline );
}
#endif
```

### 5.11 UART Trace Setup (`CMakeLists.txt`)

For `main_edf_test` target in RP2040 Standard demo CMake:

```cmake
# Route EDF logs to UART to reduce USB scheduling jitter in traces.
pico_enable_stdio_uart(main_edf_test 1)
pico_enable_stdio_usb(main_edf_test 0)
```

In `main.c`, keep `stdio_init_all();` in hardware init so UART stdio is initialized by Pico SDK.

### 5.12 Integrated Test Design (`main_edf_test.c`)

You should update the EDF test to verify all required scenarios (accept, reject, runtime add, miss/drop).

```c
#include "FreeRTOS.h"
#include "task.h"
#include "pico/stdlib.h"

/* Test pins to visualize key tasks on a logic analyzer. */
#define PIN_TASK_IMPLICIT   2
#define PIN_TASK_CONSTR     3

/* Forward declarations for task bodies. */
static void vImplicitTask( void * pvParameters );
static void vConstrainedTask( void * pvParameters );
static void vRuntimeCreatorTask( void * pvParameters );

/* Simple periodic task with D=T case for implicit admission path. */
static void vImplicitTask( void * pvParameters )
{
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        gpio_put( PIN_TASK_IMPLICIT, 1 );

        /* Simulate bounded execution below WCET. */
        TickType_t xStart = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xStart ) < pdMS_TO_TICKS( 30 ) )
        {
            /* Busy compute for deterministic demo behavior. */
        }

        gpio_put( PIN_TASK_IMPLICIT, 0 );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

/* Constrained task with D<T to force exact DBF admission path. */
static void vConstrainedTask( void * pvParameters )
{
    TickType_t xLastWake = xTaskGetTickCount();

    for( ;; )
    {
        gpio_put( PIN_TASK_CONSTR, 1 );

        TickType_t xStart = xTaskGetTickCount();
        while( ( xTaskGetTickCount() - xStart ) < pdMS_TO_TICKS( 20 ) )
        {
            /* Busy compute for constrained workload. */
        }

        gpio_put( PIN_TASK_CONSTR, 0 );
        vTaskDelayUntilNextPeriod( &xLastWake );
    }
}

/* Runtime creator validates "add task while running" and admission rejection path. */
static void vRuntimeCreatorTask( void * pvParameters )
{
    /* Wait so scheduler is clearly running before runtime add. */
    vTaskDelay( pdMS_TO_TICKS( 2000 ) );

    /* This one should typically pass if your base set has slack. */
    ( void ) xTaskCreateEDF( vImplicitTask,
                             "RT_ADD_OK",
                             256,
                             NULL,
                             pdMS_TO_TICKS( 300 ),
                             pdMS_TO_TICKS( 300 ),
                             pdMS_TO_TICKS( 60 ),
                             NULL );

    /* This one is intentionally heavy to trigger admission reject. */
    ( void ) xTaskCreateEDF( vConstrainedTask,
                             "RT_ADD_REJECT",
                             256,
                             NULL,
                             pdMS_TO_TICKS( 100 ),
                             pdMS_TO_TICKS( 80 ),
                             pdMS_TO_TICKS( 95 ),
                             NULL );

    /* Optionally force overload/miss by spinning too long in this task or others,
     * then confirm kernel emits [drop] trace and skips late job immediately. */
    for( ;; )
    {
        vTaskDelay( pdMS_TO_TICKS( 1000 ) );
    }
}

void main_edf_test( void )
{
    gpio_init( PIN_TASK_IMPLICIT );
    gpio_set_dir( PIN_TASK_IMPLICIT, GPIO_OUT );
    gpio_init( PIN_TASK_CONSTR );
    gpio_set_dir( PIN_TASK_CONSTR, GPIO_OUT );

    /* Print startup task table so demo clearly shows all (C,T,D) values. */
    printf( "[EDF][startup] Creating initial task set...\r\n" );

    /* Implicit case: D = T -> utilization admission path. */
    ( void ) xTaskCreateEDF( vImplicitTask,
                             "IMPLICIT_A",
                             256,
                             NULL,
                             pdMS_TO_TICKS( 200 ),
                             pdMS_TO_TICKS( 200 ),
                             pdMS_TO_TICKS( 40 ),
                             NULL );

    /* Constrained case: D < T -> exact DBF path. */
    ( void ) xTaskCreateEDF( vConstrainedTask,
                             "CONSTR_B",
                             256,
                             NULL,
                             pdMS_TO_TICKS( 300 ),
                             pdMS_TO_TICKS( 150 ),
                             pdMS_TO_TICKS( 35 ),
                             NULL );

    /* Runtime creator task itself can be normal FreeRTOS or EDF.
     * Using xTaskCreate keeps this helper outside EDF admission accounting. */
    ( void ) xTaskCreate( vRuntimeCreatorTask,
                          "RUNTIME_CREATOR",
                          256,
                          NULL,
                          tskIDLE_PRIORITY + 1U,
                          NULL );

    vTaskStartScheduler();

    for( ;; )
    {
        /* Should never execute unless scheduler fails to start. */
    }
}
```

### 5.13 Required Event Logs Checklist

Make sure your logs include all of these (assignment + your request):
- startup task table (`C`, `T`, `D`, first absolute deadline),
- admission result for each create request (accept/reject + reason),
- runtime task add events,
- job release,
- job finish,
- preemption,
- resume after preemption,
- deadline miss + late job dropped.

### 5.14 End-of-Section Checklist (Task 1 Complete When All True)

- EDF dispatch is active when `configUSE_EDF_SCHEDULING == 1`.
- Fixed-priority behavior still works when EDF config is `0`.
- Admission path correctly chooses implicit vs constrained test.
- Runtime task creation is atomically admitted/rejected.
- Late jobs are dropped immediately once deadline is exceeded.
- UART trace clearly shows admission and runtime events.
- `main_edf_test.c` demonstrates pass/fail/runtime-add/drop scenarios.

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

This is exactly why SRP fits your new EDF implementation well: you can model a bounded SRP blocking term during **admission control**, still allow **runtime task creation** safely, and keep runtime behavior deterministic when combined with your **deadline-miss drop** policy.

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
| `FreeRTOS/Source/tasks.c` | Keep SRP data in TCB (`uxPreemptionLevel`, SRP blocking metadata) and preserve your dynamic-ceiling implementation | Scheduler + analysis state |
| `FreeRTOS/Source/tasks.c` | Keep SRP resource registry (`SRPResourceEntry_t`) with multi-unit resources and per-task unit demand | Preserve your custom SRP algorithm |
| `FreeRTOS/Source/tasks.c` | Integrate SRP blocking into EDF admission helpers (`prvEDFAdmissionControl`, implicit path, constrained path) | Admission must reflect EDF+SRP |
| `FreeRTOS/Source/tasks.c` | Keep SRP gate in preemption and task selection; keep late-drop logic from EDF section | Runtime correctness |
| `FreeRTOS/Source/tasks.c` | Add SRP-aware trace logs aligned with EDF traces (admission, preempt/resume, drop, resource take/give) | Demo/debug visibility |
| `FreeRTOS/Source/include/task.h` | Keep/extend SRP API declarations and any SRP admission metadata API you expose | User-facing API |
| `FreeRTOS/Source/include/FreeRTOS.h` | Keep defaults for `configUSE_SRP` / stack sharing toggles | Feature gating |
| `Demo/.../FreeRTOSConfig.h` | Ensure `configUSE_EDF_SCHEDULING`, `configUSE_SRP`, trace macros are enabled for test profile | Test setup |

### 6.5 Step-by-Step Implementation (Updated for Your New EDF Code)

> **Goal:** Keep SRP algorithm behavior unchanged, but make it fully consistent with your new EDF admission control, runtime task creation, overload drop policy, and trace requirements.

#### Step 1: Configuration and Build-Time Switches

**`FreeRTOSConfig.h` (demo profile):**
```c
#define configUSE_EDF_SCHEDULING    1
#define configUSE_SRP               1

/* Keep enabled while validating behavior. */
#define configEDF_TRACE_ENABLE      1
#define configPRINTF( x )           printf x

/* Optional feature if you implemented stack sharing. */
#define configSRP_STACK_SHARING     1
```

**`FreeRTOS.h` defaults:**
```c
#ifndef configUSE_SRP
    #define configUSE_SRP    0
#endif

#ifndef configSRP_STACK_SHARING
    #define configSRP_STACK_SHARING    0
#endif
```

This keeps all fallback behavior intact when SRP is disabled.

#### Step 2: Keep Required Task Metadata (`T`, `D`, `C`, `B`, Preemption Level)

Admission and runtime scheduling now depend on a complete metadata set:
- `T`: period
- `D`: relative deadline
- `C`: WCET
- `B`: SRP worst-case blocking bound
- `pi`: SRP preemption level

**`tasks.c` (inside `TCB_t`, representative):**
```c
#if ( configUSE_EDF_SCHEDULING == 1 )
    TickType_t xPeriod;
    TickType_t xRelativeDeadline;
    TickType_t xAbsoluteDeadline;
    TickType_t xWCET;
#endif

#if ( configUSE_SRP == 1 )
    UBaseType_t uxPreemptionLevel;
    TickType_t xSRPBlockingBound;
#endif

#if ( configSRP_STACK_SHARING == 1 )
    TickType_t xSavedWakeTime;
#endif
```

If your code stores these in a different structure, keep that layout; the requirement is equivalent data availability at admission/runtime.

#### Step 3: Preserve Your Dynamic-Ceiling Multi-Unit SRP Core

Keep your existing SRP resource model and do **not** simplify it to classic fixed-ceiling binary locks.

For each resource `r` with available units `a_r`:

```text
ceiling(r) = max preemption level among registered users i
             where units_needed(i, r) > a_r

system_ceiling = max over all resources r of ceiling(r)
```

That is your core algorithm and should remain unchanged.

#### Step 4: SRP Resource API (Create/Register/Take/Give) with Trace Hooks

Keep existing APIs and add trace points so events line up with EDF timeline logs.

**`task.h` declarations (representative):**
```c
#if ( configUSE_SRP == 1 )
    SemaphoreHandle_t xSRPResourceCreate( UBaseType_t uxMaxUnits );
    void vSRPResourceRegisterUser( SemaphoreHandle_t xResource,
                                   UBaseType_t uxPreemptionLevel,
                                   UBaseType_t uxUnitsNeeded );
    BaseType_t xSRPResourceTake( SemaphoreHandle_t xResource,
                                 UBaseType_t uxUnits );
    void vSRPResourceGive( SemaphoreHandle_t xResource,
                           UBaseType_t uxUnits );
#endif
```

**`tasks.c` implementation pattern (representative):**
```c
#if ( configUSE_SRP == 1 )
BaseType_t xSRPResourceTake( SemaphoreHandle_t xResource,
                             UBaseType_t uxUnits )
{
    SRPResourceEntry_t * pxRes = prvFindResource( xResource );
    configASSERT( pxRes != NULL );

    taskENTER_CRITICAL();
    {
        configASSERT( uxUnits <= pxRes->uxAvailableUnits );

        for( UBaseType_t i = 0; i < uxUnits; i++ )
        {
            BaseType_t xTaken = xSemaphoreTake( xResource, 0 );
            configASSERT( xTaken == pdTRUE );
        }

        pxRes->uxAvailableUnits -= uxUnits;
        prvRecalculateSystemCeiling();

        #if ( configEDF_TRACE_ENABLE == 1 )
        configPRINTF(("[SRP] TAKE task=%s res=%p units=%u avail=%u sysceil=%u\r\n",
                      pcTaskGetName( NULL ),
                      ( void * ) xResource,
                      ( unsigned ) uxUnits,
                      ( unsigned ) pxRes->uxAvailableUnits,
                      ( unsigned ) uxSystemCeiling ));
        #endif
    }
    taskEXIT_CRITICAL();

    return pdTRUE;
}

void vSRPResourceGive( SemaphoreHandle_t xResource,
                       UBaseType_t uxUnits )
{
    SRPResourceEntry_t * pxRes = prvFindResource( xResource );
    configASSERT( pxRes != NULL );

    taskENTER_CRITICAL();
    {
        configASSERT( ( pxRes->uxAvailableUnits + uxUnits ) <= pxRes->uxTotalUnits );

        for( UBaseType_t i = 0; i < uxUnits; i++ )
        {
            xSemaphoreGive( xResource );
        }

        pxRes->uxAvailableUnits += uxUnits;
        prvRecalculateSystemCeiling();

        #if ( configEDF_TRACE_ENABLE == 1 )
        configPRINTF(("[SRP] GIVE task=%s res=%p units=%u avail=%u sysceil=%u\r\n",
                      pcTaskGetName( NULL ),
                      ( void * ) xResource,
                      ( unsigned ) uxUnits,
                      ( unsigned ) pxRes->uxAvailableUnits,
                      ( unsigned ) uxSystemCeiling ));
        #endif
    }
    taskEXIT_CRITICAL();
}
#endif
```

#### Step 5: Compute SRP Blocking Bound `B` for Admission

To combine EDF + SRP correctly, each task must have a conservative SRP blocking bound `B_i`.

A practical method in your design:
1. Keep resource-user registration table (already present).
2. Store/derive each critical section WCET estimate (per task or per task-resource usage).
3. Compute `B_i` as maximum lower-preemption-level interference relevant to task `i` under your ceiling model.

Store resulting bound in task metadata (`xSRPBlockingBound`) and candidate admission parameters.

#### Step 6: Extend Admission Candidate Struct

Your EDF candidate should explicitly include SRP blocking:

```c
typedef struct EDFAdmissionTaskParams
{
    TickType_t xPeriod;             /* T */
    TickType_t xRelativeDeadline;   /* D */
    TickType_t xWCET;               /* C */
    TickType_t xBlocking;           /* B from SRP */
} EDFAdmissionTaskParams_t;
```

Then `prvEDFAdmissionControl()` dispatch remains the same style as Section 5, but both admission paths consume `xBlocking`.

#### Step 7: Implicit Path Admission (`D = T`) Must Use `(C+B)/T`

For implicit-deadline systems, your fixed-point utilization check should account for blocking:

```text
sum_i (C_i + B_i) / T_i <= 1
```

Representative kernel implementation:

```c
static BaseType_t prvEDFAdmissionImplicit( const EDFAdmissionTaskParams_t * pxCandidate,
                                           const char ** ppcReason )
{
    const uint32_t ulScale = 1000000UL;
    uint64_t ullUtil = 0;

    for( each admitted EDF task j )
    {
        ullUtil += ( ( uint64_t ) ( pxTCBj->xWCET + pxTCBj->xSRPBlockingBound ) * ulScale )
                   / ( uint64_t ) pxTCBj->xPeriod;
    }

    ullUtil += ( ( uint64_t ) ( pxCandidate->xWCET + pxCandidate->xBlocking ) * ulScale )
               / ( uint64_t ) pxCandidate->xPeriod;

    if( ullUtil <= ulScale )
    {
        #if ( configEDF_TRACE_ENABLE == 1 )
        configPRINTF(("[ADM][IMPLICIT][PASS] util=%llu/1000000\r\n", ullUtil ));
        #endif
        return pdPASS;
    }

    *ppcReason = "implicit admission failed: sum((C+B)/T) > 1";
    #if ( configEDF_TRACE_ENABLE == 1 )
    configPRINTF(("[ADM][IMPLICIT][FAIL] util=%llu reason=%s\r\n", ullUtil, *ppcReason ));
    #endif
    return pdFAIL;
}
```

#### Step 8: Constrained Path Admission (`D <= T`) Must Include Blocking in DBF Test

For constrained deadlines, keep your exact test style and add blocking term:

```text
For each tested time point t in deadline grid:
    demand(t) + blocking_term(t) <= t
```

Representative structure:

```c
static BaseType_t prvEDFAdmissionConstrained( const EDFAdmissionTaskParams_t * pxCandidate,
                                              const char ** ppcReason )
{
    for( each deadline-grid point t )
    {
        uint64_t ullDemand = 0;
        TickType_t xBlocking = 0;

        for( each admitted task j and candidate )
        {
            uint64_t ullJobs = 0;
            if( t >= Dj )
            {
                ullJobs = ( ( t - Dj ) / Tj ) + 1U;
            }

            ullDemand += ullJobs * Cj;

            if( Bj > xBlocking )
            {
                xBlocking = Bj;
            }
        }

        if( ( ullDemand + xBlocking ) > t )
        {
            *ppcReason = "constrained admission failed: DBF + B exceeds test point";
            #if ( configEDF_TRACE_ENABLE == 1 )
            configPRINTF(("[ADM][CONSTR][FAIL] t=%u demand=%llu B=%u\r\n",
                          ( unsigned ) t,
                          ullDemand,
                          ( unsigned ) xBlocking ));
            #endif
            return pdFAIL;
        }
    }

    #if ( configEDF_TRACE_ENABLE == 1 )
    configPRINTF(("[ADM][CONSTR][PASS]\r\n"));
    #endif
    return pdPASS;
}
```

#### Step 9: Integrate SRP Blocking in `xTaskCreateEDF` Admission (Boot + Runtime)

Your updated create path should:
1. Build candidate `(T, D, C, B)`.
2. Run admission dispatch.
3. Accept/reject atomically.
4. Emit trace message with reason.

Representative flow:

```c
BaseType_t xTaskCreateEDF( TaskFunction_t pxTaskCode,
                           const char * const pcName,
                           const configSTACK_DEPTH_TYPE uxStackDepth,
                           void * const pvParameters,
                           TickType_t xPeriod,
                           TickType_t xRelativeDeadline,
                           TickType_t xWCET,
                           TaskHandle_t * const pxCreatedTask )
{
    BaseType_t xReturn = pdFAIL;
    const char * pcReason = "unset";

    EDFAdmissionTaskParams_t xCandidate;
    xCandidate.xPeriod = xPeriod;
    xCandidate.xRelativeDeadline = xRelativeDeadline;
    xCandidate.xWCET = xWCET;

    #if ( configUSE_SRP == 1 )
    xCandidate.xBlocking = prvComputeCandidateSRPBlockingBound( pcName, xPeriod, xRelativeDeadline, xWCET );
    #else
    xCandidate.xBlocking = 0;
    #endif

    taskENTER_CRITICAL();
    {
        if( prvEDFAdmissionControl( &xCandidate, &pcReason ) == pdPASS )
        {
            xReturn = prvCreateAndInsertEDFTask( pxTaskCode,
                                                 pcName,
                                                 uxStackDepth,
                                                 pvParameters,
                                                 xPeriod,
                                                 xRelativeDeadline,
                                                 xWCET,
                                                 xCandidate.xBlocking,
                                                 pxCreatedTask );
        }
        else
        {
            xReturn = pdFAIL;
        }
    }
    taskEXIT_CRITICAL();

    #if ( configEDF_TRACE_ENABLE == 1 )
    configPRINTF(("[ADM][%s] task=%s T=%u D=%u C=%u B=%u reason=%s\r\n",
                  ( xReturn == pdPASS ) ? "ACCEPT" : "REJECT",
                  pcName,
                  ( unsigned ) xPeriod,
                  ( unsigned ) xRelativeDeadline,
                  ( unsigned ) xWCET,
                  ( unsigned ) xCandidate.xBlocking,
                  pcReason ));
    #endif

    return xReturn;
}
```

Because `xTaskCreateEDF` can be called while running, this automatically supports **runtime admission** with SRP-aware analysis.

#### Step 10: Keep SRP Gate in Both Preemption and Scheduler Pick

In `xTaskIncrementTick()` (or equivalent wake-up path), keep:

```c
if( pxTCB->uxPreemptionLevel > uxSystemCeiling )
{
    /* Existing EDF deadline comparison logic from Section 5 */
}
else
{
    #if ( configEDF_TRACE_ENABLE == 1 )
    configPRINTF(("[SRP][BLOCK] task=%s level=%u ceiling=%u\r\n",
                  pcTaskGetName( ( TaskHandle_t ) pxTCB ),
                  ( unsigned ) pxTCB->uxPreemptionLevel,
                  ( unsigned ) uxSystemCeiling ));
    #endif
}
```

In `taskSELECT_HIGHEST_PRIORITY_TASK()` for EDF mode, skip ready tasks that fail SRP gate (`level <= ceiling`).

#### Step 11: Keep Deadline-Miss Drop Policy, Add SRP Context in Logs

Do not change your late-drop policy. Just make logs richer:

```c
#if ( configEDF_TRACE_ENABLE == 1 )
configPRINTF(("[DROP] task=%s now=%u abs=%u level=%u sysceil=%u\r\n",
              pcTaskGetName( ( TaskHandle_t ) pxCurrentTCB ),
              ( unsigned ) xTickCount,
              ( unsigned ) pxCurrentTCB->xAbsoluteDeadline,
              ( unsigned ) pxCurrentTCB->uxPreemptionLevel,
              ( unsigned ) uxSystemCeiling ));
#endif
```

This preserves behavior while improving diagnosis.

#### Step 12: Trace Events You Must Print

To match your updated EDF requirement, include:

1. Task table before scheduler start (`T,D,C,B,level`).
2. New task create request (boot/runtime).
3. Admission result + reason + admission path.
4. Job release.
5. Job finish.
6. Preempt event.
7. Resume event.
8. Late drop event.
9. SRP resource take/give (units + availability + system ceiling).
10. SRP block-at-dispatch (`level <= system_ceiling`).

A consistent prefix scheme helps grading and debugging:

```text
[TASK_TABLE]
[ADM][IMPLICIT][PASS/FAIL]
[ADM][CONSTR][PASS/FAIL]
[SRP] TAKE
[SRP] GIVE
[SRP][BLOCK]
[JOB][RELEASE]
[JOB][FINISH]
[PREEMPT]
[RESUME]
[DROP]
```

#### Step 13: Stack Sharing (If Enabled)

Keep your existing stack-sharing algorithm and add/keep these notes:

```c
/* SRP stack-sharing rule:
 * Tasks at same preemption level do not execute concurrently under SRP,
 * so one physical stack may be shared.
 * Persistent state must live in TCB/global data (not local stack). */
```

For periodic EDF helper, preserve Section 5 behavior and ensure persistent wake metadata is safe under shared stack mode.

#### Step 14: Required Test Scenarios for SRP+EDF

Your SRP tests should now include these scenarios explicitly:

1. Accepted boot-time EDF+SRP task set.
2. Rejected boot-time EDF+SRP task set.
3. Runtime accepted task insertion.
4. Runtime rejected task insertion.
5. SRP blocks an otherwise-ready task due to `level <= ceiling`.
6. Late job is dropped under overload.
7. At least one constrained-deadline (`D<T`) case uses constrained path.
8. At least one implicit-deadline (`D=T`) case uses implicit path.

### 6.6 Common Integration Bugs (and How to Avoid Them)

1. **Admission ignores `B` for runtime-created task.**
   - Fix: compute candidate `B` inside every `xTaskCreateEDF` call.

2. **SRP gate in tick path only, not in task-pick path.**
   - Fix: apply gate in both places.

3. **Idle task edge case breaks EDF comparison.**
   - Fix: keep your guard for non-EDF current task (`xPeriod == 0`).

4. **System ceiling not recomputed after every take/give.**
   - Fix: always recalculate in the same critical section as unit update.

5. **Trace says admitted but task creation failed later.**
   - Fix: log final status after both admission + create path finalize.

6. **Stack-sharing tasks keep state in locals.**
   - Fix: move persistent state to TCB/global storage.

### 6.7 Summary of All Changes for SRP (Updated)

| File | Change |
|------|--------|
| `tasks.c` | Keep dynamic-ceiling multi-unit SRP implementation unchanged in algorithm |
| `tasks.c` | Keep SRP gate in preemption and EDF task selection |
| `tasks.c` | Extend admission candidate with SRP blocking bound `B` |
| `tasks.c` | Update implicit admission to use `(C+B)/T` |
| `tasks.c` | Update constrained admission DBF test to include blocking term |
| `tasks.c` | Ensure `xTaskCreateEDF` performs SRP-aware admission for boot/runtime creation |
| `tasks.c` | Keep late-drop behavior; add SRP context to trace logs |
| `tasks.c` | Add SRP trace events for take/give and SRP dispatch blocking |
| `tasks.c` | Keep stack-sharing rules and persistent-state handling (if enabled) |
| `task.h` | Keep/extend SRP API declarations and related metadata APIs |
| `FreeRTOS.h` | Keep SRP feature defaults |
| `FreeRTOSConfig.h` | Enable EDF+SRP+trace for SRP test builds |

### 6.8 End-of-Section Checklist (Task 2 Complete When All True)

- SRP algorithm behavior (dynamic ceiling, multi-unit resources, per-task unit demand) is preserved.
- EDF admission includes SRP blocking in both implicit and constrained paths.
- Runtime `xTaskCreateEDF` admission/rejection works with SRP enabled.
- Scheduler only dispatches tasks satisfying SRP gate (`level > system ceiling`).
- Late-job drop policy still works and logs SRP context.
- Trace output includes admission, release, finish, preempt, resume, drop, and SRP resource events.
- Stack-sharing constraints are documented and respected if that mode is enabled.

### 6.9 SRP Normal Test Script + Logic Analyzer Setup

Use this exact test flow for a clean SRP validation run on the RP2040 demo target.

#### Test goal

Validate all baseline SRP behaviors in one run:
1. EDF+SRP boot-time admission.
2. Runtime EDF task admission (one accepted, one rejected).
3. SRP resource take/give with dynamic system ceiling updates.
4. SRP dispatch blocking (`level <= system ceiling`).
5. Trace visibility for scheduling/resource events.

#### Expected GPIO channels in this test

- `GPIO10`: low-level SRP task (`SRP_LOW`)
- `GPIO11`: high-level SRP task (`SRP_HIGH`)
- `GPIO12`: background EDF task (`SRP_BG`)
- `GPIO13`: runtime-admitted EDF task (`RT_OK`)

#### PowerShell test script (copy/paste)

```powershell
# Run from workspace root: Raspberry_pico
Set-Location .\FreeRTOS\FreeRTOS\Demo\ThirdParty\Community-Supported-Demos\CORTEX_M0+_RP2040\Standard

# Configure and build EDF+SRP test target
cmake -S . -B .\build -G "Ninja" -DPICO_BOARD=pico
cmake --build .\build --target main_edf_test

# Flash (pick one method)
# Method A: copy UF2 when Pico is mounted as RPI-RP2
Copy-Item .\build\main_edf_test.uf2 E:\

# Method B (if picotool is installed and device is in BOOTSEL)
# picotool load .\build\main_edf_test.uf2 -f

# Serial monitor (pick one tool)
# screen / PuTTY / VS Code serial monitor at 115200 baud
```

#### Logic analyzer wiring setup

1. Connect analyzer GND to Pico GND.
2. Connect digital channels:
    - CH0 -> GPIO10
    - CH1 -> GPIO11
    - CH2 -> GPIO12
    - CH3 -> GPIO13
3. Recommended analyzer settings:
    - Sample rate: `>= 1 MHz`
    - Voltage threshold: `3.3V logic`
    - Capture duration: `>= 20 s`
4. Trigger suggestion:
    - Rising edge on CH1 (high-level task) or CH3 (runtime-admitted task).

#### What you should observe

- `GPIO10` and `GPIO11` both pulse periodically, but `GPIO11` can be delayed while low task holds SRP resource (with matching `[SRP][BLOCK]` trace lines).
- `GPIO13` remains low until runtime creator adds `RT_OK`; then it starts periodic pulses.
- Serial logs should include:
  - `[SRP] TAKE` / `[SRP] GIVE`
  - `[SRP][BLOCK]`
  - `[EDF][admission] ... ACCEPT/REJECT`
  - runtime add summary (`add_ok` and `add_reject`)

If all observations match, your normal SRP integration test is passing.

### 6.10 Admission Control Bug Fixes and Corrections

This section documents three bugs that were discovered in the EDF + SRP admission control implementation and the exact fixes applied to the kernel code.

---

#### Bug 1: Constrained-Path Loop Started at `t = 1` (Spurious Rejection)

**Symptom:** When SRP is enabled, the constrained-path admission test (`prvEDFAdmissionConstrained`) would reject task sets that are actually feasible. For example, `SRP_HIGH` (T=5000, D=2500, C=700) was being rejected even though the combined utilization of the task set is well below 1.

**Root cause:** The loop `for( xTTest = 1U; xTTest <= xHorizon; xTTest++ )` evaluated the demand-bound feasibility check at every integer starting from `t = 1`. With SRP blocking, the check is:

```
DBF(t) + max(B_i) <= t
```

At `t = 1`, no task has an absolute deadline yet, so `DBF(1) = 0`. But the blocking term `max(B_i) = 1200` (from the SRP_LOW task's critical section). The check becomes `0 + 1200 > 1`, which falsely fails.

**Why this is wrong:** The DBF feasibility test is only meaningful at **deadline instants** -- time points where at least one task has a deadline. For `t < min(D_i)`, no job has a deadline, so there is nothing to schedule and blocking is irrelevant. The standard result from the literature (Baruah, 2006) only tests at deadline instants `{k * T_i + D_i | i in tasks, k >= 0}`.

**Fix:** Start the loop from `min(D_i)` across all tasks (including the candidate) instead of `t = 1`. At any `t >= min(D_i)`, at least one task has a deadline, so the blocking term is meaningful and the check `DBF(t) + B <= t` is valid.

**Before:**
```c
for( xTTest = 1U; xTTest <= xHorizon; xTTest++ )
```

**After:**
```c
/* Find smallest deadline across all tasks. */
TickType_t xMinDeadline = pxCandidate->xD;
for( /* each admitted task pxTCB */ )
{
    if( pxTCB->xRelativeDeadline < xMinDeadline )
        xMinDeadline = pxTCB->xRelativeDeadline;
}

/* Test only from min(D_i) where the check is meaningful. */
for( xTTest = xMinDeadline; xTTest <= xHorizon; xTTest++ )
```

**Worked example with the fix:**
```
Task set: SRP_LOW (C=1600, T=7000, D=5500, B=0)
          SRP_HIGH candidate (C=700, T=5000, D=2500, B=1200)

min(D_i) = min(5500, 2500) = 2500
Loop starts at t = 2500.

t=2500: DBF_LOW=0, DBF_HIGH=700.  demand=700.   700 + 1200 = 1900 <= 2500  PASS
t=5500: DBF_LOW=1600, DBF_HIGH=700.  demand=2300. 2300 + 1200 = 3500 <= 5500  PASS
t=7500: DBF_LOW=1600, DBF_HIGH=1400. demand=3000. 3000 + 1200 = 4200 <= 7500  PASS
...all test points pass -> SRP_HIGH ADMITTED
```

**File changed:** `tasks.c` -- `prvEDFAdmissionConstrained()`

---

#### Bug 2: Constrained-Path Horizon Was Too Short

**Symptom:** For certain task sets, the analysis window could end before a feasibility violation becomes visible, causing an infeasible task set to be silently accepted.

**Root cause:** The horizon was computed as `max(T_i + D_i)` -- the deadline of the second job of the task with the largest `T + D`. This is not a theoretically correct upper bound for the demand-bound function analysis.

**Why this is wrong:** The standard result says the DBF test must be evaluated at all deadline instants up to the **hyperperiod** `H = lcm(T_1, ..., T_n)`. The expression `T_i + D_i` is just the second-job deadline of task `i` and can miss violations that only appear later.

**Fix:** Compute the horizon as the LCM of all task periods, capped at `configEDF_MAX_ANALYSIS_TICKS` (default: 100,000 ticks) to prevent runaway analysis time on the embedded target.

**Before:**
```c
TickType_t xHorizon = pxCandidate->xT + pxCandidate->xD;

for( /* each admitted task */ )
{
    if( ( pxTCB->xPeriod + pxTCB->xRelativeDeadline ) > xHorizon )
        xHorizon = pxTCB->xPeriod + pxTCB->xRelativeDeadline;
}
```

**After:**
```c
TickType_t xHorizon = pxCandidate->xT;

for( /* each admitted task pxTCB */ )
{
    xHorizon = prvLCM( xHorizon, pxTCB->xPeriod );

    if( xHorizon > configEDF_MAX_ANALYSIS_TICKS )
    {
        xHorizon = configEDF_MAX_ANALYSIS_TICKS;
        break;
    }
}
```

**New helper functions added to `tasks.c`:**
```c
static TickType_t prvGCD( TickType_t a, TickType_t b )
{
    while( b != 0U )
    {
        TickType_t t = b;
        b = a % b;
        a = t;
    }
    return a;
}

static TickType_t prvLCM( TickType_t a, TickType_t b )
{
    if( ( a == 0U ) || ( b == 0U ) ) return 0U;
    /* Divide first to reduce overflow risk. */
    return ( a / prvGCD( a, b ) ) * b;
}
```

**New config constant added:**
```c
/* FreeRTOS.h (default) */
#ifndef configEDF_MAX_ANALYSIS_TICKS
    #define configEDF_MAX_ANALYSIS_TICKS    100000U
#endif

/* FreeRTOSConfig.h (user setting) */
#define configEDF_MAX_ANALYSIS_TICKS 100000U
```

**Files changed:** `tasks.c` (helpers + horizon logic), `FreeRTOS.h` (default), `FreeRTOSConfig.h` (user override)

---

#### Bug 3: SRP Blocking Bound Missing Resource Ceiling Check

**Symptom:** The SRP blocking bound `B_i` could be overly conservative, potentially rejecting task sets that are actually feasible. In the worst case, a task would be assigned a large blocking bound from a resource that could never actually block it under SRP.

**Root cause:** `prvSRPComputeBlockingBoundForLevel()` checked whether each resource user had a preemption level lower than the task (`pi_j < pi_i`) but did **not** check whether the resource's ceiling was high enough to actually block the task (`ceil(R_k) >= pi_i`).

**Why this is wrong:** Under SRP, a task `tau_i` can only be blocked by a resource `R_k` if the resource's ceiling is at or above `tau_i`'s preemption level. This is because the system ceiling only rises to `ceil(R_k)` when `R_k` is held, and SRP blocks a task only when its preemption level is `<= system ceiling`. If `ceil(R_k) < pi_i`, the system ceiling cannot rise high enough to block `tau_i` via `R_k`.

The correct SRP blocking bound formula from the literature is:

```
B_i = max over all resources R_k, over all tasks tau_j where:
        pi_j < pi_i              (lower preemption level)
    AND ceil(R_k) >= pi_i        (resource ceiling can block task i)
  of CS(j, k)
```

where `ceil(R_k) = max preemption level among all registered users of R_k`.

**Fix:** Compute the **static ceiling** of each resource (max preemption level of any user) and skip resources whose ceiling is below the task's level.

**Before:**
```c
static TickType_t prvSRPComputeBlockingBoundForLevel( UBaseType_t uxTaskLevel )
{
    for( each resource )
    {
        for( each user of resource )
        {
            if( user.level < taskLevel && user.CS > bound )
                bound = user.CS;
        }
    }
    return bound;
}
```

**After:**
```c
static TickType_t prvSRPComputeBlockingBoundForLevel( UBaseType_t uxTaskLevel )
{
    for( each resource )
    {
        /* Compute static resource ceiling. */
        uxResourceCeiling = max( user.level for all users );

        /* Skip if ceiling too low to block this task. */
        if( uxResourceCeiling < uxTaskLevel )
            continue;

        for( each user of resource )
        {
            if( user.level < taskLevel && user.CS > bound )
                bound = user.CS;
        }
    }
    return bound;
}
```

**File changed:** `tasks.c` -- `prvSRPComputeBlockingBoundForLevel()`

---

#### Additional Fix: Uninitialized Variables in Test File

**File:** `main_edf_test.c`

The variables `xBgCreateResult` and `xRuntimeCreateResult` were declared without initialization, but the background and runtime task creations were commented out. The `printf` at the end still referenced these variables, causing undefined behavior.

**Fix:** Initialize all result variables to `pdFAIL`:
```c
BaseType_t xLowCreateResult = pdFAIL;
BaseType_t xHighCreateResult = pdFAIL;
BaseType_t xBgCreateResult = pdFAIL;
BaseType_t xRuntimeCreateResult = pdFAIL;
```

---

#### Summary of All Files Changed

| File | Change | Bug # |
|------|--------|-------|
| `tasks.c` | Added `prvGCD()` and `prvLCM()` helper functions | 2 |
| `tasks.c` | `prvEDFAdmissionConstrained()`: horizon now uses LCM of periods (capped) | 2 |
| `tasks.c` | `prvEDFAdmissionConstrained()`: loop starts from `min(D_i)` instead of 1 | 1 |
| `tasks.c` | `prvSRPComputeBlockingBoundForLevel()`: added static resource ceiling check | 3 |
| `FreeRTOS.h` | Added `configEDF_MAX_ANALYSIS_TICKS` default (100000) | 2 |
| `FreeRTOSConfig.h` | Added `configEDF_MAX_ANALYSIS_TICKS` user setting | 2 |
| `main_edf_test.c` | Initialized result variables to `pdFAIL` | N/A |

#### How the Existing Code Was Already Correct

The following parts of the admission control were already correct and did **not** need changes:

- **Dispatch logic** (`prvEDFAdmissionControl`): correctly detects constrained vs implicit task sets by checking if any task has `D < T`.
- **Implicit path** (`prvEDFAdmissionImplicit`): already uses `(C + B) / T` for each task, accounting for SRP blocking.
- **DBF formula itself**: `max(0, floor((t - D) / T) + 1) * C` is correct.
- **Blocking in constrained path**: `max(B_i)` is computed and added to demand at each test point (this was correct in logic, just applied to the wrong range of test points).
- **Candidate struct**: already includes `xB` (blocking) and `uxLevel` (preemption level) fields.
- **`xTaskCreateEDF`**: already calls `prvSRPComputeBlockingBoundForLevel()` to compute blocking before admission, and stores the result in the TCB.

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
