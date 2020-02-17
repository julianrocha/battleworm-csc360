#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED

#include "scheduler.h"

#include <assert.h>
#include <curses.h>
#include <ucontext.h>

#include "util.h"

// This is an upper limit on the number of tasks we can create.
#define MAX_TASKS 128

// This is the size of each task's stack memory
#define STACK_SIZE 65536

// task states:
#define READY 1
#define SLEEPING 2
#define BLOCKED 3
#define WAITING_FOR_INPUT 4
#define TERMINATED 5

// This struct will hold the all the necessary information for each task
typedef struct task_info {
  // This field stores all the state required to switch back to this task
  ucontext_t context;
  
  // This field stores another context. This one is only used when the task
  // is exiting.
  ucontext_t exit_context;
  
  // TODO: Add fields here so you can:
  //   a. Keep track of this task's state.
  int state;
  //   b. If the task is sleeping, when should it wake up?
  size_t wake_time; // use time_ms + sleep_ms to set a wake time in the future
  //   c. If the task is waiting for another task, which task is it waiting for?
  task_t blocking_task;
  //   d. Was the task blocked waiting for user input? Once you successfully
  //      read input, you will need to save it here so it can be returned.
  int user_input;
} task_info_t;

int current_task = 0; //< The handle of the currently-executing task
int num_tasks = 1;    //< The number of tasks created so far
task_info_t tasks[MAX_TASKS]; //< Information for every task

ucontext_t scheduler_thread;

void scheduler_entry_point(){
  while(num_tasks > 0) {
    //printf("there are %d tasks\n", num_tasks);
    for(int i = 0; i < num_tasks; i++){
      //printf("task %d state is %d\n", i, tasks[i].state);
      if(tasks[i].state == READY){
        current_task = i;
        swapcontext(&scheduler_thread, &tasks[i].context);
      }
      if(tasks[i].state == SLEEPING && tasks[i].wake_time <= time_ms()){
        tasks[i].state = READY;
      }
      if(tasks[i].state == BLOCKED && tasks[tasks[i].blocking_task].state == TERMINATED){
        tasks[i].state = READY;
        tasks[i].blocking_task = -1;
      }
      if(tasks[i].state == WAITING_FOR_INPUT){
        tasks[i].user_input = getch();
        if(tasks[i].user_input != ERR){
          tasks[i].state = READY;
        }
      }
    }
  }
  puts("scheduler_entry_point finished");
}

/**
 * Initialize the scheduler. Programs should call this before calling any other
 * functiosn in this file.
 */
void scheduler_init() {
  getcontext(&scheduler_thread);
  
  // Allocate a stack for the new task and add it to the context
  scheduler_thread.uc_stack.ss_sp = malloc(STACK_SIZE);
  scheduler_thread.uc_stack.ss_size = STACK_SIZE;

  scheduler_thread.uc_link = 0;
  
  // And finally, set up the context to execute the task function
  makecontext(&scheduler_thread, scheduler_entry_point, 0);
}


/**
 * This function will execute when a task's function returns. This allows you
 * to update scheduler states and start another task. This function is run
 * because of how the contexts are set up in the task_create function.
 */
void task_exit() {
  tasks[current_task].state = TERMINATED;
  swapcontext(&tasks[current_task].context, &scheduler_thread);
}

/**
 * Create a new task and add it to the scheduler.
 *
 * \param handle  The handle for this task will be written to this location.
 * \param fn      The new task will run this function.
 */
void task_create(task_t* handle, task_fn_t fn) {
  // Claim an index for the new task
  int index = num_tasks;
  num_tasks++;
  
  // Set the task handle to this index, since task_t is just an int
  *handle = index;
 
  // We're going to make two contexts: one to run the task, and one that runs at the end of the task so we can clean up. Start with the second
  
  // First, duplicate the current context as a starting point
  getcontext(&tasks[index].exit_context);
  
  // Set up a stack for the exit context
  tasks[index].exit_context.uc_stack.ss_sp = malloc(STACK_SIZE);
  tasks[index].exit_context.uc_stack.ss_size = STACK_SIZE;
  
  // Set up a context to run when the task function returns. This should call task_exit.
  makecontext(&tasks[index].exit_context, task_exit, 0);
  
  // Now we start with the task's actual running context
  getcontext(&tasks[index].context);
  
  // Allocate a stack for the new task and add it to the context
  tasks[index].context.uc_stack.ss_sp = malloc(STACK_SIZE);
  tasks[index].context.uc_stack.ss_size = STACK_SIZE;
  
  // Now set the uc_link field, which sets things up so our task will go to the exit context when the task function finishes
  tasks[index].context.uc_link = &tasks[index].exit_context;
  
  // And finally, set up the context to execute the task function
  makecontext(&tasks[index].context, fn, 0);

  // initialize rest of task_info struct
  tasks[index].state = READY;
}

/**
 * Wait for a task to finish. If the task has not yet finished, the scheduler should
 * suspend this task and wake it up later when the task specified by handle has exited.
 *
 * \param handle  This is the handle produced by task_create
 */
void task_wait(task_t handle) {
  // Turn main thread into a blocked task in the tasks array
  tasks[0].state = BLOCKED;
  tasks[0].blocking_task = handle;

  swapcontext(&tasks[0].context, &scheduler_thread);
}

/**
 * The currently-executing task should sleep for a specified time. If that time is larger
 * than zero, the scheduler should suspend this task and run a different task until at least
 * ms milliseconds have elapsed.
 * 
 * \param ms  The number of milliseconds the task should sleep.
 */
void task_sleep(size_t ms) {
  if(ms == 0){
    return;
  }
  tasks[current_task].state = SLEEPING;
  tasks[current_task].wake_time = time_ms() + ms;

  swapcontext(&tasks[current_task].context, &scheduler_thread);
}

/**
 * Read a character from user input. If no input is available, the task should
 * block until input becomes available. The scheduler should run a different
 * task while this task is blocked.
 *
 * \returns The read character code
 */
int task_readchar() {
  // TODO: Block this task until there is input available.
  // To check for input, call getch(). If it returns ERR, no input was available.
  // Otherwise, getch() will returns the character code that was read.
  tasks[current_task].user_input = ERR;
  tasks[current_task].state = WAITING_FOR_INPUT;
  swapcontext(&tasks[current_task].context, &scheduler_thread);
  
  return tasks[current_task].user_input;
}
