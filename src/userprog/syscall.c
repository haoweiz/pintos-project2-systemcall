#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "pagedir.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "threads/synch.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "userprog/process.h"
#include "threads/malloc.h"

/* A lock which is used to read and write.*/
static struct lock filesys_lock;

static void syscall_handler (struct intr_frame *);

/* Save memory access*/
bool validate_pointer(const void *vaddr);

/* All 13 system call functions*/
void halt(void);
void exit(int status);
int read(int fd,void *buffer,unsigned size);
int write(int fd,const void *buffer,unsigned size);
pid_t exec(const char *cmd_line);
int wait(pid_t pid);
bool create(const char *file,unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
void seek(int fd,unsigned position);
unsigned tell(int fd);
void close(int fd);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}


/* Use f->esp to extract arguments from user stack and save them in args[3], syscall_nr is system call number which correspond to enumeration defined in syscall_nr.h */
static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  lock_init(&filesys_lock);
  if(!validate_pointer(f->esp))
    exit(-1);
  int args[3];
  int syscall_nr = *(int*)f->esp;
  switch(syscall_nr){
    case SYS_HALT:halt();break;
    case SYS_EXIT:{
           args[0] = *((int*)f->esp+1);
           if(args[0] < -1)
             exit(-1);
           exit(args[0]);
         };break;
    case SYS_EXEC:{
           args[0] = *((int*)f->esp+1);
           f->eax = exec((const char*)args[0]);
         };break;
    case SYS_WAIT:{
           args[0] = *((int*)f->esp+1);
           f->eax = wait((pid_t)args[0]);
         };break;
    case SYS_CREATE:{
           args[0] = *((int*)f->esp+1);
           args[1] = *((int*)f->esp+2);
           f->eax = create((const char*)args[0],(unsigned)args[1]);
         };break;
    case SYS_REMOVE:{
           args[0] = *((int*)f->esp+1);
           f->eax = remove((const char*)args[0]);
         };break;
    case SYS_OPEN:{
           args[0] = *((int*)f->esp+1);
           f->eax = open((const char*)args[0]);
         };break;
    case SYS_FILESIZE:{
           args[0] = *((int*)f->esp+1);
           f->eax = filesize(args[0]);
         };break;
    case SYS_READ:{
           args[0] = *((int*)f->esp+1);
           args[1] = *((int*)f->esp+2);
           args[2] = *((int*)f->esp+3);
           f->eax = read(args[0],(void*) args[1],(unsigned) args[2]);
         };break;
    case SYS_WRITE:{
           args[0] = *((int*)f->esp+1);
           args[1] = *((int*)f->esp+2);
           args[2] = *((int*)f->esp+3);
           f->eax = write(args[0],(const void*) args[1],(unsigned) args[2]);
         };break;
    case SYS_SEEK:{
           args[0] = *((int*)f->esp+1);
           args[1] = *((int*)f->esp+2);
           seek(args[0],(unsigned)args[1]);
         };break;
    case SYS_TELL:{
           args[0] = *((int*)f->esp+1);
           f->eax = tell(args[0]);
         };break;
    case SYS_CLOSE:{
           args[0] = *((int*)f->esp+1);
           close(args[0]);
         };break;
  }
}

/* Test whether the pointer is problematic, if *vaddr is null pointer, or point to unmapped user memory, or point to kernel address, return false */
bool validate_pointer(const void *vaddr){
  if(vaddr != NULL && is_user_vaddr(vaddr) && pagedir_get_page(thread_current()->pagedir,vaddr) != NULL)
    return true;
  else
    return false;
}

/* Shut the pintos down*/
void halt(void){
  shutdown_power_off();
}

/* Wait for child child process terminates.*/
int wait(pid_t pid){
  return process_wait(pid);
}

/* Execute a new process with command line *cmd_line*/
pid_t exec(const char *cmd_line){
  if(!validate_pointer(cmd_line))
    exit(-1);
  pid_t pid = (pid_t)process_execute(cmd_line);
  if(pid == TID_ERROR)
    return -1;

  struct thread *child_process;
  struct list_elem *e;
  for (e = list_begin(&thread_current()->child_list); e != list_end(&thread_current()->child_list);e = list_next(e)){
    if(list_entry(e,struct thread,elem)->tid == pid){
      child_process = list_entry(e,struct thread,elem);
      break;
    }
  }
  sema_down(&thread_current()->load_sema);    /* Block current thread until child_process finish loading*/
  if(!child_process->load_bool)
    return -1;
  return pid;
}

/* Exit current process with exit_status and print the exit status */
void exit(int status){
  if(thread_current()->parent->is_alive){
    thread_current()->is_alive = false;
    thread_current()->exit_status = status;
  }
  printf("%s: exit(%d)\n",thread_current()->name,thread_current()->exit_status);
  thread_exit();
}

/* If fd equals STDIN_FILENO, read from console, otherwise read from file whose file descriptor is fd.*/
int read(int fd,void *buffer,unsigned size){
  unsigned read_bytes = 0;
  if(fd == STDIN_FILENO){
    for(;read_bytes < size;read_bytes++){
      *(uint8_t*)(buffer+read_bytes) = input_getc();
    }
    return size;
  }
  if(!validate_pointer(buffer))
    exit(-1);
  lock_acquire(&filesys_lock);
  struct list_elem *e;
  struct file *current_file;
  current_file = NULL;
  for(e = list_begin(&thread_current()->process_files);e != list_end(&thread_current()->process_files);e = list_next(e)){
    if(list_entry (e, struct file_info, elem)->handle == fd){
      current_file = list_entry (e, struct file_info, elem)->file;
      break;
    }
  }
  if(current_file == NULL){
    lock_release(&filesys_lock);
    return -1;
  }
  read_bytes = file_read(current_file,buffer,size);
  lock_release(&filesys_lock);
  return read_bytes;
}

/* If fd equals STDOUT_FILENO, print to console, otherwise write to file whose file descriptor is fd.*/
int write(int fd,const void *buffer,unsigned size){
  if(fd == STDOUT_FILENO){
    putbuf(buffer,size);
    return size;
  }
  if(!validate_pointer(buffer))
    exit(-1);
  lock_acquire(&filesys_lock);
  struct list_elem *e;
  struct file *current_file;
  current_file = NULL;
  for(e = list_begin(&thread_current()->process_files);e != list_end(&thread_current()->process_files);e = list_next(e)){
    if(list_entry (e, struct file_info, elem)->handle == fd){
      current_file = list_entry (e, struct file_info, elem)->file;
      break;
    }
  }
  if(current_file == NULL){
    lock_release(&filesys_lock);
    return -1;
  }
  int wrote_bytes = file_write(current_file,buffer,size);
  lock_release(&filesys_lock);
  return wrote_bytes;
}

/* Create a file whose size is initial_size */
bool create(const char *file,unsigned initial_size){
  if(file == NULL || !validate_pointer(file))
    exit(-1);
  bool success = filesys_create(file, initial_size);
  return success;
}

/* Open a file whose name is file */
int open(const char *file){
  if(file == NULL)
    return -1;
  if(!validate_pointer(file))
    exit(-1);
  lock_acquire(&filesys_lock);
  struct file *f = filesys_open(file);
  if(f == NULL || file == ""){
    lock_release(&filesys_lock);
    return -1;
  }
  struct file_info *file_info = (struct file_info*)malloc(sizeof(struct file_info));
  file_info->file = f;
  file_info->handle = thread_current()->open_file_number + 2;
  thread_current()->open_file_number++;
  list_push_back(&thread_current()->process_files,&file_info->elem);
  lock_release(&filesys_lock);
  return file_info->handle;
}

/* Return the size of file whose file descriptor is fd*/
int filesize(int fd){
  struct list_elem *e;
  struct file *current_file;
  current_file = NULL;
  for(e = list_begin(&thread_current()->process_files);e != list_end(&thread_current()->process_files);e = list_next(e)){
    if(list_entry (e, struct file_info, elem)->handle == fd){
      current_file = list_entry (e, struct file_info, elem)->file;
      break;
    }
  }
  if(current_file == NULL)
    return -1;
  return file_length(current_file);
}

/* Delete file whose name is file*/
bool remove(const char *file){
  return filesys_remove(file);
}

/* Changes the next byte to be read or written in open file fd to position */
void seek(int fd,unsigned position){
  struct list_elem *e;
  struct file *current_file;
  current_file = NULL;
  for(e = list_begin(&thread_current()->process_files);e != list_end(&thread_current()->process_files);e = list_next(e)){
    if(list_entry (e, struct file_info, elem)->handle == fd){
      current_file = list_entry (e, struct file_info, elem)->file;
      break;
    }
  }
  if(current_file == NULL)
    return;
  file_seek(current_file,position);
}

/* Returns the position of the next byte to be read or written in open file fd, expressed
in bytes from the beginning of the file*/
unsigned tell(int fd){
  struct list_elem *e;
  struct file *current_file;
  current_file = NULL;
  for(e = list_begin(&thread_current()->process_files);e != list_end(&thread_current()->process_files);e = list_next(e)){
    if(list_entry (e, struct file_info, elem)->handle == fd){
      current_file = list_entry (e, struct file_info, elem)->file;
      break;
    }
  }
  if(current_file == NULL)
    return -1;
  return file_tell(current_file);
}

/*Close file whose file descriptor is fd, if fd equals -1, close all of the files*/
void close(int fd){
  struct list_elem *e;
  if(fd != -1){
    struct file *current_file;
  current_file = NULL;
    for(e = list_begin(&thread_current()->process_files);e != list_end(&thread_current()->process_files);e = list_next(e)){
      if(list_entry (e, struct file_info, elem)->handle == fd){
        current_file = list_entry (e, struct file_info, elem)->file;
        break;
      }
    }
    if(current_file == NULL)
      return;
    file_close(current_file);
    thread_current()->open_file_number--;
    list_remove(&list_entry(e,struct file_info,elem)->elem);
    free(list_entry(e,struct file_info,elem));
    return;
  }
  else{
    for(e = list_begin(&thread_current()->process_files);e != list_end(&thread_current()->process_files);e = list_next(e)){
      file_close(list_entry(e,struct file_info,elem)->file);
      thread_current()->open_file_number--;
      list_remove(&list_entry(e,struct file_info,elem)->elem);
      free(list_entry(e,struct file_info,elem));
    }
  }
}





