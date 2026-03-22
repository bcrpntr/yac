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
- [x] OTA firmware — fixed weak cert check on firmware download; added TODO for signature verification
- [x] Web UI XSS — all pages escape properly; no URL param reflection; JSON via ArduinoJson (safe)
- [ ] Remaining: WebDAVHandler path traversal audit (not reviewed in this pass)
- [ ] Remaining: Settings API rate limiting (low priority, internal LAN device)
- **Status:** in_progress (6 vulns fixed/improved, 2 items deferred)

### Phase 3: Code Quality
- [x] DarkGray dither: 4×4 Bayer matrix replaces checkerboard (commits: e0117f8)
- [x] BeeperActivity: all hardcoded strings now use i18n keys (commits: 2586469)
- [ ] Dead code sweep: codebase is clean — no significant dead code found
- [ ] Remaining: `GfxRenderer.cpp:587` TODO "Rotate bits" — needs clarification
- **Status:** in_progress

### Phase 4: Feature Work
- [x] KOReader sync documentation — full protocol documented in KOReaderSyncClient.h (auth, endpoints, conflict resolution, security notes)
- [x] News reader security review — no injection risk found; single-feed limitation noted
- [x] Settings web UI gaps — all 4 CrossPet settings confirmed exposed in web UI
- [x] Pet evolution sprites — fully documented (encoding, stages, variants, types)
- [ ] Triage Beeper app integration completeness
- [ ] Evaluate missing i18n strings for new features
- **Status:** in_progress

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
