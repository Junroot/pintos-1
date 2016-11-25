#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/palloc.h"
#include "vm/page.h"
#include "vm/swap.h"

void lru_list_init(void);
void add_page_to_lru_list(struct page *page);
void del_page_to_lru_list(struct page *page);
void* try_to_free_pages(enum palloc_flags flags);

struct list lru_list;
struct lock lru_list_lock;
struct list_elem *lru_clock;

#endif
