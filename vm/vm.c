/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/mmu.h"
// #include "lib/kernel/list.h"

static void page_destroy(const struct hash_elem *p_, void *aux UNUSED);


struct list frame_table;
struct list_elem *start;
struct lock lock_vm;

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */

void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	start = NULL;
	lock_init(&lock_vm);
	list_init(&frame_table);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)
	struct supplemental_page_table *spt = &thread_current ()->spt;
	void *initializer = NULL;
	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initializer according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		struct page *new_pg = (struct page *)malloc(sizeof(struct page));
		if (new_pg == NULL) goto err;
		switch (type)
		{
			case VM_ANON:
				/* code */
				initializer = anon_initializer;
				break;
			case VM_FILE:
				initializer = file_backed_initializer;
				break;
			case VM_PAGE_CACHE:
				break;
			default:
				break;
		}
		/* TODO: Insert the page into the spt. */
		uninit_new (new_pg, upage, init, type, aux, initializer);
		new_pg->is_writable = writable;
		new_pg->not_present = true;
		spt_insert_page(spt, new_pg);
	}
	else goto err;
	return true;
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = (struct page *)malloc(sizeof(struct page));
	if (page == NULL) return NULL;
	struct page *ret = NULL;
	struct hash_elem *e = NULL;
	page->va = pg_round_down(va);
	e = hash_find(&spt->spt_hash, &page->hash_elem);
	if (e){
		ret = hash_entry(e, struct page, hash_elem);
	}
	else {
		free(page);
		return NULL;
	}
	free(page);
	return ret;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	/* TODO: Fill this function.c */
	if (!hash_insert(&spt->spt_hash, &page->hash_elem))
		succ = true;
	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	if (hash_empty(&spt->spt_hash)) return;
	hash_delete(&spt->spt_hash, &page->hash_elem);
	if (page) vm_dealloc_page (page);
	return;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */
	if(start == NULL)
		start = list_begin(&frame_table);
	while (start != list_tail(&frame_table)){
		victim = list_entry(start, struct frame, list_e);
		struct thread *t = victim->thread;
		if (pml4_is_accessed(t->pml4, victim->kva))
			pml4_set_accessed(t->pml4, victim->kva, 0);
		else{
			start = list_remove(start);
			return victim;
		}
		start = list_next(start);
	}
	start = list_begin(&frame_table);
	while (start != list_tail(&frame_table)){
		victim = list_entry(start, struct frame, list_e);
		struct thread *t = victim->thread;
		if (pml4_is_accessed(t->pml4, victim->kva))
			pml4_set_accessed(t->pml4, victim->kva, 0);
		else{
			start = list_remove(start);
			return victim;
		}
		// list_next(start);
		start = list_next(start);
	}
	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	lock_acquire(&lock_vm);
	struct frame *victim UNUSED = vm_get_victim ();
	lock_release(&lock_vm);
	// /* TODO: swap out the victim and return the evicted frame. */
	if (victim){
		return victim;
	}
	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
// static struct frame *
// vm_get_frame (void) {
// 	struct frame *frame = NULL;
// 	/* TODO: Fill this function. */
// 	frame = (struct frame *)malloc(sizeof frame);
// 	frame->kva = palloc_get_page(PAL_USER);
// 	// printf("[Debug] frame->kva %p\n", frame->kva);
// 	frame->page = NULL;
// 	/*
// 	TODO : if user pool memory is full, do evict.
// 	*/
// 	if(!is_kernel_vaddr(frame->kva)){
// 		// free(frame);
// 		// return NULL;
// 		PANIC("todo");
// 	}
// 	ASSERT (frame != NULL);
// 	ASSERT (frame->page == NULL);
// 	return frame;
// }

static struct frame *
vm_get_frame(void)
{
	struct frame *frame = NULL;
	/* TODO: Fill this function. */
	void *new_kva = palloc_get_page(PAL_USER);
	if (new_kva == NULL)
	{
		frame = vm_evict_frame();
		swap_out(frame->page);
		frame->thread = NULL;
		// PANIC("todo");
		// free(frame);
		if (frame == NULL) return NULL;
	}
	else{
		frame = (struct frame *)malloc(sizeof(struct frame));
		frame->kva = new_kva;
	}
	list_push_back(&frame_table, &frame->list_e);
	frame->page = NULL;
	frame->thread = thread_current();
	/*
	TODO : if user pool memory is full, do evict.
	*/
	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	
	return frame;
}


/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
	void *stack_bottom = (void *) ((uint8_t *) thread_current()->stack_bottom);
	while (addr < stack_bottom){
		stack_bottom -= PGSIZE;
		vm_alloc_page_with_initializer(VM_ANON, stack_bottom, 1, NULL, NULL);
		thread_current()->stack_bottom = stack_bottom;
	}
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	// void* stack_bottom = &thread_current()->stack_bottom;
	void *stack_bottom = (void *) (((uint8_t *) thread_current()->stack_bottom));
	void *rsp = user ? f->rsp : thread_current()->user_rsp; 
	// printf("stackbottom start %p\n", stack_bottom);
	struct page *page = NULL;
	if (is_kernel_vaddr(addr)){
		return false;
	}
	// if(!write && not_present== false) return false;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	// printf("[Debug]try_handle_fault\n");
	// printf("[Debug]if_rsp : %p\n", f->rsp);
	// printf("[Debug]fault_addr : %p\n", addr);
	// printf("[Debug]thread_tid : %d\n", thread_tid());
	// printf("[Debug]page->user : %d\n", user);
	if (addr == NULL) return false;
	else if (addr >= STACK_LIMIT && addr <= stack_bottom && addr >= rsp - 8){
		vm_stack_growth(addr);
	}
	// printf("[Debug]before spt_find_page\n");
	page = spt_find_page(spt, addr);
	// printf("[Debug]spt_find_page : %p\n", page->va);
	if (page != NULL && page->frame == NULL){	
		return vm_do_claim_page (page);
	// printf("[Debug]page->frame->kva : %p\n", page->frame->kva);
	}
	else{
		// printf("[Debug]check fault false\n");

		// printf("[Debug]fault_addr : %p\n", addr);
		return false;
	}
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function */
	page = spt_find_page (&thread_current()->spt, va);
	// if (page){
	// printf("check\n");
	return vm_do_claim_page (page);
	
	// else{
	// 	vm_alloc_page(VM_ANON, va, 1);
	// 	return false;
	// }
}

/* Claim the PAGE and set up the mmu. */
static bool
 vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();
	// if (frame == NULL)
	// {
	// 	printf("check before evict %p\n", frame);
	// 	frame = vm_evict_frame();
	// 	printf("check after evict %p\n", frame);
	// 	exit(-999);
	// 	swap_out(frame->page);
	// 	// frame = vm_get_frame();
	// }

	/* Set links */
	frame->page = page;
	page->frame = frame;
	// page->not_present = false;
	// printf("page %p\n", page->va);
	// printf("frame->page->va %p\n", frame->page->va);
	// printf("check in vm_do_claim_page page->writable %d\n", page->is_writable);
	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	if (pml4_get_page(thread_current()->pml4, page->va) == NULL) {// table에 해당하는 값이 없으면
		if (!pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->is_writable)) // table에 해당하는 page 넣어주고,
			{
				printf("check false pml4_set_page\n");
				return false;
			}
	}
	// printf("frame_table %p\n", &frame_table);
	// printf("frame_table begin %p\n", list_begin(&frame_table));
	// printf("frame->list_elem %p\n", &frame->list_elem);
	page->not_present=false;
	return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init(&(spt->spt_hash), page_hash, page_less, 0);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
	struct hash_iterator i;
	bool success = true;
	hash_first (&i, &src->spt_hash);
	while (hash_next(&i)){
		struct page *src_page = hash_entry (hash_cur(&i), struct page, hash_elem);
		struct page *dst_page = (struct page *)malloc(sizeof(struct page));
		memcpy(dst_page, src_page, sizeof(struct page));
		if (src_page->frame){
			if (vm_do_claim_page(dst_page))
			{
				memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
				if(src_page->file.type == VM_FILE) {
					struct file_info * file_info = (struct file_info *)malloc(sizeof(struct file_info));
					memcpy(file_info, (struct file_info *)src_page->uninit.aux, sizeof(file_info));
					file_info->file = file_reopen(file_info->file);
					thread_current()->open_addr = dst_page->va;
					dst_page->uninit.aux = file_info;
				}
			}
		}
		success &= spt_insert_page(dst, dst_page);
	}
	return success;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	struct hash_iterator i;
	while (!hash_empty(&spt->spt_hash)){
		hash_first (&i, &spt->spt_hash);
		hash_next(&i);
		struct page *target = hash_entry (hash_cur(&i), struct page, hash_elem);
		// list_remove(&target->frame->list_elem);
		if (target->uninit.type == VM_FILE) munmap(target->va);
		else spt_remove_page(spt, target);
	}
	// hash_destroy(&spt->spt_hash, page_destroy);
	// if(!hash_empty(&spt->spt_hash)) hash_destroy(&spt->spt_hash, page_hash);
}

// static void
// page_destroy(const struct hash_elem *p_, void *aux UNUSED){
// 	struct page *target = hash_entry(p_, struct page, hash_elem);
// 	printf("check page %p\n", target);
// 	vm_dealloc_page (target);
// }

unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED) {
  const struct page *p = hash_entry (p_, struct page, hash_elem);
  return hash_bytes (&p->va, sizeof p->va);
//   return hash_bytes (&p->addr, sizeof p->addr);
}

bool
page_less (const struct hash_elem *a_,
           const struct hash_elem *b_, void *aux UNUSED) {
  const struct page *a = hash_entry (a_, struct page, hash_elem);
  const struct page *b = hash_entry (b_, struct page, hash_elem);

//   return a->addr < b->addr;
return a->va < b->va;
}

struct page *
page_lookup (const void *address) {
  struct page p;
  struct hash_elem *e;

  p.va = address;
  e = hash_find (&thread_current()->spt.spt_hash, &p.hash_elem);
  return e != NULL ? hash_entry (e, struct page, hash_elem) : NULL;
}