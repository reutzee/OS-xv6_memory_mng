#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"



#ifdef NONE
#else
#ifdef LAPA
static uint default_age=0xffffffff;
#else
static uint default_age=0;
#endif
#endif


struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc()
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;
  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

#ifndef NONE
  p->pagedout=0;
  p->pages_in_memory_counter=0;
  p->pages_in_swapfile_counter=0;
  p->fault_counter=0;
  int i=0;
  for(i=0;i<MAX_PSYC_PAGES;i++)
  {
    p->disk_pg_arr[i].va=(char*)-1;//0xFFFFFFFF
    p->disk_pg_arr[i].location=-1;
    p->memory_pg_arr[i].va=(char*)-1;//0XFFFFFFFF
    p->memory_pg_arr[i].age=default_age;
    p->memory_pg_arr[i].next=0;
    p->memory_pg_arr[i].prev=0;
  }
  p->head=0;
  p->tail=0;
#endif
  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S
  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);
  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
}

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

   pid = np->pid;

#ifndef NONE//copy stuff from parent to son;
  np->pages_in_swapfile_counter=curproc->pages_in_swapfile_counter;
  np->pages_in_memory_counter=curproc->pages_in_memory_counter;
  //cprintf("np->page in swapfile =%d \n",np->pages_in_swapfile_counter);
  np->pagedout=0;
  np->fault_counter=0;
  for(i=0;i<MAX_PSYC_PAGES;i++)
  {
    np->memory_pg_arr[i].va=curproc->memory_pg_arr[i].va;
    np->memory_pg_arr[i].age=curproc->memory_pg_arr[i].age;
    //np->memory_pg_arr[i].next=curproc->memory_pg_arr[i].next;
    //np->memory_pg_arr[i].prev=curproc->memory_pg_arr[i].prev;
    np->disk_pg_arr[i].va=curproc->disk_pg_arr[i].va;
    np->disk_pg_arr[i].location=curproc->disk_pg_arr[i].location;

  }

int j;
  for(i=0;i<MAX_PSYC_PAGES;i++)
  {
    for(j=0;j<MAX_PSYC_PAGES;++j)
    {
//      if(memory_pg_arr[i].next!=0)
      if(np->memory_pg_arr[j].va==curproc->memory_pg_arr[i].next->va)
        np->memory_pg_arr[i].next=&np->memory_pg_arr[i];
    
      if(np->memory_pg_arr[j].va==curproc->memory_pg_arr[i].prev->va)
        np->memory_pg_arr[i].prev=&np->memory_pg_arr[i];
    }

  }
for(i=0;i<MAX_PSYC_PAGES;i++)
{
  if (curproc->head->va ==np->memory_pg_arr[i].va)
  {
      np->head=&np->memory_pg_arr[i];
  }

 if (curproc->tail->va ==np->memory_pg_arr[i].va)
{      np->tail=&np->memory_pg_arr[i];
}

}

#ifndef NONE
  if(curproc->pid!=1)//if my parent is init
  {
    createSwapFile(np);
    copySwapFile(curproc,np);
  }
#endif


 // np->tail=curproc->tail;
#endif



  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;
  if(curproc == initproc)
   panic("init exiting");
  
  
      #if TRUE
   acquire(&ptable.lock);

  procdump_proc(myproc());
  release(&ptable.lock);

  #endif
    #ifndef NONE
     if(!(curproc->pid==1||curproc->parent->pid==1))//NOT shell or init
{
    if (removeSwapFile(curproc) != 0)
     {
    panic("failed in delete the swap file at exit\n");
     }
     }


    #endif



  
  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }


  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE; 
 
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}




pte_t *
proc_walkpagedir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}


 int updateAccessBit_proc(char *va,struct proc* p)
{
  uint accessed;
  pte_t *pte = proc_walkpagedir(p->pgdir, (void*)va, 0);
  if (!pte){
      cprintf("pid=%d\n",p->pid);
    panic("updateAccessBi111t: pte is empty");
  }
  if(!*pte)
  {
    return -2;
  }
  accessed = (*pte) & PTE_A;
  (*pte) &= ~PTE_A;
  if(accessed==0)
  {
      return 0;
    }
    else
    {
        return 1;
    }
}


//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
#ifndef NONE
#ifndef SCFIFO
  int i=0;
#endif
#endif
#ifdef AQ
  uint tmp_age;
  char* tmp_va=0;
#endif
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      #ifndef NONE
      #ifndef SCFIFO
      #ifndef AQ
      if(!(p->pid==1||p->parent->pid==1))
      {
        for(i=0;i<MAX_PSYC_PAGES;i++)
        {
            if(p->memory_pg_arr[i].va!=(char*)-1)
            {
            p->memory_pg_arr[i].age=p->memory_pg_arr[i].age>>1;
            int tmp=updateAccessBit_proc(p->memory_pg_arr[i].va,p)==1;
            if(tmp==1)
            p->memory_pg_arr[i].age=p->memory_pg_arr[i].age|(1<<31);
            }
        }
      }
      #endif
      #endif
      #endif
      #ifdef AQ
      if(!(p->pid==1||p->parent->pid==1))
      {
        for(i=0;i<MAX_PSYC_PAGES;i++)
        {
          if(p->memory_pg_arr[i].va!=(char*)-1)
          {
            int tmp=updateAccessBit_proc(p->memory_pg_arr[i].va,p)==1;
            if(tmp==1)
            {
              p->memory_pg_arr[i].age=1;
            }
            else
            {
              p->memory_pg_arr[i].age=0;
            }          
          }
        }
        struct free_page* curpage=p->head;//remove from tail
        while(curpage!=0)
        {
          if(curpage->prev != 0)
          {
            if(curpage->prev->age==0&&curpage->age==1)
            {
              tmp_va=curpage->prev->va;
              curpage->prev->va=curpage->va;
              curpage->va=tmp_va;
              tmp_age=curpage->age;
              curpage->age=curpage->prev->age;
              curpage->prev->age=tmp_age;
            }
          }
          curpage=curpage->next;
        }
      }
      #endif
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.



void
procdump_proc(struct proc* p)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  char *state;
  uint pc[10];
  uint page_counter;
  
    if(p->state == UNUSED)
      return;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    page_counter=p->pages_in_memory_counter+p->pages_in_swapfile_counter;
    cprintf(" %d %s ", p->pid, state);
    cprintf("%d  %d  %d  %d  %s  ",page_counter,p->pages_in_swapfile_counter,p->fault_counter,p->pagedout,p->name);
   // cprintf("%d %s %s",p->pid,state,p->name);
    //<field 1><field 2><allocated memory pages><paged out><page faults><total number of paged out><field set 3> 
    //  cprintf("%d  %s %d %d %d %d %s",p->pid,state,page_counter,p->pages_in_swapfile_counter,p->fault_counter,p->pagedout,p->name);
    //cprintf("%d %s %d %d %d %s", p->pid,state,page_counter,p->pages_in_swapfile_counter,p->fault_counter,p->pagedout,p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
      cprintf("%d / %d free pages in the system\n", getfreepages(),getTotalfreePages());
}






#ifdef TRUE
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    //cprintf("\npid:%d state:%s\n", p->pid, state);
    //cprintf("allocated memory pages:%d paged out:%d page faults:%d total number of paged out:%d\n name:%s\n",page_counter,p->pages_in_swapfile_counter,p->fault_counter,p->pagedout,p->name);
    cprintf(" %d %s ", p->pid, state);
    cprintf("%d  %d  %d  %d  %s  ",(p->pages_in_swapfile_counter+p->pages_in_memory_counter),p->pages_in_swapfile_counter,p->fault_counter,p->pagedout,p->name);
  //  cprintf("%d %s %s",p->pid,state,p->name);
    //<field 1><field 2><allocated memory pages><paged out><page faults><total number of paged out><field set 3> 
    //  cprintf("%d  %s %d %d %d %d %s",p->pid,state,page_counter,p->pages_in_swapfile_counter,p->fault_counter,p->pagedout,p->name);
    //cprintf("%d %s %d %d %d %s", p->pid,state,page_counter,p->pages_in_swapfile_counter,p->fault_counter,p->pagedout,p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
    //<current free pages>/ <total free pages>free pages in the system
    //cprintf("free pages =%d totalfree page =%d \n",getfreepages(),getTotalfreePages());
  }
      cprintf("%d / %d free pages in the system\n", getfreepages(),getTotalfreePages());
}
#else//is none
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

#endif
