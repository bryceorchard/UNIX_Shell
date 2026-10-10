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
myshell> sleep 5       # foreground: blocks until it finishes
```

## Test

A small unit-test suite lives in `tests/test_main.c` and links against the
shell modules (everything except `main.c`).

```bash
make test
```

It covers line reading, command parsing (including the background `&` flag
and misplaced-`&` errors), and foreground vs. background execution behavior.

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
