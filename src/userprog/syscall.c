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
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
tid_t exec (const char *cmd_line);
int wait(tid_t tid);

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
	//wait for loading child process
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
	int ret;
	if (file == NULL)
	{
		lock_release(&filesys_lock);
		return -1;
	}
	lock_acquire(&filesys_lock);
	f = filesys_open(file);
	if (f == NULL)
	{	
		lock_release(&filesys_lock);
		return -1;
	}
	ret = process_add_file(f);
	lock_release(&filesys_lock);

	return ret;
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
	process_close_file(fd);
}

void check_valid_buffer (void *buffer, unsigned size, void *esp, bool to_write)
{
	unsigned i;
	char *temp = (char *)buffer;
	struct vm_entry* vme;

	for (i = 0; i < size; i++)
	{
		vme = check_address(temp,esp);
		if (vme != NULL && to_write == true)
		{
			if (vme->writable == false) exit(-1);
		}
		temp++;
	}
}

void check_valid_string (const void *str, void *esp)
{
	char* ch = (char*)str;
	while (*ch != '\0')
	{
		check_address((void*)ch,esp);
		ch++;
	}
}

//Check addr is user area
struct vm_entry * check_address (void *addr, void* esp /*Unused*/)
{
	if(addr<0x8048000 || addr>=0xc0000000)
	{
		exit(-1);
	}
	return find_vme(addr);
}

void 
get_argument (void *esp, int *arg, int count)
{
	int i;
	int *sp = esp;
	for (i=0; i<count; ++i)
	{
		check_address(sp + i + 1, esp);
		arg[i] = *(sp + i + 1);
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
  int arg[4];
  check_address(esp,esp);// esp check
  switch(*esp)
  {
	case SYS_HALT:
		halt();
		break;
    case SYS_EXIT:
		get_argument(esp,arg,1);// If argument is pointer, check address
		exit((int)arg[0]);
		break;
	case SYS_CREATE:
		get_argument(esp,arg,2);
		check_valid_string((void*)arg[0], esp);
		f->eax = create((const char *)arg[0],(unsigned)arg[1]);
		break;
	case SYS_REMOVE:
		get_argument(esp,arg,1);
		check_valid_string((void*)arg[0], esp);
		f->eax = remove((const char *)arg[0]);
		break;
    case SYS_EXEC:
		get_argument(esp,arg,1);
		check_valid_string((void*)arg[0], esp);
		f->eax = exec((const char *)arg[0]);
		break;
	case SYS_WAIT:
		get_argument(esp,arg,1);
		f->eax = wait((tid_t)arg[0]);
		break;
    case SYS_OPEN:
		get_argument(esp,arg,1);
		check_valid_string((void*)arg[0],esp);
		f->eax = open((const char *)arg[0]);
		break;
	case SYS_FILESIZE:
		get_argument(esp,arg,1);
		f->eax = filesize((int)arg[0]);
		break;
	case SYS_READ:
		get_argument(esp,arg,3);
		check_valid_buffer((void*)arg[1], (unsigned)arg[2], esp, true);
		f->eax = read((int)arg[0],(void *)arg[1],(unsigned)arg[2]);
		break;
	case SYS_WRITE:
		get_argument(esp,arg,3);
		check_valid_buffer((void*)arg[1], (unsigned)arg[2], esp, false);
		f->eax = write((int)arg[0],(void *)arg[1],(unsigned)arg[2]);
		break;
	case SYS_SEEK:
		get_argument(esp,arg,2);
		seek((int)arg[0],(unsigned)arg[1]);
		break;
	case SYS_TELL:
		get_argument(esp,arg,1);
		f->eax = tell((int)arg[0]);
		break;
	case SYS_CLOSE:
		get_argument(esp,arg,1);
		close((int)arg[0]);
		break;	
  }
}
