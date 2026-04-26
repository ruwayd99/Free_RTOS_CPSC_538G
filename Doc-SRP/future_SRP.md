# future_SRP.md

Follow-up items that would make the SRP and stack-sharing
implementation more accurate, faster, safer, and easier to maintain.
Ordered roughly by impact.

---

## 1. Admission-control accuracy

### 1.1 Tighter multi-unit blocking bound

The current `prvSRPComputeBlockingBoundForLevel` over-approximates
`B_i` for multi-unit resources: it counts the full CS of any lower-level
user that could raise the ceiling, even if the candidate could run
concurrently on the remaining units. A tighter bound would walk
unit counts and yield smaller `B_i`resulting in more accepts
without losing soundness. We found in Chen et al.'s "SRP with multiple units"
analysis for the exact formulation.

### 1.2 Re-admission hook for runtime resource registration

If a task is admitted, then a new resource is created at runtime, then
a new user registration would have changed the earlier task's `B_i`,
we silently skip the recomputation. Adding
`xTaskRevalidateAdmission(handle)` which re-runs the DBF test with
up-to-date `B_i` for every admitted task would remove this
assumption. Rejection would return `pdFAIL` without altering scheduler
state.

### 1.3 Per-task resource usage declaration at `xTaskCreateEDF`

Right now resource usage is declared via
`vSRPResourceRegisterUser(resource, level, units, cs)` unambiguously
per user, but attached to the **resource**, not the task. A task-side
API like
`xTaskDeclareResourceUsage(taskHandle, resource, units, cs)` would let
us assert at `xSRPResourceTake` time that the calling task had actually
declared this resource. 

---

## 2. Stack sharing

### 2.1 Dynamic shared-buffer allocation by preemption level

Currently the demo statically allocates one `.bss` buffer per group.
The kernel could instead maintain a small hash `uxPreemptionLevel →
StackType_t *`, allocating buffers lazily on first use and resizing
them if a later task in the same group requests a bigger stack. This
removes the "you must know your groups up front" constraint.

### 2.2 Port abstraction for the initial frame

`uxPrivateContextWords` is computed as
`buffer_top - pxTopOfStack`. That works for the ARMv6-M Pico port
because the initial frame sits contiguously at the buffer top with no
port metadata elsewhere. A port-level hook `portGetInitialFrameSize()`
would make the code safely portable. See `bugs_SRP.md §2.2`.

---

## 3. Testing and tooling

### 3.1 Chaos tests for the stack-sharing guard

Randomly generate task sets where same-level tasks get preempted by
higher-level ones at unpredictable times, then verify every task
completes its run without corruption. This is the only way to
exercise the `prvSharedBufferHasMidExecutionTask` guard in its full
state space.

### 3.2 `main_srp_test_dynamic` should verify drop-late under SRP

The current demo only exercises normal-load admission. Add a phase
where NESTED is deliberately overrun (inject extra busy work) so
`[EDF][drop]` fires while SRP units are held. Verify ceiling returns
to 0 and the next NESTED period starts normally.

### 3.3 Automated HWM diff between Mode OFF and Mode ON

The 100-task monitor prints a single report per run. A scripted
diff with Mode OFF report vs. Mode ON report would make the savings
claim a regression test instead of a manual check.
