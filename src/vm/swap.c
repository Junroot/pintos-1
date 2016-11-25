#include "vm/swap.h"

int swap_size = 8 * 1024;

void swap_init()
{
	swap_block = block_get_role(BLOCK_SWAP);
	swap_bitmap = bitmap_create(swap_size);
	bitmap_set_all(swap_bitmap, 0);
	lock_init(&swap_lock);
}

void swap_in(unsigned int used_index, void *kaddr)
{
	
	//struct block* swap_block = block_get_role(BLOCK_SWAP);
	ASSERT(swap_block != NULL);
	ASSERT(swap_bitmap != NULL);
	int i = 0;

	lock_acquire(&swap_lock);
	ASSERT(bitmap_test(swap_bitmap, used_index));
	//bitmap_set_multiple(swap_bitmap, used_index, 1, false);
	bitmap_flip(swap_bitmap, used_index);


	for(i = 0; i < 8; i++)
	{
		block_read(swap_block, 8 * used_index + i, kaddr + BLOCK_SECTOR_SIZE * i);
	}

	lock_release(&swap_lock);
}

unsigned int swap_out(void *kaddr)
{
	lock_acquire(&swap_lock);
	//struct block *swap_block = block_get_role(BLOCK_SWAP);
	ASSERT(swap_block != NULL);
	ASSERT(swap_bitmap != NULL);
	int i = 0;
	unsigned int index = bitmap_scan_and_flip(swap_bitmap, 0, 1, false);
	if (index == BITMAP_ERROR)
	{
		lock_release(&swap_lock);
		return index;
	}

	for(i = 0; i < 8; i++)
	{
		block_write(swap_block, 8 * index + i, kaddr + BLOCK_SECTOR_SIZE * i);
	}

	lock_release(&swap_lock);
	return index;
}
