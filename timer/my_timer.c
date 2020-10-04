#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <linux/string.h>

MODULE_LICENSE("Dual BSD/GPL");

#define BUF_LEN 100

static struct proc_dir_entry* proc_entry;

static char msg[BUF_LEN];
static char procfs_buf_len;
static struct timespec t0; //previous time
static struct timespec t1; //current time

//struct timeval diff = {a.tv_sec-b.tv_sec, a.tv_usec-b.tv_usec};
static ssize_t procfile_read(struct file* file, char * ubuf, size_t count, loff_t *ppos)
{
  printk(KERN_INFO "proc_read\n");	
  char current_t [100]= "Current time: ";

  printk(KERN_INFO "Prompt %s\n", current_t);
  t1 = current_kernel_time();

  char time[21];
  sprintf(time, "%lld", (long long) t1.tv_sec);

  //append time   
  strcat(current_t, time);
  procfs_buf_len = strlen(current_t);
  if (*ppos > 0 || count < procfs_buf_len)
    return 0;
  if (copy_to_user(ubuf, current_t, procfs_buf_len))
    return -EFAULT;

  *ppos = procfs_buf_len;
  printk(KERN_INFO "time is  %s\n", time);
  printk(KERN_INFO "gave to user %s\n", current_t);
  
  return procfs_buf_len;
}


static ssize_t procfile_write(struct file* file, const char * ubuf, size_t count, loff_t* ppos)
{

/*  printk(KERN_INFO "proc_write\n");

  if (count > BUF_LEN)
    procfs_buf_len = BUF_LEN;
  else
    procfs_buf_len = count;

    copy_from_user(msg, ubuf, procfs_buf_len);

    printk(KERN_INFO "got from user: %s\n", msg);

    return procfs_buf_len;
*/
}

static struct file_operations procfile_fops = {
  .owner = THIS_MODULE,
  .read = procfile_read,
  .write = procfile_write,
};


static int timer_init(void)
{
  proc_entry = proc_create("timer", 0666, NULL, &procfile_fops);
  if(proc_entry == NULL)
    return -ENOMEM;
  return 0;
}


static void timer_exit(void)
{
  proc_remove(proc_entry);
  return;
}

module_init(timer_init);
module_exit(timer_exit);
