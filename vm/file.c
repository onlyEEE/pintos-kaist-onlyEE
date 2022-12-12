/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "userprog/syscall.h"
#include "userprog/process.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);
struct lock lock;
/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
	lock_init(&lock);
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;
	struct file_page *file_page = &page->file;
	vm_initializer *init = file_page->init;
	file_page->aux = page->uninit.aux;
	file_page->type = type;
	
	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
	struct file_info *file_info = page->file.aux;
	if (file_read_at(file_info->file, kva, file_info->read_bytes, file_info->ofs) != file_info->read_bytes){
		printf("swap_in_false\n");
		return false;
	}
	return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	struct thread *page_holder = page->frame->thread;
	bool is_dirty = pml4_is_dirty(page_holder->pml4, page->va);
	struct file_info *file_info = page->file.aux;
	if (is_dirty){
		// printf("%s\n", page->frame->kva);
		// int checker = file_write(file_info->file, curr->open_addr, file_info->read_bytes);
		if (page->is_writable){
			pml4_set_dirty(page_holder->pml4, page->va, 0);
			file_write_at(file_info->file, page->va, file_info->read_bytes, file_info->ofs);
		}
		// memcpy(addr, page->frame->kva, file_info->read_bytes);
		// palloc_free_page(page->frame->kva);
	}
	pml4_clear_page(page_holder->pml4, page->va);
	page->frame = NULL;
	return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	struct frame *frame = page->frame;
	if (page){
		if (file_page) {
			if (file_page->aux){
				free(file_page->aux);
				file_page->aux = NULL;
			}
		}
		if(frame){
			list_remove(&page->copy_elem);
			frame->write_protected--;
			if(frame->write_protected == 0){
				page->frame = NULL;
				free(frame);
			} else if(frame->page == page){
				frame->page = list_entry(list_begin(&frame->page_list), struct page, copy_elem);
			}
		}
	}
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	/*addr = NULL, length = NULL, fd로 연 file의 길이가 0인 경우, 
	  page_aligned 가 안된 경우, offset PGSIZE의 배수가 아닌 경우,
	  addr도 마찬가지. fd가  STDIN, STDOUT 인 경우.
	  기존 mapping page와 겹치는 경우.*/
	if (file == STDIN || file == STDOUT) return NULL;
	// if (!writable) file_deny_write(file);
	struct thread * curr = thread_current();
	struct supplemental_page_table *spt = &curr->spt;
	void *init_addr = addr;
	size_t zero_bytes = PGSIZE - (length % PGSIZE);
	while (length > 0 || zero_bytes > 0) {
		if (spt_find_page(spt, addr) != NULL) return NULL;
		size_t page_read_bytes = length < PGSIZE ? length : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;
		struct file_info *aux_file = (struct file_info *)malloc(sizeof(struct file_info));
		aux_file->file = file;
		aux_file->ofs = offset;
		aux_file->read_bytes = page_read_bytes;
		aux_file->zero_bytes = page_zero_bytes;
		if (!vm_alloc_page_with_initializer (VM_FILE, addr,
					writable, lazy_load_segment, aux_file))
				return NULL;
		length -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		offset += PGSIZE;
		addr += PGSIZE;
	}
	curr->open_addr = init_addr;
	return init_addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	struct thread * curr = thread_current();
	bool is_dirty = pml4_is_dirty(curr->pml4, addr);
	struct page *page = spt_find_page(&curr->spt, addr);
	if (page == NULL) exit(-1);
	struct file_info *file_info = page->file.aux;

	if (page->not_present) return ;
	if (addr != curr->open_addr)
		return ;
	if (is_dirty){
		// printf("%s\n", page->frame->kva);
		// int checker = file_write(file_info->file, curr->open_addr, file_info->read_bytes);
		if (page->is_writable){
			file_write_at(file_info->file, addr, file_info->read_bytes, file_info->ofs);
			pml4_set_dirty(curr->pml4, addr, 0);
		}
		// memcpy(addr, page->frame->kva, file_info->read_bytes);
		palloc_free_page(page->frame->kva);
	}
	spt_remove_page(&curr->spt, page);
}
