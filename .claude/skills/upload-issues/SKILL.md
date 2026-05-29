---
name: upload-issues
description: Upload issues from a phase issues file to GitHub one by one with proper labels and dependencies.
---

# Skill: Upload Phase Issues to GitHub

Upload issues from a phase issues file to GitHub one by one, with proper labels (prefixed by phase) and dependencies.

## Usage

```
/upload-issues <phase-issues-file>
```

Example: `/upload-issues @specification/phase1-core-mechanics.md`

## Instructions

### Step 1: Read the phase issues file

Read the provided phase issues file (e.g., `specification/phase{N}-*.md`).

Determine from the file:
- **Phase number** (y): from the filename or heading (e.g., `phase1-core-mechanics.md` -> y = `1`)
- **Label prefix**: `p{y}::` (e.g., `p1::`)

Parse the **Issues Summary Table** to extract for each issue:
- `ID` (e.g., PCP-001)
- `Title`
- `Size` (S, M, L)
- `Stage` (e.g., "1 -- Foundation")
- `Dependencies` (list of PCP-xxx IDs)

Then parse each **detailed issue section** (heading with PCP-xxx) to extract:
- `Description`
- `What needs to be done` (full content)
- `Dependencies`
- `Expected result`
- `Acceptance criteria` (checklist)

### Step 2: Confirm with user

Show the user a summary of what will be created:
- Number of issues
- Label prefix (e.g., `p1::`)
- Full list of labels that will be created
- Ask for confirmation before proceeding

### Step 3: Create labels (if they don't exist)

All labels MUST be prefixed with `p{y}::` (phase number).

Label format: `p{y}::{category}:{value}`

Use `gh` to create these labels if they don't already exist:

```bash
# Phase label
gh label create "p1::phase:1" --color "0E8A16" --description "Phase 1" 2>/dev/null || true

# Size labels
gh label create "p1::size:S" --color "28A745" --description "Small (1-2 days)" 2>/dev/null || true
gh label create "p1::size:M" --color "FFC107" --description "Medium (3-5 days)" 2>/dev/null || true
gh label create "p1::size:L" --color "DC3545" --description "Large (5-8 days)" 2>/dev/null || true

# Stage labels (extract from issues)
gh label create "p1::stage:Foundation" --color "6F42C1" 2>/dev/null || true
# ... etc for each unique stage found in the issues
```

### Step 4: Create issues ONE BY ONE

**IMPORTANT:** Issues must be created one at a time, sequentially. After creating each issue:
1. Show the user the result (issue number, URL)
2. Proceed to the next issue immediately (do not wait for confirmation between issues)

For each issue (in order from the summary table):

1. Build the issue body in markdown:

```markdown
## Description
{description from the detailed section}

## What needs to be done
{full content from the detailed section}

## Dependencies
{dependency list, with references to already-created issue numbers}

## Expected result
{expected result from the detailed section}

## Acceptance criteria
{checklist from the detailed section}

---
**ID:** {PCP-xxx}
**Size:** {S/M/L}
**Phase:** {y}
**Stage:** {stage name}
```

2. Create the issue with a single `gh issue create` command (one issue per command, never batch):

```bash
gh issue create \
  --title "PCP-xxx: {title}" \
  --label "p1::phase:1,p1::size:{S/M/L},p1::stage:{stage-name}" \
  --body "$(cat <<'BODY'
{issue body}
BODY
)"
```

3. Record the mapping: PCP-xxx -> GitHub issue #number

4. Report to user: `Created PCP-xxx -> #{number}: {title}`

5. If the issue has dependencies on already-created issues, add a comment:

```bash
gh issue comment {issue-number} --body "Blocked by #{dep-issue-number} (PCP-xxx)"
```

6. Move to the next issue.

### Step 5: Generate report

After all issues are created, generate a report file at:
`specification/phase{N}-github-report.md`

Content:

```markdown
# Phase {y} -- GitHub Issues Report

**Uploaded:** {date}
**Repository:** {github repo URL}
**Total issues:** {count}

## Issue Mapping

| PCP ID | GitHub # | Title | Labels | URL |
|--------|----------|-------|--------|-----|
| PCP-001 | #5 | One-second tick | p1::phase:1, p1::size:S, p1::stage:Foundation | {url} |
| ... | ... | ... | ... | ... |

## Labels Created

- p1::phase:1
- p1::size:S, p1::size:M, p1::size:L
- p1::stage:{list}
```

### Step 6: Report to user

Show the user:
- Total issues created
- Link to the GitHub issues page
- Path to the generated report file

## Error Handling

- If `gh` is not authenticated, tell the user to run `gh auth login`
- If an issue already exists with the same title, skip it and note in the report
- If label creation fails, continue (labels may already exist)
- On any failure, report what was created so far and what remains
