/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
// #include "lib/kernel/bitmap.h"
/* DO NOT MODIFY BELOW LINE */
// struct bitmap *swap_table;
// struct lock lock;
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
	// swap_disk = disk_get(1, 1);
	// lock_init(&lock);
	// size_t swap_disk_size = disk_size(swap_disk);
	// swap_table = bitmap_create(swap_disk_size << 3);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;
	// page->bit_idx = bitmap_size(swap_table) + 1;
	struct anon_page *anon_page = &page->anon;
	vm_initializer *init = anon_page->init;
	// anon_page->aux = page->uninit.aux;
	anon_page->type = type;
	void *aux = anon_page->aux;
	// task init_function.
	return true;
	// return anon_page->page_initializer (page, anon_page->type, kva) &&
		// (init ? init (page, aux) : true); // return value 생각해보기.
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	// printf("check swap_in\n");
	struct anon_page *anon_page = &page->anon;
	// if (page->bit_idx > bitmap_size(swap_table)) return false;
	// bitmap_set(swap_table, page->bit_idx, 0);
	// lock_acquire(&lock);
	// for (int i = 0; i < 8; i++)
	// 	disk_read(swap_disk, (page->bit_idx)*8 + i, page->va);
	// lock_release(&lock);
	// vm_claim_page(page->va);
	// page->bit_idx = bitmap_size(swap_table) + 1;
	// return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	// struct thread *cur = thread_current();
	// size_t bitmap_idx = bitmap_scan(swap_table, 0, 1, 0);
	// if (bitmap_idx == BITMAP_ERROR) return false;
	// if (page->bit_idx <= bitmap_size(swap_table)) return false;
	// //이부분 효율성 추구헤보기.
	// lock_acquire(&lock);
	// for(int i = 0; i < 8; i++)
	// 	disk_write(swap_disk, (bitmap_idx)*8 + i, page->frame->kva);
	// lock_release(&lock);
	// page->bit_idx = bitmap_idx;
	// palloc_free_page(page->frame->kva);
	// free(page->frame);
	// page->frame = NULL;
	// return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	struct frame *frame = page->frame;
	if (page){
		if(anon_page) {
			if (anon_page->aux){
				// free(anon_page->aux);
				anon_page->aux = NULL;
			}
		}
		if(frame){
			page->frame = NULL;
			free(frame);
		}
	}
	return ;
}
