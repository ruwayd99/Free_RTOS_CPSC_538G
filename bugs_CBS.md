# CBS - Known Bugs

### 1. **CBS Budget Refresh Race Condition**
In rare scenarios where a CBS task wakes from blocking state exactly at a period boundary (within 1-2 ticks), the refresh logic might apply the replenishment from a stale previous period.

**Trigger Condition:**
- Task blocks very late in a period (tick 998 of 1000)
- Another task wakes it at tick 1000 or 1001
- Refresh logic might see deadline as "already past"

**Impact:** None observed in practice; replenishment still occurs, just off by ≤1 tick

**Workaround:** None required; impact is negligible for tick granularity applications


# Potential issues (not necessarily bugs)

### 2. **CBS Task Deletion During Execution (Not tested)**
If a CBS task is deleted (via `vTaskDelete`) while it is running or has an active deadline in the EDF ready list, the cleanup might not immediately update the EDF ready list.

**Trigger Condition:**
- CBS task calls `vTaskDelete(NULL)` on itself
- Or another task calls `vTaskDelete(xCBSTaskHandle)` while CBS task is ready

**Impact:** Task is removed cleanly; no crashes observed

---

### 3. **CBS Task Suspend/Resume (Not Tested)**
Suspending a CBS task via `vTaskSuspend()` and later resuming it may not refresh the server state correctly if a period boundary was crossed during suspension.

**Trigger Condition:**
- CBS task suspended at tick 500
- Tick count advances to tick 2500 (crossed 2 period boundaries)
- CBS task resumed

**Impact:** Not observed; not a typical use case (suspension usually for debugging)
