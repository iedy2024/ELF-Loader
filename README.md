# ELF Loader

A userspace loader for 64-bit Linux ELF executables, written in C. It does the kernel's `execve()` job by hand: it parses the binary, maps its segments into memory with the right page permissions, builds the initial process stack and jumps to the program's entry point.

It runs syscall-only binaries with no libc, static non-PIE programs and static PIE programs, which it relocates to a random base address.

## Why this matters for security

Most memory-corruption exploitation relies on knowing exactly how a process is laid out in memory. This project builds that layout from scratch:

- **Segment permissions and W^X.** Segments are mapped and then locked down with `mprotect()` according to their `PF_R` / `PF_W` / `PF_X` flags, so code is not writable and data is not executable. The test suite checks this with binaries that crash if `.text` is writable or `.rodata` is executable.
- **ASLR for PIE binaries.** Position-independent executables are loaded at a base address chosen by the kernel, and every segment address and the entry point are adjusted by that base. This is the mechanism an attacker has to defeat with an info leak.
- **The auxiliary vector.** The loader builds `auxv` by hand (`AT_PHDR`, `AT_PHNUM`, `AT_ENTRY`, `AT_BASE`, `AT_PAGESZ`, `AT_RANDOM`, ...). `AT_RANDOM` is filled from `/dev/urandom`, because glibc uses those 16 bytes to seed the **stack canary** and pointer mangling. Getting it wrong crashes libc at startup.
- **Initial stack layout.** `argc`, `argv`, `envp` and `auxv` are placed exactly as the System V x86-64 ABI expects, with 16-byte alignment. The same layout matters when writing shellcode, ROP chains or `ret2libc` payloads.
- **Header validation.** Invalid ELF magic and non-64-bit binaries are rejected before anything is mapped.

## How it works

1. `mmap()` the file read-only and validate the ELF header (magic bytes, `ELFCLASS64`).
2. For PIE (`ET_DYN`) binaries, find the full address range of all `PT_LOAD` segments, reserve it with one `PROT_NONE` mapping and use it as the load base.
3. Map every `PT_LOAD` segment at its (rebased) page-aligned address and copy its file contents in. The zero-filled `.bss` comes for free from the anonymous mapping.
4. Apply each segment's real permissions with `mprotect()`.
5. Allocate an 8 MB stack and push the environment and argument strings, 16 random bytes for `AT_RANDOM`, the auxiliary vector, the `envp` and `argv` pointer arrays, and `argc`.
6. Switch `rsp` to the new stack, clear `rbp` and `jmp` to the entry point using inline assembly.

## Build and run

```sh
cd src
make
./elf-loader /path/to/static-binary [args...]
```

Requirements: `gcc`, `make`. The tests also need `nasm` and a static glibc (`glibc` on Arch, `libc6-dev` on Debian/Kali).

## Tests

```sh
cd tests
make            # builds the loader and the test programs
./run_tests.sh
```

The suite covers bad magic, non-64-bit input, raw syscall binaries (including strict no-RWX variants), static non-PIE programs that check `argc`, `argv`, `envp` and `auxv`, and a static PIE program. **All tests pass (100/100).**

## Layout

```
src/elf-loader.c     the loader
tests/snippets/      test programs (C and x86-64 assembly)
tests/ref/           expected output
```

## Context

Built for the Operating Systems course at the National University of Science and Technology POLITEHNICA Bucharest. The test suite comes from the course; the loader is my own work.
