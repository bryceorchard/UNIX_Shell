# COEN 346 — UNIX Shell (Group 10)

A simple command-line shell for UNIX, built for COEN 346 (Operating Systems,
Fall 2026) at Concordia University.
Based on the [ensimag-shell](https://github.com/gmounie/ensimag-shell)
skeleton.

## Build

From the project root:

```bash
make          # compile; produces bin/unix_shell
make clean    # remove build artifacts (obj/, bin/)
```

## Run

```bash
./bin/unix_shell
```

You'll get a `myshell>` prompt. Type commands as in a normal shell; type
`exit` (or Ctrl-D) to quit.

```text
myshell> echo coen346
coen346
myshell> sleep 5 &     # background: the prompt returns immediately

# Background task runtime is immediately printed upon exit.

myshell> sleep 5       # foreground: blocks until it finishes
myshell> jobs          # lists all currently running background processes
myshell> cat resources/contacts.txt | sort    # pipe: output of cat becomes input of sort
myshell> cat < resources/contacts.txt | sort | uniq > out.txt   # multiple pipes with redirection
```

## Test

A small unit-test suite lives in `tests/test_main.c` and links against the
shell modules (everything except `main.c`).

```bash
make test
```

It covers line reading, command parsing (including the background `&` flag
and misplaced-`&` errors), foreground vs. background execution behavior, the `jobs` command,
simple and multiple pipes (`cmd1 | cmd2 | ...`), input and output redirection, and background pipelines.

## Project structure

```text
.
├── src/        # implementation (main, shell, parser, executor, utils)
├── include/    # public headers
├── tests/      # unit tests (test_main.c)
├── resources/  # sample input files for testing commands
├── docs/       # Doxygen-generated documentation (make docs)
└── Makefile
```

## Authors

See `AUTHORS.txt` for names and student IDs.

> Before submitting: add `AUTHORS.txt` (each member's last name, first name,
> and student ID), and name the submission folder `<S1_id>_<S2_id>_pa1`.

## AI disclosure

Per the course policy, use of generative AI tools on this assignment is
disclosed below.

### Bryce Orchard log

- **Tool used:** Claude (Anthropic).
- **Intent:**
  - Conceptual explanation of background-process handling, zombie reaping,
    signals (`SIGCHLD`, `EINTR`), and terminal control codes.
  - Debugging a defect where foreground command output printed *after* the
    prompt once a background command had run — traced to using `wait(NULL)`
    (wait for any child) instead of `waitpid(pid, …)` (wait for the specific
    child).
  - Generating the unit tests in `tests/test_main.c` for the
    background-execution feature.
  - Co-authoring the README
- **Claude chat URL:** [claude.ai](https://claude.ai/share/55c21943-0ed2-4061-b77b-2629ae1ed79a)
- **Adaptation / verification:** AI-suggested code was reviewed and verified.
  The tests were compiled and run (15/15 passing) before inclusion. The generated README draft was heavily modified.

### Bryce Orchard log (Issue #4 — jobs / background-process listing)

- **Tool used:** Claude (Anthropic).
- **Intent:**
  - Conceptual discussion of data structures for tracking background jobs
    (fixed job table vs. linked list vs. tree), job-id assignment and reuse,
    and where the add / remove / list logic belongs in the executor.
  - Design discussion of a `SIGCHLD`-based reaping approach — async-signal
    safety, blocking `SIGCHLD` around `fork()`/`add_job`, and `EINTR` /
    `SA_RESTART` handling.
  - Generating the unit tests in `tests/test_main.c` for the `jobs` built-in
    / background-process listing feature.
- **Claude chat URL:** [claude.ai](https://claude.ai/share/a4fb6e61-c6d1-4fb6-a732-b8f103552182)
- **Adaptation / verification:** AI-suggested tests were reviewed and run via
  `make test` (all assertions passing) before inclusion. None of the non-test AI suggested code was copied, and was only used as inspiration.

### Bryce Orchard log (Issue #8 — process computation time)

- **Tool used:** Claude (Anthropic).
- **Intent:**
  - Conceptual discussion of the computation-time feature — why
    async-signal-safety still matters even though `SIGCHLD` is blocked during
    the handler, draining coalesced signals with `waitpid(-1, WNOHANG)`,
    `SA_RESTART`, and whether `fflush(stdout)` before `fork()` is needed.
  - Review of the existing implementation (the `SIGCHLD` handler plus the
    `add_job`/`remove_job` start-time and elapsed-time logic).
  - Generating the unit tests in `tests/test_main.c` for the process
    computation time (Issue #8) feature.
- **Claude chat URL:** [claude.ai](https://claude.ai/share/4137fd13-b14d-4301-b6cc-4b6674c9c00f)
- **Adaptation / verification:** AI-suggested tests were reviewed and run via
  `make test` (all assertions passing) before inclusion. None of the non-test
  AI-suggested code was copied.

### Ismail Cherfaoui log (Issue #6 - simple pipe)

- **Tool used:** Claude (Anthropic).
- **Intent:**
  - Understanding the assignment, the skeleton (`struct cmdline`, `execute`) and Bryce's implementation of `execute_command`.
  - Conceptual explanation of file descriptors and the different system calls used for pipes (`pipe()`, `fork()`, `dup()`/`dup2()`, `close()`).
  - Explanation of the order in which the shell and each child call `pipe()`, `fork()`, `dup2()`, `close()` for a pipeline.
  - Discussion of how pipes should be created in the loop and how to pass `in`/`out` file descriptors to `execute_command`.
  - Review of my implementation of pipes in `executor.c` for possible bugs or overlooked cases.
  - Generating the pipe unit tests in `tests/test_main.c`: suggesting test commands with expected outputs, and writing the `capture_output` helper function and test loop from my struct and array draft.
- **Claude chat URLs:** [Pipe implementation](https://claude.ai/share/cfb64113-afb3-4ab3-8c61-c06254025a16), [Pipe tests](https://claude.ai/share/c678a2cb-1215-4268-8b2d-ae00b3b12384)
- **Adaptation / verification:** The pipe implementation (in `executor.c`) was fully written by myself; AI was used for explanations, design discussion and review of my implementation. The pipe testing helper function, test loop and most test cases were AI-generated from my struct and array draft, then reviewed line by line and adjusted. Tested manually against the system shell and `make test` passes (83/83). Review findings from Pipe implementation chat (large-output deadlock, stray pipe read end) are currently left as known limitations (Issue #7). 

### Ismail Cherfaoui log (Issue #7 - multiple pipes)

- **Tool used:** Claude (Anthropic).
- **Intent:**
  - Conceptual explanation of how input/output redirection fits into my existing pipe loop.
  - Explanation of `open()` flags (`O_RDONLY`, `O_WRONLY`, `O_CREAT`), the permission mode, and the difference between `ftruncate()` and `O_TRUNC` (file size vs offset).
  - Review of my pipe loop in `executor.c`, which flagged the large-output deadlock, the stray pipe read end, and the child inheriting the blocked `SIGCHLD` mask.
  - Generating the unit tests in `tests/test_main.c` for multiple pipes, redirection, and background pipelines.
- **Claude chat URL:** [Multiple pipes](https://claude.ai/share/f8760da7-b567-41d9-960a-786018754fa2)
- **Adaptation / verification:** The reidrection implementation (`executor.c`) was fully written by myself; AI was used for explanations and review. The tests were AI-generated, then reviewed and adjusted (fixed an inaccurate comment). Tested manually, and `make test` passes (109/109). The deadlock and stray read end are currently left as known limitations.