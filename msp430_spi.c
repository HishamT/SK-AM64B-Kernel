#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>

struct msp430_data {
  struct spi_device *spi;
  struct miscdevice miscdev;
  u8 response[4];
  struct mutex lock;
};

static int msp430_spi_transfer(struct spi_device *spi,
                               const u8 *tx_buf,
                               u8 *rx_buf,
                               size_t len)
{
  struct spi_transfer transfer = {
    .tx_buf = tx_buf,
    .rx_buf = rx_buf,
    .len = len,
  };
  int ret;

  if (len != 4)
    return -EINVAL;

  ret = spi_sync_transfer(spi, &transfer, 1);
  if (ret < 0) {
    dev_err(&spi->dev,
            "SPI transfer failed: %d\n",
            ret);
    return ret;
  }

  dev_info(&spi->dev,
           "TX: %*ph\n",
           (int)len,
           tx_buf);

  dev_info(&spi->dev,
           "RX: %*ph\n",
           (int)len,
           rx_buf);

  return 0;
}

static ssize_t transfer_store(struct device *dev,
                              struct device_attribute *attr,
                              const char *buf,
                              size_t count)
{
  struct spi_device *spi = to_spi_device(dev);
  struct msp430_data *data = spi_get_drvdata(spi);
  u8 tx_buf[4];
  unsigned int b0;
  unsigned int b1;
  unsigned int b2;
  unsigned int b3;
  int ret;

  ret = sscanf(buf,
               "%x %x %x %x",
               &b0,
               &b1,
               &b2,
               &b3);

  if (ret != 4)
    return -EINVAL;

  if (b0 > 0xff ||
      b1 > 0xff ||
      b2 > 0xff ||
      b3 > 0xff)
    return -EINVAL;

  tx_buf[0] = b0;
  tx_buf[1] = b1;
  tx_buf[2] = b2;
  tx_buf[3] = b3;

  mutex_lock(&data->lock);

  ret = msp430_spi_transfer(spi,
                            tx_buf,
                            data->response,
                            sizeof(tx_buf));

  mutex_unlock(&data->lock);

  if (ret < 0)
    return ret;

  return count;
}

static ssize_t response_show(struct device *dev,
                             struct device_attribute *attr,
                             char *buf)
{
  struct spi_device *spi = to_spi_device(dev);
  struct msp430_data *data = spi_get_drvdata(spi);
  ssize_t ret;

  mutex_lock(&data->lock);

  ret = sysfs_emit(buf,
                   "%02x %02x %02x %02x\n",
                   data->response[0],
                   data->response[1],
                   data->response[2],
                   data->response[3]);

  mutex_unlock(&data->lock);

  return ret;
}

static DEVICE_ATTR_WO(transfer);
static DEVICE_ATTR_RO(response);

static ssize_t msp430_write(struct file *file,
                            const char __user *buf,
                            size_t count,
                            loff_t *ppos)
{
  struct miscdevice *miscdev = file->private_data;
  struct msp430_data *data =
      container_of(miscdev, struct msp430_data, miscdev);

  u8 tx_buf[4];
  int ret;

  if (count != sizeof(tx_buf))
    return -EINVAL;

  if (copy_from_user(tx_buf, buf, sizeof(tx_buf)))
    return -EFAULT;

  mutex_lock(&data->lock);

  ret = msp430_spi_transfer(data->spi,
                            tx_buf,
                            data->response,
                            sizeof(tx_buf));

  mutex_unlock(&data->lock);

  if (ret < 0)
    return ret;

  return count;
}

static ssize_t msp430_read(struct file *file,
                           char __user *buf,
                           size_t count,
                           loff_t *ppos)
{
  struct miscdevice *miscdev = file->private_data;
  struct msp430_data *data =
      container_of(miscdev, struct msp430_data, miscdev);

  size_t len = sizeof(data->response);

  if (*ppos >= len)
    return 0;

  if (count > len - *ppos)
    count = len - *ppos;

  mutex_lock(&data->lock);

  if (copy_to_user(buf,
                   data->response + *ppos,
                   count)) {
    mutex_unlock(&data->lock);
    return -EFAULT;
  }

  mutex_unlock(&data->lock);

  *ppos += count;

  return count;
}

static const struct file_operations msp430_fops = {
  .owner = THIS_MODULE,
  .write = msp430_write,
  .read = msp430_read,
};

static int msp430_spi_probe(struct spi_device *spi)
{
  struct msp430_data *data;
  int ret;

  data = devm_kzalloc(&spi->dev,
                      sizeof(*data),
                      GFP_KERNEL);
  if (!data)
    return -ENOMEM;

  mutex_init(&data->lock);
  spi_set_drvdata(spi, data);

  data->spi = spi;

  data->miscdev.minor = MISC_DYNAMIC_MINOR;
  data->miscdev.name = "msp430";
  data->miscdev.fops = &msp430_fops;
  data->miscdev.parent = &spi->dev;

  dev_info(&spi->dev,
           "MSP430 SPI device probed\n");

  dev_info(&spi->dev,
           "max speed: %u Hz\n",
           spi->max_speed_hz);

  dev_info(&spi->dev,
           "chip select: %u\n",
           spi_get_chipselect(spi, 0));

  dev_info(&spi->dev,
           "SPI mode: %u\n",
           spi->mode);

  ret = device_create_file(&spi->dev,
                           &dev_attr_transfer);
  if (ret < 0)
    return ret;

  ret = device_create_file(&spi->dev,
                           &dev_attr_response);

  ret = misc_register(&data->miscdev);
  if (ret < 0) {
    device_remove_file(&spi->dev,
                      &dev_attr_response);

    device_remove_file(&spi->dev,
                      &dev_attr_transfer);

    return ret;
  }
  if (ret < 0) {
    device_remove_file(&spi->dev,
                       &dev_attr_transfer);
    return ret;
  }

  return 0;
}

static void msp430_spi_remove(struct spi_device *spi)
{
  struct msp430_data *data = spi_get_drvdata(spi);

  misc_deregister(&data->miscdev);
  device_remove_file(&spi->dev,
                     &dev_attr_response);

  device_remove_file(&spi->dev,
                     &dev_attr_transfer);

  dev_info(&spi->dev,
           "MSP430 SPI device removed\n");
}

static const struct of_device_id msp430_of_match[] = {
  { .compatible = "hisham,msp430fr2433" },
  { }
};

MODULE_DEVICE_TABLE(of, msp430_of_match);

static const struct spi_device_id msp430_spi_ids[] = {
  { "msp430fr2433", 0 },
  { }
};

MODULE_DEVICE_TABLE(spi, msp430_spi_ids);

static struct spi_driver msp430_spi_driver = {
  .driver = {
    .name = "msp430_spi",
    .of_match_table = msp430_of_match,
  },

  .probe = msp430_spi_probe,
  .remove = msp430_spi_remove,
  .id_table = msp430_spi_ids,
};

module_spi_driver(msp430_spi_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Hisham");
MODULE_DESCRIPTION("SPI driver for MSP430FR2433");