Introduction
===
In this programming activity you will extend an industry standard Real-Time Operating System (RTOS), namely FreeRTOS, to support task-dynamic priority scheduling and resource access control. The following are some goals for this assignment:

* Understand the architecture and design choices behind a real-time operating system kernel;
* Extend the capabilities of the RTOS kernel;
* Gain an appreciation for the challenges of embedded software development;
* Improve upon work of others by examining prior engineering choices;
* Design and implement techniques for testing your modifications to the kernel.

# [Task 1] EDF support
Extend the scheduler to support EDF. You need to support
_constrained-deadline_ systems where relative deadline D<sub>i</sub> is smaller
than or equal to time period < T<sub>i</sub>. Implement admission control in
the kernel, and provide the ability to accept new tasks as the system is
running. 
If the task set is constrained-deadline, then you will have to carry out
_exact_ EDF analysis (processor demand) to decide schedulability. Otherwise
(i.e., implicit-deadline), of course one would use the LL bound since it is
sufficient and necessary. 

You may use your `printk()` (after modification to use the UART implementation
of the FreeRTOS port) to debug your scheduler. It is crucial that you
demonstrate that the schedules produced are indeed EDF. Some messages printed
to the console using your `printk()` should suffice. 

Also one has to deal with the situation when a deadline-miss occurs (transient
overload). The  overrun management mechanism is left to you as design choice.
Would you drop the late job? Make it the lowest priority? Or restart the system
altogether? Document your design choices clearly. 

Indicate all the changes that you made to FreeRTOS in order to support EDF. 
Document all the changes in a README as well.

In a configuration file, expose some configurable constants that can be used by
the user to enable/disable EDF and/or SRP. If the user chooses not to use your
extensions to FreeRTOS, you should instead run the default mechanisms provided
by FreeRTOS.

**Testing**: To see the difference in performance between the processor demand
analysis and the LL bound, you may perform admission control on roughly 100 periodic 
tasks running simultaneously.  

# [Task 2] Stack-based resource-access control (SRP)
To support resource sharing with the EDF scheduler, extend the semaphore
implementation of FreeRTOS to use SRP. You only need to extend FreeRTOS with
SRP for 
_binary semaphores_. The resource access protocol used in FreeRTOS is the
Priority Inheritance Protocol.

Also extend the admission control tests developed in the previous part to
include blocking times and test schedulability using EDF + SRP. It is up to you
to decide how the worst-case estimates of the lengths of the critical sections
are given to the scheduler.

**Hint:** You may want to utilize a stack (in the kernel) to keep track of the 
system ceiling and its evolution.

One of the consequences of using SRP is the ability to share the **run-time
stack** (which is different from the stack used to implement SRP). Recall that
each 
process must have a private run-time stack space, sufficient to store its
context (the content of the CPU registers) and its local variables. Observe
that in 
SRP, if two tasks have the same preemption level, they can never occupy stack 
space at the same time. Thus, the space used by one such task can be "reused"
by the other, eliminating the need to allocate two separate stack spaces, one
for each task, even if both are active (ready for execution). This is a
consequence of the fact that in SRP, once a task starts execution, it can never
blocked on any resources. Note that the storage savings resulting from sharing
the run-time stack become more substantial as the number of tasks with the same
preemption level increase. _In your implementation of SRP, provide support for
sharing the run-time stack among the running tasks._ Read section 7.8.5 of the 
text, as well [T.P. Baker's paper](http://ieeexplore.ieee.org/document/128747/)
on SRP for more details.

Again, develop suitable tests to demonstrate that your implementation of SRP 
and stack sharing are correct. To see the savings we reap as a result of
sharing the run-time stack, carry out a quantitative study with stack sharing
vs. no stack sharing. Report the gains in terms of the maximum run-time stack
storage used. It is sufficient to run 100 tasks simultaneously for your
quantitative analysis. 

# [Task 3] Constant Bandwidth Server (CBS) Support

You will add support for servicing aperiodic, soft real-time requests alongside hard real-time tasks, by implementing reservation-based, dynamic server schemes for handling aperiodic tasks, on top of EDF. You will be using your EDF implementation from the previous programming assignment.

Read the chapter on Dynamic Priority Servers in the text (chapter 7). In essence, CBS allows tasks that exceed their nominal CPU reserve to continue execution, or stay in the ready queue, by postponing the deadline of the task. FreeRTOS provides methods for creating and managing periodic tasks. For this project, add a new type of task that would be managed by a constant bandwidth server. Creating a task that is managed by a CBS would be similar to creating a regular periodic task.
 
 Add the primitives needed to manage constant bandwidth servers at the programming interface and within the kernel/scheduler. It is possible to have multiple CBS tasks running concurrently. A software developer should be able to build an application that uses a combination of regular periodic tasks and constant bandwidth servers. Tasks are scheduled using EDF. Remember that priority ties are always broken in favor of the server. To simplify matters, you may assume that a job of a task is not released until the previous job of the same task is complete.

# [Task 4] Supporting Multiprocessor Real-time Scheduling in FreeRTOS

TBD


# Submission Instructions
(In the following, replace `LABEL` with `EDF`, `SRP`, `CBS`, or `MP` for each of the four tasks.)

1. **Changes.** In a markdown file with the name `changes_LABEL.md`, record all of the changes 
   (and additions) that you  
   made to FreeRTOS in order to support the functionality in every task. 
   Make sure to include all the 
   files that you altered as well as the functions you changed, and also
   list the new functions that you added. 
3. **Design.** Create a markdown document named `design_LABEL.md`. In this document, 
   include all your design choices, including brief explanation of how you
   implemented partitioned and global EDF. Detail the feasibility tests you used in each. Flowcharts are nice here! 
5. **Testing**. Create a markdown document named `testing_LABEL.md`. For every task,
   include the general testing methodology you used, in addition 
   to all the test cases. For each test case, provide an explanation
   as to the specific functionality/scenario that it tests. Also indicate the 
   result of each test case. 
6. **Bugs**. Create a file named `bugs_LABEL.md` and include a list of the current bugs
   in your implementations.
7. **Future improvements**. Create a file named `future_LABEL.md` and include a list 
   of the things you could do to improve your implementations, 
   have you had the time to do them. This includes optimizations, 
   decision choices, and basically anything you deemed lower priority `TODO`.

**More instructions on the end-of-term demo TBD.**