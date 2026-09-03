# Delegating issues to an opencode agent

This project uses [opencode](https://opencode.ai) to delegate well-scoped GitHub issues
to a background AI agent.  The workflow below describes how to run it, review
the result, and have the agent take the pull request through merge.

---

## Quick start

Always run the agent inside a dedicated git worktree so it does not
clobber the main working tree and so parallel agents cannot interfere
with each other:

```sh
# Create a throwaway worktree for the issue
git worktree add /tmp/open-rts-issue-N -b dc/issue-N-<slug>

# Run the agent inside that worktree
(cd /tmp/open-rts-issue-N && opencode run -m opencode/mimo-v2.5-free "
Work on GitHub issue #N: <title>.
<...problem / acceptance criteria / smoke-test command...>

When finished:
The agent must add extensive unit tests for the changed behavior, including
boundary cases and failure paths, and run the relevant unit tests before
opening or merging the PR.
1. Add and run extensive unit tests covering normal behavior, boundary cases,
   failure paths, and regressions for the changed behavior.
2. Commit all changes with message 'DC-N: <title>\n\nCloses #N'.
3. Push the branch: git push -u origin dc/issue-N-<slug>
4. Create a PR to main: gh pr create --base main --title '...' --body '...'
5. Review the PR, address every finding, and push the fixes.
6. Rebase the PR branch onto main when needed, resolve conflicts, rerun all
   tests, and push it with --force-with-lease.
7. Approve and merge the PR with gh pr merge after all checks pass.

The orchestrating agent does not review, rebase, or merge the PR; it only
monitors the delegated run and reports blockers.
")
```

The final lines in the prompt tell the agent to implement, test, commit, push,
review, rebase, and merge the PR itself — no manual follow-up required.

Run in the background when handling multiple issues in parallel:

```sh
(cd /tmp/open-rts-issue-N && opencode run -m opencode/mimo-v2.5-free "...") &
```

The model flag (`-m`) selects the provider/model.  Verified working:

| Model string              | Notes                     |
|---------------------------|---------------------------|
| `opencode/mimo-v2.5-free` | Free tier, good for C/data-structure issues |
| `opencode/mimo-v2.5-pro`  | Paid, stronger reasoning  |

---

## Writing a good prompt

Paste the issue title, the key **Problem** paragraph, the relevant file
paths, the exact smoke-test command, and the **Acceptance criteria** list.
The agent has no memory of prior sessions, so everything it needs must be
in the prompt.

Minimal template:

```
Work on GitHub issue #N (<title>).

Problem: <one-paragraph summary from the issue body>

Key files: <path/to/foo.c>, <path/to/bar.h>

Verify with:
  env SDL_VIDEODRIVER=dummy build/bin/dark-colony --check data/DCOLONY <MAP>

Acceptance:
- <criterion 1>
- <criterion 2>
- Extensive unit tests cover normal behavior, boundary cases, and failure paths.
- All relevant unit tests and smoke tests pass before the PR is opened and merged.
```

---

## Running multiple agents in parallel

Give each agent its own worktree so they never interfere:

```sh
git worktree add /tmp/open-rts-issue-4 -b dc/issue-4-city-anchor
git worktree add /tmp/open-rts-issue-3 -b dc/issue-3-d2play01

(cd /tmp/open-rts-issue-4 && opencode run -m opencode/mimo-v2.5-free "
Work on issue #4 ...
When done: commit, push, and gh pr create --base main ...
") 2>&1 | tee /tmp/issue-4.log &

(cd /tmp/open-rts-issue-3 && opencode run -m opencode/mimo-v2.5-free "
Work on issue #3 ...
When done: commit, push, and gh pr create --base main ...
") 2>&1 | tee /tmp/issue-3.log &

wait
```

Each agent's transcript (todo list, tool calls, shell output, summary) is
written to its log file.  Clean up worktrees after merging:

```sh
git worktree remove /tmp/open-rts-issue-4
git worktree remove /tmp/open-rts-issue-3
```

---

## Monitoring the result

After the agent finishes:

1. **Read the transcript** — scan the todo list at the top and the summary
   at the bottom.  Verify the agent added extensive unit tests, ran the
   relevant unit and smoke-test commands, reviewed the PR, rebased when
   necessary, and merged it successfully.

2. Confirm the PR is merged and the linked issue is closed.  If the agent
   reports a review finding, rebase conflict, failed test, or merge blocker,
   investigate that blocker in the worktree rather than taking over the PR
   lifecycle.

---

## Creating pull requests

The preferred flow is to have the agent create, review, rebase, and merge the
PR itself.  Include these lines at the end of every prompt:

```
When finished:
1. Add and run extensive unit tests covering normal behavior, boundary cases, failure paths, and regressions for the changed behavior.
2. Commit all changes: git add -A && git commit -m "DC-N: <title>\n\nCloses #N\n\nCo-Authored-By: Claude <noreply@anthropic.com>"
3. Push the branch: git push -u origin <branch-name>
4. Open a PR to main: gh pr create --base main --title "DC-N: <title>" --body "..."
5. Review the PR and fix every legitimate finding; do not merge with unresolved review comments or failing checks.
6. Rebase the branch onto main when needed, resolve conflicts, rerun all tests, and force-push only the rebased branch with --force-with-lease.
7. Approve and merge the PR with `gh pr merge <number> --squash --delete-branch` after review and all checks pass.
```

### Manual fallback (only if the agent cannot complete the lifecycle)

The orchestrating agent should not normally perform these steps.  Use them
only to recover from an OpenCode failure, and record why the agent could not
complete the PR lifecycle.

```sh
cd /tmp/open-rts-issue-N
git add -A
git commit -m "DC-N: <title>

Closes #N

Co-Authored-By: Claude <noreply@anthropic.com>"
git push -u origin dc/issue-N-<slug>
gh pr create --base main --head dc/issue-N-<slug> --title "..." --body "..."
```

### Stacked PRs for dependent issues

When issue B's branch was created after issue A's (or B depends on A),
target B's PR at A's branch so the diff is incremental:

```sh
gh pr create --base main       --head dc/issue-A --title "..."   # PR A
gh pr create --base dc/issue-A --head dc/issue-B --title "..."   # PR B (stacked)
```

Merge A first.  GitHub automatically re-targets B to `main` once A lands.

---

## Agent-owned review, rebase, and merge

OpenCode owns every step after the branch is pushed.  For each PR it must:

1. **Read the diff**:
   ```sh
   gh pr diff <number>
   ```
   Check for spurious changes, leftover debug prints, and changes outside
   the issue scope.

2. **Run extensive unit tests and smoke-test the branch locally**:
   ```sh
   git fetch origin
   git checkout dc/issue-N-<slug>
   make
   build/bin/tests/dark-colony/test_game_model_headless data/DCOLONY
   env SDL_VIDEODRIVER=dummy build/bin/dark-colony --check data/DCOLONY SCENARIO/HUMAN/HUMAN03.MAP
   env SDL_VIDEODRIVER=dummy build/bin/dark-colony --check data/DCOLONY SCENARIO/MPLAYER/D2PLAY01.MAP
   ```

3. **Fix issues directly on the branch** if review or tests find something
   incomplete:
   ```sh
   # Work in the existing worktree — no need to re-create it
   cd /tmp/open-rts-issue-N
   # edit, build, test
   git add -p
   git commit -m "DC-N: fix up <what>"
   git push
   ```

4. **Review and approve the PR** after resolving all findings:
   ```sh
   gh pr review <number> --comment -b "..."
   gh pr review <number> --approve
   ```

### Rebase and merge

Once the PR is clean, extensive unit tests and smoke-tests pass, and all
review comments are resolved, rebase and merge it:

```sh
git fetch origin
git rebase origin/main
git push --force-with-lease
gh pr merge <number> --squash --delete-branch
```

Use `--squash` by default to keep `main` linear.  Use `--merge` only
when the agent made multiple meaningful commits that should be preserved.

After merging, GitHub closes the linked issue automatically when the PR
body contains `Closes #N`.  Verify:

```sh
gh issue view N   # state should be CLOSED
```

If the issue is still open (e.g. the agent forgot the `Closes` line):

```sh
gh issue close N --comment "Resolved in PR #<pr-number>."
```

### Clean up the worktree

After merging, remove the worktree and the local branch reference:

```sh
git worktree remove /tmp/open-rts-issue-N
git branch -d dc/issue-N-<slug>   # already deleted on remote by --delete-branch
```

---

## Known quirks

- **Binary `open-rts` does not exist** — the agent may try
  `build/bin/open-rts --game dark-colony`.  The correct binary is
  `build/bin/dark-colony` (one binary per game, no `--game` flag).
  Paste the correct smoke-test command in the prompt to avoid this.

- **The agent may over-silence stderr** — removing `fprintf(stderr, ...)`
  lines for "skipped" objects can hide real errors.  Verify that warnings
  you care about are still present after the agent finishes.

- **Worktree shares the build cache** — each worktree compiles into its
  own `build/` directory under its own root, so parallel agents do not
  race on object files.  If a worktree is missing `build/`, just run
  `make` inside it once before the agent starts.

- **`gh pr create` requires auth** — the agent needs `gh auth login` to
  have been run at least once on the machine.  If it fails, fall back to
  the manual PR step above.
