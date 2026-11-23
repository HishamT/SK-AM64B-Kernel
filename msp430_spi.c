#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>

#define MSP430_FRAME_LEN 4

struct msp430_data {
  struct spi_device *spi;
  struct miscdevice miscdev;
  u8 response[MSP430_FRAME_LEN];
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

  if (len != MSP430_FRAME_LEN)
    return -EINVAL;

  ret = spi_sync_transfer(spi, &transfer, 1);
  if (ret < 0) {
    dev_err(&spi->dev,
            "SPI transfer failed: %d\n",
            ret);
    return ret;
  }

  return 0;
}


static ssize_t msp430_write(struct file *file,
                            const char __user *buf,
                            size_t count,
                            loff_t *ppos)
{
  struct miscdevice *miscdev = file->private_data;
  struct msp430_data *data =
      container_of(miscdev, struct msp430_data, miscdev);

  u8 tx_buf[MSP430_FRAME_LEN];
  u8 dummy_tx[MSP430_FRAME_LEN] = { 0 };
  int ret;

  if (count != sizeof(tx_buf))
    return -EINVAL;

  if (copy_from_user(tx_buf, buf, sizeof(tx_buf)))
    return -EFAULT;

  mutex_lock(&data->lock);

  /*
   * Transaction 1:
   * Send command to MSP430.
   */
  ret = msp430_spi_transfer(data->spi,
                          tx_buf,
                          NULL,
                          sizeof(tx_buf));
  if (ret < 0)
    goto out;

  /*
   * Transaction 2:
   * Clock the prepared response out of the MSP430.
   */
  ret = msp430_spi_transfer(data->spi,
                            dummy_tx,
                            data->response,
                            sizeof(dummy_tx));

out:
  mutex_unlock(&data->lock);

  if (ret < 0) return ret;

  *ppos = 0;

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

  data->spi = spi;

  data->miscdev.minor = MISC_DYNAMIC_MINOR;
  data->miscdev.name = "msp430";
  data->miscdev.fops = &msp430_fops;
  data->miscdev.parent = &spi->dev;

  spi_set_drvdata(spi, data);

  


  ret = misc_register(&data->miscdev);
  if (ret < 0) {
    dev_err(&spi->dev, "failed to register misc device: %d\n", ret);
    return ret;
  }

  dev_info(&spi->dev, "MSP430 SPI device probed\n");

  return 0;
}

static void msp430_spi_remove(struct spi_device *spi)
{
  struct msp430_data *data = spi_get_drvdata(spi);

  misc_deregister(&data->miscdev);

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