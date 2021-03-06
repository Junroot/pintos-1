#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/buffer_cache.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define DIRECT_BLOCK_ENTRIES 123
#define INDIRECT_BLOCK_ENTRIES 128

enum direct_t
{
	NORMAL_DIRECT,
	INDIRECT,
	DOUBLE_INDIRECT,
	OUT_LIMIT
};

struct sector_location
{
	int directness;
	int index1;
	int index2;
};

struct inode_indirect_block
{
	block_sector_t map_table[INDIRECT_BLOCK_ENTRIES];
};

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SI	ZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
	uint32_t is_dir;
    block_sector_t direct_map_table[DIRECT_BLOCK_ENTRIES];
	block_sector_t indirect_block_sec;
	block_sector_t double_indirect_block_sec;
  };


/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
  	
	struct lock extend_lock;
  };

static bool get_disk_inode(const struct inode *, struct inode_disk *);
static void locate_byte (off_t, struct sector_location *);
static bool register_sector(struct inode_disk *, block_sector_t, struct sector_location);
static bool inode_update_file_length(struct inode_disk *, off_t, off_t);
static void free_inode_sectors (struct inode_disk *);
static block_sector_t byte_to_sector(const struct inode_disk*, off_t pos);

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, uint32_t is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
	  memset(disk_inode, 0xFF, sizeof(*disk_inode));
     // size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
     /* if (free_map_allocate (sectors, &disk_inode->start)) 
        {
          block_write (fs_device, sector, disk_inode);
          if (sectors > 0) 
            {
              static char zeros[BLOCK_SECTOR_SIZE];
              size_t i;
              
              for (i = 0; i < sectors; i++) 
                block_write (fs_device, disk_inode->start + i, zeros);
            }
          success = true; 
        } */
	  disk_inode->is_dir = is_dir;
	  if (length > 0)
	  {
	  		inode_update_file_length(disk_inode, 0, length - 1);
	  }
	  bc_write(sector, disk_inode, 0, BLOCK_SECTOR_SIZE, 0);
	  success = true;
    }
    free (disk_inode);
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init(&inode->extend_lock);
  //block_read (fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          struct inode_disk inode_disk;
		  get_disk_inode(inode, &inode_disk);
		  free_inode_sectors(&inode_disk);
		  free_map_release(inode->sector, 1);
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;
  struct inode_disk inode_disk;

  lock_acquire(&inode->extend_lock);

  get_disk_inode(inode, &inode_disk);

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (&inode_disk, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;
/*
      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          // Read full sector directly into caller's buffer.
		  bc_read (sector_idx, (void*)buffer, bytes_read, chunk_size, sector_ofs);
          //block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          // Read sector into bounce buffer, then partially copy
          //   into caller's buffer. 
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
		  bc_read (sector_idx, (void*)buffer, bytes_read, chunk_size, sector_ofs);
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }*/

	  bc_read (sector_idx, (void*)buffer, bytes_read, chunk_size, sector_ofs);
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  bc_write(inode->sector, &inode_disk, 0, BLOCK_SECTOR_SIZE, 0);

  lock_release(&inode->extend_lock);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;
  struct inode_disk inode_disk;

  inode_disk.length = 0;

  if (inode->deny_write_cnt)
    return 0;

  lock_acquire(&inode->extend_lock);

  get_disk_inode(inode, &inode_disk);
  int old_length = inode_disk.length;
  int write_end =  offset + size - 1;
  if(write_end > old_length - 1)
  {
  	inode_update_file_length(&inode_disk, old_length, write_end);
	bc_write(inode->sector, &inode_disk, 0, BLOCK_SECTOR_SIZE, 0);
  }

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (&inode_disk, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_disk.length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

/*      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
		  bc_write (sector_idx, (void*)buffer, bytes_written, chunk_size, sector_ofs);
          // Write full sector directly to disk.
          //block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          // We need a bounce buffer.
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          // If the sector contains data before or after the chunk
          //   we're writing, then we need to read in the sector
          //   first.  Otherwise we start with a sector of all zeros.
          if (sector_ofs > 0 || chunk_size < sector_left)
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
		  //bc_write (sector_idx, (void*)buffer, bytes_written, chunk_size, sector_ofs);
		  block_write (fs_device, sector_idx, bounce);
		  
      }*/

	  
	  	bc_write (sector_idx, (void*)buffer, bytes_written, chunk_size, sector_ofs);
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);
  lock_release(&inode->extend_lock);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  struct inode_disk inode_disk;
  bc_read(inode->sector, &inode_disk, 0, BLOCK_SECTOR_SIZE, 0);
  return inode_disk.length;
}

static bool get_disk_inode (const struct inode *inode, struct inode_disk *inode_disk)
{
	return bc_read(inode->sector, inode_disk, 0, sizeof(struct inode_disk), 0);
}

static void locate_byte (off_t pos, struct sector_location *sec_loc)
{
	off_t pos_sector = pos / BLOCK_SECTOR_SIZE;

	if (pos_sector < DIRECT_BLOCK_ENTRIES)
	{
		sec_loc->directness = NORMAL_DIRECT;
		sec_loc->index1 = pos_sector;
	}
	else if (pos_sector < DIRECT_BLOCK_ENTRIES + INDIRECT_BLOCK_ENTRIES)
	{
		pos_sector -= DIRECT_BLOCK_ENTRIES;
		sec_loc->directness = INDIRECT;
		sec_loc->index1 = pos_sector;
	}
	else if (pos_sector < DIRECT_BLOCK_ENTRIES + INDIRECT_BLOCK_ENTRIES + INDIRECT_BLOCK_ENTRIES * INDIRECT_BLOCK_ENTRIES)
	{
		pos_sector -= (DIRECT_BLOCK_ENTRIES + INDIRECT_BLOCK_ENTRIES);
		sec_loc->directness = DOUBLE_INDIRECT;
		sec_loc->index1 = pos_sector / INDIRECT_BLOCK_ENTRIES;
		sec_loc->index2 = pos_sector % INDIRECT_BLOCK_ENTRIES;
	}
	else
	{
		sec_loc->directness = OUT_LIMIT;
	}
}

static bool register_sector (struct inode_disk *inode_disk, block_sector_t new_sector, struct sector_location sec_loc)
{
	struct inode_indirect_block first_block, second_block;

	switch (sec_loc.directness)
	{
		case NORMAL_DIRECT:
			inode_disk->direct_map_table[sec_loc.index1] = new_sector;
			break;
		case INDIRECT:
			if(inode_disk->indirect_block_sec == -1)
			{
				if(!free_map_allocate(1, &inode_disk->indirect_block_sec))	return false;
				memset(&first_block, 0xFF, sizeof(struct inode_indirect_block));
			}
			bc_read(inode_disk->indirect_block_sec, &first_block, 0, sizeof(struct inode_indirect_block), 0);
			first_block.map_table[sec_loc.index1] = new_sector;
			bc_write(inode_disk->indirect_block_sec, &first_block, 0, sizeof(struct inode_indirect_block), 0);
			break;
		case DOUBLE_INDIRECT:
			if(inode_disk->double_indirect_block_sec == -1)
			{
				if(!free_map_allocate(1, &inode_disk->double_indirect_block_sec)) return false;
				memset(&first_block, 0xFF, sizeof(struct inode_indirect_block));
			}
			bc_read(inode_disk->double_indirect_block_sec, &first_block, 0, sizeof(struct inode_indirect_block), 0);
			if(first_block.map_table[sec_loc.index1] == -1)
			{
				if(!free_map_allocate(1, &first_block.map_table[sec_loc.index1])) return false;
				bc_write(inode_disk->double_indirect_block_sec, &first_block, 0, sizeof(struct inode_indirect_block), 0);
				memset(&second_block, 0xFF, sizeof(struct inode_indirect_block));
			}
			bc_read(first_block.map_table[sec_loc.index1], &second_block, 0, sizeof(struct inode_indirect_block), 0);
			second_block.map_table[sec_loc.index2] = new_sector;
			bc_write(first_block.map_table[sec_loc.index1], &second_block, 0, sizeof(struct inode_indirect_block), 0);
			break;
		default:
			return false;
	}
	return true;
}

static block_sector_t byte_to_sector(const struct inode_disk *inode_disk, off_t pos)
{
	block_sector_t result_sec;
	if (pos >= inode_disk->length) return -1;
	struct inode_indirect_block ind_block;
	struct sector_location sec_loc;
	block_sector_t sec;
	locate_byte(pos, &sec_loc);

	switch(sec_loc.directness)
	{
		case NORMAL_DIRECT:
			return inode_disk->direct_map_table[sec_loc.index1];

		case INDIRECT:
			bc_read(inode_disk->indirect_block_sec, &ind_block, 0, sizeof(struct inode_indirect_block), 0);
			return ind_block.map_table[sec_loc.index1];

		case DOUBLE_INDIRECT:
			bc_read(inode_disk->double_indirect_block_sec, &ind_block, 0, sizeof(struct inode_indirect_block), 0);
			sec = ind_block.map_table[sec_loc.index1];
			bc_read(sec, &ind_block, 0, sizeof(struct inode_indirect_block), 0);
			return ind_block.map_table[sec_loc.index2];

		default:
			return -1;
	}
}

bool inode_update_file_length(struct inode_disk *inode_disk, off_t start_pos, off_t end_pos)
{
	char *zeros = calloc(BLOCK_SECTOR_SIZE, sizeof(char));
	off_t size = end_pos - start_pos + 1;
	off_t offset = start_pos;
	block_sector_t sector;
	struct sector_location sec_loc;

	inode_disk->length = end_pos + 1;
	while(size > 0)
	{
		int sector_ofs = offset % BLOCK_SECTOR_SIZE;
		int chunk_size = BLOCK_SECTOR_SIZE - sector_ofs;
		if (sector_ofs == 0)
		{
			if(!free_map_allocate(1, &sector))
			{
				free(zeros);
				return false;
			}
			locate_byte(offset, &sec_loc);
			register_sector(inode_disk, sector, sec_loc);
			bc_write(sector, zeros, 0, BLOCK_SECTOR_SIZE, 0);
		}
		size -= chunk_size;
		offset += chunk_size;
	}

	free(zeros);
	return true;
}

static void free_inode_sectors(struct inode_disk *inode_disk)
{
	int i, j;
	struct inode_indirect_block first_block, second_block; 
	for(i = 0; i < DIRECT_BLOCK_ENTRIES; i++)
	{
		if (inode_disk->direct_map_table[i] == -1)	break;
		free_map_release(inode_disk->direct_map_table[i], 1);
	}
	
	if (inode_disk->indirect_block_sec != -1)
	{
		bc_read (inode_disk->indirect_block_sec, &first_block, 0, sizeof(struct inode_indirect_block), 0);
		for(i = 0; i < INDIRECT_BLOCK_ENTRIES; i++)
		{
			if (first_block.map_table[i] == -1) break;
			free_map_release(first_block.map_table[i], 1);
		}
		free_map_release(inode_disk->indirect_block_sec, 1);
	}

	if (inode_disk->double_indirect_block_sec != -1)
	{
		bc_read (inode_disk->double_indirect_block_sec, &first_block, 0, sizeof(struct inode_indirect_block), 0);
		for(i = 0; i < INDIRECT_BLOCK_ENTRIES; i++)
		{
			if (first_block.map_table[i] == -1)	break;
			bc_read(first_block.map_table[i], &second_block, 0, sizeof(struct inode_indirect_block), 0);
			for(j = 0; j < INDIRECT_BLOCK_ENTRIES; j++)
			{
				if (second_block.map_table[j] == -1)	break;
				free_map_release(second_block.map_table[j], 1);
			}
			free_map_release(first_block.map_table[i], 1);
		}
		free_map_release(inode_disk->double_indirect_block_sec, 1);
	}
}

bool inode_is_dir(const struct inode *inode)
{
	struct inode_disk inode_disk;

	get_disk_inode(inode, &inode_disk);
	return inode_disk.is_dir;
}
