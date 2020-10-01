#include <linux/init.h>
#include <linux/module.h>
#include <asm/semaphore.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/kernal.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

MODULE_LICENSE("Dual BSD/GPL");

#define BUF_LEN 100     //max length of read/write message

static struct proc_dir_entry* proc_entry;//pointer to proc entry

static char msg[BUF_LEN];//buffer to store read/write message
static int procfs_buf_len;//variable to hold length of message

/*************************READ****************************/
static ssize_t procfile_read(struct file* file, char * ubuf, size_t count, loff_t *ppos)
{
	printk(KERN_INFO "proc_read\n");
	procfs_buf_len = strlen(msg);
	if (*ppos > 0 || count < procfs_buf_len)    //check if data already read and if space in user buffer
		return 0;
	if (copy_to_user(ubuf, msg, procfs_buf_len))    //send data to user buffer
		return -EFAULT;
	*ppos = procfs_buf_len;//update position
	printk(KERN_INFO "gave to user %s\n", msg);
	return procfs_buf_len;     //return number of characters read
}

/*************************WRITE****************************/
static ssize_t procfile_write(struct file* file, const char * ubuf, size_t count, loff_t* ppos)
{
	printk(KERN_INFO "proc_write\n");
	//write min(user message size, buffer length) characters
	if (count > BUF_LEN)
		procfs_buf_len = BUF_LEN;
	else
		procfs_buf_len = count;

	copy_from_user(msg, ubuf, procfs_buf_len);
	printk(KERN_INFO "got from user: %s\n", msg);
	return procfs_buf_len;
}

/************************MEMORY COPING***********************/
unsigned long copy_to_user (void __user *to, const void *from, unsigned long size);
unsigned long copy_from_user (void *to, const void __user* from, unsigned long size);

/**********************INIT********************************/
static struct file_operations procfile_fops = 
{
	.owner = THIS_MODULE,
	.read = procfile_read,             //fill in callbacks to read/write functions
	.write = procfile_write,
};
static int hello_init(void)
{
	//proc_create(filename, permissions, parent, pointer to fops)
	proc_entry = proc_create("hello", 0666, NULL, &procfile_fops);
	if (proc_entry == NULL)
		return -ENOMEM;
	return 0;
}

/*************************EXIT*************************/
static void hello_exit(void)
{
	proc_remove(proc_entry);
	return;
}
