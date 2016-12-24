#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/buffer_cache.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  bc_init();
  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();

  thread_current()->cur_dir = dir_open_root();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
  bc_term();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  char *prename = palloc_get_page(0);
  char *filename = palloc_get_page(0);

  strlcpy(prename, name, PGSIZE);

  block_sector_t inode_sector = 0;
  struct dir *dir = parse_path(prename, filename);
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, 0)
                  && dir_add (dir, filename, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  palloc_free_page(prename);
  palloc_free_page(filename);
  dir_close (dir);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  char *prename = palloc_get_page(0);
  char *filename = palloc_get_page(0);
  strlcpy(prename, name, PGSIZE);

  struct dir *dir = parse_path(prename, filename);
  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, filename, &inode);
  dir_close (dir);
  palloc_free_page(prename);
  palloc_free_page(filename);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  if(name == NULL)	return false;
  bool success = false;
  struct inode *inode;

  char *prename = palloc_get_page(0);
  char *filename = palloc_get_page(0);

  strlcpy(prename, name, PGSIZE);
  struct dir *dir = parse_path(prename, filename);
  if(dir_lookup(dir, filename, &inode))
  {
  	if(!inode_is_dir(inode))
	{
		success = dir_remove(dir, filename);
	}
	else
	{
		struct dir *dir = dir_open(inode);
		if(!dir_readdir(dir, filename))
		{
			success = dir_remove(dir, filename);
		}
	}
  }

  palloc_free_page(prename);
  palloc_free_page(filename);
  dir_close (dir); 

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  struct dir *rootdir = dir_open_root();
  dir_add(rootdir, ".", ROOT_DIR_SECTOR);
  dir_add(rootdir, "..", ROOT_DIR_SECTOR);
  dir_close(rootdir);
  printf ("done.\n");
}

struct dir* parse_path(char *path_name, char* file_name)
{
	struct dir *dir;
	struct inode *inode;
	if (path_name == NULL || file_name == NULL)	return NULL;
	if (strlen(path_name) == 0)	return NULL;
	char *token, *nextToken, *savePtr;

	if(path_name[0] == '/')	dir = dir_open_root();
	else	dir = dir_reopen(thread_current()->cur_dir);

	if (dir == NULL)	return NULL;

	token = strtok_r(path_name, "/", &savePtr);
	nextToken =strtok_r(NULL, "/", &savePtr);

	if(token == NULL)
	{
		strlcpy(file_name, ".", 2);
		return dir;
	}

	while (token != NULL && nextToken != NULL)
	{
		if(!dir_lookup(dir, token, &inode) || !inode_is_dir(inode))
		{
			dir_close(dir);
			return NULL;
		}
		dir_close(dir);
		dir = dir_open(inode);
		token = nextToken;
		nextToken = strtok_r(NULL, "/", &savePtr);
	}
	strlcpy(file_name, token ,strlen(token) + 1);
	return dir;
}

bool filesys_create_dir(const char *name)
{
	char *prename = palloc_get_page(0);
	char *filename = palloc_get_page(0);
	
	strlcpy(prename, name, PGSIZE);

    block_sector_t inode_sector = 0;
	struct dir *dir = parse_path(prename, filename);
	bool success = (dir != NULL && free_map_allocate(1, &inode_sector) && dir_create(inode_sector, 16) && dir_add(dir, filename, inode_sector));

	if(!success && inode_sector != 0)	free_map_release(inode_sector, 1);

	if (success)
	{
		struct dir *newdir = dir_open(inode_open(inode_sector));
		dir_add(newdir, ".", inode_sector);
		dir_add(newdir, "..", inode_get_inumber(dir_get_inode(dir)));
		dir_close(newdir);
	}

	dir_close(dir);
	palloc_free_page(prename);
	palloc_free_page(filename);

	return success;
}
