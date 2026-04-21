# CBS (Constant Bandwidth Server) - Future Improvements

## Overview
This document outlines potential enhancements and optimizations for the CBS implementation.

---

## 1. High Priority / Medium Effort

### 1.1 Bandwidth Admission Control
**Description:** Add a kernel-level function to validate total bandwidth before accepting new CBS tasks.

**Reason:** Detect over-subscription early and prevent silent deadline misses.

**Proposal:**
```c
// Check if adding a new CBS would exceed 100% total bandwidth
BaseType_t xTaskCanCreateCBS(TickType_t xBudget, TickType_t xPeriod) {
    uint64_t ulNewBandwidth = prvEDFUtilToMicro(xBudget, xPeriod);
    uint64_t ulTotalBandwidth = ulEDFTotalUtilMicro + ulNewBandwidth;
    return (ulTotalBandwidth <= 1000000ULL) ? pdTRUE : pdFAIL;
}

// Use in xTaskCreateCBS before creating:
if (xTaskCanCreateCBS(xBudget, xPeriod) == pdFAIL) {
    return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;  // Admission denied
}
```

**Impact:** Prevents silent over-subscription; provides early feedback to developers.

---

### 1.2 Dynamic Budget Adjustment API
**Description:** Allow changing a CBS task's budget and period at runtime.

**Reason:** Adaptive systems that adjust resource reservations based on load.


**Impact:** Enables adaptive reservation systems; adds flexibility.

## 2. Testing and Validation Improvements

### .1 CBS Stress Test Suite
**Goal:** Expanded tests for edge cases, high concurrency, load variations

**Scenarios:**
- 50+ concurrent CBS servers
- Rapid create/destroy cycles
- Variable-load patterns (bursty, periodic, random)
- Mixed with SRP resource sharing
