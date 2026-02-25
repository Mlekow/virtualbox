# Implementation Plan: PGM Memory Mapping Tests

**Branch**: `001-pgm-memory-mapping-tests` | **Date**: 2026-02-25 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/001-pgm-memory-mapping-tests/spec.md`

## Summary

Extend VirtualBox's PGM unit test suite with comprehensive memory mapping tests covering RAM range registration/lookup, MMIO region mapping/unmapping, and ROM registration. All tests integrate into the existing `tstVMMUnitTests-1` harness, run in driverless mode, and use IPRT test framework.

## Technical Context

**Language/Version**: C/C++ (GCC 13.3.0)  
**Primary Dependencies**: VMMStatic, DisasmR3, IPRT Runtime  
**Storage**: N/A (in-memory VM state)  
**Testing**: IPRT RTTest framework (RTTestISub, RTTESTI_CHECK, RTTESTI_CHECK_RC_OK)  
**Target Platform**: Linux x86_64  
**Project Type**: C++ unit test extension  
**Performance Goals**: All tests complete in < 30 seconds  
**Constraints**: Must run in driverless mode without kernel modules; no hardware virtualization required  
**Scale/Scope**: ~3 new test source files, ~500-800 lines of test code total

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. C/C++ Standards | ✅ PASS | Tests written in C++ following VBox conventions |
| II. Test-Driven QA | ✅ PASS | This feature IS the test implementation |
| III. Ring-Aware Architecture | ✅ PASS | Tests run in Ring-3 driverless mode |
| IV. Build System Integration | ✅ PASS | Will add to Makefile.kmk for tstVMMUnitTests-1 |
| V. Backward Compatibility | ✅ PASS | Additive only, no existing code modified |

## Project Structure

### Documentation (this feature)

```text
specs/001-pgm-memory-mapping-tests/
├── spec.md              # Feature specification
├── plan.md              # This file
└── tasks.md             # Task breakdown (next step)
```

### Source Code (repository root)

```text
src/VBox/VMM/testcase/
├── tstVMMUnitTests-1.cpp     # Main test harness (modify: add new test function calls)
├── tstVMMUnitTests-1.h       # Header (modify: declare new test functions)
├── tstPGM-1.cpp              # Existing PGM tests (DO NOT MODIFY)
├── tstPGMMemMap.cpp           # NEW: Memory mapping tests (MMIO, ROM, extended RAM)
└── Makefile.kmk              # Build config (modify: add new source file)
```

**Structure Decision**: New tests are added as a new source file `tstPGMMemMap.cpp` compiled into the existing `tstVMMUnitTests-1` binary. This follows the existing pattern where `tstPGM-1.cpp` is compiled into the same binary. The header `tstVMMUnitTests-1.h` is extended with new function declarations.

## Research

### Existing Test Infrastructure

- `tstVMMUnitTests-1` creates a VM with `VMR3Create(1, NULL, VMCREATE_F_DRIVERLESS, ...)` and calls `testPGM(pVM)`.
- Tests use `VMR3ReqCallWait` to execute functions in the EMT (Emulation Thread) context when needed for API calls requiring it.
- The existing test in `tstPGM-1.cpp` tests lookup functions by directly manipulating `pVM->pgm.s.aRamRangeLookup[]`.
- PGMR3PhysRegisterRam is already tested indirectly (the VM creates default RAM ranges during init).

### Key APIs for New Tests

1. **PGMR3PhysRegisterRam(PVM, RTGCPHYS GCPhys, RTGCPHYS cb, const char *pszDesc)** — Register RAM range
2. **PGMR3PhysMmioRegister(PVM, PVMCPU, RTGCPHYS cb, const char *pszDesc, uint16_t *pidRamRange)** — Register MMIO region
3. **PGMR3PhysMmioMap(PVM, PVMCPU, RTGCPHYS GCPhys, RTGCPHYS cb, uint16_t idRamRange, ...)** — Map MMIO
4. **PGMR3PhysMmioUnmap(PVM, PVMCPU, RTGCPHYS GCPhys, RTGCPHYS cb, uint16_t idRamRange)** — Unmap MMIO
5. **PGMR3PhysGetRamRangeCount(PVM)** — Get total RAM range count
6. **PGMR3PhysGetRange(PVM, uint32_t iRange, ...)** — Get range info by index
7. **pgmPhysGetRangeSlow / pgmPhysGetRangeAtOrAboveSlow** — Internal lookup functions
8. **PGMR3PhysRomRegister(PVM, PPDMDEVINS, RTGCPHYS, RTGCPHYS, ...)** — Register ROM (requires device instance, complex setup)

### Constraints Discovered

- MMIO registration (`PGMR3PhysMmioRegister`) requires a PVMCPU and is an internal API (VMMR3_INT_DECL). It needs EMT context via `VMR3ReqCallWait`.
- ROM registration (`PGMR3PhysRomRegister`) requires a PPDMDEVINS (device instance), making standalone testing complex. ROM tests should focus on verifying ROM-related lookup behavior using the VM's existing ROM ranges (BIOS, VGA BIOS registered during VM creation).
- RAM registration via `PGMR3PhysRegisterRam` is a public API but should be called from EMT context.
- After registration, lookup verification can be done directly from the test process.

## Complexity Tracking

No constitution violations. Implementation is purely additive.
