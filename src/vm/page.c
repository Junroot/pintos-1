#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "threads/palloc.h"
#include "threads/thread.h"

static unsigned vm_hash_func (const struct hash_elem *e, void *aux)
{
	ASSERT(e != NULL);
	return hash_int(hash_entry(e, struct vm_entry, elem)->vaddr);
}

static bool vm_less_func(const struct hash_elem *a, const struct hash_elem *b)
{
	ASSERT(a != NULL);
	ASSERT(b != NULL);
	return (hash_entry(a, struct vm_entry, elem)->vaddr) < (hash_entry(b, struct vm_entry, elem)->vaddr);
}

void vm_init(struct hash *vm)
{
	ASSERT(vm != NULL);
	hash_init(vm, vm_hash_func, vm_less_func, NULL);
}

bool insert_vme(struct hash *vm, struct vm_entry *vme)
{
	ASSERT(vm != NULL);
	ASSERT(vme != NULL);
	return hash_insert(vm, &vme->elem) == NULL;
}

bool delete_vme(struct hash *vm, struct vm_entry *vme)
{
	ASSERT(vm != NULL);
	ASSERT(vm != NULL);

	return hash_delete(vm, &vme->elem) != NULL;
}

struct vm_entry *find_vme(void *vaddr)
{
	struct vm_entry vme;
	struct hash_elem *e;

	vme.vaddr = pg_round_down(vaddr);
	e = hash_find(&thread_current()->vm,&vme.elem);
	return e == NULL? NULL : hash_entry(e, struct vm_entry, elem);
}

void vm_destroy_func(struct hash_elem *e, void *aux UNUSED)
{
	ASSERT(e != NULL);
	struct vm_entry *vme = hash_entry(e, struct vm_entry, elem);
	if(vme->is_loaded)
	{
		palloc_free_page(pagedir_get_page(thread_current()->pagedir, vme->vaddr));
		pagedir_clear_page(thread_current()->pagedir, vme->vaddr);
	}
	free(vme);
}

void vm_destroy(struct hash *vm)
{
	ASSERT(vm != NULL);
	hash_destroy(vm, vm_destroy_func);
}

bool load_file(void *kaddr, struct vm_entry *vme)
{
	ASSERT(kaddr != NULL);
	ASSERT(vme != NULL);

	if (file_read_at(vme->file, kaddr, vme->read_bytes, vme->offset) != (int) vme->read_bytes)	return false;

	memset(kaddr + vme->read_bytes, 0, vme->zero_bytes);
	return true;
}

struct page* alloc_page(enum palloc_flags flags)
{
	struct page *p = (struct page *)malloc(sizeof(struct page));
	if(p == NULL)	return NULL;

	p->thread = thread_current();
	p->kaddr = NULL;
	p->vme = NULL;
	p->kaddr = palloc_get_page(flags);
	while (p->kaddr == NULL)
	{
		p->kaddr = try_to_free_pages(flags);
	}
	
	lock_acquire(&lru_list_lock);
	add_page_to_lru_list(p);
	lock_release(&lru_list_lock);
	return p;
}

void free_page(void *kaddr)
{
	struct list_elem *e;
	lock_acquire(&lru_list_lock);
	for(e = list_begin(&lru_list); e != list_end(&lru_list); e = list_next(e))
	{
		struct page *p = list_entry(e, struct page, lru);
		if(p->kaddr == kaddr)
		{
			__free_page(p);
			break;
		}
	}
	lock_release(&lru_list_lock);
}

void __free_page(struct page *page)
{
	ASSERT(page != NULL);
	pagedir_clear_page(page->thread->pagedir, page->vme->vaddr);
	del_page_from_lru_list(page);
	palloc_free_page(page->kaddr);
	free(page);
}
