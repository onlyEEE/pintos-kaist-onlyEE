#ifndef VM_FILE_H
#define VM_FILE_H
#include "filesys/file.h"
#include "vm/vm.h"
// #include "userprog/syscall.h"

struct page;
enum vm_type;
struct file_info {
	struct file* file;
	uint32_t read_bytes;
	uint32_t zero_bytes;
	off_t ofs;
};

struct file_page {
	vm_initializer *init;
	enum vm_type type;
	void *aux;

	bool (*page_initalizer) (struct page *, enum vm_type, void *kva);
};

void vm_file_init (void);
bool file_backed_initializer (struct page *page, enum vm_type type, void *kva);
void *do_mmap(void *addr, size_t length, int writable,
		struct file *file, off_t offset);
void do_munmap (void *va);
#endif
