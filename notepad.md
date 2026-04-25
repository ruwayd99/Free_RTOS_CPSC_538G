# Problem
- whenever CBS task is triggered past its deadline, EDF sees this as a deadline miss
- however, CBS itself does try to extend deadline if it thinks it's gonna miss it (in `xTaskRemoveFromEventList`)

# What I think is happening
- `xTaskRemoveFromEventList` is ran AFTER CBS task is triggered and already running?
- This way, if tick increment runs before `xTaskRemoveFromEventList`, then EDF algorithm has a chance to detect a deadline miss, even though we were right about to extend it again

# How to solve
- Investigate where `xTaskRemoveFromEventList` is being called.
- Move the deadline extension logic from `xTaskRemoveFromEventList` somewhere else? Maybe we can even let whoever triggers the CBS task to update its deadline 

# Investigation results
- `xTaskRemoveFromEventList` is mostly used by queue/semaphore paths (see `queue.c` call sites).
- Task notifications (`xTaskNotifyGive`, `xTaskNotifyGiveFromISR`, and generic notify APIs) do **not** use `xTaskRemoveFromEventList`; they unblock the waiting task directly in `tasks.c`.
- Therefore, CBS tasks woken by notifications were skipping CBS job-arrival refresh and could be seen by EDF with stale deadlines.

# Implemented fix
- Added a shared helper in `tasks.c`: `prvCBSRefreshServerOnJobArrival( pxTCB, xTickCount )`.
- Called this helper from all unblock paths before ready-list insertion:
	- delayed-list unblocks in `xTaskIncrementTick`
	- event-list unblocks in `xTaskRemoveFromEventList`
	- notification unblock path in `xTaskGenericNotify`
	- notification unblock path in `xTaskGenericNotifyFromISR`
	- notification unblock path in `vTaskGenericNotifyGiveFromISR`

# Outcome
- CBS deadline/budget refresh is now applied consistently on wake-up regardless of which kernel path unblocked the task.
- This prevents stale CBS deadlines from being observed by EDF when notify-based aperiodic releases are used.