/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "userprog/syscall.h"
#include "userprog/process.h"

static bool file_backed_swap_in(struct page *page, void *kva);
static bool file_backed_swap_out(struct page *page);
static void file_backed_destroy(struct page *page);
/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void vm_file_init(void)
{
}

/* Initialize the file backed page */
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva)
{
	/* Set up the handler */
	page->operations = &file_ops;
	struct file_page *file_page = &page->file;

	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in(struct page *page, void *kva)
{
	struct file_page *file_page UNUSED = &page->file;
	struct file_info *file_info = page->file.aux;
	if (file_read_at(file_info->file, kva, file_info->read_bytes, file_info->ofs) != file_info->read_bytes)
	{
		printf("swap_in_false\n");
		return false;
	}
	return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out(struct page *page)
{
	struct file_page *file_page UNUSED = &page->file;
	struct thread *page_holder = page->frame->thread;
	bool is_dirty = pml4_is_dirty(page_holder->pml4, page->va);
	struct file_info *file_info = page->file.aux;
	if (is_dirty)
	{
		// printf("%s\n", page->frame->kva);
		// int checker = file_write(file_info->file, curr->open_addr, file_info->read_bytes);
		if (page->is_writable)
		{
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
file_backed_destroy(struct page *page)
{
	struct file_page *file_page UNUSED = &page->file;
	struct frame *frame = page->frame;
	if (page)
	{
		if (frame)
		{
			list_remove(&page->copy_elem);
			frame->write_protected--;
			if (frame->write_protected == 0)
			{
				page->frame = NULL;
				// palloc_free_page(frame->kva);
				free(frame);
				if (file_page)
				{
					if (file_page->aux)
					{
						struct file_info *file_info = (struct file_info *)file_page->aux;
						// free(file_info);
						file_page->aux = NULL;
					}
				}
			}
			else if (frame->page == page)
			{
				frame->page = list_entry(list_begin(&frame->page_list), struct page, copy_elem);
			}
		}
	}
}

/* Do the mmap */
void *
do_mmap(void *addr, size_t length, int writable,
		struct file *file, off_t offset)
{
	/*addr = NULL, length = NULL, fd로 연 file의 길이가 0인 경우,
	  page_aligned 가 안된 경우, offset PGSIZE의 배수가 아닌 경우,
	  addr도 마찬가지. fd가  STDIN, STDOUT 인 경우.
	  기존 mapping page와 겹치는 경우.*/
	if (file == STDIN || file == STDOUT) return NULL;
	// if (!writable) file_deny_write(file);
	// printf("check length %d\n", length);
	struct thread *curr = thread_current();
	struct supplemental_page_table *spt = &curr->spt;
	void *init_addr = addr;
	size_t zero_bytes = PGSIZE - (length % PGSIZE);
	while (length > 0 || zero_bytes > 0)
	{
		if (spt_find_page(spt, addr) != NULL)
			return NULL;
		size_t page_read_bytes = length < PGSIZE ? length : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct file_info *aux_file = (struct file_info *)malloc(sizeof(struct file_info));
		aux_file->file = file;
		aux_file->ofs = offset;
		aux_file->read_bytes = page_read_bytes;
		aux_file->zero_bytes = page_zero_bytes;
		aux_file->open_addr = init_addr;
		aux_file->close_addr = init_addr + (uint64_t) pg_round_up(length);
		// printf("check length %d\n", pg_round_down(length));
		// printf("check aux_fiel close addr %p\n", aux_file->close_addr);
		if (!vm_alloc_page_with_initializer(VM_FILE, addr,
											writable, lazy_load_segment, aux_file))
			return NULL;
		length -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;
		offset += PGSIZE;
	}
	// void *page_init_addr = init_addr;
	// while (page_init_addr < addr)
	// {
	// 	struct page *page = spt_find_page(spt, init_addr);
	// 	struct file_info *file_info = (struct file_info *)page->uninit.aux;
	// 	file_info->close_addr = addr;
	// 	page_init_addr += PGSIZE;
	// }
	// struct page *page = spt_find_page(spt, init_addr);
	// struct file_info *file_info = (struct file_info *)page->uninit.aux;
	// printf("checkin mmap file_info->open_addr %p\n", file_info->open_addr);
	// printf("checkin mmap file_info->close_addr %p\n", file_info->close_addr);
	return init_addr;
}

/* Do the munmap */
void do_munmap(void *addr)
{
	struct thread *curr = thread_current();
	struct supplemetary_page_table *spt = &curr->spt;
	struct page *page = spt_find_page(&curr->spt, addr);
	if (page == NULL) return ;
	struct file_info *file_info = (struct file_info*) page->file.aux;
	if (page->not_present)
		return;
	if (addr != file_info->open_addr)
		return;
	// printf("check addr %p\n", addr);
	// printf("check openaddr %p\n", file_info->open_addr);
	// printf("check close_addr %p\n", file_info->close_addr);
	void *close_addr = file_info->close_addr;
	while (page->va < close_addr)
	{
		bool is_dirty = pml4_is_dirty(curr->pml4, addr);
		if (is_dirty)
		{
			if (page->is_writable)
			{
				file_write_at(file_info->file, addr, file_info->read_bytes, file_info->ofs);
				pml4_set_dirty(curr->pml4, addr, 0);
			}
		}
		spt_remove_page(&curr->spt, page);
		// if (pml4_get_page(curr->pml4, page->va) != NULL)
			// pml4_clear_page(curr->pml4, page->va);
		addr += PGSIZE;
		page = spt_find_page(spt, addr);
		if (page == NULL) break;
		file_info = (struct file_info*) page->file.aux;
		if (file_info == NULL) break;
	// printf("do munmap check addr %p\n", addr);
	// printf("file_info->close_addr %p\n", file_info->close_addr);
	}
	// lock_acquire(&lock_read);

	// file_close(file_info->file);
	// lock_release(&lock_read);
}