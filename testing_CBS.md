# CBS (Constant Bandwidth Server) - Testing Document

## 1. Testing Methodology

The CBS tests are organized around three discrete executable scenarios. Each one targets a different behavior of the CBS path in the kernel: a normal periodic/aperiodic mix, an overloaded variant, and a dynamic-arrival variant. All three depend on the same observability pattern:

1. GPIO pins mark when a task is actively executing.
2. `printf()` traces record release times, start/end ticks, and trigger times.
3. `xTaskNotifyGive()` is used to release the CBS task from a separate trigger task.
4. `vTaskDelayUntilNextPeriod()` is used by the periodic tasks so that the tests exercise periodic EDF behavior rather than ad hoc delays.

The pass criteria are not framed as generic CBS theory alone; they are tied to the attached programs and the load they create.

## 2. Test Cases

### Test 1: Baseline CBS behaviour
**File:** [main_cbs_test.c](FreeRTOS/FreeRTOS/Demo/ThirdParty/Community-Supported-Demos/CORTEX_M0+_RP2040/Standard/main_cbs_test.c)

**What it tests:**
This is the reference CBS scenario. It creates three periodic EDF tasks at startup, one CBS-managed aperiodic task, and one trigger task that periodically notifies the CBS task. The purpose is to show that a CBS server with modest load can coexist with periodic EDF work.

**Key setup from the program:**
1. Three periodic tasks on GPIO 10, 11, and 12.
2. One CBS task on GPIO 21.
3. Periodic workload: 1000 ms period, 150 ms WCET.
4. CBS server: 1000 ms period, 150 ms budget.
5. CBS trigger task wakes roughly every 700 ms.

**Expected result:**
The periodic tasks should continue to execute once per period without overrunning their budgets, and the CBS task should only run when notified. Because the combined load is intentionally below 100%, no deadline misses are expected in a correct build.

**Pass criterion:**
- All three periodic tasks keep producing regular GPIO pulses.
- The CBS task prints one start/end pair per notification.
- The CBS task never runs for longer than its configured budget in a single server period.

### Test 2: System overload (deadline-miss behaviour test)
**File:** [main_cbs_test_overrun.c](FreeRTOS/FreeRTOS/Demo/ThirdParty/Community-Supported-Demos/CORTEX_M0+_RP2040/Standard/main_cbs_test_overrun.c)

**What it tests:**
This variant is a heavier load version of the same CBS pattern. It increases both the periodic WCET and the CBS WCET, then uses a periodic task to opportunistically notify the CBS task only when the previous CBS job has finished. The code also uses `bIsCBSFinished` as a guard so a new notification is not issued while the CBS server is still busy.

**Key setup from the program:**
1. Three periodic tasks on GPIO 10, 11, and 12.
2. One CBS task on GPIO 21.
3. Periodic workload: 1000 ms period, 250 ms WCET.
4. CBS server: 1000 ms period, 400 ms budget.
5. CBS trigger happens from inside the periodic task path when enough time has elapsed.
6. CBS WCET is 300 ms, which intentionally pushes the system closer to saturation.

**Expected result:**
This test is meant to stress the CBS implementation rather than demonstrate a perfectly slack schedule. The scheduler should remain stable, the CBS task should not be re-notified while still executing, and the trace output should make any overload or delay visible. If deadlines are missed under this load, that is acceptable so long as the system stays responsive and the CBS server continues to operate.

**Pass criterion:**
- The `bIsCBSFinished` guard prevents overlapping CBS jobs.
- The CBS task still prints sensible start/end tick pairs.
- The system does not deadlock or stop issuing periodic GPIO activity.

### Test 3: High system load and dynamically adding more tasks
**File:** [main_cbs_test_dynamic_multi.c](FreeRTOS/FreeRTOS/Demo/ThirdParty/Community-Supported-Demos/CORTEX_M0+_RP2040/Standard/main_cbs_test_dynamic_multi.c)

**What it tests:**
This is the dynamic admission test. It starts with a large periodic task set, then adds more periodic tasks later while the CBS tasks are already active. The goal is to verify that CBS keeps working when the ready set changes after the scheduler has started.

**Key setup from the program:**
1. Ten initial periodic tasks.
2. Four more periodic tasks are created later by a dispatcher task.
3. Two CBS tasks are created at startup and two more are created later.
4. One trigger task round-robins notifications across all CBS tasks.
5. Initial periodic tasks do not use GPIO pins.
6. Subsequent periodic tasks use GPIO 10..13.
7. CBS tasks use GPIO 18..21.
8. CBS server parameters are identical for all CBS tasks: 1000 ms period, 200 ms budget.

**Pin map used by the program:**
- Initial periodic tasks do not get GPIO pins.
- Subsequent periodic tasks 10..13 use GPIO 10..13.
- CBS tasks 0..3 use GPIO 18..21.

**Expected result:**
The test should demonstrate that CBS continues to function while new EDF tasks are created after startup. The dispatcher should print creation messages for the later tasks, and the CBS trigger task should continue to notify whichever CBS worker is next in the round-robin order.

**Pass criterion:**
- The dispatcher creates the three later periodic tasks successfully.
- The dispatcher creates the four later periodic tasks successfully.
- The dispatcher creates the two later CBS tasks successfully.
- The CBS trigger task continues to notify all four CBS workers over time.
- The trace output reflects all 14 periodic tasks plus the four CBS tasks without kernel instability.
