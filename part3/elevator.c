#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/linkage.h>

MODULE_LICENSE("GPL");

#define ENTRY_NAME "elevator"
#define ENTRY_SIZE 1024
#define PERMS 0644
#define DEFAULT_SLEEP_TIME 1
static struct file_operations fops;

// Elevator details
#define CAPACITY 10
#define MOVE_TIME 2
#define LOAD_TIME 1
#define FLOORS 10

//possible sates of the elevator
#define OFFLINE 0
#define IDLE 1
#define LOADING 2
#define UP 3
#define DOWN 4
#define EQUAL 5

//*****PROTOTYPE*****************
int remove_passenger(void);
int loadPassengers(void);
int unloadPassengers(void);
void moveElevator(void);
int scheduler(void *data);
int print_who_on_it(void);
int print_passengers(void);

// Struct for elevator
typedef struct elevator_status
{
    int state; // 0 = offline, 1 = idle, 2 = loading, 3 = up, 4 = down
    int current_floor;
    int next_floor;
    int loads;//num of passengers onboard
    int deactiving;
    int started;
    int status; //0 uninfected, 1 infected
    struct list_head onboard;
} Elevator;

// Struct passenger
typedef struct passenger_data
{
    int type;
    int start_floor;
    int destination_floor;
    struct list_head list;
} Passenger;

// Struct for  thread data
typedef struct thread_parameter
{
    int id;
    int cnt;
    struct task_struct *kthread;
    struct mutex mutex;
} Thread;

Elevator elevator;
Thread thread1;
static char *message;
static int read_p;

struct list_head waiting_list[FLOORS]; //list of passengers in each floor
static int serviced;                   //total number of serviced
static int num_waiting;                //total number of waiting passengers
static int waiting_on_floor[FLOORS];   //totoal number of waiting in each floor

/************************SYSTEM CALL**********************/
extern long (*STUB_start_elevator)(void);
extern long (*STUB_issue_request)(int, int, int);
extern long (*STUB_stop_elevator)(void);

/***********************************************************/

// System Call Start Elevator
long start_elevator(void)
{
    printk(KERN_INFO "STARTED ELEVATOR: CALLED\n");
    if (elevator.started)
        return 1;
    else
    {
        elevator.started = 1;
        elevator.deactiving = 0;
    }
    return 0;
}

// System Call Issue Request
long issue_request(int start_floor, int destination_floor, int type)
{
    // Error Checking
    printk(KERN_INFO "ISSUE REQUEST: CALLED\n");
    if (!elevator.started)
    {
        return 1;
    }
    if (type < 0 || type > 1)
    {
        return 1;
    }
    if (start_floor < 1 || start_floor > FLOORS)
    {
        return 1;
    }
    if (destination_floor < 1 || destination_floor > FLOORS)
    {
        return 1;
    }
    // Add passengers to waiting list
    if (mutex_lock_interruptible(&thread1.mutex) == 0)
    {
        //printk(KERN_INFO "ISSUE REQUEST: GAINED LOCK\n");
        if (!elevator.deactiving)
        {
            Passenger *p;
            p = kmalloc(sizeof(Passenger), __GFP_RECLAIM);
            p->type = type;
            p->start_floor = start_floor;
            p->destination_floor = destination_floor;
            list_add_tail(&p->list, &waiting_list[start_floor - 1]);
            //printk(KERN_INFO "ISSUE REQUEST: ADDED PASSENGER TO WAITING LIST\n");
            waiting_on_floor[p->start_floor - 1]++;
            num_waiting++;
        }
    }
    mutex_unlock(&thread1.mutex);
    //printk(KERN_INFO "ISSUE REQUEST: DONE REQUEST & RELEASE LOCK\n");
    //print_passengers();
    return 0;
}
//for debuging use
int print_passengers(void)
{
    Passenger *p;
    struct list_head *temp;
    int i;
    printk("PRINTING ALL FLOORS\n");
    for (i = 0; i < 10; i++)
    {

        //iterate through nodes
        printk("floor # is = %d\n", i + 1);

        list_for_each(temp, &waiting_list[i])
        { //iterate through nodes of list

            p = list_entry(temp, Passenger, list); //list_entry gets you the struct the list is in.
            printk("type: %d, start: %d, destination: %d\n", p->type, p->start_floor, p->destination_floor);
        }
    }
    return 0;
}

// System Call Stop Elevator
long stop_elevator(void)
{
    printk(KERN_INFO "STOP ELEVATOR: CALLED");
    struct list_head *temp;
    struct list_head *dummy;
    struct list_head delete_list;
    Passenger *p;
    int i;
    INIT_LIST_HEAD(&delete_list);
    if (!elevator.started)
    {
        return 1;
    }
    if (elevator.deactiving)
    {
        return 1;
    }
    elevator.deactiving = 1;
    elevator.started = 0;
    if (mutex_lock_interruptible(&thread1.mutex) == 0)
    {
        //printk(KERN_INFO "STOP ELEVATOR: OFFLOAD PASSENGERS BEFORE DEACTIVING\n");
        list_for_each_safe(temp, dummy, &elevator.onboard)
        {
            list_move_tail(temp, &delete_list);
        }
    }
    mutex_unlock(&thread1.mutex);
    list_for_each_safe(temp, dummy, &delete_list)
    {
        p = list_entry(temp, Passenger, list);
        list_del(temp);
        kfree(p);
    }
    //printk(KERN_INFO "STOP ELEVATOR: DONE STOP ELEVATOR & RELEASE LOCK\n");
    return 0;
}

//unload passengers
int unloadPassengers(void)
{
    printk(KERN_INFO "UNLOAD_ELEVATOR: CALLED\n");
    //printk("CURRENT FLOOR IS %d\n", elevator.current_floor);

    struct list_head *temp;
    struct list_head *dummy;
    struct list_head delete_list;
    Passenger *p;
    int unloaded = 0;

    //Initialize delete list
    INIT_LIST_HEAD(&delete_list);

    //unload passengers
    if (mutex_lock_interruptible(&thread1.mutex) == 0)
    {
        list_for_each_safe(temp, dummy, &elevator.onboard)
        {
            p = list_entry(temp, Passenger, list);
            if (p->destination_floor == elevator.current_floor)
            {
                //printk("UNLOAD PASSENGER p.type %d p.start %d p.dest %d\n", p->type, p->start_floor, p->destination_floor);
                list_move_tail(temp, &delete_list);
                elevator.loads--;
                serviced++;
                unloaded = 1;
            }
        }
    }
    mutex_unlock(&thread1.mutex);

    // remove passengers from delete list
    list_for_each_safe(temp, dummy, &delete_list)
    {
        p = list_entry(temp, Passenger, list);
        list_del(temp);
        kfree(p);
    }
    //printk("LOADS AFTER UNLOAD %d\n", elevator.loads);
    if(elevator.loads == 0)//if no passenger, set status back to uninfected
    {
        elevator.status = 0;
    }
    return unloaded;
}

//load passengers
int loadPassengers(void)
{
    printk(KERN_INFO "LOAD_ELEVATOR: CALLED\n");
    //printk("CURRENT FLOOR IS %d\n", elevator.current_floor);
    struct list_head *temp;
    struct list_head *dummy;
    Passenger *p;
    int loaded = 0;
    int floor = elevator.current_floor;

    if (mutex_lock_interruptible(&thread1.mutex) == 0)
    {
        list_for_each_safe(temp, dummy, &waiting_list[floor - 1])
        {
            //printk(KERN_INFO "LOAD ELEVATOR: GAINED LOCK\n");
            p = list_entry(temp, Passenger, list);
            if (p->start_floor == floor)
            {
                if (elevator.loads + 1 <= CAPACITY)
                {
                    if (p->type == 1)
                    {
                        //printk(KERN_INFO "LOAD_ELEVATOR: LOADING A ZOMBIE\n");
                        list_move_tail(temp, &elevator.onboard);
                        elevator.status = 1;
                        elevator.loads++;
                        waiting_on_floor[p->start_floor - 1]--;
                        num_waiting--;
                        loaded = 1;
                    }
                    if (p->type == 0 & elevator.status == 0)
                    {
                        //printk(KERN_INFO "LOAD_ELEVATOR: LOADING A HUMAN\n");
                        list_move_tail(temp, &elevator.onboard);
                        elevator.loads++;
                        waiting_on_floor[p->start_floor - 1]--;
                        num_waiting--;
                        loaded = 1;
                    }
                }
            }
        }
    }
    //print_who_on_it();
    mutex_unlock(&thread1.mutex);
    //printk("LOADS AFTER LOADING IS %d\n", elevator.loads);
    //printk(KERN_INFO "LOAD_ELEVATOR: DONE LOADING AND RELEASE LOCK\n");
    return loaded;
}
//for degbuing use
int print_who_on_it(void)
{

    struct list_head *temp;
    struct list_head *dummy;
    Passenger *p;

    printk(KERN_INFO "printing people on elevator\n");

    list_for_each_safe(temp, dummy, &elevator.onboard)
    {
        p = list_entry(temp, Passenger, list);

        printk("type: %d, start: %d, destination: %d\n", p->type, p->start_floor, p->destination_floor);
    }
    return 0;
}

//change elevator movement state
void changeElevatorState(void)
{
    if (elevator.next_floor > elevator.current_floor)
        elevator.state = UP;
    else
        elevator.state = DOWN;
}

//move elevator
void moveElevator(void)
{
    printk(KERN_INFO "MOVE_ELEVATOR: CALLED\n");
    if (elevator.state == UP)
    {
        //printk(KERN_INFO "MOVE_ELEVATOR: MOVING UP\n");
        elevator.current_floor++;
        if (elevator.current_floor == FLOORS)
            elevator.next_floor = elevator.current_floor - 1;
        else
            elevator.next_floor = elevator.current_floor + 1;
    }
    if (elevator.state == DOWN)
    {
        //printk(KERN_INFO "MOVE_ELEVATOR: MOVING DOWN\n");
        elevator.current_floor--;
        if (elevator.current_floor == 1)
            elevator.next_floor = elevator.current_floor + 1;
        else
            elevator.next_floor = elevator.current_floor - 1;
    }
}

//thread run in the background
int scheduler(void *data)
{
    while (!kthread_should_stop())
    {
        printk(KERN_INFO "SCHEDULER: CALLED\n");
        // unload/load passenger and move elevator
        if (num_waiting != 0 || !list_empty(&elevator.onboard))
        {
            // indicator for loading sleep
            int unload_sleep = 0;
            int load_sleep = 0;

            // unload and load passengers
            unload_sleep = unloadPassengers();
            if (elevator.loads < CAPACITY)
            {
                load_sleep = loadPassengers();
            }
            // sleep for loading 
            if (unload_sleep || load_sleep)
            {
                elevator.state = LOADING;
                ssleep(LOAD_TIME);
            }

            //Move elevator if there are passengers
            if (elevator.loads != 0 || num_waiting != 0)
            {
                changeElevatorState();
                ssleep(MOVE_TIME);
                moveElevator();
            }
            else if (elevator.deactiving)
            {//if deactiving, change state to offline
                elevator.state = 0;
                elevator.deactiving = 0;
            }
        }
        else if (elevator.started) //change state to idle if no more passengers
        {
            elevator.state = IDLE;
            ssleep(DEFAULT_SLEEP_TIME); // prevents busy waiting
        }
        else //change to offline if elevator is not started and no more passegers
        {
            elevator.state = OFFLINE;
            ssleep(DEFAULT_SLEEP_TIME); // prevents busy waiting
        }
    }
    return 0;
}

//set up thread
void thread_init_parameter(struct thread_parameter *parm)
{
    static int id = 1;
    parm->id = id++;
    parm->cnt = 0;
    mutex_init(&parm->mutex);
    parm->kthread = kthread_run(scheduler, parm, "thread  %d", parm->id);
}

//open proc and prepare messeage
int elevator_proc_open(struct inode *sp_inode, struct file *sp_file)
{
    printk(KERN_INFO "ELEVATOR_PROC_OPEN: CALLED \n");
    //temp string
    char *temp = kmalloc(sizeof(char) * 100, __GFP_RECLAIM | __GFP_IO | __GFP_FS);

    read_p = 1;
    message = kmalloc(sizeof(char) * ENTRY_SIZE, __GFP_RECLAIM | __GFP_IO | __GFP_FS);
    if (message == NULL)
    {
        printk(KERN_WARNING "ELEVATOR_PROC_open");
        return -ENOMEM;
    }

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
    {
        sprintf(temp, "ELevator status: Uninfected\n");
    }
    else
    {
        sprintf(temp, "ELevator status: Infected\n");
    }
    strcat(message, temp);

    //current floor
    sprintf(temp, "Current floor: %d\n", elevator.current_floor);
    strcat(message, temp);

    sprintf(temp, "Number of passengers: %d\n", elevator.loads);
    strcat(message, temp);

    sprintf(temp, "Number of waiting: %d\n", num_waiting);
    strcat(message, temp);

    sprintf(temp, "Number passengers serviced: %d\n\n", serviced);
    strcat(message, temp);
    //print passengers on each floor

    int i;
    for (i = FLOORS; i > 0; i--)
    {
        char indicator = ' ';
        if (elevator.current_floor == i)
        {
            indicator = '*';
        }
        if (i == FLOORS)
        {
            sprintf(temp, "[%c] Floor %d:  %d ", indicator, i, waiting_on_floor[i - 1]);
        }
        else
        {
            sprintf(temp, "[%c] Floor  %d:  %d ", indicator, i, waiting_on_floor[i - 1]);
        }
        Passenger *p;
        struct list_head *tempList;

        list_for_each(tempList, &waiting_list[i - 1])
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

//send data to user space
ssize_t elevator_proc_read(struct file *sp_file, char __user *buf, size_t size, loff_t *offset)
{
    int len = strlen(message);
    read_p = !read_p;
    if (read_p)
    {
        return 0;
    }
    copy_to_user(buf, message, len);
    return len;
}

//free memory allocated for message
int elevator_proc_release(struct inode *sp_inode, struct file *sp_file)
{
    printk(KERN_INFO "ELEVATOR_PROC_RELEASE: CALLED");
    kfree(message);
    return 0;
}

//set up elevator for the initilization
void setup_elevator(void)
{
    int i;
    elevator.state = 0;
    elevator.current_floor = 1;
    elevator.next_floor = 2;
    elevator.loads = 0;
    elevator.deactiving = 0;
    elevator.started = 0;
    INIT_LIST_HEAD(&elevator.onboard);
    serviced = 0;
    //initialize passenger list in each floor
    for (i = 0; i < FLOORS; i++)
    {
        INIT_LIST_HEAD(&waiting_list[i]);
        waiting_on_floor[i] = 0;
    }
}

//initilize proc
static int elevator_init(void)
{
    printk(KERN_INFO "-----CALLED ELEVATOR_INIT %s-----\n", ENTRY_NAME);
    setup_elevator();

    // setup system calls
    STUB_start_elevator = start_elevator;
    STUB_issue_request = issue_request;
    STUB_stop_elevator = stop_elevator;

    // Set up proc file
    fops.open = elevator_proc_open;
    fops.read = elevator_proc_read;
    fops.release = elevator_proc_release;
    if (!proc_create(ENTRY_NAME, PERMS, NULL, &fops))
    {
        printk(KERN_WARNING "thread_init");
        remove_proc_entry(ENTRY_NAME, NULL);
        return -ENOMEM;
    }

    // start thread
    thread_init_parameter(&thread1);
    if (IS_ERR(thread1.kthread))
    {
        printk(KERN_WARNING "error spawning thread");
        remove_proc_entry(ENTRY_NAME, NULL);
        return PTR_ERR(thread1.kthread);
    }
    return 0;
}
module_init(elevator_init);

//remove all passengers
void removeAllPassengers(void)
{
    struct list_head *temp;
    struct list_head *dummy;
    struct list_head delete_list;
    Passenger *p;
    // Initalize Delete List
    INIT_LIST_HEAD(&delete_list);
    int i;
    // Add all passengers to delete list
    if (mutex_lock_interruptible(&thread1.mutex) == 0)
    {
        list_for_each_safe(temp, dummy, &elevator.onboard)
        {
            list_move_tail(temp, &delete_list);
        }
        for (i = 0; i < FLOORS; i++)
        {
            list_for_each_safe(temp, dummy, &waiting_list[i])
            {
                list_move_tail(temp, &delete_list);
            }
        }
    }
    mutex_unlock(&thread1.mutex);
    // Delete from delete list
    list_for_each_safe(temp, dummy, &delete_list)
    {
        p = list_entry(temp, Passenger, list);
        list_del(temp);
        kfree(p);
    }
}

//stop thread and remove proc
static void elevator_exit(void)
{
    STUB_start_elevator = NULL;
    STUB_issue_request = NULL;
    STUB_stop_elevator = NULL;
    // stop thread
    kthread_stop(thread1.kthread);
    // remove all pasengers
    removeAllPassengers();
    // destroy mutex
    mutex_destroy(&thread1.mutex);
    // remove proc entry
    remove_proc_entry(ENTRY_NAME, NULL);
}

module_exit(elevator_exit);
