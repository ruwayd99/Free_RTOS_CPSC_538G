# bugs_SRP.md 
Current list of known defects and soft spots in the SRP and
stack-sharing implementation.

---

## 1. Functional bugs

### 1.1 Single-holder model breaks when two tasks hold overlapping units of the same resource

`SRPResourceControl_t` tracks exactly one `xCurrentHolder`. That is
fine for binary locks and even for multi-unit resources held by a
single task (with or without re-entry). But if SRP ever lets **two
different tasks** hold non-overlapping units of the same resource at
the same time (legal under classic multi-unit SRP when both requests
can be satisfied), the second take overwrites `xCurrentHolder` and the
first holder's identity is lost.

**Why this hasn't exploded.** The SRP gate + admission-time `B_i`
usually prevents two tasks from concurrently holding units unless
there are "enough" to satisfy both. The dynamic test works around it
by deliberately routing the runtime `RT_OK` task to `R3` instead of
`R1` so it can't hold R1 concurrently with `NESTED`. The comment in
[`main_srp_test_dynamic.c`](FreeRTOS/FreeRTOS/Demo/ThirdParty/Community-Supported-Demos/CORTEX_M0+_RP2040/Standard/main_srp_test_dynamic.c) says this explicitly.

**Fix.** Promote `xCurrentHolder` to a `holders[configMAX_SRP_USERS_PER_RESOURCE]` vector
and iterate on unit accounting.

### 1.2 Admission `B_i` is over-approximate for multi-unit resources

`prvSRPComputeBlockingBoundForLevel` treats each resource as if the
entire critical section of the worst low-level user could block the
candidate. For a multi-unit resource where the user only needs
`units_needed < uxTotalUnits`, the real blocking bound can be smaller
(the candidate may run concurrently on the remaining units). The
admission test is therefore **safe but pessimistic** and we reject some
sets that are actually feasible.

**Impact.** None of the provided tests hit the pessimism boundary, but
on dense workloads this will produce false rejections.

### 1.3 `vSRPResourceRegisterUser` is not called for dynamically-created resources after admission

The API is "register before admit." If a test creates a new resource
at runtime and calls `vSRPResourceRegisterUser` for an already-admitted
task, that task's `xSRPBlockingBound` is **not** recomputed. The
admission snapshot is stale.

**Mitigation.** Test code always registers users before admission.
**Fix.** Implement an `xTaskRevalidateAdmission` hook that re-runs
`prvEDFAdmissionControl` over the current set plus new blocking
terms.

### 1.4 No tracking of which resources a task is "allowed" to take

Nothing prevents a task from calling `xSRPResourceTake` on a resource
for which it was not registered. The assertions inside
`xSRPResourceTake` will fail only if `uxUnits > uxAvailableUnits`. A
rogue caller that takes a resource without registering will bypass the
`B_i` accounting entirely.

**Fix.** Track per-task registrations and assert on take.

---

## 2. Stack-sharing bugs

### 2.1 Snapshot is allocated from the same heap that sharing is supposed to save

Every shared-stack task allocates a private snapshot
(`pxPrivateContextSnapshot`) from `pvPortMalloc`. For Cortex-M0+ the
initial frame is ~20 words = 80 bytes, so 100 tasks waste ~8 KB of
heap. The net savings vs. full per-task stacks are still large (~95%
in the 100-task demo), but we're paying a small "sharing tax" that a
smarter implementation would avoid.

**Fix options.**
* Put snapshots in a per-group `.bss` array the same way stack buffers
  are allocated.
* Reconstruct the initial frame from a tiny template + the task entry
  point + pvParameters at dispatch time instead of storing a full copy.

### 2.2 Snapshot size is computed from `pxTopOfStack - buffer_end` — port-dependent

`uxPrivateContextWords = buffer_top - pxTopOfStack` assumes the port's
initial frame is contiguous and sits at the top of the buffer. The
RP2040 Cortex-M0+ port is well-behaved, but if this code ever moves to
a port that initializes the frame differently (e.g. some Cortex-M33
implementations that embed FPU state), the `memcpy` in the dispatch
hook will fail silently.

**Mitigation.** A `configASSERT` bounds-check is already in
`xTaskCreateEDFWithStack`, but it only catches out-of-buffer, not
wrong-shape. Needs a port-abstracted "initial frame size" API.

### 2.3 `prvSharedBufferHasMidExecutionTask` does not scan the delayed lists

The guard walks `xEDFReadyList` and checks `pxCurrentTCB`, but not
`pxDelayedTaskList` or `pxOverflowDelayedTaskList`. A task in the
delayed list should have `xSharedStackFreshStart == pdTRUE` (because
`vTaskDelayUntilNextPeriod` flipped it before blocking), so skipping
the delayed lists is correct for the **expected** path — but it leaves
no defensive check if an SRP-held task was forced into the delayed
list by some other code path without flipping the flag.

**Fix.** Scan the delayed lists too, cheap enough given we already
walk the ready list.

### 2.4 Stack HWM report under Mode ON conflates all tasks in a group

`uxTaskGetStackHighWaterMark(task)` reads the canary from the buffer
that `task` is currently pointed at. Since all 20 tasks in a group
share one buffer, all 20 HWM reads return the same number — not per
task. The monitor output in
[`main_srp_test_100.c`](FreeRTOS/FreeRTOS/Demo/ThirdParty/Community-Supported-Demos/CORTEX_M0+_RP2040/Standard/main_srp_test_100.c) handles this by collapsing to
one peak per group, but users looking at a single task's HWM in Mode
ON will get a misleading number.

**Fix.** Expose `uxSharedStackHighWaterMark(pxBuffer)` explicitly so
the API reads cleanly.

---

## 3. Trace / observability bugs

### 3.1 `[SRP][BLOCK]` can flood the UART under ceiling contention

When a task is ceiling-blocked, the selector runs every context
switch and emits a trace line. Under high tick rate with many
blocked tasks the UART backpressures everything. Observed in
Mode OFF of the 100-task test for ~200 ms while admission was still
running.

**Mitigation.** Raise `configTICK_RATE_HZ` only as needed.
**Fix.** Rate-limit the trace (emit once per `(task, ceiling)`
transition, not per-selection).

### 3.2 `[SRP][task-meta]` doesn't print the per-resource `B` contributions

The one line lumps everything into a single `B` scalar. Debugging why
a task was admitted with a surprisingly large `B` requires adding
printfs inside `prvSRPComputeBlockingBoundForLevel`.

---

## 4. Build / configuration issues

### 4.1 Mode switch requires a full rebuild

Toggling `configSRP_STACK_SHARING` between `0` and `1` touches
`FreeRTOS.h`, which is included by every translation unit.
Incremental builds usually still rebuild everything, but a user who
manually caches artifacts can get an ABI mismatch (TCB layout differs
between modes).

**Mitigation.** The demo CMakeLists sets the toggle via
`target_compile_definitions`, which CMake treats as a full-rebuild
trigger.

### 4.2 `configMAX_SRP_RESOURCES = 8` and `configMAX_SRP_USERS_PER_RESOURCE = 16` are hard-coded in `FreeRTOSConfig.h`

Not a bug, but an easy footgun on larger deployments. The
`xSRPResources` storage is sized at compile time; exceeding these
limits produces a `configASSERT` at runtime rather than a nicer
diagnostic.
