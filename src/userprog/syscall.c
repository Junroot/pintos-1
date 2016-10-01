#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "devices/shutdown.h"
#include "threads/thread.h"
#include "filesys/filesys.h"

static void syscall_handler (struct intr_frame *);

void 
halt()
{
	shutdown_power_off();
}

void 
exit(int status)
{
	struct thread *t = thread_current();
	t->exit_status = status;
	printf("%s: exit(%d)\n" ,t->name , status);
	thread_exit();
}

bool 
create (const char *file, unsigned initial_size)
{
	bool ret;
	if (file == NULL) exit(-1);
	ret = filesys_create(file,initial_size);
	return ret;
}

bool 
remove (const char *file)
{
	return filesys_remove(file);
}

tid_t exec (const char *cmd_line)
{
	struct thread *t = get_child_process(process_execute(cmd_line));
	sema_down(&(t->sema_load));
	if (t->load) return t->tid;
	else return -1;
}

int
wait(tid_t tid)
{
	return process_wait (tid);
}

int
open(const char *file)
{
	struct file *f;
	if (file == NULL) return -1;
	f = filesys_open(file);
	if (f == NULL) return -1;

	return process_add_file(f);
}

int
filesize(int fd)
{
	struct file *f = process_get_file(fd);
	if (f == NULL) return -1;

	return file_length(f);
}

int read (int fd, void *buffer, unsigned size)
{
	struct file *f;
	int res= 0;
	lock_acquire(&filesys_lock);
	
	if (fd == 0)
	{
		unsigned i;
		for(i=0; i<size; ++i)
		{
			*(uint8_t *)(buffer + i) = input_getc();
		}
		res = size;
	}
	else if (fd == 1)
	{
		res = -1;
	}
	else
	{
		f = process_get_file(fd);
		if (f==NULL)	res = -1;
		else
		{
			res = file_read(f,buffer,size);
		}
	}
	lock_release(&filesys_lock);
	return res;
}

int 
write(int fd, void *buffer, unsigned size)
{
	int res=0;
	struct file * f;
	lock_acquire(&filesys_lock);
	if (fd == 0)
	{
		res = -1;
	}
	else if (fd == 1)
	{
		putbuf(buffer,size);
		res = size;
	}
	else
	{
		f = process_get_file(fd);
		if (f == NULL)	res=-1;
		else
		{
			res = file_write(f,buffer,size);
		}
	}
	lock_release(&filesys_lock);
	return res;
}

void
seek(int fd, unsigned position)
{
	
	struct file *f = process_get_file(fd);
	file_seek(f,position);
}

unsigned
tell (int fd)
{
	struct file *f = process_get_file(fd);
	return file_tell(f);
}

void close (int fd)
{
	struct file *f = process_close_file(fd);
}

void 
check_address (void *addr)
{
	if(addr<0x8048000 || addr>=0xc0000000)
	{
		exit(-1);
	}
}

void 
get_argument (void *esp, int *arg, int count)
{
	int i;
	int *sp = esp;
	for (i=0; i<count; ++i)
	{
		check_address(sp+i+1);
		arg[i] = *(sp+i+1);
	}
}

void
syscall_init (void) 
{
  lock_init(&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int *esp = f->esp;
  int ret;
  int* arg = (int*)malloc(sizeof(int)*100);
  check_address(esp);
  switch(*esp)
  {
	case SYS_HALT:
		halt();
		break;
    case SYS_EXIT:
		get_argument(esp,arg,1);
		exit((int)arg[0]);
		break;
	case SYS_CREATE:
		get_argument(esp,arg,2);
		check_address(arg[0]);
		ret=create((const char *)arg[0],(unsigned)arg[1]);
		break;
	case SYS_REMOVE:
		get_argument(esp,arg,1);
		check_address(arg[0]);
		ret=remove((const char *)arg[0]);
		break;
    case SYS_EXEC:
		get_argument(esp,arg,1);
		check_address(arg[0]);
		ret=exec((const char *)arg[0]);
		break;
	case SYS_WAIT:
		get_argument(esp,arg,1);
		ret=wait((tid_t)arg[0]);
		break;
    case SYS_OPEN:
		get_argument(esp,arg,1);
		check_address(arg[0]);
		ret=open((const char *)arg[0]);
		break;
	case SYS_FILESIZE:
		get_argument(esp,arg,1);
		ret=filesize((int)arg[0]);
		break;
	case SYS_READ:
		get_argument(esp,arg,3);
		check_address(arg[1]);
		ret=read((int)arg[0],(void *)arg[1],(unsigned)arg[2]);
		break;
	case SYS_WRITE:
		get_argument(esp,arg,3);
		check_address(arg[1]);
		ret=write((int)arg[0],(void *)arg[1],(unsigned)arg[2]);
		break;
	case SYS_SEEK:
		get_argument(esp,arg,2);
		seek((int)arg[0],(unsigned)arg[1]);
		break;
	case SYS_TELL:
		get_argument(esp,arg,1);
		ret=tell((int)arg[0]);
		break;
	case SYS_CLOSE:
		get_argument(esp,arg,1);
		close((int)arg[0]);
		break;	
  }
  f->eax = ret;
  free(arg);
}
