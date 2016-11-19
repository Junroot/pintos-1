#include "vm/page.h"
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

	return hash_delete(vm,&vme->elem) != NULL;
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
	palloc_free_page(pagedir_get_page(thread_current()->pagedir, vme->vaddr));
	pagedir_clear_page(thread_current()->pagedir, vme->vaddr);
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
