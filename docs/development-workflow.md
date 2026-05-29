# Development workflow (Claude Code skills)

Three slash commands automate the spec-to-release pipeline. Run them inside
Claude Code (`claude` CLI or IDE extension) from the project root.

## /upload-issues -- publish a phase spec to GitHub

Parses a phase specification file, creates GitHub labels (prefixed by phase,
e.g. `p1::stage:Foundation`), and uploads each issue with its full description,
acceptance criteria, and dependency links.

```
/upload-issues @specification/phase1-core-mechanics.md
```

What it does:
1. Parses the Issues Summary Table and detailed sections from the spec file.
2. Shows a summary (issue count, labels) and asks for confirmation.
3. Creates phase-prefixed labels (`p1::phase:1`, `p1::size:S`, `p1::stage:*`).
4. Creates issues one by one, mapping PCP-xxx IDs to GitHub issue numbers.
5. Adds "Blocked by #N" comments for issues with dependencies.
6. Writes a report to `specification/phase{N}-github-report.md`.

Prerequisites: `gh auth login` (GitHub CLI authenticated).

## /execute-issues -- implement, validate, commit, push

Picks up the GitHub issues created by `/upload-issues` and executes them in
dependency order: reads the spec, implements the code changes, validates,
commits (one commit per issue with `Closes #N`), pushes, and closes each issue
with a summary comment. When every issue in the phase is done it bumps the
version and tags the release.

```
/execute-issues p1::phase:1                      # all open issues in phase 1
/execute-issues p1::phase:1 --issue PCP-003      # single issue
/execute-issues p1::phase:1 --dry-run            # show plan, change nothing
```

What it does:
1. Verifies clean working tree, reads the phase spec + GitHub report.
2. Builds an execution queue respecting dependency order.
3. For each issue: implement -> validate -> commit -> push -> close on GitHub.
4. On failure: reverts changes, comments on the issue, asks whether to continue.
5. When the full phase completes: bumps version (Phase 1 -> `v0.1.0`), creates
   `RELEASE.txt`, tags and pushes.
6. Writes `specification/phase{N}-execution-report.md`.

## /release-version -- manual version bump

Bumps the version, writes release notes, commits, tags, and pushes. Normally
called automatically by `/execute-issues` on phase completion, but can also be
run standalone.

```
/release-version 0.1.0
/release-version 0.1.1 Fix disk I/O overflow; Add GPU threshold to Stats
```

What it does:
1. Validates the version is newer than the current one.
2. Auto-generates changelog from commits since the last tag (or uses provided items).
3. Updates `RELEASE.txt` (creates it if missing).
4. Commits, creates an annotated tag `v{version}`, and pushes.

## Typical workflow

```
1.  Write specification/phase{N}-*.md        (issue specs with PCP-xxx IDs)
2.  /upload-issues @specification/phase{N}-*.md   (specs -> GitHub issues)
3.  /execute-issues p{N}::phase:{N}              (implement all issues)
    -- or run issues one at a time with --issue PCP-xxx
4.  /release-version (only if not auto-triggered by step 3)
```
