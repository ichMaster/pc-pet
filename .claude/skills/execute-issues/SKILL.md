---
name: execute-issues
description: Execute GitHub issues for a phase sequentially - implement, validate, commit, push, and generate a report.
---

# Skill: Execute GitHub Issues

Execute GitHub issues for a phase sequentially: implement, validate, commit, push, and generate a report.

## Usage

```
/execute-issues <label> [--issue PCP-xxx] [--dry-run]
```

The `<label>` is the GitHub phase label exactly as it appears (e.g., `p1::phase:1`).

- `/execute-issues p1::phase:1` -- execute all issues labeled `p1::phase:1`
- `/execute-issues p1::phase:1 --issue PCP-003` -- execute a single issue from that phase
- `/execute-issues p1::phase:1 --dry-run` -- show execution plan without making changes

## Instructions

### Step 0: Verify prerequisites

1. Confirm we are on the expected branch (e.g., `main` or the user's working branch)
2. Confirm working tree is clean (`git status`)
3. Confirm `gh` is authenticated
4. Parse the label to determine phase:
   - Label `p1::phase:1` -> phase `y=1`
5. Fetch issues from GitHub:
   ```bash
   gh issue list --label "{label}" --state open --limit 100
   ```
6. Read the phase issues file for detailed descriptions: `specification/phase{y}-*.md`
7. If a GitHub report exists (`phase{y}-github-report.md`), read the PCP-to-GitHub# mapping

### Step 1: Build execution queue

From the GitHub issue list, build an ordered queue based on dependencies:
- Parse PCP-xxx IDs from issue titles (format: `PCP-xxx: {title}`)
- Determine dependency order from the phase issues file dependency tree
- Issues with no unmet dependencies go first
- Skip issues already closed on GitHub
- If `--issue PCP-xxx` is specified, execute only that issue (but verify its dependencies are closed)

Show the user the execution plan and ask for confirmation.

### Step 2: Execute each issue (loop)

For each issue in the queue:

#### 2a. Assign and announce

Print: `--- Starting PCP-xxx: {title} ---`

#### 2b. Read issue details

Read the full issue description from the phase issues file (the detailed section for this PCP-xxx).

#### 2c. Implement

Execute the tasks described in the issue. Follow the project conventions in `CLAUDE.md`. Key rules:

- **Firmware changes:** edit `firmware/pc_tamagotchi/pc_tamagotchi.ino` following the conventions:
  - Globals above `setup()` (C++ visibility)
  - M_-prefixed mood enum values (ESP32 ROM headers define BUSY/HOT)
  - BLE callback stays tiny (no parsing/buffers/printf)
  - Shared state guarded with `g_mux` (portMUX critical sections)
  - Rendering through offscreen `M5Canvas` then `pushSprite`
- **Agent changes:** edit `agent/pc_pet_agent.py`
- **Protocol changes:** update `docs/protocol.md` alongside agent + firmware
- Follow existing code style and patterns
- Verify the Arduino sketch compiles if possible (`arduino-cli compile` or note that manual verification is needed)

#### 2d. Validate

Run validation checks:

1. **Syntax check (agent):** `python3 -m py_compile {changed_py_files}` for each new/modified .py file
2. **Import check (agent):** `python3 -c "import {module}"` for changed modules
3. **Protocol consistency:** verify agent packet format matches firmware parser and docs/protocol.md
4. **Acceptance criteria:** go through each criterion from the issue and verify

Record pass/fail for each check.

Note: firmware compilation requires Arduino IDE or PlatformIO with the board connected. If not available, note that firmware validation is deferred to manual upload.

#### 2e. Commit

```bash
git add {specific files created/modified}
git commit -m "$(cat <<'EOF'
PCP-xxx: {title}

{1-2 sentence summary of what was implemented}

Closes #{github-issue-number}

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>
EOF
)"
```

#### 2f. Push

```bash
git push
```

#### 2g. Close issue with summary

```bash
gh issue close {issue-number} --comment "$(cat <<'EOF'
## Implementation Summary

**Commit:** {commit-hash}
**Files changed:** {count}

### What was done
{bullet list of key changes}

### Validation
{pass/fail status for each check}

### Acceptance criteria
{checklist with pass/fail}
EOF
)"
```

#### 2h. Log progress

Append to the in-memory execution log:
- Issue ID, title
- Commit hash
- Files changed (list)
- Validation results
- Status: success/partial/failed

### Step 3: Handle failures

If implementation or validation fails for an issue:

1. Do NOT commit broken code
2. Stash or revert changes: `git checkout -- .`
3. Add a comment to the GitHub issue explaining what failed
4. Log the failure
5. Ask the user: continue to next issue (if no dependency), or stop?

### Step 3b: Version bump on phase completion

After ALL issues in the phase are completed successfully (none failed, none remaining):

1. Determine the target version from the phase number:
   - Phase 1 -> `0.1.0`, Phase 2 -> `0.2.0`, etc.

2. Update `README.md` with version note if appropriate

3. Update or create `RELEASE.txt` -- prepend a new version entry:

```
Version {version} ({YYYY-MM-DD})
---------------------------
- {PCP-xxx title}: {1-sentence summary of what was implemented}
- {PCP-xxx title}: {1-sentence summary}
...
```

4. Commit the version bump:

```bash
git add README.md RELEASE.txt
git commit -m "$(cat <<'EOF'
Release v{version} -- Phase {y} complete

All {count} issues implemented and validated.

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>
EOF
)"
```

5. Tag the release:

```bash
git tag -a v{version} -m "Phase {y}: {phase milestone name}"
```

6. Report to user: `Phase {y} complete -> version bumped to {version}, tagged v{version}`

If some issues failed or were skipped, do NOT bump the version. Note in the execution report that the phase is incomplete.

### Step 4: Generate execution report

After all issues are processed (or on stop), generate:
`specification/phase{y}-execution-report.md`

```markdown
# Phase {y} -- Execution Report

**Date:** {date}
**Branch:** {branch name}
**Label:** {label}
**Target version:** {version}
**Executed by:** Claude Code

## Summary

| Status | Count |
|--------|-------|
| Completed | {n} |
| Failed | {n} |
| Skipped | {n} |
| Remaining | {n} |

## Issues

| # | PCP ID | Title | Status | Commit | Files | Tests |
|---|--------|-------|--------|--------|-------|-------|
| 1 | PCP-001 | One-second tick | completed | a1b2c3d | 1 | N/A |
| 2 | PCP-002 | Buzzer patterns | completed | e4f5g6h | 1 | N/A |
| ... | ... | ... | ... | ... | ... | ... |

## Detailed Results

### PCP-001: One-second tick

**Status:** completed
**Commit:** a1b2c3d
**Files changed:**
- `firmware/pc_tamagotchi/pc_tamagotchi.ino` (modified)

**Validation:**
- [x] Protocol consistency: no protocol change
- [x] Acceptance criteria: all pass
- [ ] Firmware compilation: deferred to manual upload

---

### PCP-002: Buzzer patterns
...

## Next Steps

{List of remaining issues not yet executed, with their dependencies}
```

Commit and push this report:

```bash
git add specification/phase{y}-execution-report.md
git commit -m "$(cat <<'EOF'
Add phase {y} execution report

{n} issues completed, {n} failed, {n} remaining.

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>
EOF
)"
git push
```

## Important Rules

- **One issue at a time.** Never work on multiple issues simultaneously.
- **Dependency order.** Never start an issue whose dependencies are not closed.
- **Clean commits.** Each issue = one commit. No mixing work across issues.
- **No broken code.** Only commit code that passes validation.
- **No emoji.** Never in code, comments, UI text, or commit messages (per CLAUDE.md).
- **M_-prefixed moods.** The ESP32 ROM headers define BUSY/HOT -- always use M_BUSY, M_HOT, etc.
- **BLE callback stays tiny.** No parsing, buffers, or Serial.printf inside the callback.
- **Ask on ambiguity.** If an issue description is unclear, ask the user rather than guessing.
- **Progress updates.** Print a short status line after each issue completes.
