#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "vm/page.h"

void syscall_init (void);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write (int fd, void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell (int fd);
void close(int fd);
void halt();
void exit(int status);
struct vm_entry *check_address (void *addr,void* esp);
void get_argument (void *esp, int *arg, int count);

struct lock filesys_lock;

#endif /* userprog/syscall.h */
