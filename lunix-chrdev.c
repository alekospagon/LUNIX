/*
 * lunix-chrdev.c
 *
 * Implementation of character devices
 * for Lunix:TNG
 *
 * MY NAME HERE
 *
 */

#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mmzone.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>

#include "lunix.h"
#include "lunix-chrdev.h"
#include "lunix-lookup.h"

/*
 * Global data
 */
struct cdev lunix_chrdev_cdev;






/*
 * Just a quick [unlocked] check to see if the cached
 * chrdev state needs to be updated from sensor measurements.
 */
static int lunix_chrdev_state_needs_refresh(struct lunix_chrdev_state_struct *state)
{
	struct lunix_sensor_struct *sensor;
	
	debug("Lets refresh\n ");
	WARN_ON ( !(sensor = state->sensor));

	//Compare timestamp of this state with the actual raw_data instant
	if( state->buf_timestamp < sensor->msr_data[state->type]->last_update)
		return 1;
	else
		return 0;
}






/*
 * Updates the cached state of a character device
 * based on sensor data. Must be called with the
 * character device state lock held.
 */
static int lunix_chrdev_state_update(struct lunix_chrdev_state_struct *state)
{
	/* =============== Declare your parameters =============== */
	
	unsigned long flags;
	int need_refresh, data_size;
	long int data;
	uint32_t raw_data, raw_time;
	char sign;
	
	debug("Im gonna update dude \n");

	//Do i need refresh?
	need_refresh = lunix_chrdev_state_needs_refresh( state ); 
	
	/* =============== Check for new data and take them =============== */
	if(need_refresh) {

		// =========== LOCK ===========
		debug("Getting spinlock \n");
		spin_lock_irqsave(&(state->sensor->lock), flags);	//lock state->sensor
		debug("Spinlocked \n");



		// ===== CRITICAL SECTION =====		
		//Getting info fast.
		raw_data = state->sensor->msr_data[state->type]->values[0];
		raw_time = state->sensor->msr_data[state->type]->last_update;
		// ===== CRITICAL SECTION =====


		// ========== UNLOCK ==========
		spin_unlock_irqrestore(&(state->sensor->lock), flags); //unlock now

	}
	else {	
		//Nothing new. Go away
		debug("Again!\n");
		return -EAGAIN; 
	}

	/* =============== Upoloipo tmhma kwdika =============== */

	/* =============== Take the data =============== */
	switch(state->type) {
		case BATT: 
			data = lookup_voltage[raw_data]; 
			break;

		case TEMP: 
			data = lookup_temperature[raw_data]; 
			break;

		case LIGHT: 
			data = lookup_light[raw_data]; 
			break;

	}

	/* =============== Write data to state->buf_data =============== */
	
	// 1) Take data in correct form
	sign = (data >= 0) ? '+' : '-';
	data = (data > 0) ? data : (-data);

	// 2) Write data to state
	data_size = sprintf(state->buf_data, "%c%ld.%03ld\n", sign, data/1000, data%1000 );

	// 3) Keep track of time and take data size
	state->buf_timestamp = raw_time;
	state->buf_lim = data_size;

	debug("leaving update\n");
	return 0;
}

/*************************************
 * Implementation of file operations
 * for the Lunix character device
 *************************************/





static int lunix_chrdev_open(struct inode *inode, struct file *filp)
{
	/* =============== Declarations =============== */
	
	//Sensor and its state. 
	struct lunix_sensor_struct 			*sensor;	//Sensor
	struct lunix_chrdev_state_struct 	*state;		//State of sensor

	//Return value
	int ret;

	//Device number and type
	unsigned int minor_num, sensor_num;
	unsigned int sensor_data_type;


	/* =============== Can you open the file? =============== */
	debug("entering\n");

	//Is it even accessible?
	ret = -EACCES; 				
	if(filp->f_mode & FMODE_WRITE) 	//file is open for writing
		goto out;

	//Is it even here?
	ret = -ENODEV;				
	if ((ret = nonseekable_open(inode, filp)) < 0)	//no such device
		goto out;

	/* ==================== File can open now ================== */
	/* =============== Gather Sensor information =============== */

	/*
	 * Associate this open file with the relevant sensor based on
	 * the minor number of the device node [/dev/sensor<NO>-<TYPE>]
	 */
	
	/*   To pdf elege: minor = ais8hthras * 8 + metrhsh.	*/

	//Take minor nums
	minor_num  = iminor(inode);				//take minor number from i-node
	//Take Major num and data type
	sensor_num = minor_num / 8;				//sensor number
	sensor_data_type = minor_num % 8;		//Type number
	//LINK IT WITH CORRECT SENSOR
	sensor = &lunix_sensors[sensor_num];	//link with sensor

	
	debug("OK. I got sensor :) \n");

	/* =============== Initialize state struct =============== */

	//Allocate space for "state" and fill with Zeros
	state = kzalloc(sizeof(*state), GFP_KERNEL);	//flag: allocation in kernel space

	/* No allocation damn it */
	if(!state) {	
		debug("Allocation of state struct failed :( \n");
		ret = -ENOMEM; 	//no memory dude
		goto out;
	}


	//__Init__ state struct (state struct is on chrdev.h):

	state->sensor			= sensor;				//SENSOR
	state->type 			= sensor_data_type;		//TYPE
	state->buf_lim			= 0;					//init buffer
	state->buf_timestamp 	= get_seconds();		//Smile. I take photo
	sema_init(&(state->lock) , 1);					//Then I lock you :(. <synch.h>

	debug("All ok with semaphore");


	//for struct file information:
	//https://github.com/torvalds/linux/blob/master/include/linux/fs.h
	//my data is the state struct i made. 
	filp->private_data 	= state;			
	filp->f_pos 		= 0;

	//Everything OK? then return 0
	ret = 0;
	
out:
	debug("leaving, with ret = %d\n", ret);
	return ret;
}




static int lunix_chrdev_release(struct inode *inode, struct file *filp)
{
	//Free pointer to memory allocated by kzalloc in open
	kfree(filp->private_data);
	return 0;
}




static long lunix_chrdev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	/* Why? */
	return -EINVAL;
}

static ssize_t lunix_chrdev_read(struct file *filp, char __user *usrbuf, size_t cnt, loff_t *f_pos)
{
	/* ====================== Declarations ===================== */
	
	ssize_t ret;

	struct lunix_sensor_struct *sensor;
	struct lunix_chrdev_state_struct *state;

	int di, bytes;


	/* =============== Link with sensor and state =============== */

	//link state from file pointer
	//see open func
	state = filp->private_data;		//TAKE STATE
	WARN_ON(!state);

	//link sensor from state struct
	sensor = state->sensor;			//TAKE SENSOR
	WARN_ON(!sensor);


	//https://elixir.bootlin.com/linux/latest/source/kernel/locking/semaphore.c#L75
	/*
	 * Attempts to acquire the semaphore.  If no more tasks are allowed to
	 * acquire the semaphore, calling this function will put the task to sleep.
	 * If the sleep is interrupted by a signal, this function will return -EINTR.
	 * If the semaphore is successfully acquired, this function returns 0.
	*/
	 
	/* Lock */
	di = down_interruptible(&(state->lock));
	
	if(di) {
		debug("Failed down_interruptible \n");
		ret = -EINTR;	//Interrupt found
		goto out;
	}
	

	/* =============== wait mechanism. =============== */


	/*
	 * If the cached character device state needs to be
	 * updated by actual sensor data (i.e. we need to report
	 * on a "fresh" measurement), do so
	 */
	 
	if (*f_pos == 0) {
		
		//keep updating: wait for something to be written
		while (lunix_chrdev_state_update(state) == -EAGAIN) {
			
			//Release lock
			up(&(state->lock));

			//Wait for something new to happen

			//wait_even_interruptible(wq, condition):
			//wq: wait queue to wait on
			//sleeps until condition gets true or signal received
			//if woke up returnes 0
			//if interrupted from signal returns ERESTARTSYS
			
			if (wait_event_interruptible(sensor->wq,
			 lunix_chrdev_state_needs_refresh(state)))
			 
				return -ERESTARTSYS; /* signal: tell the fs layer to handle it */

			//LOCK AGAIN
			di = down_interruptible(&(state->lock));
			if (di) {
				/* Failed to V */
				debug("failed down_interruptible\n");
				ret = -EINTR;
				goto out;
			}
			
		}
		
	}


	debug("Copy_to_user Time\n");


	//how many bytes read?
	if (cnt <   state->buf_lim - *f_pos )	//there were enough to be read
		bytes = cnt;
	else
		bytes = state->buf_lim - *f_pos;	//couldnt read much
		
	
	//copy_to_user(to, from, number of bytes):
	//Returns number of bytes that could not be copied
	ret = copy_to_user(usrbuf, state->buf_data + *f_pos, bytes);	


	if (ret) {
		/* Failed to copy */
		debug("copy_to_user failed\n");
	}
	
	
	//move position into file
	(*f_pos) += bytes;

	//out of position. reset position
	if(*f_pos >= state->buf_lim) *f_pos = 0;

	//return number of bytes
	ret = bytes;


out:
	//unlock after all
	up(&(state->lock));
	return ret;
}

static int lunix_chrdev_mmap(struct file *filp, struct vm_area_struct *vma)
{
	return -EINVAL;
}

static struct file_operations lunix_chrdev_fops = 
{
        .owner          = THIS_MODULE,
	.open           = lunix_chrdev_open,
	.release        = lunix_chrdev_release,
	.read           = lunix_chrdev_read,
	.unlocked_ioctl = lunix_chrdev_ioctl,
	.mmap           = lunix_chrdev_mmap
};

int lunix_chrdev_init(void)
{
	/*
	 * Register the character device with the kernel, asking for
	 * a range of minor numbers (number of sensors * 8 measurements / sensor)
	 * beginning with LINUX_CHRDEV_MAJOR:0
	 */
	int ret;
	dev_t dev_no;
	unsigned int lunix_minor_cnt = lunix_sensor_cnt << 3;
	
	debug("initializing character device\n");
	//Init device
	cdev_init(&lunix_chrdev_cdev, &lunix_chrdev_fops);
	//link with owner
	lunix_chrdev_cdev.owner = THIS_MODULE;
	
	//take major number
	dev_no = MKDEV(LUNIX_CHRDEV_MAJOR, 0);
	
	//register region
	ret = register_chrdev_region(dev_no, lunix_minor_cnt, "lunix");

	//couldnt register
	if (ret < 0) {
		debug("failed to register region, ret = %d\n", ret);
		goto out;
	}	

	//add device 
	ret = cdev_add(&lunix_chrdev_cdev, dev_no, lunix_minor_cnt);

	if (ret < 0) {
		debug("failed to add character device\n");
		goto out_with_chrdev_region;
	}
	debug("completed successfully\n");
	return 0;

out_with_chrdev_region:
	unregister_chrdev_region(dev_no, lunix_minor_cnt);
out:
	return ret;
}

void lunix_chrdev_destroy(void)
{
	dev_t dev_no;
	unsigned int lunix_minor_cnt = lunix_sensor_cnt << 3;
		
	debug("entering\n");
	dev_no = MKDEV(LUNIX_CHRDEV_MAJOR, 0);
	cdev_del(&lunix_chrdev_cdev);
	unregister_chrdev_region(dev_no, lunix_minor_cnt);
	debug("leaving\n");
}
