#include "filesys/buffer_cache.h"
#include <string.h>
#include "threads/malloc.h"

#define BUFFER_CACHE_ENTRY_NB 64

void *p_buffer_cache;
struct buffer_head buffer_head[BUFFER_CACHE_ENTRY_NB];
//struct buffer_head *clock_hand;
int clock_hand;

bool bc_read (block_sector_t sector_idx, void *buffer, off_t bytes_read, int chunk_size, int sector_ofs)
{
	struct buffer_head *bh;
	if (!(bh = bc_lookup(sector_idx)))
	{
		bh = bc_select_victim();
		bc_flush_entry(bh);
		bh->dirty = false;
		bh->valid = true;
		bh->sector = sector_idx;
		block_read(fs_device, sector_idx, bh->buffer);
	}
	memcpy (buffer + bytes_read, bh->buffer + sector_ofs, chunk_size);
	bh->clock = true;
	return true;
}

bool bc_write (block_sector_t sector_idx, void *buffer, off_t bytes_written, int chunk_size, int sector_ofs)
{
	bool success = false;
	struct buffer_head *bh;
	if (!(bh = bc_lookup(sector_idx)))
	{
		bh = bc_select_victim();
		bc_flush_entry(bh);
		bh->valid = true;
		bh->sector = sector_idx;
		block_read (fs_device, sector_idx, bh->buffer);
	}
	memcpy(bh->buffer + sector_ofs, buffer + bytes_written, chunk_size);
	bh->clock = true;
	bh->dirty = true;
	success = true;
	return success;
}

void bc_init(void)
{
	int i;
	p_buffer_cache = (void*)malloc(BUFFER_CACHE_ENTRY_NB * 512);
	for(i = 0; i < BUFFER_CACHE_ENTRY_NB; i++)
	{
		buffer_head[i].dirty = false;
		buffer_head[i].valid = false;
		buffer_head[i].buffer = p_buffer_cache + i * 512;
		buffer_head[i].clock = false;
		lock_init(&buffer_head[i].lock);
	}
	clock_hand = 0;
}

void bc_term(void)
{
	bc_flush_all_entries();
	free(p_buffer_cache);
}

struct buffer_head *bc_select_victim (void)
{
	while(true)
	{
		if(!buffer_head[clock_hand].valid || !buffer_head[clock_hand].clock)
		{
			return &buffer_head[clock_hand];
		}
		buffer_head[clock_hand].clock = false;
		clock_hand = (clock_hand + 1) % BUFFER_CACHE_ENTRY_NB;
		/*if (clock_hand == buffer_head + BUFFER_CACHE_ENTRY_NB)
		{
			clock_hand = buffer_head;
		}
		if(!clock_hand->valid || !clock_hand->clock)
		{
			return clock_hand++;
		}
		clock_hand->clock = false;
		clock_hand++;*/
	}
}

struct buffer_head* bc_lookup(block_sector_t sector)
{
	int i;
	for (i = 0; i < BUFFER_CACHE_ENTRY_NB; i++)
	{
		if(buffer_head[i].valid && buffer_head[i].sector == sector)
		{
			return &buffer_head[i];
		}
	}
	return NULL;
}

void bc_flush_entry(struct buffer_head *p_flush_entry)
{
	if (p_flush_entry->valid && p_flush_entry->dirty)
	{
		block_write(fs_device, p_flush_entry->sector, p_flush_entry->buffer);
		p_flush_entry->dirty = false;
	}
}

void bc_flush_all_entries(void)
{
	int i;
	for (i = 0; i < BUFFER_CACHE_ENTRY_NB; i++)
	{
		bc_flush_entry(&buffer_head[i]);
	}
}
