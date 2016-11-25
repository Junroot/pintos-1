#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "threads/vaddr.h"
#include "threads/synch.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "devices/block.h"
#include <bitmap.h>

void swap_init(void);
void swap_in(unsigned int used_index, void *kaddr);
unsigned int swap_out(void *kaddr);

struct lock swap_lock;
struct bitmap *swap_bitmap;
struct block *swap_block;

#endif
