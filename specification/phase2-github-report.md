# Phase 2 -- GitHub Issues Report

**Uploaded:** 2026-05-30
**Repository:** https://github.com/ichMaster/pc-pet
**Total issues:** 7

## Issue Mapping

| PCP ID | GitHub # | Title | Labels | URL |
|--------|----------|-------|--------|-----|
| PCP-005 | #9 | ENV III sensor bring-up | p2::phase:2, p2::size:M, p2::stage:Sensors | https://github.com/ichMaster/pc-pet/issues/9 |
| PCP-006 | #10 | ENV screen | p2::phase:2, p2::size:M, p2::stage:Display | https://github.com/ichMaster/pc-pet/issues/10 |
| PCP-007 | #11 | Pressure trend log | p2::phase:2, p2::size:S, p2::stage:Trend | https://github.com/ichMaster/pc-pet/issues/11 |
| PCP-008 | #12 | ENV mood modifier | p2::phase:2, p2::size:M, p2::stage:Mood | https://github.com/ichMaster/pc-pet/issues/12 |
| PCP-009 | #13 | ENV telemetry to PC | p2::phase:2, p2::size:M, p2::stage:Telemetry | https://github.com/ichMaster/pc-pet/issues/13 |
| PCP-010 | #14 | ENV log retention (rotation) | p2::phase:2, p2::size:S, p2::stage:Telemetry | https://github.com/ichMaster/pc-pet/issues/14 |
| PCP-011 | #15 | ENV CSV viewer TUI (separate app) | p2::phase:2, p2::size:M, p2::stage:Tooling | https://github.com/ichMaster/pc-pet/issues/15 |

## Dependencies

| Issue | Blocked by |
|-------|-----------|
| #10 (PCP-006) | #9 |
| #11 (PCP-007) | #9, #10 |
| #12 (PCP-008) | #9, #11 |
| #13 (PCP-009) | #9 |
| #14 (PCP-010) | #13 |
| #15 (PCP-011) | #13 (soft: #14) |

## Labels Created

- p2::phase:2
- p2::size:S, p2::size:M
- p2::stage:Sensors, p2::stage:Display, p2::stage:Trend, p2::stage:Mood, p2::stage:Telemetry, p2::stage:Tooling
