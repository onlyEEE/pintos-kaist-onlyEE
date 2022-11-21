#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	switch(f->rsp){
		case SYS_HALT :
		halt();
		case SYS_EXIT :
		exit();
		case SYS_FORK :
		fork();                 /* Clone current process. */
		case SYS_EXEC :
		exec();                  /* Terminate this process. */
		case SYS_WAIT :
		wait();                   /* Wait for a child process to die. */
		case SYS_CREATE :
		create();                 /* Create a file. */
		case SYS_REMOVE :                /* Delete a file. */
		remove();
		case SYS_OPEN :
		open();
		case SYS_FILESIZE :               /* Obtain a file's size. */
		filesize();
		case SYS_READ :                   /* Read from a file. */
		read();
		case SYS_WRITE :                  /* Write to a file. */
		write();
		case SYS_SEEK :                   /* Change position in a file. */
		seek();
		case SYS_TELL :                   /* Report current position in a file. */
		tell();
		case SYS_CLOSE :
		close();
	}
	printf ("system call!\n");
	thread_exit ();
}
