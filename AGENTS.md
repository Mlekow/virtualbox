# AGENTS.md

## Cursor Cloud specific instructions

### Project Overview
Oracle VirtualBox v7.2.97 — a full x86_64 desktop hypervisor. The codebase is primarily C/C++ (~38k files) with Python build scripts, using Oracle's **kBuild** build system (`kmk`).

### Build System
- **kBuild** is a git submodule at `kBuild/`. It contains an inner `kBuild/kBuild/` directory with the actual build system files and binaries (e.g., `kBuild/kBuild/bin/linux.amd64/kmk`). The `--with-kbuild-path=/workspace/kBuild/kBuild` flag is required when running `./configure`.
- Always source `env.sh` before using `kmk` or VBox binaries: `source /workspace/env.sh`
- Build with: `VBOX_WITHOUT_LINUX_TEST_BUILDS=1 VBOX_WITHOUT_ADDITIONS=1 kmk`
  - `VBOX_WITHOUT_LINUX_TEST_BUILDS=1` skips kernel module test builds (kernel headers for the VM's kernel are unavailable)
  - `VBOX_WITHOUT_ADDITIONS=1` skips Guest Additions (which have X11 symbol check failures in this environment)
- The `--disable-hardening` configure flag is required since the VM environment lacks setuid/root support for hardened builds.
- Qt GUI (`--disable-qt`) is disabled because Ubuntu 24.04 ships Qt 6.4 but VirtualBox requires Qt >= 6.8. The CLI tools (`VBoxManage`, `VBoxHeadless`, etc.) build and work normally without it.

### Running Binaries
- Built binaries are in `out/linux.amd64/release/bin/`.
- Set `LD_LIBRARY_PATH=/workspace/out/linux.amd64/release/bin` when running VBox binaries.
- Key binaries: `VBoxManage` (CLI), `VBoxSVC` (service), `VBoxHeadless` (headless VM frontend).

### Running Tests
- Test binaries are in `out/linux.amd64/release/bin/testcase/` (~255 test executables).
- Run individual tests: `LD_LIBRARY_PATH=out/linux.amd64/release/bin ./out/linux.amd64/release/bin/testcase/<testname>`
- VirtualBox cannot actually start VMs in this environment (no kernel modules / hardware virtualization), but `VBoxManage` commands for VM creation, modification, and listing all work.

### Configure Command Reference
```sh
./configure --disable-hardening --disable-qt --with-kbuild-path=/workspace/kBuild/kBuild
```

### Lint / Static Analysis
- Python files can be checked with `python3 -m py_compile <file>` or `pylint`.
- C/C++ warnings are enforced by GCC flags configured during the build; the build itself acts as the primary lint check.

### Non-obvious Caveats
- The kBuild submodule nests the build system one level deeper than expected (`kBuild/kBuild/`). If configure fails with "No suitable kBuild path found", ensure the submodule is initialized (`git submodule update --init kBuild`) and use `--with-kbuild-path=/workspace/kBuild/kBuild`.
- Optional dependencies (Java/wsimport, OpenWatcom, libssh, libvncserver) are disabled at configure time without impacting core functionality.
- `gsoapsources` warning about missing GSOAP source package is harmless; it only affects web service builds.
