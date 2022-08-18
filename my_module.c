#include<linux/kernel.h>
#include<linux/init.h>
#include<linux/module.h>
#include<linux/kdev_t.h>
#include<linux/fs.h>
#include<linux/cdev.h>
#include<linux/device.h>
#include<linux/slab.h>
#include<linux/uaccess.h>

/* TODO 1: define the size of the dynamically 
*  allocated memory region as a macro here and
*  declare the global pointer variable that will 
*  point to a dynamically allocated memory block
*/

#define SIZE 200

void* my_pointer;

dev_t dev = 0;

static struct class *dev_class;
static struct cdev my_cdev;

int no_of_bytes;

static int __init my_driver_init(void);
static void __exit my_driver_exit(void);
static int my_open(struct inode *inode, struct file *file);
static int my_release(struct inode *inode, struct file *file);
static ssize_t my_read(struct file *filp, char __user *buf, size_t len, loff_t *off);
static ssize_t my_write(struct file *filp, const char *buf, size_t len, loff_t *off);

static struct file_operations fops = 
{
	.owner	= THIS_MODULE,
	.read 	= my_read,
	.write	= my_write,
	.open	= my_open,
	.release = my_release, 
};

static int my_open(struct inode *inode, struct file * file)
{	
/* TODO 2: dynamically allocate a single block 
*  of memory region here
*/
	printk(KERN_INFO "my_open function is called");

	my_pointer = kmalloc(SIZE,GFP_KERNEL);
	if(my_pointer != NULL){
		printk(KERN_INFO "Memory Allocated Succesfully. \n");
	}
	return 0;
}

static int my_release(struct inode *inode, struct file *file)
{
/* TODO 3: free the dynamically allocated memory region here 
*/
	printk(KERN_INFO "my_release is called.");
	kfree(my_pointer);

	printk(KERN_INFO "Memory has been freed.");
	return 0;
}

static ssize_t my_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
/* TODO 4: make the user application read the content of
*  the dynamically allocated memory region here
*/

	printk(KERN_INFO "my_read is called");

	//strncpy_from_user(buf, my_pointer, len);
	return len;
}

static ssize_t my_write(struct file *filp,const char __user *buf, size_t len, loff_t* off)
{
/* TODO 5: make the user application write to
*  the dynamically allocated memory region here
*/
	printk(KERN_INFO "my_write is called");


	copy_from_user(my_pointer, buf, len);
	
	printk("This is written %s",(char *)my_pointer);
	return len;
}

static int __init my_driver_init(void)
{
	/* Allocating Major number dynamically*/
	/* &dev is address of the device */
	/* <0 to check if major number is created */
	if((alloc_chrdev_region(&dev, 0, 1, "my_Dev")) < 0) {
		printk(KERN_INFO"Cannot allocate the major number...\n");
	}

	printk(KERN_INFO"Major = %d Minor =  %d..\n", MAJOR(dev),MINOR(dev));

	/* creating cdev structure*/
	cdev_init(&my_cdev, &fops);

	/* adding character device to the system */
	if((cdev_add(&my_cdev, dev, 1)) < 0) {
		printk(KERN_INFO "Cannot add the device to the system...\n");
		goto r_class;
	}	 

	/* creating struct class */
	if((dev_class =  class_create(THIS_MODULE, "my_class")) == NULL) {
		printk(KERN_INFO " cannot create the struct class...\n");
		goto r_class;
	}

	/* creating device */

	if((device_create(dev_class, NULL, dev, NULL, "my_device")) == NULL) {
		printk(KERN_INFO " cannot create the device ..\n");
		goto r_device;
	}

	printk(KERN_INFO"Device driver insert...done properly...");
	return 0;

r_device: 
	class_destroy(dev_class);

r_class:
	unregister_chrdev_region(dev, 1);
	return -1;
}

void __exit my_driver_exit(void) {
	device_destroy(dev_class, dev);
	class_destroy(dev_class);
	cdev_del(&my_cdev);
	unregister_chrdev_region(dev, 1);
	printk(KERN_INFO "Device driver is removed successfully...\n");
}

module_init(my_driver_init);
module_exit(my_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Aditya");
MODULE_DESCRIPTION("The character device driver");



