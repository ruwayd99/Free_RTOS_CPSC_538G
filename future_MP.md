# SMP - Future Improvements

## Overview
This document outlines potential enhancements to the SMP/multiprocessor EDF implementation.

---

### 1 Core Affinity Mask Enhancement
**Description:** Extend `uxCoreAffinityMask` to support multi-core affinity (not just single core).

**Motivation:** Allow task to run on *either* core 0 or core 1, but not both, without forcing single-core assignment.

**Current Limitation:**
- Global mode: affinity hint is soft (task can run anywhere)
- Partitioned mode: task pinned to single core (hard affinity)
- No middle ground: "preferred cores" that task can run on

**Impact:**
- More flexible task placement
- Enables heterogeneous workloads (some tasks restricted, others free)
- Still compatible with both global and partitioned modes


---

### 2 Improved Admission Test for Partitioned Mode
**Description:** Replace greedy Decreasing First Fit with something better at packing cores optimally.

**Motivation:** DFF is simple but suboptimal; some schedulable sets are rejected.

**Proposal:**
- **First Fit Decreasing (FFD):** Assign each task to first core with capacity
- **Best Fit Decreasing (BFD):** Assign to core with least leftover capacity
- **Worst Fit Decreasing (WFD):** Assign to core with most leftover capacity

**Example (BFD):**
```
Tasks: A(0.45), B(0.35), C(0.30), D(0.30), E(0.25)
Sort:   A(0.45), B(0.35), C(0.30), D(0.30), E(0.25)

BFD:
  A → Core with 0.55 leftover (best fit)
  B → Core with 0.65 leftover
  C → Core with smallest leftover ≥ 0.30
  ... (minimize fragmentation)
```

**Impact:**
- Accept more schedulable task sets
- Better utilization
- Still O(n log n) complexity


### 3 Heterogeneous Multiprocessor Support
**Description:** Support different core types (big/little or different speeds).

**Motivation:** Real-world systems have heterogeneous cores (e.g., ARM big.LITTLE).

**Proposal:**
- Track per-core speed/frequency multiplier
- Adjust utilization calculations: `U_i = C_i / (T_i * speed_factor)`
- Admission test accounts for core differences

**Impact:** Support diverse hardware; relevant for mobile/IoT


---

### 4 SRP Integration with SMP
**Description:** Enable binary semaphore (SRP) support for multiprocessor tasks.

**Motivation:** Currently, SMP modes don't support resource sharing. Limits realism of workloads.

**Proposal:**
- Global mode: Use global SRP system ceiling (complex)
- Partitioned mode: Per-core SRP ceilings (simpler)
- Migrate blocked tasks if waiting for remote resource?

**Complexity:** Very high (resource ownership, priority inversion across cores)

**Impact:** Full SMP feature parity with single-core FreeRTOS

