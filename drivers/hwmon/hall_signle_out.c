/******************** (C) COPYRIGHT 2014 ZTEMT ********************
*
* File Name          : hall_signle_out.c
* Authors            : Zhu Bing
* Version            : V.1.0.0
* Date               : 04/16/2014
*
********************************************************************************
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*
* THE PRESENT SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES
* OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, FOR THE SOLE
* PURPOSE TO SUPPORT YOUR APPLICATION DEVELOPMENT.
* AS A RESULT, STMICROELECTRONICS SHALL NOT BE HELD LIABLE FOR ANY DIRECT,
* INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING FROM THE
* CONTENT OF SUCH SOFTWARE AND/OR THE USE MADE BY CUSTOMERS OF THE CODING
* INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
*
********************************************************************************
********************************************************************************
Version History.
 
Revision 1-0-0 04/16/2014
 first revision

*******************************************************************************/

#include <linux/i2c/hall_signle_out.h>

#define LOG_TAG "HALL_DEVICE"
#define DEBUG_ON //DEBUG SWITCH

#define SENSOR_LOG_FILE__ strrchr(__FILE__, '/') ? (strrchr(__FILE__, '/')+1) : __FILE__

#ifdef  CONFIG_FEATURE_ZTEMT_SENSORS_LOG_ON
#define SENSOR_LOG_ERROR(fmt, args...) printk(KERN_ERR   "[%s] [%s: %d] "  fmt,\
                                              LOG_TAG,__FUNCTION__, __LINE__, ##args)
    #ifdef  DEBUG_ON
#define SENSOR_LOG_INFO(fmt, args...)  printk(KERN_INFO  "[%s] [%s: %d] "  fmt,\
                                              LOG_TAG,__FUNCTION__, __LINE__, ##args)
                                              
#define SENSOR_LOG_DEBUG(fmt, args...) printk(KERN_DEBUG "[%s] [%s: %d] "  fmt,\
                                              LOG_TAG,__FUNCTION__, __LINE__, ##args)
    #else
#define SENSOR_LOG_INFO(fmt, args...)
#define SENSOR_LOG_DEBUG(fmt, args...)
    #endif

#else
#define SENSOR_LOG_ERROR(fmt, args...)
#define SENSOR_LOG_INFO(fmt, args...)
#define SENSOR_LOG_DEBUG(fmt, args...)
#endif

 
static dev_t const   hall_device_dev_t   = MKDEV(MISC_MAJOR, 252);

static struct class  *hall_device_class;

static const struct dev_pm_ops hall_device_pm_ops = {
    .suspend = hall_device_suspend,
    .resume  = hall_device_resume,
};

static const struct i2c_device_id hall_device_idtable_id[] = {
     { "zte,hall_signle_out", 0 },
     { },
 };
 
static struct of_device_id of_hall_device_idtable[] = {
     { .compatible = "zte,hall_signle_out",},
     {}
};

static struct i2c_driver hall_device_driver = {
    .driver = {
        .name = "hall_signle_out",
        .of_match_table = of_hall_device_idtable,
        .pm = &hall_device_pm_ops,
    },
    .id_table = hall_device_idtable_id,
    .probe = hall_device_probe,
    .remove = hall_device_remove,
};

static int hall_device_init(void)
{
    SENSOR_LOG_INFO("driver: init\n");
    return i2c_add_driver(&hall_device_driver);
}
 
static void hall_device_exit(void)
{
    SENSOR_LOG_INFO("driver: exit\n");
    i2c_del_driver(&hall_device_driver);
}

static void hall_device_wakelock_ops(struct hall_device_wake_lock *wakelock, bool enable)
{
    if (enable == wakelock->locked)
    {
        SENSOR_LOG_INFO("doubule %s %s, retern here\n",enable? "lock" : "unlock",wakelock->name);
        return;
    }

    if (enable)
    {
        wake_lock(&wakelock->lock);
    }
    else
    {
        wake_unlock(&wakelock->lock);
    }

    wakelock->locked = enable;

    SENSOR_LOG_INFO("%s %s \n",enable? "lock" : "unlock",wakelock->name);
}

static enum hrtimer_restart hall_device_unlock_wakelock_work_func(struct hrtimer *timer)
{ 
    struct hall_device_chip *chip = container_of(timer, struct hall_device_chip, unlock_wakelock_timer);

    if (false == chip->on_irq_working)
    {
        hall_device_wakelock_ops(&(chip->wakeup_wakelock), false);
    }

    return HRTIMER_NORESTART;
}

static void hall_device_irq_work(struct work_struct *work)
{
	struct hall_device_chip *chip = container_of(work, struct hall_device_chip, irq_work);

	mutex_lock(&chip->lock);
    //SENSOR_LOG_INFO("enter\n");

    if (0 == gpio_get_value(chip->irq.irq_pin))
    {  
        SENSOR_LOG_INFO("MAGNETIC_DEVICE NEAR\n");
        input_report_rel(chip->idev, REL_RX, MAGNETIC_DEVICE_NEAR);
        hall_device_wakelock_ops(&(chip->wakeup_wakelock),false);
    }
    else
    {
        SENSOR_LOG_INFO("MAGNETIC_DEVICE FAR\n");
        input_report_rel(chip->idev, REL_RX, MAGNETIC_DEVICE_FAR);
        hrtimer_start(&chip->unlock_wakelock_timer, ktime_set(3, 0), HRTIMER_MODE_REL);
    }
    input_sync(chip->idev);

	chip->on_irq_working = false;

    hall_device_irq_enable(&(chip->irq), true, true);
    //SENSOR_LOG_INFO("exit\n");
	mutex_unlock(&chip->lock);
};

static void hall_device_check_state(struct hall_device_chip *chip)
{
    if (1 == gpio_get_value(chip->irq.irq_pin))
    {
        SENSOR_LOG_INFO("MAGNETIC_DEVICE FAR\n");
        input_report_rel(chip->idev, REL_RX, MAGNETIC_DEVICE_FAR);
    }
    else
    {
        SENSOR_LOG_INFO("MAGNETIC_DEVICE NEAR\n");
        input_report_rel(chip->idev, REL_RX, MAGNETIC_DEVICE_NEAR);
    }

    input_sync(chip->idev);
};

static irqreturn_t hall_device_irq(int irq, void *handle)
{
	struct hall_device_chip *chip = handle;
    //SENSOR_LOG_INFO("enter\n");
    hall_device_irq_enable(&(chip->irq), false, false);
	chip->on_irq_working = true;
	hrtimer_cancel(&chip->unlock_wakelock_timer);

    if (true == chip->enabled)
    {
        hall_device_wakelock_ops(&(chip->wakeup_wakelock), true);
    }

	if (0 == schedule_work(&chip->irq_work))
    {
        SENSOR_LOG_INFO("schedule_work failed!\n");
    }

    //SENSOR_LOG_INFO("exit\n");
	return IRQ_HANDLED;
}

static void hall_device_irq_enable(struct hall_device_irq * irq, bool enable, bool flag_sync)
{
    if (enable == irq->enabled)
    {
        SENSOR_LOG_INFO("doubule %s irq %d, retern here\n",enable? "enable" : "disable", irq->irq_num);
        return;
    }
    else
    {
        irq->enabled  = enable;
        SENSOR_LOG_INFO("%s irq %d\n",enable? "enable" : "disable",irq->irq_num);
    }

    if (enable)
    {
        enable_irq(irq->irq_num);
    }
    else
    {
        if (flag_sync)
        {
            disable_irq(irq->irq_num);
        }
        else
        {
            disable_irq_nosync(irq->irq_num);
        }
    }
}


static void hall_device_enable(struct hall_device_chip *chip, int on)
{
    SENSOR_LOG_INFO("%s hall_device\n",on? "enable" : "disable");

	if (on) 
    {
		hall_device_irq_enable(&(chip->irq), true, true);
        hall_device_check_state(chip);
	} 
    else 
    {
        hall_device_irq_enable(&(chip->irq), false, true);
    }
}

static ssize_t hall_check_show(
	struct device *dev,	struct device_attribute *attr,	char *buf)
{
    return sprintf(buf, "hall_signle_out");
}

static ssize_t hall_device_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hall_device_chip *chip = dev_get_drvdata(dev);
	return snprintf(buf, PAGE_SIZE, "%d\n", chip->enabled);
}

static ssize_t hall_device_enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	struct hall_device_chip *chip = dev_get_drvdata(dev);
	bool value;

	if (strtobool(buf, &value))
		return -EINVAL;
    mutex_lock(&chip->lock);

    chip->enabled = (value>0) ? true : false;
    hall_device_enable(chip, chip->enabled);

    mutex_unlock(&chip->lock);

	return size;
}

static struct device_attribute attrs_hall_device[] = {
	__ATTR(hall_check, 0444, hall_check_show,         NULL),
	__ATTR(enable,     0640, hall_device_enable_show, hall_device_enable_store),
};

static int create_sysfs_interfaces(struct device *dev)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(attrs_hall_device); i++)
		if (device_create_file(dev, attrs_hall_device + i))
			goto error;
	return 0;

error:
	for ( ; i >= 0; i--)
		device_remove_file(dev, attrs_hall_device + i);
	dev_err(dev, "%s:Unable to create interface\n", __func__);
	return -1;
}

static void hall_device_chip_data_init(struct hall_device_chip *chip)
{
    chip->enabled = false;
    chip->irq.enabled = true;
    chip->wakeup_wakelock.name = "hall_device_wakelock";
    chip->wakeup_wakelock.locked = false;
    chip->on_irq_working = false;
}

static int hall_device_parse_dt(struct hall_device_chip *chip)
{
	int rc = 0;
	struct device_node *np = chip->client->dev.of_node;
	rc = of_get_named_gpio_flags(np, "hall_device,irq-gpio", 0, NULL);

	if(rc < 0)
	{
		SENSOR_LOG_ERROR("Unable to read irq gpio\n");
		return rc;
	}

	chip->irq.irq_pin = rc;
    SENSOR_LOG_INFO("irq_pin is %d\n", chip->irq.irq_pin);
    return 0;
}

/* POWER SUPPLY VOLTAGE RANGE */
#define HALL_VIO_MIN_UV	1750000
#define HALL_VIO_MAX_UV	1950000

static int hall_device_power_init(struct hall_device_chip *chip, bool on)
{
	int rc = 0;

	SENSOR_LOG_ERROR("on = %d\n", on);

	if (!on) {
		if (regulator_count_voltages(chip->vio) > 0)
			regulator_set_voltage(chip->vio, 0,
				HALL_VIO_MAX_UV);
		regulator_put(chip->vio);
	} else {
		chip->vio = regulator_get(&chip->client->dev, "vio");
		if (IS_ERR(chip->vio)) {
			rc = PTR_ERR(chip->vio);
			dev_err(&chip->client->dev,
				"Regulator get failed vio rc=%d\n", rc);
			return rc;
		}

		if (regulator_count_voltages(chip->vio) > 0) {
			rc = regulator_set_voltage(chip->vio,
				HALL_VIO_MIN_UV, HALL_VIO_MAX_UV);
			if (rc) {
				dev_err(&chip->client->dev,
				"Regulator set failed vio rc=%d\n", rc);
				goto err_vio_set;
			}
		}

		rc = regulator_enable(chip->vio);
		if (rc) {
			dev_err(&chip->client->dev,
				"Regulator vio enable failed rc=%d\n", rc);
			goto err_vio_enable;
		}
	}
	return 0;

err_vio_enable:
	if (regulator_count_voltages(chip->vio) > 0)
		regulator_set_voltage(chip->vio, 0, HALL_VIO_MAX_UV);
err_vio_set:
	regulator_put(chip->vio);
	return rc;
}

static int hall_device_probe(struct i2c_client *client,
                  const struct i2c_device_id *id)
{
    int ret = 0;
	static struct hall_device_chip *chip = NULL;

    SENSOR_LOG_INFO("probe start\n");

    chip = kzalloc(sizeof(struct hall_device_chip), GFP_KERNEL);
    if (!chip) {
        ret = -ENOMEM;
        goto malloc_failed;
    }

	chip->client = client;
	i2c_set_clientdata(client, chip);

    hall_device_chip_data_init(chip);

    hall_device_parse_dt(chip);

	mutex_init(&chip->lock);

    hall_device_class   = class_create(THIS_MODULE, "hall_device");

    chip->hall_device_dev = device_create(hall_device_class, NULL, hall_device_dev_t, &hall_device_driver ,"hall_device");
    if (IS_ERR(chip->hall_device_dev)) 
    {
       ret = PTR_ERR(chip->hall_device_dev);
       goto create_hall_device_dev_failed;
    }

	dev_set_drvdata(chip->hall_device_dev, chip);

    ret = gpio_request(chip->irq.irq_pin, "irq_hall_device");
    if (ret)    
    {
        SENSOR_LOG_INFO("gpio %d is busy and then to free it\n",chip->irq.irq_pin);
        
        gpio_free(chip->irq.irq_pin);
        ret = gpio_request(chip->irq.irq_pin, "irq_hall_device");
        if (ret) 
        {
            SENSOR_LOG_INFO("gpio %d is busy and then to free it\n",chip->irq.irq_pin);
            //return ret;
            goto create_hall_device_dev_failed;//lcx
        }

		ret = gpio_direction_input(chip->irq.irq_pin);
		if (ret) {
			dev_err(&client->dev, "hall set_direction for irq gpio failed\n");
			goto create_hall_device_dev_failed;//lcx
		}
    }
    
    //ret = gpio_tlmm_config(GPIO_CFG(chip->irq.irq_pin, 0, GPIO_CFG_INPUT, GPIO_CFG_PULL_UP, GPIO_CFG_2MA), GPIO_CFG_ENABLE);
    chip->irq.irq_num = gpio_to_irq(chip->irq.irq_pin);

    INIT_WORK(&chip->irq_work, hall_device_irq_work);
    ret = request_irq(chip->irq.irq_num, hall_device_irq, IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING | IRQF_ONESHOT, "irq_hall_device", chip);
    if (ret) {
        SENSOR_LOG_ERROR("Failed to request irq %d\n", chip->irq.irq_num);
        goto irq_register_fail;
    }

    chip->idev = input_allocate_device();
    if (!chip->idev) 
    {
        SENSOR_LOG_ERROR("no memory for idev\n");
        ret = -ENODEV;
        goto input_alloc_failed;
    }
    chip->idev->name = "hall_device";
    chip->idev->id.bustype = BUS_I2C;

    set_bit(EV_REL,     chip->idev->evbit);
    set_bit(REL_RX,     chip->idev->relbit);  //NEAR

    ret = input_register_device(chip->idev);
    if (ret) {
        input_free_device(chip->idev);
        SENSOR_LOG_ERROR("cant register input '%s'\n", chip->idev->name);
        goto input_register_failed;
    }

    create_sysfs_interfaces(chip->hall_device_dev);

    hall_device_irq_enable(&(chip->irq), false, true);

    wake_lock_init(&chip->wakeup_wakelock.lock, WAKE_LOCK_SUSPEND, chip->wakeup_wakelock.name);
    hrtimer_init(&chip->unlock_wakelock_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    chip->unlock_wakelock_timer.function = hall_device_unlock_wakelock_work_func;

    ret = hall_device_power_init(chip, 1);
	if (ret < 0)
		goto input_register_failed;

    SENSOR_LOG_INFO("probe success\n");
    return 0;

input_register_failed:
    input_free_device(chip->idev);
input_alloc_failed:
malloc_failed:
irq_register_fail:
create_hall_device_dev_failed:
    chip->hall_device_dev = NULL;
    class_destroy(hall_device_class);
    SENSOR_LOG_INFO("prob failed\n");

    return -1;
}

//resume
static int hall_device_resume(struct device *dev)
{
	struct hall_device_chip *chip = dev_get_drvdata(dev);

    SENSOR_LOG_INFO("enter\n");
    if (true == chip->enabled)
    {
        disable_irq_wake(chip->irq.irq_num);
    }
    SENSOR_LOG_INFO("exit\n");
    return 0 ;
}
 
//suspend  
static int hall_device_suspend(struct device *dev)
{
	struct hall_device_chip *chip = dev_get_drvdata(dev);

    SENSOR_LOG_INFO("enter\n");
    if (true == chip->enabled)
    {
        enable_irq_wake(chip->irq.irq_num);
    }
    SENSOR_LOG_INFO("exit\n");
    return 0 ;
}

 /**
  * hall_device_remove() - remove device
  * @client: I2C client device
  */
 static int hall_device_remove(struct i2c_client *client)
 {
     struct hall_device_chip *chip_data = i2c_get_clientdata(client);
 
     SENSOR_LOG_INFO("hall_device_remove\n");
     hall_device_power_init(chip_data, 0);
     kfree(chip_data);
     return 0;
 }
 
MODULE_DEVICE_TABLE(i2c, hall_device_idtable);
module_init(hall_device_init);
module_exit(hall_device_exit);
 
MODULE_DESCRIPTION("hall_single_out driver");
MODULE_AUTHOR("ZTEMT");
MODULE_LICENSE("GPL");
