/* Prototype module for third mandatory DM510 assignment */
#ifndef __KERNEL__
#  define __KERNEL__
#endif
#ifndef MODULE
#  define MODULE
#endif

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <asm/uaccess.h>
#include <linux/semaphore.h>
#include <linux/cdev.h>
/* #include <asm/system.h> */
#include <asm/switch_to.h>
#include <linux/wait.h>

#include <linux/sched.h>


#define DM510_IOC_MAGIC 'p'
#define DM510_IOC_MAXNO 2
#define DM510_IOCBUFSIZE _IO(DM510_IOC_MAGIC,  1)
#define DM510_IOCNOREADERS _IO(DM510_IOC_MAGIC,  2)

#define init_MUTEX(LOCKNAME) sema_init(LOCKNAME,1) // From scull_pipe

/* Prototypes - this would normally go in a .h file */
static int dm510_open( struct inode*, struct file* );
static int dm510_release( struct inode*, struct file* );
static ssize_t dm510_read( struct file*, char*, size_t, loff_t* );
static ssize_t dm510_write( struct file*, const char*, size_t, loff_t* );
long dm510_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

#define DEVICE_NAME "dm510_dev" /* Dev name as it appears in /proc/devices */
#define MAJOR_NUMBER 254
#define MIN_MINOR_NUMBER 0
#define MAX_MINOR_NUMBER 1

#define DEVICE_COUNT 2
/* end of what really should have been in a .h file */

struct dm510_dev {
	struct dm510_buffer* read_buffer;
	struct dm510_buffer* write_buffer;
	int size;
	struct cdev cdev;
};

struct dm510_buffer {
	char *buffer;
	int index;
	int size;
	struct semaphore sem;
	wait_queue_head_t read_wait_queue;
	wait_queue_head_t write_wait_queue;
	int no_readers;
	int no_writers;
};

struct dm510_buffer* buffer1;
struct dm510_buffer* buffer2;

struct dm510_dev *devs;

int BUFFER_SIZE = 100;
int NO_READERS = 10;
int NO_WRITERS = 1;

/* file operations struct */
static struct file_operations dm510_fops = {
	.owner   = THIS_MODULE,
	.read    = dm510_read,
	.write   = dm510_write,
	.open    = dm510_open,
	.release = dm510_release,
    .unlocked_ioctl   = dm510_ioctl
};

void free_buffers(void)
{
	kfree(buffer1->buffer);
	kfree(buffer2->buffer);
}

void setup_buffers(void)
{
	buffer1 = (struct dm510_buffer *) kmalloc(sizeof(struct dm510_buffer), GFP_KERNEL);
	buffer1->buffer = (char *) kmalloc(sizeof(char) * BUFFER_SIZE, GFP_KERNEL);
	buffer1->size = BUFFER_SIZE;
	buffer1->index = 0;
	init_MUTEX(&buffer1->sem);
	init_waitqueue_head(&buffer1->read_wait_queue);
	init_waitqueue_head(&buffer1->write_wait_queue);

	buffer2 = (struct dm510_buffer *) kmalloc(sizeof(struct dm510_buffer), GFP_KERNEL);
	buffer2->buffer = (char *) kmalloc(sizeof(char) * BUFFER_SIZE, GFP_KERNEL);
	buffer2->size = BUFFER_SIZE;
	buffer2->index = 0;
	init_MUTEX(&buffer2->sem);
	init_waitqueue_head(&buffer2->read_wait_queue);
	init_waitqueue_head(&buffer2->write_wait_queue);
}

/* called when module is loaded */
int dm510_init_module( void ) {

	setup_buffers();
	/* initialization code belongs here */

	devs = (struct dm510_dev*)kmalloc(2 * sizeof(struct dm510_dev), GFP_KERNEL);

	/* kmalloc buffers */

	int devno = MKDEV(MAJOR_NUMBER, MIN_MINOR_NUMBER);
	//printk("devno: %d\n", devno);

	printk("%d\n", register_chrdev_region(devno, 1, "dm510-0"));


	devs[0].read_buffer = buffer1;
	devs[0].write_buffer = buffer2;

	cdev_init(&devs[0].cdev, &dm510_fops);
	devs[0].cdev.owner = THIS_MODULE;
	devs[0].cdev.ops = &dm510_fops;
	cdev_add(&devs[0].cdev, devno, 1); //TODO: error checking

	devno = MKDEV(MAJOR_NUMBER, MAX_MINOR_NUMBER);
	//printk("devno: %d\n", devno);

	printk("%d\n", register_chrdev_region(devno, 1, "dm510-1")); //TODO: is this registerd right??? check scull

	devs[1].read_buffer = buffer2;
	devs[1].write_buffer = buffer1;

	cdev_init(&devs[1].cdev, &dm510_fops);
	devs[1].cdev.owner = THIS_MODULE;
	devs[1].cdev.ops = &dm510_fops;
	cdev_add(&devs[1].cdev, devno, 1); //TODO: error checking

	printk(KERN_ALERT "DM510: Hello from your device!\n");
	//printk(KERN_ALERT "dev0 %p, cdev %p\n", &devs[0], &devs[0].cdev);
	return 0;
}

/* Called when module is unloaded */
void dm510_cleanup_module( void ) {

	/* kfree buffers */
	free_buffers();

	kfree(buffer1);
	kfree(buffer2);

	unregister_chrdev_region(devs[0].cdev.dev, 1);
	cdev_del(&devs[0].cdev);

	unregister_chrdev_region(devs[1].cdev.dev, 1);
	cdev_del(&devs[1].cdev);

	/* clean up code belongs here */

	//void unregister_chrdev_region(dev_t first, unsigned int count);

	printk(KERN_ALERT "DM510: Module unloaded.\n");
}

// is leaving must be called with 1 or -1 TODO: fix this function or don't use it (in the case of decrementing)
int set_readers_and_writers(struct inode *inode, struct file *filp, int is_leaving)
{
	struct dm510_dev *dev;
	dev = container_of(inode->i_cdev, struct dm510_dev, cdev);
	filp->private_data = dev;

	if (filp->f_mode & O_RDWR)
	{
		printk(KERN_ALERT "Opened for both reading and writing\n");
		if (down_interruptible(&dev->read_buffer->sem))
		{
			return -ERESTARTSYS;
		}

		if (down_interruptible(&dev->write_buffer->sem))
		{
			up(&dev->read_buffer->sem);
			return -ERESTARTSYS;
		}

		if (dev->read_buffer->no_readers < NO_READERS && dev->write_buffer->no_writers < NO_WRITERS)
		{
			dev->read_buffer->no_readers += is_leaving;
			dev->write_buffer->no_writers += is_leaving;
		}

		up(&dev->read_buffer->sem);
		up(&dev->write_buffer->sem);


	}
	else if (filp->f_mode & FMODE_READ)
	{
		printk(KERN_ALERT "Opened for reading\n");
		if (down_interruptible(&dev->read_buffer->sem))
		{
			return -ERESTARTSYS;
		}

		if (dev->read_buffer->no_readers < NO_READERS)
		{
			dev->read_buffer->no_readers += is_leaving;
			up(&dev->read_buffer->sem);
		}
		else
		{
			up(&dev->read_buffer->sem);
			// TODO: check errorcode
			return -1;
		}
	}
	else if (filp->f_mode & FMODE_WRITE)
	{
		printk(KERN_ALERT "Opened for writing\n");
		if (down_interruptible(&dev->write_buffer->sem))
		{
			return -ERESTARTSYS;
		}
		if (dev->write_buffer->no_writers < NO_WRITERS)
		{
			dev->write_buffer->no_writers += is_leaving;
			up(&dev->write_buffer->sem);
		}
		else
		{
			up(&dev->write_buffer->sem);
			// TODO: check errorcode
			return -1;
		}
	}

	return 0;
}

/* Called when a process tries to open the device file */
static int dm510_open( struct inode *inode, struct file *filp )
{
	return set_readers_and_writers(inode, filp, 1);
}


/* Called when a process closes the device file. */
static int dm510_release( struct inode *inode, struct file *filp )
{
	return set_readers_and_writers(inode, filp, -1);
}

/* Called when a process, which already opened the dev file, attempts to read from it. */
static ssize_t dm510_read( struct file *filp,
    char *buf,      /* The buffer to fill with data     */
    size_t count,   /* The max number of bytes to read  */
    loff_t *f_pos )  /* The offset in the file           */
{
	if (count < 1)
	{
		return -1; //TODO
	}

	struct dm510_dev *dev = filp->private_data;

	if (down_interruptible(&dev->read_buffer->sem))
	{
		return -ERESTARTSYS;
	}

	while (dev->read_buffer->index == 0) // Buffer is empty
	{
		up(&dev->read_buffer->sem); // Release the lock
		if (filp->f_flags & O_NONBLOCK)
		{
			return -EAGAIN;
		}

		if (wait_event_interruptible(dev->read_buffer->read_wait_queue, (dev->read_buffer->index > 0)))
		{
			printk(KERN_ALERT "Bad stuff\n");
			return -ERESTARTSYS; // Restart the system call
		}

		if (down_interruptible(&dev->read_buffer->sem))
		{
			return -ERESTARTSYS;
		}
	}

	int max_read = min(count, dev->read_buffer->index);

	char *new_buffer = kmalloc(sizeof(char) * dev->read_buffer->size, GFP_KERNEL);
	memcpy(new_buffer, dev->read_buffer->buffer + max_read, dev->read_buffer->size - max_read);

	char *old_buffer = dev->read_buffer->buffer;
	dev->read_buffer->buffer = new_buffer;
	dev->read_buffer->index -= max_read;

	up(&dev->read_buffer->sem);
	wake_up_interruptible(&dev->read_buffer->write_wait_queue);

	//TODO error checking

	int copy_res = copy_to_user(buf, old_buffer, max_read);

	kfree(old_buffer);

	return max_read - copy_res; //return number of bytes read
}


/* Called when a process writes to dev file */
static ssize_t dm510_write( struct file *filp,
    const char *buf,/* The buffer to get data from      */
    size_t count,   /* The max number of bytes to write */
    loff_t *f_pos )  /* The offset in the file           */
{

	struct dm510_dev *dev = filp->private_data;

	if (down_interruptible(&dev->write_buffer->sem))
	{
		return -ERESTARTSYS;
	}

	// if non-blocking and buffer is full then leave
	while (dev->write_buffer->index == dev->write_buffer->size)
	{
		up(&dev->write_buffer->sem); // Release the lock

		if (filp->f_flags & O_NONBLOCK)
		{
			printk(KERN_ALERT "Released the lock due to non-block\n");
			return -EAGAIN;
		}

		if (wait_event_interruptible(dev->write_buffer->write_wait_queue, (dev->write_buffer->index < dev->write_buffer->size)))
		{
			printk(KERN_ALERT "Got interupted \n");
			return -ERESTARTSYS; // Restart the system call TODO sure?
		}

		if (down_interruptible(&dev->write_buffer->sem))
		{
			return -ERESTARTSYS;
		}
	}

	int max_write = min(count, dev->write_buffer->size - dev->write_buffer->index);

	int copy_res = copy_from_user(dev->write_buffer->buffer + dev->write_buffer->index, buf, max_write);


	// copy_res is the number of chars not copied to user
	dev->write_buffer->index += max_write - copy_res;

	up(&dev->write_buffer->sem); // Release the lock
	wake_up_interruptible(&dev->write_buffer->read_wait_queue);

	return max_write - copy_res; //return number of bytes written
}

/* called by system call icotl */
long dm510_ioctl(
    struct file *filp,
    unsigned int cmd,   /* command passed from the user */
    unsigned long arg ) /* argument of the command */
{

	if (_IOC_TYPE(cmd) != DM510_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > DM510_IOC_MAXNO) return -ENOTTY;

	switch(cmd)
	{
		case DM510_IOCBUFSIZE:
			BUFFER_SIZE = arg;
			free_buffers();
			setup_buffers();
			break;
		case DM510_IOCNOREADERS:
			NO_READERS = arg;
			break;
		default:
	  		return -ENOTTY;
	}

	return 0;
}

module_init( dm510_init_module );
module_exit( dm510_cleanup_module );

MODULE_AUTHOR( "Kristoffer Abell, Steffen Klenow, Mikkel Pilegaard" );
MODULE_LICENSE( "GPL" );
