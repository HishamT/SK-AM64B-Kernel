#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/types.h>
#include "hello_ioctl.h"

#define DEVICE_NAME "hello"
#define BUFFER_SIZE 128

static struct platform_device *hello_pdev;

static const struct of_device_id hello_of_match[] = {
  { .compatible = "hisham,hello" },
  { }
};

MODULE_DEVICE_TABLE(of, hello_of_match);

struct hello_dev_data {
  char *buffer;
  size_t buffer_len;
  struct mutex lock;
  struct cdev cdev;
  struct device *device;
};



static struct hello_dev_data hello_data;

static dev_t hello_dev;
static struct class *hello_class;


static int hello_open(struct inode *inode, struct file *file) {
  struct hello_dev_data *data;

  data = container_of(inode->i_cdev, struct hello_dev_data, cdev);
  file->private_data = data;

  pr_info("hello device opened\n");
  return 0;
}

static int hello_release(struct inode *inode, struct file *file) {
  pr_info("hello device closed\n");
  return 0;
}

static ssize_t hello_read(struct file *file, char __user *buffer, size_t len, loff_t *offset)
{
  struct hello_dev_data *data = file->private_data;
  ssize_t ret;

  if (!data)
    return -ENODEV;

  mutex_lock(&data->lock);

  if (*offset >= data->buffer_len) {
    ret = 0;
    goto out;
  }

  if (len > data->buffer_len - *offset)
    len = data->buffer_len - *offset;

  if (copy_to_user(buffer, data->buffer + *offset, len)) {
    ret = -EFAULT;
    goto out;
  }

  *offset += len;
  ret = len;

out:
  mutex_unlock(&data->lock);
  return ret;
}

static ssize_t hello_write(struct file *file, const char __user *buffer, size_t len, loff_t *offset)
{
  struct hello_dev_data *data = file->private_data;
  ssize_t ret;

  if (!data)
    return -ENODEV;

  if (len >= BUFFER_SIZE)
    len = BUFFER_SIZE - 1;

  mutex_lock(&data->lock);

  if (copy_from_user(data->buffer, buffer, len)) {
    ret = -EFAULT;
    goto out;
  }

  data->buffer[len] = '\0';
  data->buffer_len = len;

  pr_info("user wrote: %s\n", data->buffer);

  ret = len;

out:
  mutex_unlock(&data->lock);
  return ret;
}

static long hello_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
  struct hello_dev_data *data = file->private_data;

  if (!data)
    return -ENODEV;

  switch (cmd) {
  case HELLO_IOCTL_CLEAR:
    mutex_lock(&data->lock);
    data->buffer[0] = '\0';
    data->buffer_len = 0;
    mutex_unlock(&data->lock);

    pr_info("hello buffer cleared\n");
    return 0;

  default:
    return -EINVAL;
  }
}

static struct file_operations hello_fops = {
  .owner = THIS_MODULE,
  .open = hello_open,
  .read = hello_read,
  .write = hello_write,
  .release = hello_release,
  .unlocked_ioctl = hello_ioctl,
};

static ssize_t buffer_len_show(struct device *dev,
                               struct device_attribute *attr,
                               char *buf)
{
  struct hello_dev_data *data = dev_get_drvdata(dev);
  ssize_t ret;

  if (!data)
    return -ENODEV;

  mutex_lock(&data->lock);
  ret = sysfs_emit(buf, "%zu\n", data->buffer_len);
  mutex_unlock(&data->lock);

  return ret;
}

static DEVICE_ATTR_RO(buffer_len);

static int hello_probe(struct platform_device *pdev)
{
  int ret;
  
  hello_data.buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
  if (!hello_data.buffer) {
    pr_err("failed to allocate hello buffer\n");
    return -ENOMEM;
  }

  hello_data.buffer[0] = '\0';
  hello_data.buffer_len = 0;
  mutex_init(&hello_data.lock);

  platform_set_drvdata(pdev, &hello_data);

  ret = alloc_chrdev_region(&hello_dev, 0, 1, DEVICE_NAME);
  if (ret < 0) {
    pr_err("failed to allocate character device region\n");
    goto fail_buffer;
  }

  cdev_init(&hello_data.cdev, &hello_fops);
  hello_data.cdev.owner = THIS_MODULE;

  ret = cdev_add(&hello_data.cdev, hello_dev, 1);
  if (ret < 0) {
    pr_err("failed to add character device\n");
    goto fail_chrdev_region;
  }

  hello_class = class_create(DEVICE_NAME);
  if (IS_ERR(hello_class)) {
    pr_err("failed to create device class\n");
    ret = PTR_ERR(hello_class);
    goto fail_cdev;
  }

  hello_data.device = device_create(hello_class, NULL, hello_dev, NULL, DEVICE_NAME);
  if (IS_ERR(hello_data.device)) {
    pr_err("failed to create device\n");
    ret = PTR_ERR(hello_data.device);
    goto fail_class;
  }
  dev_set_drvdata(hello_data.device, &hello_data);

  ret = device_create_file(hello_data.device, &dev_attr_buffer_len);
  if (ret < 0) {
    pr_err("failed to create buffer_len sysfs attribute\n");
    goto fail_device;
  }

  pr_info("registered /dev/%s with major %d minor %d\n",
          DEVICE_NAME,
          MAJOR(hello_dev),
          MINOR(hello_dev));

  return 0;

fail_device:
  device_destroy(hello_class, hello_dev);

fail_class:
  class_destroy(hello_class);

fail_cdev:
  cdev_del(&hello_data.cdev);

fail_chrdev_region:
  unregister_chrdev_region(hello_dev, 1);

fail_buffer:
  kfree(hello_data.buffer);
  return ret;
}

static void hello_remove(struct platform_device *pdev)
{
  struct hello_dev_data *data = platform_get_drvdata(pdev);

  pr_info("hello platform device removed\n");

  if (!data)
    return;

  device_remove_file(data->device, &dev_attr_buffer_len);
  device_destroy(hello_class, hello_dev);
  class_destroy(hello_class);
  cdev_del(&data->cdev);
  unregister_chrdev_region(hello_dev, 1);
  kfree(data->buffer);
}

static struct platform_driver hello_platform_driver = {
  .probe = hello_probe,
  .remove = hello_remove,
  .driver = {
    .name = "hello",
    .of_match_table = hello_of_match,
  },
};

static int __init hello_init(void)
{
  int ret;

  ret = platform_driver_register(&hello_platform_driver);
  if (ret < 0) {
    pr_err("failed to register hello platform driver\n");
    return ret;
  }

  hello_pdev = platform_device_register_simple("hello", -1, NULL, 0);
  if (IS_ERR(hello_pdev)) {
    pr_err("failed to register hello platform device\n");
    ret = PTR_ERR(hello_pdev);
    platform_driver_unregister(&hello_platform_driver);
    return ret;
  }

  pr_info("hello platform driver registered\n");
  return 0;
}

static void __exit hello_exit(void)
{
  platform_device_unregister(hello_pdev);
  platform_driver_unregister(&hello_platform_driver);

  pr_info("hello platform driver unregistered\n");
}


module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Hisham");
MODULE_DESCRIPTION("Simple character device driver");
