#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <array.h>
#include <synch.h>
#include <mips/trapframe.h>
#include <kern/fcntl.h>
#include <vfs.h>
#include <limits.h>

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode, bool exit_by_call) {
#if OPT_A2
  if (proc_table_cv == NULL){
    proc_table_cv = cv_create("proc_table_cvv");
  }
  pid_t currpid = curproc -> pid;
  lock_acquire(proc_table_lock);
  struct proc_table* pt = find_proc_table(currpid);
  KASSERT(pt != NULL);
  if (pt -> ppid != -1){ // curproc is a child.
    struct proc_table* ppt = find_proc_table(pt -> ppid);
    // if (ppt == NULL){ // MYDEBUG
    //   DEBUG(DB_SYSCALL, "PROC_SYSCALLS_C, ppt is null");
    // }
    if (exit_by_call){
      pt -> exitcode = _MKWAIT_EXIT(exitcode);
    } else {
      pt -> exitcode = _MKWAIT_SIG(exitcode);
    }
    if (ppt -> state == PROC_RUNNING){
      pt -> state = PROC_ZOMBIE;
      cv_broadcast(proc_table_cv, proc_table_lock);
    } else {
      // if parent exit, i exit. if parent zombie, i also exit
      // since a parent can only become zombie when it run _exit() / exiting.
      // so that parent won't have chance to be interested in me.
      pt -> state = PROC_EXITED;
      lock_acquire(pid_pool_lock);
      array_add(pid_pool, &currpid, NULL);
      lock_release(pid_pool_lock);
    }
  } else {
    pt -> state = PROC_EXITED;
    lock_acquire(pid_pool_lock);
    array_add(pid_pool, &currpid, NULL);
    lock_release(pid_pool_lock);
  }
  for (unsigned i = 0; i < array_num(proc_tables); ++i){
    struct proc_table* pt = array_get(proc_tables, i);
    if ((pt -> ppid == currpid) && (pt -> state == PROC_ZOMBIE)){
      pt -> state = PROC_EXITED;
      lock_acquire(pid_pool_lock);
      array_add(pid_pool, &(pt -> pid), NULL);
      lock_release(pid_pool_lock);
    }
  }
  lock_release(proc_table_lock);
#endif
  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  (void)exitcode;

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);
  
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
#if OPT_A2
    //init curproc??
    *retval = curproc -> pid;
#else
    *retval = 1;
#endif
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

  if (options != 0) {
    return(EINVAL);
  }
  /* for now, just pretend the exitstatus is 0 */
#if OPT_A2
  if (proc_table_cv == NULL){
    proc_table_cv = cv_create("proc_table_cvv");
  }
  lock_acquire(proc_table_lock);
  struct proc_table* pt = find_proc_table(curproc -> pid);
  KASSERT(pt != NULL);
  struct proc_table* wait_pt = find_proc_table(pid);
  if (wait_pt == NULL) {
    lock_release(proc_table_lock);
    return (ESRCH);
  }
  if (wait_pt -> ppid != curproc -> pid){
    lock_release(proc_table_lock);
    return (ECHILD);
  }
  // DEBUG(DB_SYSCALL, "wait_pt -> state == %d", wait_pt -> state);
  while (wait_pt -> state == PROC_RUNNING){
    cv_wait(proc_table_cv, proc_table_lock);
  }
  exitstatus = wait_pt -> exitcode;
  lock_release(proc_table_lock);
#else
  exitstatus = 0;
#endif
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}
#if OPT_A2
int sys_fork(pid_t* retval, struct trapframe *tf){
    struct proc *child_proc = proc_create_runprogram(curproc -> p_name);
    struct proc_table* pt = find_proc_table(child_proc -> pid);
    pt -> ppid = curproc -> pid;
    if (child_proc == NULL) {
        proc_destroy(child_proc);
        return ENPROC;
    }
    if (as_copy(curproc_getas(), &(child_proc -> p_addrspace)) != 0) {
        proc_destroy(child_proc);
        return ENOMEM;
    }
    struct trapframe* child_tf = kmalloc(sizeof(struct trapframe));
    if (!child_tf) {
        proc_destroy(child_proc);
        // https://www.linuxjournal.com/article/6930
        return ENOMEM;
    }
    // https://stackoverflow.com/questions/13284033/copying-structure-in-c-with-assignment-instead-of-memcpy#comment18110975_13284033
    memcpy(child_tf, tf, sizeof *child_tf);
    int thread_fork_return = thread_fork(curthread -> t_name, child_proc, &enter_forked_process_wrapper, (void*)child_tf, 2333);
    if (thread_fork_return) {
        kfree(child_tf);
        proc_destroy(child_proc);
        return thread_fork_return;
    }
    (void)thread_fork_return;
    *retval = child_proc -> pid;
    return 0;
}

int sys_execv(userptr_t program, userptr_t args) {
  if (program == NULL) {
    return ENODEV;
  }
  int result;
  // Count the number of arguments
  int path_length = strlen((char *)program) + 1;
  int argc = 0;
  int all_arg_length = 0;
  char* arg;
  copyin(args, &arg, sizeof(char*));
  int i = 1;
  while(arg != NULL){
    all_arg_length += strlen(((char **)args)[argc]) + 1;
    copyin(args + (i++)*(sizeof(char*)), &arg, sizeof(char*));
    ++argc;
  }
  if (all_arg_length > ARG_MAX || path_length > PATH_MAX){
    return E2BIG;
  }
  if (path_length == 1){
    return ENOENT;
  }
  // and copy them into the kernel
  char** argv = kmalloc((argc+1) * sizeof *argv);
  for(int i = 0; i < argc; i++){
    copyin(args + i * (sizeof(char*)), &arg, sizeof(char*));
    argv[i] = kmalloc((strlen(arg) + 1) * sizeof **argv);
    result = copyinstr((userptr_t)arg, argv[i], strlen(arg) + 1, NULL);
    if (result) {
      return (result);
    }
  }
  argv[argc] = NULL;

  // Copy the program path into the kernel
  char* program_path = kmalloc((path_length) * sizeof(char));
  result = copyinstr(program, program_path, path_length, NULL);
  if (result) {
    return result;
  }
  struct vnode *v;
  struct addrspace *as1;
  vaddr_t entrypoint, stackptr;
  result = vfs_open(program_path, O_RDONLY, 0, &v);
  if (result) {
		return result;
	}
  as1 = as_create();
	if (as1 ==NULL) {
		vfs_close(v);
		return ENOMEM;
	}
  struct addrspace* as = curproc_setas(as1);
	as_activate();
  result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		return result;
	}
  vfs_close(v);
  result = as_define_stack(as1, &stackptr);
  if (result) {
    /* p_addrspace will go away when curproc is destroyed */
    return result;
  }
  vaddr_t* tmp_ptrs = kmalloc((argc + 1) * sizeof *tmp_ptrs);
  for (int i = argc - 1; i >= 0; --i) {
    stackptr -= ROUNDUP(strlen(argv[i]) + 1, 4);
    result = copyoutstr(argv[i], (userptr_t)stackptr, strlen(argv[i]) + 1, NULL);
    if (result) {
      return (result);
    }
    tmp_ptrs[i] = stackptr;
  }
  tmp_ptrs[argc] = (vaddr_t)NULL;

  for (int i = argc; i >= 0; --i) {
    stackptr -= ROUNDUP(sizeof *tmp_ptrs, 4);
    result = copyout((void*)&tmp_ptrs[i], (userptr_t)stackptr, sizeof *tmp_ptrs);
    if (result) {
      return result;
    }
  }
  as_destroy(as);
  enter_new_process(argc, (userptr_t)stackptr, stackptr, entrypoint);
  return EINVAL;
  // What about ENODEV, ENOTDIR, EISDIR, ENOEXEC and EIO?
}
#endif
