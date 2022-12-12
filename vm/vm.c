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
struct lock lock_copy;
struct lock lock_kill;

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */

void vm_init(void)
{
	vm_anon_init();
	vm_file_init();
#ifdef EFILESYS /* For project 4 */
	pagecache_init();
#endif
	register_inspect_intr();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	start = NULL;
	lock_init(&lock_vm);
	lock_init(&lock_copy);
	lock_init(&lock_kill);
	list_init(&frame_table);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type(struct page *page)
{
	int ty = VM_TYPE(page->operations->type);
	switch (ty)
	{
	case VM_UNINIT:
		return VM_TYPE(page->uninit.type);
	default:
		return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
									vm_initializer *init, void *aux)
{

	ASSERT(VM_TYPE(type) != VM_UNINIT)
	struct supplemental_page_table *spt = &thread_current()->spt;
	void *initializer = NULL;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page(spt, upage) == NULL)
	{
		/* TODO: Create the page, fetch the initializer according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		struct page *new_pg = (struct page *)malloc(sizeof(struct page));

		if (new_pg == NULL)
			goto err;

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
		uninit_new(new_pg, upage, init, type, aux, initializer);
		new_pg->is_writable = writable;
		new_pg->not_present = true;
		spt_insert_page(spt, new_pg);
	}
	else
		goto err;
	return true;
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page(struct supplemental_page_table *spt UNUSED, void *va UNUSED)
{
	struct page *page = (struct page *)malloc(sizeof(struct page));
	if (page == NULL)
		return NULL;

	page->va = pg_round_down(va);

	struct page *ret = NULL;

	struct hash_elem *e = NULL;
	e = hash_find(&spt->spt_hash, &page->hash_elem);

	if (e != NULL)
	{
		ret = hash_entry(e, struct page, hash_elem);
		free(page);
		return ret;
	}

	free(page);
	return NULL;
}

/* Insert PAGE into spt with validation. */
bool spt_insert_page(struct supplemental_page_table *spt UNUSED,
					 struct page *page UNUSED)
{
	int succ = false;
	/* TODO: Fill this function.c */
	if (!hash_insert(&spt->spt_hash, &page->hash_elem))
		succ = true;

	return succ;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page)
{
	if (hash_empty(&spt->spt_hash))
		return;

	hash_delete(&spt->spt_hash, &page->hash_elem);
	if (page)
		vm_dealloc_page(page);
	return;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim(void)
{
	struct frame *victim = NULL;
	/* TODO: The policy for eviction is up to you. */
	if (start == NULL)
		start = list_begin(&frame_table);
	while (start != list_tail(&frame_table))
	{
		victim = list_entry(start, struct frame, list_e);
		struct thread *t = victim->thread;
		if (pml4_is_accessed(t->pml4, victim->kva))
			pml4_set_accessed(t->pml4, victim->kva, 0);
		else
		{
			start = list_remove(start);
			return victim;
		}
		start = list_next(start);
	}
	start = list_begin(&frame_table);
	while (start != list_tail(&frame_table))
	{
		victim = list_entry(start, struct frame, list_e);
		struct thread *t = victim->thread;
		if (pml4_is_accessed(t->pml4, victim->kva))
			pml4_set_accessed(t->pml4, victim->kva, 0);
		else
		{
			start = list_remove(start);
			return victim;
		}
		start = list_next(start);
	}
	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame(void)
{
	lock_acquire(&lock_vm);
	struct frame *victim UNUSED = vm_get_victim();
	lock_release(&lock_vm);

	// /* TODO: swap out the victim and return the evicted frame. */
	if (victim)
	{
		/* 추가된 부분 */
		struct list_elem *e = list_begin(&victim->page_list);
		for (; e != list_tail(&victim->page_list); e = e->next)
		{
			// struct page *temp_page = list_entry(e, struct page, copy_elem);
			// temp_page->frame = NULL;

			// pml4_clear_page(victim->thread->pml4, temp_page->va);
		}

		return victim;
	}
	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/

static struct frame *
vm_get_frame(void)
{
	struct frame *frame = NULL;
	/* TODO: Fill this function. */
	void *new_kva = palloc_get_page(PAL_USER);
	if (new_kva == NULL)
	{
		frame = vm_evict_frame();

		if (frame == NULL)
			return NULL;

		struct list_elem *e = list_begin(&frame->page_list);
		struct page *temp_page = list_entry(e, struct page, copy_elem);

		swap_out(temp_page);

		for (; e != list_tail(&frame->page_list); e = e->next)
		{
			struct page *temp_page = list_entry(e, struct page, copy_elem);
			temp_page->frame = NULL;
		}
		list_init(&frame->page_list);

		frame->thread = NULL;
	}
	else
	{
		frame = (struct frame *)malloc(sizeof(struct frame));
		frame->kva = new_kva;
		list_init(&frame->page_list);
	}
	list_push_back(&frame_table, &frame->list_e);

	frame->thread = thread_current();
	/*
	TODO : if user pool memory is full, do evict.
	*/
	ASSERT(frame != NULL);

	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth(void *addr UNUSED)
{
	void *stack_bottom = (void *)((uint8_t *)thread_current()->stack_bottom);
	while (addr < stack_bottom)
	{
		stack_bottom -= PGSIZE;
		vm_alloc_page_with_initializer(VM_ANON, stack_bottom, 1, NULL, NULL);
		thread_current()->stack_bottom = stack_bottom;
	}
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp(struct page *page UNUSED)
{
	struct frame *old_frame = page->frame;
	struct thread *cur = thread_current();

	/* 독립해서 나가는 page와 frame과의 연결을 끊어준다 */
	page->frame = NULL;
	list_remove(&page->copy_elem);
	pml4_clear_page(cur->pml4, page->va);

	/* frame에서 연결된 page의 숫자를 줄여준다 = 독립해서 나가므로 */
	old_frame->write_protected--;

	if (vm_do_claim_page(page))
	{
		lock_acquire(&lock_copy);
		memcpy(page->frame->kva, old_frame->kva, PGSIZE);
		lock_release(&lock_copy);
		return true;
	}

	else
		return false;
}

/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED,
						 bool user UNUSED, bool write UNUSED, bool not_present UNUSED)
{
	struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
	void *stack_bottom = (void *)(((uint8_t *)thread_current()->stack_bottom));
	void *rsp = user ? f->rsp : thread_current()->user_rsp;

	struct page *page = NULL;
	if (is_kernel_vaddr(addr))
		return false;

	if (addr == NULL)
		return false;

	else if (addr >= STACK_LIMIT && addr <= stack_bottom && addr >= rsp - 8)
		vm_stack_growth(addr);

	page = spt_find_page(spt, addr);

	if (page == NULL)
		return false;

	else if (page != NULL && page->frame == NULL && not_present)
		return vm_do_claim_page(page);

	else if (page->frame != NULL && write && !not_present && page->frame->write_protected > 1)
		return vm_handle_wp(page);

	else
		return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page)
{
	destroy(page);
	free(page);
}

/* Claim the page that allocate on VA. */
bool vm_claim_page(void *va UNUSED)
{
	struct page *page = NULL;
	/* TODO: Fill this function */
	page = spt_find_page(&thread_current()->spt, va);
	return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page(struct page *page)
{
	struct frame *frame = vm_get_frame();
	if (pml4_get_page(thread_current()->pml4, page->va) == NULL)
	{																						 // table에 해당하는 값이 없으면
		if (!pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->is_writable)) // table에 해당하는 page 넣어주고,
		{
			printf("check false pml4_set_page\n");
			return false;
		}
	}

	frame->write_protected = 1;

	/* Set links */
	page->frame = frame;
	list_push_back(&frame->page_list, &page->copy_elem);
	// page->frame->thread = thread_current();
	// printf("======claim thread->tid is %d========\n", page->frame->thread->tid);
	page->not_present = false;

	return swap_in(page, frame->kva);
}

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED)
{
	hash_init(&(spt->spt_hash), page_hash, page_less, 0);
}

/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
								  struct supplemental_page_table *src UNUSED)
{
	struct hash_iterator i;
	bool success = true;
	hash_first(&i, &src->spt_hash);
	/* spt를 돌면서 복사해준다 */
	while (hash_next(&i))
	{
		struct page *src_page = hash_entry(hash_cur(&i), struct page, hash_elem);
		struct page *dst_page = (struct page *)malloc(sizeof(struct page));

		lock_acquire(&lock_copy);
		memcpy(dst_page, src_page, sizeof(struct page));
		lock_release(&lock_copy);

		if (src_page->frame)
		{
			/* frame에서 관리하는 page 리스트에 추가해준다 = link */
			lock_acquire(&lock_copy);
			list_push_back(&src_page->frame->page_list, &dst_page->copy_elem);
			lock_release(&lock_copy);

			/* 자식의 pml4에서 writable bit를 0으로 바꾼다. */
			pml4_set_page(thread_current()->pml4, dst_page->va, dst_page->frame->kva, 0);

			/* frame에서 연결된 page 수를 늘려준다. */
			src_page->frame->write_protected++;

			if (src_page->file.type == VM_FILE)
			{
				struct file_info *file_info = (struct file_info *)malloc(sizeof(struct file_info));

				lock_acquire(&lock_copy);
				memcpy(file_info, (struct file_info *)src_page->uninit.aux, sizeof(struct file_info));
				file_info->file = file_reopen(file_info->file);
				lock_release(&lock_copy);

				thread_current()->open_addr = dst_page->va;
				dst_page->uninit.aux = file_info;
			}
		}

		success &= spt_insert_page(dst, dst_page);
	}
	return success;
}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED)
{
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	struct hash_iterator i;
	while (!hash_empty(&spt->spt_hash))
	{
		hash_first(&i, &spt->spt_hash);
		hash_next(&i);
		struct page *target = hash_entry(hash_cur(&i), struct page, hash_elem);
		if (target->frame)
		{
			if (target->frame->write_protected == 1)
				list_remove(&target->frame->list_e); // 원본인 경우 frame list_e에서 제거.
			if (target->uninit.type == VM_FILE)
			{
				munmap(target->va); // munmap 안에서 spt_remove_page 실행.
				return;
			}
		}
		spt_remove_page(spt, target);
	}
}

unsigned
page_hash(const struct hash_elem *p_, void *aux UNUSED)
{
	const struct page *p = hash_entry(p_, struct page, hash_elem);
	return hash_bytes(&p->va, sizeof p->va);
	//   return hash_bytes (&p->addr, sizeof p->addr);
}

bool page_less(const struct hash_elem *a_,
			   const struct hash_elem *b_, void *aux UNUSED)
{
	const struct page *a = hash_entry(a_, struct page, hash_elem);
	const struct page *b = hash_entry(b_, struct page, hash_elem);

	//   return a->addr < b->addr;
	return a->va < b->va;
}

struct page *
page_lookup(const void *address)
{
	struct page p;
	struct hash_elem *e;

	p.va = address;
	e = hash_find(&thread_current()->spt.spt_hash, &p.hash_elem);
	return e != NULL ? hash_entry(e, struct page, hash_elem) : NULL;
}