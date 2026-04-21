# CBS (Constant Bandwidth Server) - Design Document

## 1. Overview

CBS extends EDF scheduling with reservation-based execution for aperiodic work. Each CBS task is assigned a fixed bandwidth reserve, and when that reserve is exhausted the task's virtual deadline is postponed so EDF can continue to schedule it without breaking the reservation model.

The implementation in this repository keeps the CBS behavior integrated with the existing EDF kernel rather than replacing EDF. CBS tasks are still ordinary FreeRTOS tasks, but the kernel tracks their server budget and virtual deadline separately from periodic EDF tasks.

## 2. Core Model

### 2.1 Reservation parameters
A CBS task uses two fixed parameters:

- `Q_s`: server budget
- `T_s`: server period

The bandwidth of the server is:

$$
B = \frac{Q_s}{T_s}
$$

This implementation treats that ratio as the amount of CPU time the server is allowed to consume in each period.

### 2.2 Task state
Each CBS task carries its own server state in the TCB:

- `xIsCBSTask`
- `xCBSMaxBudget`
- `xCBSCurrentBudget`
- `xCBSPeriod`
- `xCBSDeadline`

The EDF-visible deadline for the task is mirrored from the CBS deadline, so EDF ordering and CBS reservation state stay aligned.

## 3. Scheduling Behavior

### 3.1 Budget consumption
While a CBS task executes, its remaining budget is decremented at tick granularity. When the budget reaches zero, the task is not allowed to keep its current deadline. Instead, the kernel postpones the virtual deadline by one server period and replenishes the budget.

Conceptually:

1. Task runs.
2. Budget decreases.
3. Budget reaches zero.
4. Deadline is postponed by `T_s`.
5. Budget is replenished back to `Q_s`.
6. EDF sees the new deadline and continues ordering tasks normally.

### 3.2 Deadline tie-breaking
When a CBS task and a periodic EDF task have the same deadline, the CBS task is placed ahead of the periodic task in the ready ordering. That matches the project requirement that ties favor the server.

### 3.3 Job-arrival refresh
CBS tasks can wake through different kernel paths, not just the delay list. In this implementation, the server refresh logic is applied on every unblock path that can expose a task to EDF:

- delayed-list wakeup
- event-list wakeup
- task notification wakeup
- ISR notification wakeup
- notify-give wakeup from ISR

That ensures the CBS deadline and budget are refreshed consistently even when aperiodic jobs are released through notifications instead of semaphores or timed delays.

## 4. Kernel Integration

### 4.1 Task creation
`xTaskCreateCBS()` creates a normal task, marks it as CBS-managed, initializes the server fields, and inserts it into the EDF ready structure with the CBS virtual deadline.

### 4.2 EDF interaction
CBS does not replace EDF. The EDF ready list is still the scheduler's ordering mechanism; CBS just changes how the task's deadline evolves over time.

### 4.3 Overrun behavior
If a CBS task keeps executing beyond its budget, the kernel postpones its deadline rather than allowing it to keep consuming its original server reservation. That is the key behavior that separates CBS from a plain periodic task.

## 5. Design Decisions

### 5.1 Why the deadline is virtual
Using a virtual deadline keeps the implementation compatible with the existing EDF code path. The scheduler only needs to compare deadlines, while CBS keeps the budgeting policy local to the task state.

### 5.2 Why refresh happens on wakeup
A CBS task can be released through notifications or event unblocks after having been blocked long enough that its server period has advanced. Refreshing on wakeup prevents the task from reentering EDF with stale server state.

### 5.3 Why the implementation is tick based
The repo's RP2040 demo runs with a 1 kHz tick, so tick-based budget accounting is a practical fit. It keeps the implementation simple and consistent with the rest of the FreeRTOS kernel paths.

## 6. Interaction with the Demo Application

The CBS demo applications use a trigger task to notify the CBS worker task. That matches the implementation model above:

- periodic tasks represent the EDF workload
- the trigger task creates aperiodic arrivals
- the CBS worker executes under the server reservation

The dynamic multi-task CBS test further validates that the server remains stable when more periodic tasks are introduced after startup and when more than one CBS worker is active.

## 7. Summary

The design is a straightforward EDF-plus-reservation model:

- EDF remains the ordering policy.
- CBS adds per-task budget and virtual deadlines.
- Budget exhaustion postpones the deadline.
- Wakeup paths refresh CBS state consistently.
- Deadline ties favor CBS tasks.

That gives the repository a CBS implementation that is compatible with the existing EDF code and with the task-notification-based test programs used in the demos.
