# Delegating issues to an opencode agent

This project uses [opencode](https://opencode.ai) to delegate well-scoped GitHub issues
to a background AI agent.  The workflow below describes how to run it, review
the result, and create pull requests from the agent's changes.

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
1. Commit all changes with message 'DC-N: <title>\n\nCloses #N'.
2. Push the branch: git push -u origin dc/issue-N-<slug>
3. Create a PR to main: gh pr create --base main --title '...' --body '...'
")
```

The last three lines in the prompt tell the agent to commit, push, and
open the PR itself — no manual follow-up required.

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

## Reviewing the result

After the agent finishes:

1. **Read the transcript** — scan the todo list at the top and the summary
   at the bottom.  Verify the agent ran the smoke-test command and it
   passed.

2. **Check the diff** — `git diff --stat` shows which files changed.
   Pre-existing uncommitted changes in the working tree are included; the
   agent stacks on top of whatever was already there.

3. **Build and smoke-test yourself**:

   ```sh
   make
   env SDL_VIDEODRIVER=dummy build/bin/dark-colony --check data/DCOLONY SCENARIO/HUMAN/HUMAN03.MAP
   env SDL_VIDEODRIVER=dummy build/bin/dark-colony --check data/DCOLONY SCENARIO/MPLAYER/D2PLAY01.MAP
   env SDL_VIDEODRIVER=dummy build/bin/test_game_model_headless data/DCOLONY
   ```

4. Watch for spurious changes — the agent sometimes makes cosmetic or
   unrelated edits.  Review each file in the diff before committing.

---

## Creating pull requests

The preferred flow is to have the agent create the PR itself.  Include
these three lines at the end of every prompt:

```
When finished:
1. Commit all changes: git add -A && git commit -m "DC-N: <title>\n\nCloses #N\n\nCo-Authored-By: Claude <noreply@anthropic.com>"
2. Push the branch: git push -u origin <branch-name>
3. Open a PR to main: gh pr create --base main --title "DC-N: <title>" --body "..."
```

### Manual fallback (if the agent did not create a PR)

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
