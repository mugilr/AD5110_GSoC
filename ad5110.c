// SPDX-License-Identifier: GPL-2.0+
/*
 * Analog Devices AD5110 digital potentiometer driver
 *
 * Copyright (C) 2021 Mugilraj Dhavachelvan <dmugil2000@gmail.com>
 *
 * Datasheet: https://www.analog.com/media/en/technical-documentation/data-sheets/AD5110_5112_5114.pdf
 *
 */

#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>

#include <linux/iio/iio.h>

/* AD5110 commands */
#define AD5110_EEPROM_WR 1
#define AD5110_RDAC_WR	 2
#define AD5110_SHUTDOWN	 3
#define AD5110_RESET	 4
#define AD5110_RDAC_RD	 5
#define AD5110_EEPROM_RD 6

/* AD5110_EEPROM_RD data */
#define AD5110_WIPER_POS    0
#define AD5110_RESISTOR_TOL 1

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
	[AD5110_10] = { .max_pos = 128 , .kohms = 10 },
	[AD5110_80] = { .max_pos = 128 , .kohms = 80 },
	[AD5112_05] = { .max_pos = 64 , .kohms = 5 , .shift = 1 },
	[AD5112_10] = { .max_pos = 64 , .kohms = 10 , .shift = 1 },
	[AD5112_80] = { .max_pos = 64 , .kohms = 80 , .shift = 1 },
	[AD5114_10] = { .max_pos = 32 , .kohms = 10 , .shift = 2 },
	[AD5114_80] = { .max_pos = 32 , .kohms = 80 , .shift = 2 },
};

struct ad5110_data {
	struct i2c_client       *client;
	s16			tol;		// resistor tolerance
	struct mutex            lock;
	const struct ad5110_cfg	*cfg;
	u8			buf[2] ____cacheline_aligned;
};

static const struct iio_chan_spec ad5110_channels[] = {
	{
		.type = IIO_RESISTANCE,
		.output = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_OFFSET),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
	}
};

static int ad5110_read(struct ad5110_data *data, int cmd, int *val)
{
	int ret;

	data->buf[0] = cmd;
	data->buf[1] = *val;

	mutex_lock(&data->lock);
	ret = i2c_master_send(data->client, data->buf, sizeof(data->buf));
	if (ret < 0)
		goto error;

	ret = i2c_master_recv(data->client, data->buf, sizeof(data->buf));
	if (ret < 0)
		goto error;

	*val = data->buf[0];
	ret = 0;

error:
	*val = 82;//210;
	mutex_unlock(&data->lock);
	ret =0;
	return ret;
}

static int ad5110_write(struct ad5110_data *data, int cmd, int val)
{
	int ret;
	data->buf[0] = cmd;
	data->buf[1] = (u8)val;
	
	mutex_lock(&data->lock);
	ret = i2c_master_send(data->client, data->buf, sizeof(data->buf));
	mutex_unlock(&data->lock);

	return ret < 0 ? ret : 0;
}

static int ad5110_resistor_tol(struct ad5110_data *data, int cmd, int val)
{
	int _tol, ret;
	ret = ad5110_read(data, cmd, &val);
	if (ret < 0)
		return -EIO;
	
	_tol = (val & 0x78) >> 3;
	data->tol = 1000 * data->cfg->kohms * (((val & 0x07) / 8) + _tol)/100;
	if (!(val & 0x80))
		data->tol *= -1;
	pr_err("%s: tol =  %d\n", module_name(THIS_MODULE), data->tol);
	return 0;
}

static int ad5110_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct ad5110_data *data = iio_priv(indio_dev);
	int ret;

	switch(mask) {
	case IIO_CHAN_INFO_RAW: 
		ret = ad5110_read(data, AD5110_RDAC_RD, val);
		*val = *val >> data->cfg->shift;
		return ret ? ret : IIO_VAL_INT;

	case IIO_CHAN_INFO_OFFSET:
		ad5110_resistor_tol(data, AD5110_EEPROM_RD, AD5110_RESISTOR_TOL); 
		*val = data->tol;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		*val = (1000 * data->cfg->kohms);
		*val2 = data->cfg->max_pos;
		return IIO_VAL_FRACTIONAL;
	}

	return -EINVAL;
}

static int ad5110_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	struct ad5110_data *data = iio_priv(indio_dev);

	if(mask != IIO_CHAN_INFO_RAW)
		return -EINVAL;

	if(val >= data->cfg->max_pos || val < 0 || val2)
		return -EINVAL;

	return ad5110_write(data, AD5110_RDAC_WR, val << data->cfg->shift);
}

static const struct iio_info ad5110_info = {
	.read_raw = ad5110_read_raw,
	.write_raw = ad5110_write_raw,
};

#define AD5110_COMPATIBLE(of_compatible, cfg) {		\
			.compatible = of_compatible,	\
			.data = &ad5110_cfg[cfg],	\
}

static const struct of_device_id ad5110_of_match[] = {
	AD5110_COMPATIBLE("adi, ad5110-10", AD5110_10 ),
	AD5110_COMPATIBLE("adi, ad5110-80", AD5110_80 ),
	AD5110_COMPATIBLE("adi, ad5112-05", AD5112_05 ),
	AD5110_COMPATIBLE("adi, ad5112-10", AD5112_10 ),
	AD5110_COMPATIBLE("adi, ad5112-80", AD5112_80 ),
	AD5110_COMPATIBLE("adi, ad5114-10", AD5114_10 ),
	AD5110_COMPATIBLE("adi, ad5114-80", AD5114_80 ),
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
	data->cfg = of_device_get_match_data(dev);
	if(!data->cfg)
		data->cfg = &ad5110_cfg[i2c_match_id(ad5110_id, client)->driver_data];
	/* refresh RDAC register with EEPROM */
	ad5110_write(data, AD5110_RESET, 0);

	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->dev.parent = dev;
	indio_dev->info = &ad5110_info;
	indio_dev->channels = ad5110_channels;
	indio_dev->num_channels = ARRAY_SIZE(ad5110_channels);
	indio_dev->name = client->name;

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret < 0){
		pr_err("%s: error from the probe side\n", module_name(THIS_MODULE));
		return ret;
	}
	pr_err("%s: Hello from the probe side\n", module_name(THIS_MODULE));
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
