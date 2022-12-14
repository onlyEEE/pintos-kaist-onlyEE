/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "lib/kernel/bitmap.h"
/* DO NOT MODIFY BELOW LINE */
struct bitmap *swap_table;
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = NULL;
	swap_disk = disk_get(1, 1);
	size_t swap_disk_size = disk_size(swap_disk);
	swap_table = bitmap_create(swap_disk_size >> 3);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	// printf("check in anon_init\n");
	page->operations = &anon_ops;
	struct anon_page *anon_page = &page->anon;
	anon_page->bit_idx = bitmap_size(swap_table) + 1;
	vm_initializer *init = anon_page->init;
	anon_page->aux = page->uninit.aux;
	anon_page->type = type;
	// if(page->frame->kva == 0x8004521000) printf("check anon_init\n");
	// void *aux = anon_page->aux;
	// task init_function.
	return true;
	// return anon_page->page_initializer (page, anon_page->type, kva) &&
		// (init ? init (page, aux) : true); // return value 생각해보기.
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;

	// printf("check swap_in page->bit_idx %d\n", anon_page->bit_idx);
	// printf("check swap_in page->va %p\n", page->va);
	// printf("check bit_size %d\n", bitmap_size(swap_table));
	// printf("check swapin bitmap_test %d\n", bitmap_test(swap_table, anon_page->bit_idx));
	if (anon_page->bit_idx > bitmap_size(swap_table)) return true;
	if (bitmap_test(swap_table, anon_page->bit_idx) == false) return false;
	for (int i = 0; i < 8; i++)
		disk_read(swap_disk, (anon_page->bit_idx) * 8 + i, kva + 512 * i);
	// lock_acquire(&lock);
	bitmap_set(swap_table, anon_page->bit_idx, 0);
	// lock_release(&lock);
	// printf("check swap_in kva %s\n", kva);
	// printf("check swap_in page->frame->page->va %p\n", page->frame->page->va);
	// printf("check swap_in page-->va %p\n", page->va);
	// vm_claim_page(page->va);
	anon_page->bit_idx = bitmap_size(swap_table) + 1;
	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	struct thread *page_holder = page->frame->thread;
	// printf("check swap_out page->va %p\n", page->va);
	// printf("check page->bitmap_idx %d\n", anon_page->bit_idx);
	// hex_dump(page->frame->kva, page->frame->kva, PGSIZE, true);
	size_t bitmap_idx = bitmap_scan(swap_table, 0, 1, 0);
	if (bitmap_idx == BITMAP_ERROR) return false;
	if (anon_page->bit_idx <= bitmap_size(swap_table)) return false;
	//이부분 효율성 추구헤보기.
	disk_sector_t sector_idx = bitmap_idx * 8;
	for(int i = 0; i < 8; i++)
		disk_write(swap_disk, sector_idx + i, page->va + 512 * i);
	// lock_acquire(&lock);
	bitmap_set(swap_table, bitmap_idx, 1);
	// lock_release(&lock);
	anon_page->bit_idx = bitmap_idx;
	// palloc_free_page(page->frame->kva);
	pml4_clear_page(page_holder->pml4, page->va);
	// free(page->frame);
	page->frame = NULL;
	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	struct frame *frame = page->frame;
	if (page){
		if(frame){
			list_remove(&page->copy_elem);
			frame->write_protected--;
			if(frame->write_protected == 0){
				page->frame = NULL;
				// palloc_free_page(frame->kva);
				free(frame);
				if(anon_page) {
					if (anon_page->aux){
						struct file_info *file_info= (struct file_info *) anon_page->aux;
						// free(file_info);
						anon_page->aux = NULL;
					}
				}
			} else if(frame->page == page){
				// printf("check frame->page %p\n", frame->page);
				frame->page = list_entry(list_begin(&frame->page_list), struct page, copy_elem);
				// printf("check frame->page %p\n", frame->page);
			}
		}
	}
	return ;
}
