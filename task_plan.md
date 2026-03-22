# Task Plan: YAC / CrossPet Firmware Improvement

## Goal
Continuously improve the YAC (CrossPet) e-reader firmware — code quality, features, security, and documentation.

## Current Phase
Phase 1

## Phases

### Phase 1: Survey & Prioritize
- [x] Audit codebase for low-hanging fruit (dead code, tech debt, TODOs)
- [x] Check security (web server, input validation, credential handling)
- [x] Review recent commits for incomplete work
- [x] Survey docs for accuracy gaps
- **Status:** completed (findings in findings.md)

### Phase 2: Security Hardening
- [x] WebSocket filename path traversal — fixed: validate no `..`, `/`, `\` in filename
- [x] WebSocket size overflow — fixed: reject ≤0 or >100MB uploads
- [x] handleCreateFolder path traversal — fixed: reject `/`, `\`, `..` in folder name
- [x] WiFi cred encryption — documented: TODO comment added, legacy plaintext fallback documented
- [ ] Remaining: WebDAVHandler path traversal audit (not reviewed in this pass)
- [ ] Remaining: Settings API rate limiting (low priority, internal LAN device)
- **Status:** in_progress (3 vulns fixed, 2 items deferred)

### Phase 3: Code Quality
- [x] DarkGray dither: 4×4 Bayer matrix replaces checkerboard (commits: e0117f8)
- [x] BeeperActivity: all hardcoded strings now use i18n keys (commits: 2586469)
- [ ] Dead code sweep: codebase is clean — no significant dead code found
- [ ] Remaining: `GfxRenderer.cpp:587` TODO "Rotate bits" — needs clarification
- **Status:** in_progress

### Phase 4: Feature Work
- [ ] Triage Beeper app integration completeness
- [ ] Evaluate missing i18n strings for new features
- [ ] Assess KOReader sync documentation gap
- **Status:** pending

### Phase 5: Documentation
- [ ] Fill HAL layer docs gap
- [ ] Document sleep/wake power management
- [ ] Clarify build/flash process for new contributors
- **Status:** pending

## Key Questions
1. What's the build/flash toolchain? (PlatformIO per platformio.ini)
2. Is there a way to test without real hardware? (xteink-x4-emulator/ exists)
3. What's the current state of WiFi cred encryption?

## Decisions Made
| Decision | Rationale |
|----------|-----------|
| Use planning files in YAC repo root | Project-level context, not workspace-level |
| Prioritize security first | Already found path traversal, assume more |

## Errors Encountered
| Error | Attempt | Resolution |
|-------|---------|------------|
| None yet | - | - |
