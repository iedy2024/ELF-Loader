// SPDX-License-Identifier: BSD-3-Clause

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>
#include <elf.h>

#define PAGE_SIZE 4096
#define PAGE_ALIGN_DOWN(x) ((x) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_UP(x) (((x) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define STACK_SIZE (8 * 1024 * 1024)

void *map_elf(const char *filename)
{
	// This part helps you store the content of the ELF file inside the buffer.
	struct stat st;
	void *file;
	int fd;

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		perror("open");
		exit(1);
	}

	fstat(fd, &st);

	file = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (file == MAP_FAILED) {
		perror("mmap");
		close(fd);
		exit(1);
	}

	return file;
}

void load_and_run(const char *filename, int argc, char **argv, char **envp)
{
	// Contents of the ELF file are in the buffer: elf_contents[x] is the x-th byte of the ELF file.
	void *elf_contents = map_elf(filename);

	/**
	 * TODO: ELF Header Validation
	 * Validate ELF magic bytes - "Not a valid ELF file" + exit code 3 if invalid.
	 * Validate ELF class is 64-bit (ELFCLASS64) - "Not a 64-bit ELF" + exit code 4 if invalid.
	 */

	unsigned char *elf_bin = (unsigned char *) elf_contents;

	if (elf_bin[EI_MAG0] != ELFMAG0 || elf_bin[EI_MAG1] != ELFMAG1 || elf_bin[EI_MAG2] != ELFMAG2 || elf_bin[EI_MAG3] != ELFMAG3) {
		perror("Not a valid ELF file");
		exit(3);
	}

	if (elf_bin[EI_CLASS] != ELFCLASS64) {
		perror("Not a 64-bit ELF");
		exit(4);
	}

	/**
	 * TODO: Load PT_LOAD segments
	 * For minimal syscall-only binaries.
	 * For each PT_LOAD segment:
	 * - Map the segments in memory. Permissions can be RWX for now.
	 */

	Elf64_Ehdr *elf_headers = (Elf64_Ehdr *) elf_bin;
	Elf64_Phdr *program_header = (Elf64_Phdr *) (elf_bin + elf_headers->e_phoff);
	int first_pt_load = 1;
	uintptr_t first_pt_load_addr = 0;
	// Check PIE binary
	int is_pie = (elf_headers->e_type == ET_DYN);
	uintptr_t load_base = 0;

	if (is_pie) {
		uintptr_t min_vaddr = UINTPTR_MAX;
		uintptr_t max_vaddr = 0;

		for (int i = 0; i < elf_headers->e_phnum; i++) {
			if (program_header[i].p_type == PT_LOAD) {
				uintptr_t segment_start = program_header[i].p_vaddr;
				uintptr_t segment_end = program_header[i].p_vaddr + program_header[i].p_memsz;

				if (segment_start < min_vaddr)
					min_vaddr = segment_start;
				if (segment_end > max_vaddr)
					max_vaddr = segment_end;
			}

		}
		min_vaddr = PAGE_ALIGN_DOWN(min_vaddr);
		max_vaddr = PAGE_ALIGN_UP(max_vaddr);
		size_t total_size = max_vaddr - min_vaddr;
		void *base = mmap(NULL, total_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

		load_base = (uintptr_t)base - min_vaddr;
	}

	for (int i = 0; i < elf_headers->e_phnum; i++) {
		if (program_header[i].p_type == PT_LOAD) {
			uintptr_t segment_vaddr = program_header[i].p_vaddr + load_base;
			uintptr_t page_addr = PAGE_ALIGN_DOWN(segment_vaddr);
			uintptr_t page_offset = segment_vaddr - page_addr;
			size_t map_size = PAGE_ALIGN_UP(program_header[i].p_memsz + page_offset);

			if (first_pt_load) {
				first_pt_load_addr = segment_vaddr + elf_headers->e_phoff;
				first_pt_load = 0;
			}
			void *map_addr = mmap((void *)page_addr, map_size,
					PROT_READ | PROT_WRITE | PROT_EXEC,
					MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
					-1, 0);
			if (map_addr == MAP_FAILED) {
				perror("mmap PT_LOAD");
				exit(1);
			}
			memcpy((void *)(page_addr + page_offset),
					(const void *)(elf_bin + program_header[i].p_offset),
					program_header[i].p_filesz);
		}

	}
	/**
	 * TODO: Load Memory Regions with Correct Permissions
	 * For each PT_LOAD segment:
	 *	- Set memory permissions according to program header p_flags (PF_R, PF_W, PF_X).
	 *	- Use mprotect() or map with the correct permissions directly using mmap().
	 */

	for (int i = 0; i < elf_headers->e_phnum; i++) {
		if (program_header[i].p_type == PT_LOAD) {
			uintptr_t segment_vaddr = program_header[i].p_vaddr + load_base;
			uintptr_t page_addr = PAGE_ALIGN_DOWN(segment_vaddr);
			uintptr_t page_offset = segment_vaddr - page_addr;
			size_t map_size = PAGE_ALIGN_UP(program_header[i].p_memsz + page_offset);
			int prot = 0;

			if (program_header[i].p_flags & PF_R)
				prot = prot | PROT_READ;
			if (program_header[i].p_flags & PF_W)
				prot = prot | PROT_WRITE;
			if (program_header[i].p_flags & PF_X)
				prot = prot | PROT_EXEC;

			mprotect((void *)page_addr, map_size, prot);
		}
	}

	/**
	 * TODO: Support Static Non-PIE Binaries with libc
	 * Must set up a valid process stack, including:
	 *	- argc, argv, envp
	 *	- auxv vector (with entries like AT_PHDR, AT_PHENT, AT_PHNUM, etc.)
	 * Note: Beware of the AT_RANDOM, AT_PHDR entries, the application will crash if you do not set them up properly.
	 */
	void *stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (stack == MAP_FAILED) {
		perror("stack");
		exit(1);
	}
	void *sp = (char *)stack + STACK_SIZE;
	int envc = 0;

	while (envp[envc] != NULL)
		envc++;

	// Put the strings for envp on stack
	char *start_envp_strings[envc];

	for (int i = envc - 1; i >= 0; i--) {
		size_t len = strlen(envp[i]) + 1;

		sp -= len;
		mempcpy(sp, envp[i], len);
		start_envp_strings[i] = sp;
	}

	// Put the strings for argv on stack
	char *start_argv_strings[argc];

	for (int i = argc - 1; i >= 0; i--) {
		size_t len = strlen(argv[i]) + 1;

		sp -= len;
		mempcpy(sp, argv[i], len);
		start_argv_strings[i] = sp;
	}
	sp = (char *)((uintptr_t) sp & ~15);
	sp -= 16;

	//Setting up AT_RANDOM using /dev/urandom
	char *rand_bytes_addr = (char *)sp;
	int random_file = open("/dev/urandom", O_RDONLY);

	if (random_file >= 0) {
		size_t rand_bytes = read(random_file, rand_bytes_addr, 16);

		close(random_file);
	}
	sp = (char *)((uintptr_t) sp & ~15);

	//Put auxv on stack
	#define PUSH_AUXV(type, value) { \
		sp -= sizeof(Elf64_auxv_t); \
		Elf64_auxv_t *aux_addr = (Elf64_auxv_t *) sp; \
		aux_addr->a_type = type; \
		aux_addr->a_un.a_val = (uint64_t) (value); \
	}

	uintptr_t entry_addr = elf_headers->e_entry + load_base;

	PUSH_AUXV(AT_NULL, 0);
	PUSH_AUXV(AT_SECURE, 0);
	PUSH_AUXV(AT_EGID, getegid());
	PUSH_AUXV(AT_GID, getgid());
	PUSH_AUXV(AT_EUID, geteuid());
	PUSH_AUXV(AT_UID, getuid());
	PUSH_AUXV(AT_ENTRY, entry_addr);
	PUSH_AUXV(AT_FLAGS, 0);
	PUSH_AUXV(AT_PHNUM, elf_headers->e_phnum);
	PUSH_AUXV(AT_PHENT, elf_headers->e_phentsize);
	PUSH_AUXV(AT_PHDR, first_pt_load_addr);
	if (is_pie)
		PUSH_AUXV(AT_BASE, load_base);
	PUSH_AUXV(AT_RANDOM, (uintptr_t)rand_bytes_addr);
	PUSH_AUXV(AT_PAGESZ, PAGE_SIZE);

	sp -= sizeof(char *);
	*(char **)sp = NULL;

	// Put the pointers for envp on stack
	for (int i = envc - 1; i >= 0; i--) {
		sp -= sizeof(char *);
		*(char **)sp = start_envp_strings[i];
	}
	sp -= sizeof(char *);
	*(char **)sp = NULL;

	// Put the pointers for argv on stack
	for (int i = argc - 1; i >= 0; i--) {
		sp -= sizeof(char *);
		*(char **)sp = start_argv_strings[i];
	}
	sp -= sizeof(uintptr_t);
	*(uintptr_t *) sp = argc;

	/**
	 * TODO: Support Static PIE Executables
	 * Map PT_LOAD segments at a random load base.
	 * Adjust virtual addresses of segments and entry point by load_base.
	 * Stack setup (argc, argv, envp, auxv) same as above.
	 */

	// TODO: Set the entry point and the stack pointer
	void (*entry)() = (void (*)()) entry_addr;

	// Transfer control
	__asm__ __volatile__(
			"mov %0, %%rsp\n"
			"xor %%rbp, %%rbp\n"
			"jmp *%1\n"
			:
			: "r"(sp), "r"(entry)
			: "memory"
			);
}

int main(int argc, char **argv, char **envp)
{
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <static-elf-binary>\n", argv[0]);
		exit(1);
	}

	load_and_run(argv[1], argc - 1, &argv[1], envp);
	return 0;
}
