#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/syscalls.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/linkage.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/random.h>
#include <linux/sched.h>

MODULE_AUTHOR("Liting V. Zhang");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("This is an elevator simulation");

#define ENTRY_NAME "elevator"
#define ENTRY_SIZE 1000
#define PERMS 0644

//elevator details
#define CAPACITY 10
#define NUM_FLOORS 10
#define MOVE_TIME 2
#define LOAD_TIME 1

//possible sates of the elevator
#define OFFLINE 0
#define IDLE 1
#define LOADING 2
#define UP 3
#define DOWN 4
#define EQUAL 5

//sys calls
#define START_ELEVATOR 335
#define ISSUE_REQUEST 336
#define STOP_ELEVATOR 337

typedef struct passenger
{
  int start_floor;
  int destination_floor;
  int type;  //0 is human , 1 is zombie
  int state; //UP, DOWN. EQUAL
  struct list_head list;
} Passenger;

struct
{
  int state;
  int status; //0 for uninfected
  int current_floor;
  int next_floor;
  int load;
  int num_served;
  int deactiving;
  struct list_head onboard;
} elevator;

int started = 0; //if elevator has started

//list_head of passenger types in each floor
struct list_head floors[10];
//number of waiting passenger of each floor
int waiting[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
static int num_wait = 0;
static struct file_operations fops;

struct list_head *pos;

static char *message;
static int read_p;

struct thread_parameter
{
  int id;
  struct task_struct *kthread;
  struct mutex mutex;
};

struct thread_parameter thread1;

/********************PROTOTYPES************************************************/
int add_passenger(int type, int start_floor, int destination_floor);
int remove_passenger(void);
int load_elevator(void);
int unload_elevator(void);
int move_elevator(void);
int scheduler(void *data);

/**************************SYSTEM CALL DEFINITION********************************/
extern int (*STUB_start_elevator)(void);
int start_elevator(void)
{
  printk(KERN_INFO "STARTED ELEVATOR: CALLED\n");
  if (started == 1)
  {
    printk(KERN_INFO "STARTED ELEVATOR: ELEVATOR ALREADY STARTED \n");
    return 1;
  }
  else
  {
    elevator.state = IDLE;
    elevator.status = 0;
    elevator.current_floor = 1;
    elevator.next_floor = 2;
    elevator.load = 0;
    elevator.num_served = 0;
    elevator.deactiving = 0;
    started = 1;
  }
  return 0;
}

extern int (*STUB_issue_request)(int, int, int);
int issue_request(int start_floor, int destination_floor, int type)
{
  printk(KERN_INFO "ISSUE REQUEST: CALLED\n");
  //error checking
  if (type > 1 || type < 0)
  {
    return 1;
  }
  else if (start_floor > 10 || start_floor < 1)
  {
    return 1;
  }
  else if (destination_floor > 10 || destination_floor < 1)
  {
    return 1;
  }
  else
  {
    if (mutex_lock_interruptible(&thread1.mutex) == 0)
    {
      if (!elevator.deactiving)
      {
        printk(KERN_INFO "ISSUE REQUEST: GAINED LOCK\n");
        add_passenger(type, start_floor, destination_floor);
        printk(KERN_INFO "ISSUE REQUEST: ADDED PASSENGER TO WAITING LIST\n");
      }
    }
    mutex_unlock(&thread1.mutex);
    printk(KERN_INFO "ISSUE REQUEST: DONE REQUEST & RELEASE LOCK\n");
    return 0;
  }
}

extern int (*STUB_stop_elevator)(void);
int stop_elevator(void)
{
  printk(KERN_INFO "STOP ELEVATOR: CALLED");
  if (elevator.deactiving == 1)
  {
    return 1;
  }
  elevator.deactiving = 1;
  if (started == 1)
  {
    if (mutex_lock_interruptible(&thread1.mutex))
    {
      //offload passenger
      struct list_head *temp;  //current pos
      struct list_head *dummy; //next pos
      struct list_head delete_list;
      Passenger *p;

      INIT_LIST_HEAD(&delete_list);

      printk(KERN_INFO "STOP ELEVATOR: OFFLOAD PASSENGERS BEFORE DEACTIVING\n");

      list_for_each_safe(temp, dummy, &delete_list)
      {
        p = list_entry(temp, Passenger, list);
        list_del(temp);
        kfree(p);
      }
      elevator.state = OFFLINE;
      started = 0;
    }
    mutex_unlock(&thread1.mutex);
  }
  return 0;
}

/******************************THREAD***********************************/
void thread_init_parameter(struct thread_parameter *parm)
{
  static int id = 1;
  parm->id = id++;
  mutex_init(&parm->mutex);
  parm->kthread = kthread_run(scheduler, parm, "Scheduler thread %d", parm->id);
}
/*********************************************************************/

//this function add passenger to the list_head floors
int add_passenger(int type, int start_floor, int destination_floor)
{
  Passenger *p;
  p = kmalloc(sizeof(Passenger), __GFP_RECLAIM);
  if (p == NULL)
  {
    return -ENOMEM;
  }
  p->type = type;
  p->start_floor = start_floor;
  p->destination_floor = destination_floor;

  if (start_floor - destination_floor > 0)
  {
    p->state = DOWN;
  }
  else if (start_floor - destination_floor < 0)
  {
    p->state = UP;
  }
  else
  {
    p->state = EQUAL;
  }

  //add to list
  list_add_tail(&p->list, &floors[start_floor - 1]);
  num_wait++;
  waiting[p->start_floor - 1]++;
  printk("ADDED PASSENGER p.type %d p.start %d p.dest %d\n", p->type, p->start_floor, p->destination_floor);
  kfree(p);
  return 0;
}

//clear all passengers
int remove_passenger(void)
{
  struct list_head *temp;
  struct list_head *dummy;
  struct list_head delete_list;
  Passenger *p;

  INIT_LIST_HEAD(&delete_list);

  printk(KERN_INFO "REMOVE PASSENGERS: CALLED\n");
  if (mutex_lock_interruptible(&thread1.mutex) == 0)
  {
    list_for_each_safe(temp, dummy, &delete_list)
    {
      p = list_entry(temp, Passenger, list);
      list_del(temp);
      kfree(p);
    }
    int i;
    for (i = 0; i < NUM_FLOORS; i++)
    {
      waiting[i] = 0;
    }
  }
  mutex_unlock(&thread1.mutex);
  printk(KERN_INFO "REMOVE PASSENGERS: REMOVED ALL PASSSENGERS");
  return 0;
}

//this function load the elevator
int load_elevator()
{
  printk(KERN_INFO "LOAD_ELEVATOR: CALLED\n");
  printk("CURRENT FLOOR IS %d\n", elevator.current_floor);
  struct list_head *temp;
  struct list_head *dummy;

  Passenger *p;
  if (mutex_lock_interruptible(&thread1.mutex) == 0)
  {
    printk(KERN_INFO "LOAD ELEVATOR: GAINED LOCK\n");
    // if no passenger in this floor
    if (list_empty(&floors[elevator.current_floor - 1]))
    {
      printk(KERN_INFO "LOAD ELEVATOR: EMPTY FLOOR \n");
      mutex_unlock(&thread1.mutex);
      printk(KERN_INFO "LOAD ELEVATOR: DONE LOADING AND RELEASE LOCK\n");
      return -1;
    }
    //for each passenger in this floor
    list_for_each_safe(temp, dummy, &floors[elevator.current_floor - 1])
    {
      printk(KERN_INFO "LOAD ELEVATOR: LOADING PASSENGERS\n");
      p = list_entry(temp, Passenger, list);
      printk("LOAD PASSENGER p.type %d p.start %d p.dest %d\n", p->type, p->start_floor, p->destination_floor);
      if (p->state == EQUAL)
      {
        elevator.num_served += elevator.num_served;
        num_wait--;
        waiting[elevator.current_floor - 1]--;
        printk(KERN_INFO "LOAD_ELEVATOR: GOING TO THE SAME FLOOR\n");
      }
      else if (elevator.load + 1 <= CAPACITY)
      {
        //if human, and elevator is uninfected
        if (p->type == 0 && elevator.status == 0)
        {
          printk(KERN_INFO "LOAD_ELEVATOR: LOADING A HUMAN\n");
          //add passenger
          list_move_tail(&p->list, &elevator.onboard);
          elevator.load++;
          num_wait--;
          waiting[elevator.current_floor - 1]--;
        }
        else if (p->type == 0 && elevator.status == 1)
        {
          printk(KERN_INFO "LOAD_ELEVATOR: A HUMAN NOT ABOARD\n");
          //human will not board
          continue;
        }
        else if (p->type == 1) //if zombie
        {
          printk(KERN_INFO "LOAD_ELEVATOR: LOADING A ZOMBIE\n");
          list_move_tail(&p->list, &elevator.onboard);
          elevator.load++;
          elevator.status = 1;
          num_wait--;
          waiting[elevator.current_floor - 1]--;
        }
      }
      printk("LOADS %d\n", elevator.load);
    }
  }
  mutex_unlock(&thread1.mutex);
  printk(KERN_INFO "LOAD_ELEVATOR: DONE LOADING AND RELEASE LOCK\n");
  return 0;
}

int unload_elevator(void)
{
  printk(KERN_INFO "UNLOAD_ELEVATOR: CALLED\n");
  printk("CURRENT FLOOR IS %d\n", elevator.current_floor);

  struct list_head *temp;
  struct list_head *dummy;
  struct list_head delete_list;
  Passenger *p;
  INIT_LIST_HEAD(&delete_list);
  //get lock
  if (mutex_lock_interruptible(&thread1.mutex) == 0)
  {
    list_for_each_safe(temp, dummy, &elevator.onboard)
    {
      p = list_entry(temp, Passenger, list);
      printk("UNLOAD PASSENGER p.type %d p.start %d p.dest %d\n", p->type, p->start_floor, p->destination_floor);

      if (p->destination_floor == elevator.current_floor)
      {
        printk(KERN_INFO "UNLOAD_ELEVATOR: UNLOADING PASSENGER\n");
        list_move_tail(temp, &delete_list);
        elevator.load--;
        elevator.num_served += elevator.num_served;
      }
    }
  }
  //if no one on elevator, change status back to uninfected
  if (elevator.load == 0)
  {
    elevator.status = 0;
  }
  mutex_unlock(&thread1.mutex);
  list_for_each_safe(temp, dummy, &delete_list)
  {
    p = list_entry(temp, Passenger, list);
    list_del(temp);
    kfree(p);
  }
  printk("LOAD AFTER UNLOAD %d\n", elevator.load);
  return 0;
}

//Move elevator
int move_elevator(void)
{
  printk(KERN_INFO "MOVE_ELEVATOR: CALLED\n");

  if (elevator.state == UP)
  {
    printk(KERN_INFO "MOVE_ELEVATOR: MOVING UP\n");
    elevator.current_floor++;
    if (elevator.current_floor == NUM_FLOORS)
    {
      elevator.next_floor = elevator.current_floor - 1;
    }
    else
    {
      elevator.next_floor = elevator.current_floor + 1;
    }
  }
  else if (elevator.state == DOWN)
  {
    printk(KERN_INFO "MOVE_ELEVATOR: MOVING DOWN\n");
    elevator.current_floor--;
    if (elevator.current_floor == 1)
    {
      elevator.next_floor = elevator.current_floor + 1;
    }
    else
    {
      elevator.next_floor = elevator.current_floor - 1;
    }
  }

  return 0;
}

int scheduler(void *data)
{
  //struct thread_parameter *param = data;
  printk(KERN_INFO "SCHEDULER: CALLED\n");

  //keep running until thread should stop
  while (!kthread_should_stop())
  {
    //if there are waiting passengers or passengers onboard.
    if (num_wait != 0 || elevator.load != 0)
    {

      int unload_pause = unload_elevator();
      int load_pause = -1;
      if (elevator.load != CAPACITY)
      {
        load_pause = load_elevator();
      }
      //wait for for 2 sec if loading
      if (unload_pause == 0 || load_pause == 0)
      {
        ssleep(LOAD_TIME);
      }
      //move only if there are still passengers need to be served
      if (num_wait != 0 || elevator.load != 0)
      {
        if (elevator.next_floor > elevator.current_floor)
        {
          elevator.state = UP;
        }
        else
        {
          elevator.state = DOWN;
        }
        ssleep(MOVE_TIME);
        move_elevator();
      }
      else if (elevator.deactiving)
      {
        elevator.state = OFFLINE;
        elevator.deactiving = 0;
      }
    }
    else if (started)
    {
      elevator.state = IDLE;
      ssleep(1);
    }
    else
    {
      elevator.state = OFFLINE;
      ssleep(1);
    }
  }
  return 0;
}

static int elevator_proc_open(struct inode *sp_inode, struct file *sp_file)
{
  printk(KERN_INFO "ELEVATOR_PROC_OPEN: CALLED \n");
  read_p = 1;
  message = kmalloc(sizeof(char *) * ENTRY_SIZE, __GFP_RECLAIM | __GFP_IO | __GFP_FS);

  if (message == NULL)
  {
    printk(KERN_WARNING "ELEVATOR_PROC_OPEN: Elevator open\n");
    return -ENOMEM;
  }

  //temp
  char *temp = kmalloc(sizeof(char) * 100, __GFP_RECLAIM | __GFP_IO | __GFP_FS);

  if (elevator.state == OFFLINE)
    sprintf(temp, "OFFLINE\n");
  else if (elevator.state == IDLE)
    sprintf(temp, "IDELE \n");
  else if (elevator.state == LOADING)
    sprintf(temp, "LOADING \n");
  else if (elevator.state == UP)
    sprintf(temp, "UP\n");
  else if (elevator.state == DOWN)
    sprintf(temp, "DOWN\n");

  sprintf(message, "Elevator state: ");
  strcat(message, temp);

  //status
  if (elevator.status == 0)
    sprintf(temp, "ELevator status: Uninfected\n");
  else
  {
    sprintf(temp, "ELevator status: Infected\n");
  }
  strcat(message, temp);

  //current floor
  sprintf(temp, "Current floor: %d\n", elevator.current_floor);
  strcat(message, temp);

  sprintf(temp, "Number of passengers: %d\n", elevator.load);
  strcat(message, temp);

  sprintf(temp, "Number of waiting: %d\n", num_wait);
  strcat(message, temp);

  sprintf(temp, "Number passengers serviced: %d\n\n", elevator.num_served);
  strcat(message, temp);
  //print passengers on each floor

  int i;
  for (i = NUM_FLOORS; i > 0; i--)
  {
    char indicator = ' ';
    if (elevator.current_floor == i)
    {
      indicator = '*';
    }
    if (i == NUM_FLOORS)
    {
      sprintf(temp, "[%c] Floor %d:  %d ", indicator, i, waiting[i - 1]);
    }
    else
    {
      sprintf(temp, "[%c] Floor  %d:  %d ", indicator, i, waiting[i - 1]);
    }
    Passenger *p;
    struct list_head *tempList;

    list_for_each(tempList, &floors[i - 1])
    {
      p = list_entry(tempList, Passenger, list);
      if (p->type == 0)
      {
        strcat(temp, " |");
      }
      else
      {
        strcat(temp, " X");
      }
    }
    strcat(temp, "\n");
    strcat(message, temp);
  }
  kfree(temp);
  return 0;
}

static ssize_t elevator_proc_read(struct file *sp_file, char __user *buf, size_t size, loff_t *offset)
{
  read_p = !read_p;
  if (read_p)
    return 0;

  int len = strlen(message);
  copy_to_user(buf, message, len);
  printk(KERN_INFO "COPY TO USER\n");
  return len;
}
//free message if proc is removed
int elevator_proc_release(struct inode *sp_inode, struct file *sp_file)
{
  printk(KERN_INFO "ELEVATOR_PROC_RELEASE: CALLED");
  kfree(message);
  printk(KERN_INFO "ELEVATOR_PROC_RELEASE: FREE MESSAGE\n");
  return 0;
}

static int elevator_init(void)
{
  printk(KERN_INFO "-----CALLED ELEVATOR_INIT %s-----\n", ENTRY_NAME);

  fops.open = elevator_proc_open;
  fops.read = elevator_proc_read;
  fops.release = elevator_proc_release;

  STUB_start_elevator = start_elevator;
  STUB_stop_elevator = stop_elevator;
  STUB_issue_request = issue_request;

  if (!proc_create(ENTRY_NAME, PERMS, NULL, &fops))
  {
    printk(KERN_WARNING "elevator_init\n");
    remove_proc_entry(ENTRY_NAME, NULL);
    return -ENOMEM;
  }

  //start thread
  thread_init_parameter(&thread1);
  if (IS_ERR(thread1.kthread))
  {
    printk(KERN_WARNING "error spawning thread");
    remove_proc_entry(ENTRY_NAME, NULL);
    return PTR_ERR(thread1.kthread);
  }

  elevator.state = IDLE;
  elevator.status = 0;
  elevator.current_floor = 0;
  elevator.next_floor = 0;
  elevator.load = 0;
  elevator.num_served = 0;
  elevator.deactiving = 0;

  INIT_LIST_HEAD(&elevator.onboard);
  int i;
  for (i = 0; i < NUM_FLOORS; i++)
  {
    INIT_LIST_HEAD(&floors[i]);
  }
  printk(KERN_INFO "ELEVATOR_INIT: FINISH");
  return 0;
}

module_init(elevator_init);

static void elevator_exit(void)
{
  //stop thread
  kthread_stop(thread1.kthread);
  //remove all passengers
  remove_passenger();
  //destroy mutex
  mutex_destroy(&thread1.mutex);
  //remove proce tnry
  remove_proc_entry(ENTRY_NAME, NULL);
  printk(KERN_NOTICE "REMOVING /proc/%s\n", ENTRY_NAME);

  STUB_start_elevator = NULL;
  STUB_stop_elevator = NULL;
  STUB_issue_request = NULL;
}

module_exit(elevator_exit);
