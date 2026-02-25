# Feature Specification: PGM Memory Mapping Unit Tests

**Feature Branch**: `001-pgm-memory-mapping-tests`  
**Created**: 2026-02-25  
**Status**: Draft  
**Input**: User description: "Implement PGM memory mapping unit tests for VirtualBox VMM"

## User Scenarios & Testing *(mandatory)*

### User Story 1 - RAM Range Registration and Lookup Tests (Priority: P1)

A VMM developer registers guest physical RAM ranges and verifies they can be correctly looked up through the PGM subsystem's internal lookup mechanisms. The tests exercise `PGMR3PhysRegisterRam` and the various range/page lookup slow-path functions, covering boundary addresses, gaps between ranges, and full address space coverage.

**Why this priority**: RAM range lookup is the foundational operation for all PGM physical memory management. Every MMIO, ROM, and memory access operation depends on correct RAM range registration and lookup.

**Independent Test**: Can be fully tested by creating a VM in driverless mode, registering RAM ranges, and verifying lookups return correct results for hit/miss/boundary addresses.

**Acceptance Scenarios**:

1. **Given** a VM with multiple RAM ranges registered, **When** looking up an address within a range, **Then** the correct range and page descriptor are returned with VINF_SUCCESS.
2. **Given** a VM with gaps between RAM ranges, **When** looking up an address in a gap, **Then** VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS is returned and pointers are NULL.
3. **Given** a VM with RAM near the top of the 64-bit physical address space, **When** looking up addresses near RTGCPHYS_MAX, **Then** lookups correctly handle the boundary without wrapping or overflow.
4. **Given** a reduced lookup table (2 entries, or entries with gaps), **When** exercising all lookup paths, **Then** the binary search and TLB caching still return correct results.

---

### User Story 2 - MMIO Region Mapping and Unmapping Tests (Priority: P2)

A VMM developer registers MMIO regions, maps them to guest physical addresses, and verifies they are correctly inserted into the physical memory layout. The tests verify `PGMR3PhysMmioRegister`, `PGMR3PhysMmioMap`, and `PGMR3PhysMmioUnmap` produce correct lookup table state.

**Why this priority**: MMIO mapping is a critical path for device emulation. Correct mapping/unmapping ensures devices can be hotplugged and PCI BARs can be relocated without memory corruption.

**Independent Test**: Can be tested by registering MMIO ranges, mapping them at various addresses, verifying lookup results, then unmapping and confirming the region is no longer accessible.

**Acceptance Scenarios**:

1. **Given** a registered MMIO region, **When** mapping it to a guest physical address, **Then** the lookup table contains the MMIO range and lookups within the range succeed.
2. **Given** a mapped MMIO region, **When** unmapping it, **Then** lookups for addresses in the former MMIO region return "not found".
3. **Given** multiple MMIO regions mapped at different addresses, **When** querying various addresses, **Then** each query returns the correct MMIO range or "not found" for gaps.

---

### User Story 3 - ROM Registration Tests (Priority: P3)

A VMM developer registers ROM regions (e.g., BIOS, VGA BIOS) and verifies they appear in the physical memory layout. Tests exercise `PGMR3PhysRomRegister` and verify ROM protection modes.

**Why this priority**: ROM regions are essential for BIOS/firmware emulation. Incorrect ROM registration can prevent VM boot.

**Independent Test**: Can be tested by registering ROM regions with shadow pages, verifying lookups succeed, and testing ROM protect mode transitions.

**Acceptance Scenarios**:

1. **Given** a ROM region registered at a guest physical address, **When** looking up pages within the ROM range, **Then** the lookup succeeds and returns correct page descriptors.
2. **Given** a registered ROM, **When** changing its protection mode, **Then** the ROM protection state is correctly updated.

---

### Edge Cases

- What happens when registering RAM at address 0x0 (lowest possible)?
- What happens when registering RAM at addresses near RTGCPHYS_MAX?
- How does the lookup behave with exactly 1 RAM range in the table?
- What happens when looking up NIL_RTGCPHYS?
- How does the system handle overlapping registration attempts?

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Test binary MUST compile and link against VMMStatic, DisasmR3, and Runtime libraries using the VBoxR3Exe template.
- **FR-002**: Tests MUST run in driverless mode (SUPR3INIT_F_DRIVERLESS) without kernel modules.
- **FR-003**: Tests MUST use the IPRT test framework (RTTestISub, RTTESTI_CHECK, RTTESTI_CHECK_RC_OK).
- **FR-004**: Tests MUST exercise all RAM range lookup slow-path functions: pgmPhysGetRangeSlow, pgmPhysGetRangeAtOrAboveSlow, pgmPhysGetPageAndRangeExSlow, pgmPhysGetPageSlow, pgmPhysGetPageExSlow, pgmPhysGetPageAndRangeExSlowLockless.
- **FR-005**: Tests MUST verify boundary conditions (address 0, max address, gaps between ranges).
- **FR-006**: Tests MUST include lookup table variations (reduced entries, gaps between entries).
- **FR-007**: New test functions MUST be integrated into the existing tstVMMUnitTests-1 harness.
- **FR-008**: Tests MUST verify MMIO region registration, mapping, and unmapping correctness.
- **FR-009**: Tests MUST verify ROM region registration and protection state.

### Key Entities

- **PGMRAMRANGE**: Represents a contiguous range of guest physical memory (RAM, MMIO, or ROM).
- **PGMPAGE**: Descriptor for a single guest physical page within a RAM range.
- **PGMRAMRANGELOOKUPENTRY**: Entry in the RAM range lookup table used for binary search.
- **RamRangeUnion**: Combined structure holding the lookup table metadata (count, etc.).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All new test cases compile without errors and link successfully with the kBuild system.
- **SC-002**: All test cases pass when run via the tstVMMUnitTests-1 binary, reporting SUCCESS for each sub-test.
- **SC-003**: Tests cover RAM registration, MMIO mapping/unmapping, and ROM registration scenarios.
- **SC-004**: Edge cases (boundary addresses, NIL values, single-entry tables, gaps) are explicitly tested.
- **SC-005**: The test binary runs to completion in under 30 seconds.
