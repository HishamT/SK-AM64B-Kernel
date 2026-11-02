#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/of.h>

static int msp430_spi_transfer(struct spi_device *spi,
                               const u8 *tx_buf,
                               size_t len)
{
  u8 rx_buf[4] = { 0 };
  struct spi_transfer transfer = {
    .tx_buf = tx_buf,
    .rx_buf = rx_buf,
    .len = len,
  };
  int ret;

  if (len != sizeof(rx_buf))
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

  ret = msp430_spi_transfer(spi,
                            tx_buf,
                            sizeof(tx_buf));
  if (ret < 0)
    return ret;

  return count;
}

static DEVICE_ATTR_WO(transfer);

static int msp430_spi_probe(struct spi_device *spi)
{
  int ret;

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
  if (ret < 0) {
    dev_err(&spi->dev,
            "failed to create transfer sysfs attribute\n");
    return ret;
  }

  return 0;
}

static void msp430_spi_remove(struct spi_device *spi)
{
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