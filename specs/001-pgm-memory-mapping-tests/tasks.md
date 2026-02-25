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

- [ ] T001 Add `tstPGMMemMap.cpp` to tstVMMUnitTests-1 sources in `src/VBox/VMM/testcase/Makefile.kmk`
- [ ] T002 Add `testPGMMemMap(PVM pVM)` declaration to `src/VBox/VMM/testcase/tstVMMUnitTests-1.h`
- [ ] T003 Add `testPGMMemMap(pVM)` call in `src/VBox/VMM/testcase/tstVMMUnitTests-1.cpp` main function

---

## Phase 2: User Story 1 - RAM Range Registration and Lookup Tests (Priority: P1) 🎯 MVP

**Goal**: Comprehensive RAM range registration and lookup testing with boundary/edge cases

**Independent Test**: Run `tstVMMUnitTests-1` and verify all PGM RAM range sub-tests pass

### Implementation for User Story 1

- [ ] T004 [US1] Create `src/VBox/VMM/testcase/tstPGMMemMap.cpp` with file header, includes, and `testPGMMemMap` function skeleton
- [ ] T005 [US1] Implement RAM range count and enumeration test using `PGMR3PhysGetRamRangeCount` and `PGMR3PhysGetRange` in `src/VBox/VMM/testcase/tstPGMMemMap.cpp`
- [ ] T006 [US1] Implement additional RAM registration test: register new RAM at high addresses via `VMR3ReqCallWait` + `PGMR3PhysRegisterRam`, verify lookups in `src/VBox/VMM/testcase/tstPGMMemMap.cpp`
- [ ] T007 [US1] Implement boundary address tests: test lookups at address 0, at range boundaries, and at addresses near RTGCPHYS_MAX in `src/VBox/VMM/testcase/tstPGMMemMap.cpp`
- [ ] T008 [US1] Implement gap verification tests: verify that lookups for addresses between ranges return correct error codes in `src/VBox/VMM/testcase/tstPGMMemMap.cpp`
- [ ] T009 [US1] Implement NIL_RTGCPHYS lookup test: verify lookup of NIL address returns not found in `src/VBox/VMM/testcase/tstPGMMemMap.cpp`

**Checkpoint**: RAM range tests compile and pass independently

---

## Phase 3: User Story 2 - MMIO Region Mapping Tests (Priority: P2)

**Goal**: Test MMIO registration, mapping, and unmapping through lookup table verification

**Independent Test**: Run `tstVMMUnitTests-1` and verify MMIO sub-tests pass

### Implementation for User Story 2

- [ ] T010 [US2] Implement MMIO registration test: register MMIO region via `VMR3ReqCallWait` + `PGMR3PhysMmioRegister`, verify count increases in `src/VBox/VMM/testcase/tstPGMMemMap.cpp`
- [ ] T011 [US2] Implement MMIO map test: map the registered MMIO and verify lookup at that address succeeds in `src/VBox/VMM/testcase/tstPGMMemMap.cpp`
- [ ] T012 [US2] Implement MMIO unmap test: unmap the MMIO region and verify lookup no longer finds it in `src/VBox/VMM/testcase/tstPGMMemMap.cpp`

**Checkpoint**: MMIO mapping lifecycle tests compile and pass

---

## Phase 4: User Story 3 - ROM Range Verification Tests (Priority: P3)

**Goal**: Verify ROM regions created during VM init are correctly represented in lookups

**Independent Test**: Run `tstVMMUnitTests-1` and verify ROM sub-tests pass

### Implementation for User Story 3

- [ ] T013 [US3] Implement ROM range presence test: enumerate ranges to find ROM-type entries and verify page descriptors in `src/VBox/VMM/testcase/tstPGMMemMap.cpp`

**Checkpoint**: ROM verification tests compile and pass

---

## Phase 5: Polish & Integration

**Purpose**: Final validation and cleanup

- [ ] T014 Verify full build with `source env.sh && VBOX_WITHOUT_LINUX_TEST_BUILDS=1 VBOX_WITHOUT_ADDITIONS=1 kmk`
- [ ] T015 Run `tstVMMUnitTests-1` and confirm all tests pass with SUCCESS
- [ ] T016 Commit all changes with descriptive message

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies — build system setup first
- **Phase 2 (US1 RAM)**: Depends on Phase 1 completion
- **Phase 3 (US2 MMIO)**: Depends on Phase 1 completion, independent of Phase 2
- **Phase 4 (US3 ROM)**: Depends on Phase 1 completion, independent of Phase 2/3
- **Phase 5 (Polish)**: Depends on all previous phases

### Within Each Phase

- T004 must complete before T005-T009 (creates the file)
- T005-T009 are sequential within US1 (same file, building on each other)
- T010-T012 are sequential within US2 (register → map → unmap lifecycle)

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Build system setup
2. Complete Phase 2: RAM range tests
3. **STOP and VALIDATE**: Build and run tstVMMUnitTests-1
4. If passing, proceed to US2 and US3

### Incremental Delivery

1. Phase 1 → Phase 2 → Validate (MVP: RAM tests)
2. Add Phase 3 → Validate (MMIO tests)
3. Add Phase 4 → Validate (ROM tests)
4. Phase 5 → Final validation and commit
