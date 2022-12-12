#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "threads/synch.h"
#include "lib/string.h"
#include "threads/palloc.h"
#include "vm/file.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void *check_address(void* addr);
void halt(void);
void exit(int status);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int exec (const char *cmd_line);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
struct file* find_file(int fd);
int fork (const char *thread_name,struct intr_frame* if_);
int wait (int pid);
void close (int fd);
void seek (int fd, unsigned position);
unsigned tell (int fd);
int add_file(struct file *file);
int dup2(int oldfd, int newfd);
void remove_file(int fd);
void check_valid_buffer (void *buffer, size_t size, bool to_write, struct intr_frame *f);
void *mmap (void *addr, size_t length, int writable, int fd, off_t offset);
void munmap(void * addr);
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

static struct lock lock;
static struct lock lock_open;
static struct lock lock_read;
static struct lock lock_write;
	/*register uint64_t *num asm ("rax") = (uint64_t *) num_;
	register uint64_t *a1 asm ("rdi") = (uint64_t *) a1_;
	register uint64_t *a2 asm ("rsi") = (uint64_t *) a2_;
	register uint64_t *a3 asm ("rdx") = (uint64_t *) a3_;
	register uint64_t *a4 asm ("r10") = (uint64_t *) a4_;
	register uint64_t *a5 asm ("r8") = (uint64_t *) a5_;
	register uint64_t *a6 asm ("r9") = (uint64_t *) a6_;*/

void
syscall_init (void) {
	lock_init(&lock);
	lock_init(&lock_open);
	lock_init(&lock_read);
	lock_init(&lock_write);
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
	uint64_t number = f->R.rax;
	switch (number)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(f->R.rdi);
		break;
	case SYS_CREATE:
		f->R.rax = create(f->R.rdi,f->R.rsi);
		break;
	case SYS_REMOVE:
		f->R.rax = remove(f->R.rdi);
		break;
	case SYS_EXEC:
		f->R.rax = exec(f->R.rdi);
		break;
	case SYS_OPEN:
		f->R.rax = open(f->R.rdi);
		break;
	case SYS_FILESIZE:
		f->R.rax = filesize(f->R.rdi);
		break;
	case SYS_READ:
		check_valid_buffer(f->R.rsi, f->R.rdx, 0, f);
		f->R.rax = read(f->R.rdi,f->R.rsi,f->R.rdx);
		break;
	case SYS_WRITE:
		check_valid_buffer(f->R.rsi, f->R.rdx, 1, f);
		f->R.rax = write(f->R.rdi,f->R.rsi,f->R.rdx);
		break;
	case SYS_FORK:
		f->R.rax = fork(f->R.rdi,f);
		break;
	case SYS_WAIT:
		f->R.rax = wait(f->R.rdi);
		break;
	case SYS_CLOSE:
		close(f->R.rdi);
		break;
	case SYS_SEEK:
		seek(f->R.rdi,f->R.rsi);
		break;
	case SYS_TELL:
		f->R.rax = tell(f->R.rdi);
		break;
	case SYS_DUP2:	// project2 - extra
		f->R.rax = dup2(f->R.rdi, f->R.rsi);
		break;
	case SYS_MMAP:
		f->R.rax = mmap(f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8);
		break;
	case SYS_MUNMAP:
		munmap(f->R.rdi);
		break;
	default:
		thread_exit();
		break;
	}
}

int dup2(int oldfd, int newfd) {
	// 기존의 파일 디스크립터 oldfd를 새로운 newfd로 복제하여 생성하는 함수.
	// newfd가 이전에 열렸다면, 재사용하기 전에 자동으로 닫힌다.
	// oldfd가 불분명하면 이 시스템 콜은 실패하며 -1을 리턴, newfd는 닫히지 않는다.
	// oldfd가 명확하고 newfd가 oldfd와 같은 값을 가진다면, dup2() 함수는 실행되지 않고 newfd값을 그대로 반환
	struct file *file = find_file(oldfd);
	if (file == NULL){
		return -1;
	}
	if (oldfd == newfd){
		return newfd;
	}

	struct thread *curr = thread_current();
	struct file **curr_fd_table = curr->fd_table;
	if (file == STDIN){
		curr->stdin_count++;
	}else if(file == STDOUT){
		curr->stdout_count++;
	}else{
		file->dup_count++;
	}

	close(newfd);
	curr_fd_table[newfd] = file;
	return newfd;
}

/* Project2-2 User Memory Access */
void *check_address(void* addr){
	struct thread* curr = thread_current();
	void *page = NULL;
	if(!is_user_vaddr(addr) || addr == NULL) exit(-1);
	#ifdef VM
		page = (void *)spt_find_page(&curr->spt.spt_hash, addr);
		if (page == NULL)
			exit(-1);		
	#else
		page = pml4_get_page(curr->pml4, addr);
		if (page == NULL)
			exit(-1);
	#endif
	return page;
}

void check_valid_buffer (void *buffer, size_t size, bool to_write, struct intr_frame *f){
	void *rsp = f->rsp;
	// msg("checkin check valid buffer %d", to_write);
	while ((int)size > 0){
		struct page* page = (struct page*)check_address(buffer);
		// printf("size %d\n", size);
		// printf("find buffer %p\n", buffer);
		// // printf("check rsp %p\n", rsp);
		// printf("is user_pte? %p\n", rsp);
		// printf("find page->va %p\n", page->frame->kva);
		// printf("find page->va %p\n", stack_page->frame->kva);
		// printf("check (uint64_t) page->va * 1 << 10 %p\n",(uint64_t) page->va * 1 << 12);
		if(page == NULL) exit(-1);
		if(to_write == true)
		{
			if(page->file.type == VM_FILE){
				struct file_info *file_info = page->file.aux;
				if(file_info->file->deny_write == 0) exit(-1);
			}
		}
		else {
			//문제 mmap-ro.
			// printf("here? %p\n", buffer);
			if(rsp > buffer && page->is_writable == false && page->frame->write_protected == 1) exit(-1);
			// if (buffer < 0x100000 && page->is_writable == false) exit(-1); // base_memory 접근시.
		
		}
		size -= PGSIZE;
		buffer += PGSIZE;
		// size -= 1;
	}
	// msg("checkout check valid buffer %d", to_write);
}

/* Project2-3 System Call */
void halt(void){
	power_off();
}

/* Project2-3 System Call */
void exit (int status){
	/* status가 1로 넘어온 경우는 정상 종료 */
	struct thread* curr = thread_current();
	curr->exit_status = status;
	// msg("%s: exit(%d)\n",curr->name, status);
	printf("%s: exit(%d)\n",curr->name, status);
	thread_exit();
}

/* Project2-3 System Call */
bool create(const char *file, unsigned initial_size){
	check_address(file);
	return filesys_create(file,initial_size);
}

/* Project2-3 System Call */
bool remove(const char *file){
	check_address(file);
	if(filesys_remove(file)){
		return true;
	}
	return false;
}

/* Project2-3 System Call */
int exec (const char *cmd_line){
	check_address(cmd_line);
	char* copy = palloc_get_page(PAL_ZERO);
	if(copy == NULL){
		exit(-1);
	}
	strlcpy(copy,cmd_line,strlen(cmd_line) + 1);
	struct thread* curr = thread_current();
	if (process_exec(copy) == -1){
		return -1;
	}
	NOT_REACHED();
	return 0;
}

/* Project2-3 System Call */
int open (const char *file){
	check_address(file);
	lock_acquire(&lock);
	struct file *fileobj = filesys_open(file);
	if (fileobj == NULL) {
		return -1;
	}

	int fd = add_file(fileobj); // fdt : file data table
	// fd table이 가득 찼다면
	if (fd == -1) {
		file_close(fileobj);
	}
	lock_release(&lock);
	return fd;
}

int add_file(struct file *file){
	struct thread *cur = thread_current();
	struct file **fdt = cur->fd_table;

	// int idx = cur->fd_idx;
	while (cur->fd_idx < FDCOUNT_LIMIT && fdt[cur->fd_idx]) {
		cur->fd_idx++;
	}

	// fdt가 가득 찼다면
	if (cur->fd_idx >= FDCOUNT_LIMIT)
		return -1;

	fdt[cur->fd_idx] = file;
	return cur->fd_idx;
}

/* Project2-3 System Call */
int filesize (int fd){
	if(fd < 0 || fd >= FDCOUNT_LIMIT){
		return -1;
	}
	struct file *file = find_file(fd);
	return file_length(file);
}

/* Project2-3 System Call */
int read (int fd, void *buffer, unsigned size){
	off_t char_count = 0;
	struct thread *cur = thread_current();
	// lock_acquire(&lock_read);
	
	struct file *file = find_file(fd);
	// lock_release(&lock_read);
	if (fd == NULL){
		return -1;
	}
	struct page *page = spt_find_page(&cur->spt, buffer);
	// printf("check buffer %p\n", page->frame);

	// if (page->frame) 
	// {
	// // 	if (buffer + size < USER_STACK - 0x100000)
	// 		exit(-1);
	// }
	
	if (file == NULL || file == STDOUT){
		return -1;
	}

	/* Keyboard 입력 처리 */

	if(file == STDIN){
		if (cur->stdin_count == 0){
			// 더이상 열려있는 stdin fd가 없다.
			NOT_REACHED();
			remove_file(fd);
			// lock_release(&lock_read);
			return -1;
		}
		while (char_count < size)
		{
			char key = input_getc();
			*(char*)buffer = key;
			char_count++;
			(char*)buffer++;
			if (key == '\0'){
				break;
			}
		}
		
	}
	else{
		lock_acquire(&lock_read);
		char_count = file_read(file,buffer,size);
		// printf("check buffer %s\n",buffer);
		// printf("check char_count %d\n", char_count);
		lock_release(&lock_read);
	}
	return char_count;
}

/* Project2-3 System Call */
int write (int fd, const void *buffer, unsigned size) {
	off_t write_size = 0;
	struct thread *cur = thread_current();
	lock_acquire(&lock_write);
	struct file *file = find_file(fd);
	lock_release(&lock_write);
	if (fd == NULL){
		return -1;
	}

	if (file == NULL || file == STDIN){
		return -1;
	}


    if (file == STDOUT) {
		if (cur->stdout_count == 0){
			remove_file(fd);
			return -1;
		}
		putbuf(buffer, size);
		return size;
  	}else{
		lock_acquire(&lock_write);
		write_size = file_write(file,buffer,size);
		lock_release(&lock_write);
	} 
	return write_size;
}

/* Project2-3 System Call */
struct file* find_file(int fd){
	struct thread* curr = thread_current();
	if(fd < 0 || fd >= FDCOUNT_LIMIT){
		return NULL;
	}
	return curr->fd_table[fd];
}

/* Project2-3 System Call */
int fork (const char *thread_name,struct intr_frame* if_){
	check_address(thread_name);
	return process_fork(thread_name,if_);
}

/* Project2-3 System Call */
int wait (int pid){
	return process_wait(pid);
}

/* Project2-3 System Call */
void close (int fd){
	struct file* close_file = find_file(fd);
	if (close_file == NULL) {
		return;
	}

	struct thread *curr = thread_current();

	if(fd==0 || close_file==STDIN)
		curr->stdin_count--;
	else if(fd==1 || close_file==STDOUT)
		curr->stdout_count--;
	// lock_acquire(&lock_open);
	remove_file(fd);
	// lock_release(&lock_open);


	if(fd < 2 || close_file <= 2){
		return;
	}

	if(close_file->dup_count == 0){
		// lock_acquire(&lock_open);
		file_close(close_file);
		// lock_release(&lock_ope/n);
	}
	else{
		close_file->dup_count--;

	}
}

void remove_file(int fd)
{
	struct thread *cur = thread_current();
	
	if (fd < 0 || fd >= FDCOUNT_LIMIT)
		return;

	cur->fd_table[fd] = NULL;
}


/* Project2-3 System Call */
void seek (int fd, unsigned position){
	struct file* file = find_file(fd);
	if (file <= 2) {
		return;
	}
	file_seek(file,position);
}

unsigned tell (int fd){
	struct file* file = find_file(fd);
	if (file <= 2) {
		return;
	}
	return file_tell(file);
}

void *
mmap (void *addr, size_t length, int writable, int fd, off_t offset){
	if (addr <= 0 || (int) length <= 0) return NULL;
	if (addr + length > KERN_BASE || addr > KERN_BASE) return NULL;
	if (offset % PGSIZE != 0 || pg_round_down(addr) != addr) return NULL;

	enum intr_level old_level;
	// old_level = intr_disable();

	void * mapped_addr = NULL;
	lock_acquire(&lock_open);
	struct file *file = find_file(fd);
	file = file_reopen(file);
	if (file_length(file) == 0) return NULL;
	length = file_length(file) < length ? file_length(file) : length;
	lock_release(&lock_open);
	mapped_addr = do_mmap(addr, length, writable, file, offset);
	// intr_set_level(old_level);
	if (mapped_addr != NULL)
		return addr;
	else return NULL;
}

void munmap(void * addr){
	do_munmap(addr);
}