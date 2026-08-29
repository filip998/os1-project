# OS1 debugging instructions

Before doing any work, read these files in order:

1. `docs/debugging-plan.md`
2. `docs/provenance.md`
3. every existing file in `docs/rca/`

`docs/debugging-plan.md` is the source of truth for scope, current state, execution order, evidence requirements, approval gates, VM access, testing, review, and commit policy.

Do not modify source code merely because it looks suspicious. A bug must first be demonstrated by a minimal build/runtime/GDB reproduction or by a direct, unambiguous contradiction with the project PDF.

For every confirmed bug:

1. preserve the evidence;
2. write a simple Serbian RCA;
3. explain the proposed fix and wait for approval when the user is available;
4. apply one minimal fix;
5. explain the diff line by line;
6. run an independent code review;
7. run the reproducer and all previously passing regressions;
8. create one commit for that bug.

The scope is memory allocation, threads, synchronous context switching, and the ABI/bootstrap code they require. Do not repair semaphores, sleep, asynchronous timer handling, or full console handling unless they directly block this scope.

The first milestone is a clean build that reaches the canonical test-selection menu. Stop at that milestone for user review before running the memory and thread test phases.
