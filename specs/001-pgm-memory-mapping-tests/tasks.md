# Tasks: PGM Memory Mapping Tests

**Input**: Design documents from `/specs/001-pgm-memory-mapping-tests/`
**Prerequisites**: plan.md (required), spec.md (required)

**Tests**: Tests are the primary deliverable of this feature — implementation IS tests.

**Organization**: Tasks grouped by user story for independent validation.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Build Infrastructure)

**Purpose**: Integrate new test source into the kBuild system and test harness

- [X] T001 Add `tstPGMMemMap.cpp` to tstVMMUnitTests-1 sources in `src/VBox/VMM/testcase/Makefile.kmk`
- [X] T002 Add `testPGMMemMap(PVM pVM)` declaration to `src/VBox/VMM/testcase/tstVMMUnitTests-1.h`
- [X] T003 Add `testPGMMemMap(pVM)` call in `src/VBox/VMM/testcase/tstVMMUnitTests-1.cpp` main function

---

## Phase 2: User Story 1 - RAM Range Registration and Lookup Tests (Priority: P1) 🎯 MVP

**Goal**: Comprehensive RAM range registration and lookup testing with boundary/edge cases

**Independent Test**: Run `tstVMMUnitTests-1` and verify all PGM RAM range sub-tests pass

### Implementation for User Story 1

- [X] T004 [US1] Create `src/VBox/VMM/testcase/tstPGMMemMap.cpp` with file header, includes, and `testPGMMemMap` function skeleton
- [X] T005 [US1] Implement RAM range count and enumeration test using `PGMR3PhysGetRamRangeCount` and `PGMR3PhysGetRange` in `src/VBox/VMM/testcase/tstPGMMemMap.cpp`
- [X] T006 [US1] Implement additional RAM registration test: register new RAM at high addresses via `VMR3ReqCallWait` + `PGMR3PhysRegisterRam`, verify lookups in `src/VBox/VMM/testcase/tstPGMMemMap.cpp`
- [X] T007 [US1] Implement boundary address tests: test lookups at address 0, at range boundaries, and at addresses near RTGCPHYS_MAX in `src/VBox/VMM/testcase/tstPGMMemMap.cpp`
- [X] T008 [US1] Implement gap verification tests: verify that lookups for addresses between ranges return correct error codes in `src/VBox/VMM/testcase/tstPGMMemMap.cpp`
- [X] T009 [US1] Implement NIL_RTGCPHYS lookup test: verify lookup of NIL address returns not found in `src/VBox/VMM/testcase/tstPGMMemMap.cpp`

**Checkpoint**: ✅ RAM range tests compile and pass

---

## Phase 3: User Story 2 - Physical Memory Read/Write and Mapping Tests (Priority: P2)

**Goal**: Test physical memory read/write operations and GCPhys-to-host-pointer mapping

**Independent Test**: Run `tstVMMUnitTests-1` and verify memory R/W sub-tests pass

### Implementation for User Story 2

- [X] T010 [US2] Implement physical memory read/write test: write patterns via `PGMR3PhysWriteExternal`, read back via `PGMR3PhysReadExternal`, verify data in `src/VBox/VMM/testcase/tstPGMMemMap.cpp`
- [X] T011 [US2] Implement typed read/write tests: `PGMR3PhysWriteU8/U32/U64` + `PGMR3PhysReadU8/U32/U64` in `src/VBox/VMM/testcase/tstPGMMemMap.cpp`
- [X] T012 [US2] Implement GCPhys2CCPtr mapping tests: writable and read-only mappings via `PGMR3PhysGCPhys2CCPtrExternal` / `PGMR3PhysGCPhys2CCPtrReadOnlyExternal` in `src/VBox/VMM/testcase/tstPGMMemMap.cpp`

**Checkpoint**: ✅ Memory R/W and mapping tests compile and pass

---

## Phase 4: User Story 3 - Range At-or-Above and Integrity Tests (Priority: P3)

**Goal**: Verify at-or-above lookup semantics and PGM internal consistency

**Independent Test**: Run `tstVMMUnitTests-1` and verify at-or-above and integrity sub-tests pass

### Implementation for User Story 3

- [X] T013 [US3] Implement range at-or-above lookup tests using `pgmPhysGetRangeAtOrAboveSlow` in `src/VBox/VMM/testcase/tstPGMMemMap.cpp`
- [X] T014 [US3] Implement PGM integrity check test using `PGMR3CheckIntegrity` in `src/VBox/VMM/testcase/tstPGMMemMap.cpp`

**Checkpoint**: ✅ At-or-above and integrity tests compile and pass

---

## Phase 5: Polish & Integration

**Purpose**: Final validation and cleanup

- [X] T015 Verify full build with `source env.sh && VBOX_WITHOUT_LINUX_TEST_BUILDS=1 VBOX_WITHOUT_ADDITIONS=1 kmk tstVMMUnitTests-1`
- [X] T016 Run `tstVMMUnitTests-1` and confirm all tests pass with SUCCESS
- [X] T017 Commit all changes with descriptive message

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies — build system setup first
- **Phase 2 (US1 RAM)**: Depends on Phase 1 completion
- **Phase 3 (US2 Memory R/W)**: Depends on Phase 1 completion
- **Phase 4 (US3 At-or-Above)**: Depends on Phase 1 completion
- **Phase 5 (Polish)**: Depends on all previous phases

## Results

All 21 test sub-cases pass:
- 12 existing PGM tests (pgmPhysGetRangeSlow etc., 1st and 2nd round)
- 9 new memory mapping tests (RAM enumeration, registration, boundary lookups, gap detection, at-or-above, read/write, GCPhys2CCPtr, integrity check)
