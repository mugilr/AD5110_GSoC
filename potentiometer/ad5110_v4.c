// SPDX-License-Identifier: GPL-2.0+
/*
 * Analog Devices AD5110 digital potentiometer driver
 *
 * Copyright (C) 2021 Mugilraj Dhavachelvan <dmugil2000@gmail.com>
 *
 * Datasheet: https://www.analog.com/media/en/technical-documentation/data-sheets/AD5110_5112_5114.pdf
 *
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

/* AD5110 commands */
#define AD5110_EEPROM_WR	1
#define AD5110_RDAC_WR	2
#define AD5110_SHUTDOWN	3
#define AD5110_RESET	4
#define AD5110_RDAC_RD	5
#define AD5110_EEPROM_RD	6

/* AD5110_EEPROM_RD data */
#define AD5110_WIPER_POS	0
#define AD5110_RESISTOR_TOL	1

#define WIPER_RESISTANCE	70

struct ad5110_cfg {
	int max_pos;
	int kohms;
	int shift;
};

enum ad5110_type {
	AD5110_10,
	AD5110_80,
	AD5112_05,
	AD5112_10,
	AD5112_80,
	AD5114_10,
	AD5114_80,
};

static const struct ad5110_cfg ad5110_cfg[] = {
	[AD5110_10] = { .max_pos = 128, .kohms = 10 },
	[AD5110_80] = { .max_pos = 128, .kohms = 80 },
	[AD5112_05] = { .max_pos = 64, .kohms = 5, .shift = 1 },
	[AD5112_10] = { .max_pos = 64, .kohms = 10, .shift = 1 },
	[AD5112_80] = { .max_pos = 64, .kohms = 80, .shift = 1 },
	[AD5114_10] = { .max_pos = 32, .kohms = 10, .shift = 2 },
	[AD5114_80] = { .max_pos = 32, .kohms = 80, .shift = 2 },
};

struct ad5110_data {
	struct i2c_client       *client;
	s16			tol;		/* resistor tolerance */
	bool			enable;
	struct mutex            lock;
	const struct ad5110_cfg	*cfg;
	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	u8			buf[2] ____cacheline_aligned;
};

static const struct iio_chan_spec ad5110_channels[] = {
	{
		.type = IIO_RESISTANCE,
		.output = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_OFFSET),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |
						BIT(IIO_CHAN_INFO_ENABLE),
	}
};

static int ad5110_read(struct ad5110_data *data, u8 cmd, int *val)
{
	int ret;

	mutex_lock(&data->lock);
	data->buf[0] = cmd;
	data->buf[1] = *val;

	ret = i2c_master_send_dmasafe(data->client, data->buf, sizeof(data->buf));
	if (ret < 0)
		goto error;
	data->buf[1] = 0;
	data->buf[0] = 0;
	ret = i2c_master_recv_dmasafe(data->client, data->buf, sizeof(data->buf));
	if (ret < 0)
		goto error;

	*val = data->buf[0];
	ret = 0;
	dev_err(&data->client->dev, "val in i2c buf %d ,\t %d\n", data->buf[0], data->buf[1]);

error:
	mutex_unlock(&data->lock);
	return ret;
}

static int ad5110_write(struct ad5110_data *data, u8 cmd, u8 val)
{
	int ret;

	mutex_lock(&data->lock);
	data->buf[0] = cmd;
	data->buf[1] = val;

	ret = i2c_master_send_dmasafe(data->client, data->buf, sizeof(data->buf));
	mutex_unlock(&data->lock);

	return ret < 0 ? ret : 0;
}

static int ad5110_resistor_tol(struct ad5110_data *data, u8 cmd, int val)
{
	int ret;

	ret = ad5110_read(data, cmd, &val);
	if (ret < 0)
		return ret;
	dev_err(&data->client->dev, "Raw tol val %d\n", val);
	data->tol = FIELD_GET(GENMASK(6, 3), val);
	data->tol = ((val & GENMASK(2, 0)) * 1000 / 8) + data->tol * 1000;
	data->tol = data->cfg->kohms * data->tol / 100;
	if (!(val & BIT(7)))
		data->tol *= -1;

	return 0;
}

static ssize_t ad5110_eeprom_read(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad5110_data *data = iio_priv(indio_dev);
	int val = AD5110_RESISTOR_TOL;
	int ret;

	ret = ad5110_read(data, AD5110_EEPROM_RD, &val);
	if (ret)
		return ret;
	
	return iio_format_value(buf, IIO_VAL_INT, 1, &val);
}

static ssize_t ad5110_eeprom_write(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct ad5110_data *data = iio_priv(indio_dev);
	int ret;

	ret = ad5110_write(data, AD5110_EEPROM_WR, 0);
	if (ret < 0){
		dev_err(&data->client->dev, "RDAC to EEPROM write failed\n");
		return ret;
	}
		
	msleep(20);
	return ret;
}

static IIO_DEVICE_ATTR(wiper_pos_eeprom, 0644,
		       ad5110_eeprom_read,
		       ad5110_eeprom_write, 0);

static struct attribute *ad5110_attributes[] = {
	&iio_dev_attr_wiper_pos_eeprom.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad5110_attribute_group = {
	.attrs = ad5110_attributes,
};

static int ad5110_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct ad5110_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = ad5110_read(data, AD5110_RDAC_RD, val);
		if (ret)
			return ret;

		*val = *val >> data->cfg->shift;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_OFFSET:
		*val = WIPER_RESISTANCE * data->cfg->max_pos;
		*val2 = 1000 * data->cfg->kohms + data->tol;
		return IIO_VAL_FRACTIONAL;
	case IIO_CHAN_INFO_SCALE:
		*val = 1000 * data->cfg->kohms + data->tol;
		*val2 = data->cfg->max_pos;
		return IIO_VAL_FRACTIONAL;
	case IIO_CHAN_INFO_ENABLE:
		*val = data->enable;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int ad5110_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	struct ad5110_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (val >= data->cfg->max_pos || val < 0)
			return -EINVAL;
		
		return ad5110_write(data, AD5110_RDAC_WR, val << data->cfg->shift);
	case IIO_CHAN_INFO_ENABLE:
		if (val < 0 || val > 1)
			return -EINVAL;
		if (data->enable == val)
			return 0;
		ret = ad5110_write(data, AD5110_SHUTDOWN, val);
		if (ret < 0)
			return ret;
		data->enable = val;
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct iio_info ad5110_info = {
	.read_raw = ad5110_read_raw,
	.write_raw = ad5110_write_raw,
	.attrs = &ad5110_attribute_group,
};

#define AD5110_COMPATIBLE(of_compatible, cfg) {	\
			.compatible = of_compatible,	\
			.data = &ad5110_cfg[cfg],	\
}

static const struct of_device_id ad5110_of_match[] = {
	AD5110_COMPATIBLE("adi,ad5110-10", AD5110_10),
	AD5110_COMPATIBLE("adi,ad5110-80", AD5110_80),
	AD5110_COMPATIBLE("adi,ad5112-05", AD5112_05),
	AD5110_COMPATIBLE("adi,ad5112-10", AD5112_10),
	AD5110_COMPATIBLE("adi,ad5112-80", AD5112_80),
	AD5110_COMPATIBLE("adi,ad5114-10", AD5114_10),
	AD5110_COMPATIBLE("adi,ad5114-80", AD5114_80),
	{ }
};
MODULE_DEVICE_TABLE(of, ad5110_of_match);

static const struct i2c_device_id ad5110_id[] = {
	{ "ad5110-10", AD5110_10 },
	{ "ad5110-80", AD5110_80 },
	{ "ad5112-05", AD5112_05 },
	{ "ad5112-10", AD5112_10 },
	{ "ad5112-80", AD5112_80 },
	{ "ad5114-10", AD5114_10 },
	{ "ad5114-80", AD5114_80 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ad5110_id);

static int ad5110_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct iio_dev *indio_dev;
	struct ad5110_data *data;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->client = client;
	mutex_init(&data->lock);
	data->cfg = device_get_match_data(dev);
	if (!data->cfg)
		data->cfg = &ad5110_cfg[i2c_match_id(ad5110_id, client)->driver_data];

	/* refresh RDAC register with EEPROM */
	ret = ad5110_write(data, AD5110_RESET, 0);
	if (ret < 0) {
		dev_err(dev, "Refresh RDAC with EEPROM failed\n");
		return ret;
	}

	ret = ad5110_resistor_tol(data, AD5110_EEPROM_RD, AD5110_RESISTOR_TOL);
	if (ret < 0) {
		dev_err(dev, "Read resistor tolerance failed\n");
		return ret;
	}
	dev_err(dev, "resistor tolerance %d\n", data->tol);
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->dev.parent = dev;
	indio_dev->info = &ad5110_info;
	indio_dev->channels = ad5110_channels;
	indio_dev->num_channels = ARRAY_SIZE(ad5110_channels);
	indio_dev->name = client->name;

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret < 0)
		return ret;
	dev_err(dev, "Hello from probe\n");
	return ret;
}

static struct i2c_driver ad5110_driver = {
	.driver = {
		.name	= "ad5110",
		.of_match_table = ad5110_of_match,
	},
	.probe_new	= ad5110_probe,
	.id_table	= ad5110_id,
};

module_i2c_driver(ad5110_driver);

MODULE_AUTHOR("Mugilraj Dhavachelvan <dmugil2000@gmail.com>");
MODULE_DESCRIPTION("AD5110 digital potentiometer");
MODULE_LICENSE("GPL v2");

