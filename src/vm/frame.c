#include "vm/frame.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "threads/thread.h"

static struct list_elem *get_next_lru_clock(void);

void lru_list_init()
{
	list_init(&lru_list);
	lock_init(&lru_list_lock);
	lru_clock = NULL;
}

void add_page_to_lru_list(struct page *page)
{
	list_push_back(&lru_list, &page->lru);
}

void del_page_from_lru_list(struct page *page)
{
	if(lru_clock == &page->lru )
		lru_clock = list_remove(&page->lru);
	else
		list_remove(&page->lru);
}

static struct list_elem *get_next_lru_clock(void)
{
	if(lru_clock == NULL || lru_clock == list_end(&lru_list))
	{
		if(list_empty(&lru_list))	return NULL;
		else
		{
			lru_clock = list_begin(&lru_list);
		}
	}

	lru_clock = list_next(lru_clock);
	if(lru_clock == list_end(&lru_list))
	{
		return get_next_lru_clock();
	}
	
	return lru_clock;
}

void* try_to_free_pages(enum palloc_flags flags)
{
	void *kaddr;
	struct list_elem *e;
	lock_acquire(&lru_list_lock);

	while(true)
	{
		e = get_next_lru_clock();
		struct page *p = list_entry(e, struct page, lru);

		if(pagedir_is_accessed(p->thread->pagedir, p->vme->vaddr))
		{
			pagedir_set_accessed(p->thread->pagedir, p->vme->vaddr, false);
		}
		else
		{
			switch(p->vme->type)
			{
				case VM_BIN:
					if(pagedir_is_dirty(p->thread->pagedir, p->vme->vaddr))
					{
						p->vme->type = VM_ANON;
						p->vme->swap_slot = swap_out(p->kaddr);
					}
					break;
				case VM_FILE:
					if(pagedir_is_dirty(p->thread->pagedir, p->vme->vaddr))
					{
						lock_acquire(&filesys_lock);
						file_write_at(p->vme->file, p->vme->vaddr, p->vme->read_bytes, p->vme->offset);
						lock_release(&filesys_lock);
					}
					break;
				case VM_ANON:
					p->vme->swap_slot = swap_out(p->kaddr);
					break;
			}
			p->vme->is_loaded = false;
			__free_page(p);

			kaddr = palloc_get_page(flags);
			if(kaddr)	break;
		}
	}
	lock_release(&lru_list_lock);
	return kaddr;
}
