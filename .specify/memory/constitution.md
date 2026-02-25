<!-- Sync Impact Report
  Version: 0.0.0 → 1.0.0
  Modified principles: All (initial creation)
  Added sections: Core Principles (5), Build System, Governance
  Removed sections: None
  Templates requiring updates: ✅ constitution.md
  Follow-up TODOs: None
-->

# VirtualBox VMM Constitution

## Core Principles

### I. C/C++ Systems Programming Standards
All VMM code MUST be written in C/C++ following VirtualBox coding conventions. Code MUST compile without errors under GCC 13+ with warnings-as-errors enabled. All public APIs MUST use VirtualBox declaration macros (VMMR3DECL, VMM_INT_DECL, etc.) and follow the existing naming conventions (PGM prefix for Page Manager, etc.).

### II. Test-Driven Quality Assurance
Every new VMM subsystem feature or bug fix MUST include corresponding unit tests. Tests MUST be self-contained and runnable in driverless mode (SUPR3INIT_F_DRIVERLESS) without requiring kernel modules or hardware virtualization. Tests MUST use the IPRT test framework (RTTest*) and follow the existing test patterns in `src/VBox/VMM/testcase/`.

### III. Ring-Aware Architecture
Code MUST respect the ring separation model: Ring-3 (R3) for user-mode management, Ring-0 (R0) for kernel-mode execution, and shared code (All) for functions used across rings. Test code operates in Ring-3 context. Internal APIs use `_INT_DECL` variants; external APIs use standard `DECL` variants.

### IV. Build System Integration
All test targets MUST be properly integrated into the kBuild system via `Makefile.kmk` entries. Tests MUST link against `VMMStatic`, `DisasmR3`, and `Runtime` libraries. Build targets MUST use appropriate templates (`VBoxR3Exe` for full VM tests, `VBoxR3TstExe` for standalone tests).

### V. Backward Compatibility and Safety
Changes MUST NOT break existing functionality. Memory management code MUST handle edge cases (boundary addresses, NIL values, empty ranges). All guest physical address operations MUST validate inputs and return appropriate VBox status codes (VINF_SUCCESS, VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS, etc.).

## Build System

VirtualBox uses the kBuild build system with `kmk` as the make command. Configuration is done via `./configure` which generates `AutoConfig.kmk` and `env.sh`. Source `env.sh` before building. Test binaries are output to `out/linux.amd64/release/bin/testcase/`.

## Governance

This constitution governs all VMM development within the VirtualBox project scope. Amendments require documentation of rationale and impact assessment. All code contributions MUST comply with these principles and pass the existing CI/build system validation.

**Version**: 1.0.0 | **Ratified**: 2026-02-25 | **Last Amended**: 2026-02-25
