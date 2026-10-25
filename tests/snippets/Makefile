# Static glibc used to build the test programs (glibc-static / libc6-dev)
LIBC_A ?= $(shell gcc -print-file-name=libc.a)

CFLAGS_NO_PIE= -g -static -fcf-protection=none -fno-PIC -o
CFLAGS= -g -static-pie -o
LDFLAGS_NO_PIE= -no-pie
LDFLAGS=
NASMFLAGS= -felf64 -o

all: nolibc_no_rwx_rodata nolibc_no_rwx_text nolibc no_pie_hello no_pie_argc no_pie_argv no_pie_auxv pie no_pie_envp

nolibc_no_rwx_rodata.o: nolibc_no_rwx_rodata.asm
	nasm $(NASMFLAGS) $@ $?

nolibc_no_rwx_rodata: nolibc_no_rwx_rodata.o
	ld -nostdlib -o $@ $?

nolibc_no_rwx_text.o: nolibc_no_rwx_text.asm
	nasm $(NASMFLAGS) $@ $?

nolibc_no_rwx_text: nolibc_no_rwx_text.o
	ld -nostdlib -o $@ $?

nolibc.o: nolibc.asm
	nasm $(NASMFLAGS) $@ $?

nolibc: nolibc.o
	ld -nostdlib -o $@ $?

no_pie_hello: hello.c $(LIBC_A)
	gcc $(CFLAGS_NO_PIE) $@ $? $(LDFLAGS_NO_PIE)

no_pie_argc: argc.c $(LIBC_A)
	gcc $(CFLAGS_NO_PIE) $@ $? $(LDFLAGS_NO_PIE)

no_pie_argv: argv.c $(LIBC_A)
	gcc $(CFLAGS_NO_PIE) $@ $? $(LDFLAGS_NO_PIE)

no_pie_auxv: auxv.c $(LIBC_A)
	gcc $(CFLAGS_NO_PIE) $@ $? $(LDFLAGS_NO_PIE)

no_pie_envp: envp.c $(LIBC_A)
	gcc $(CFLAGS_NO_PIE) $@ $? $(LDFLAGS_NO_PIE)

pie: hello.c $(LIBC_A)
	gcc $(CFLAGS) $@ $? $(LDFLAGS)

clean:
	-rm -f *.o pie no_pie_hello no_pie_argc no_pie_argv no_pie_auxv no_pie_envp nolibc_no_rwx_text nolibc_no_rwx_rodata nolibc
