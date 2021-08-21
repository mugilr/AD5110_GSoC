# GSoC’21: Linux IIO Driver for AD5110 DigiPot

## Organization 
- The Linux Foundation

## Student
 - Mugilraj D

## Mentors
- Darius Berghe
- Dragos Bogdan

## Project Summary

The project was to build a linux IIO driver for the AD5110 digital potentiometer and test it on hardware and send the code upstream. 
The driver support to read and write functionality to RDAC register which determines the wiper position and also it supports storing the wiper position to EEPROM memory for subsequent power-ups. 
This driver has the capability of auto-configuring of scale and offset based on the tolerance of each device this will give you accurate values. 

## Driver testing

Locate the device in your sysfs tree. This is probably easiest by going into the `iio` directory of sysfs tree or go to common `i2c` directory and locating the device by the i2c slave address.

```
root@analog:~# cat /sys/bus/iio/devices/iio\:device0/name 
ad5110-10

root@analog:~# cat /sys/bus/i2c/devices/1-002f/iio\:device0/name 
ad5110-10
``` 
Listing various attributes, IIO channels, etc. of AD5110 IIO device.
```
root@analog:~# ls /sys/bus/iio/devices/iio\:device0/
dev	 out_resistance_en	out_resistance_scale  uevent
name	 out_resistance_offset	power		      store_eeprom
of_node  out_resistance_raw	subsystem
```
- `out_resistance_en` \
Writing '1' to enable the device and '0' to disable the device
```
root@analog:/sys/bus/iio/devices/iio:device0# echo 0 > out_resistance_en #disable
root@analog:/sys/bus/iio/devices/iio:device0# cat out_resistance_en 
0
root@analog:/sys/bus/iio/devices/iio:device0# echo 1 > out_resistance_en #enable
root@analog:/sys/bus/iio/devices/iio:device0# cat out_resistance_en 
1
```
- `out_resistance_raw` \
Read and write the wiper position to RDAC register.
```
root@analog:/sys/bus/iio/devices/iio:device0# echo 64 > out_resistance_raw 
root@analog:/sys/bus/iio/devices/iio:device0# cat out_resistance_raw 
64
root@analog:/sys/bus/iio/devices/iio:device0# echo 120 > out_resistance_raw 
root@analog:/sys/bus/iio/devices/iio:device0# cat out_resistance_raw 
120
```
- `out_resistance_offset` \
Read the offset value.
```
root@analog:/sys/bus/iio/devices/iio:device0# cat out_resistance_offset 
0.884938271
```
- `out_resistance_scale` \
Read the scale value.
```
root@analog:/sys/bus/iio/devices/iio:device0# cat out_resistance_scale 
79.101562500
```
- `store_eeprom` \
writing '1' to save the current wiper position from RDAC register to EEPROM memory. After power-up, the device will automatically load the saved configuration to RDAC. Read to find the saved wiper position.
```
root@analog:/sys/bus/iio/devices/iio:device0# cat out_resistance_raw 
120
root@analog:/sys/bus/iio/devices/iio:device0# cat store_eeprom 
64
root@analog:/sys/bus/iio/devices/iio:device0# echo  1 > store_eeprom 
root@analog:/sys/bus/iio/devices/iio:device0# cat store_eeprom 
120
```
## Patches

### ADI's Linux Tree `gsoc2021` branch
 1. PR - [**Skeleton driver for AD5110 Digital potentiometer**](https://github.com/analogdevicesinc/linux/pull/1571) \
  Adds a basic driver skeleton for AD5110 (single channel, a Nonvolatile Digital Potentiometer). Along with the driver, this PR includes a device-tree overlay for AD5110 to use with Raspberry Pi 3 b+ and adds it to the device-tree's list of trivial devices. \
 Consists of commits as follows -
    - [dt-bindings: iio: potentiometer: Add Ad5110 in trivial-devices](https://github.com/analogdevicesinc/linux/pull/1571/commits/a7b884450d715ba44097e1410e1902e661d8f23e)
    - [iio: potentiometer: Add dt-overlay for AD5110](https://github.com/analogdevicesinc/linux/pull/1571/commits/a36b02448dbb043d70126bddcd24acfc70c91a6d)
    - [iio: potentiometer: Add driver support for AD5110](https://github.com/analogdevicesinc/linux/pull/1571/commits/1b1a13a88e64b71c4be3cacc5a625766d46f1b3c)
 2. PR - [**iio: potentiometer: Add channel support to AD5110**](https://github.com/analogdevicesinc/linux/pull/1595) \
Add the channel, read_raw(), and write_raw() functions support to the
ad5110 driver with configured offset and scale. And added EEPROM support
to hold the RDAC value. \
Consists of commits as follows -
    - [iio: potentiometer: Add channel support to AD5110](https://github.com/analogdevicesinc/linux/pull/1595/commits/a1bf21ea721800f09823386fcfcb799807016a49)

### Linux IIO subsystem tree [upstream] 
1. [[Patch v1] iio: potentiometer: Add driver support for AD5110](https://lore.kernel.org/linux-iio/20210807050900.10075-1-dmugil2000@gmail.com/)
2. [[Patch v2] iio: potentiometer: Add driver support for AD5110](https://lore.kernel.org/linux-iio/20210809075745.160042-1-dmugil2000@gmail.com/)
3. [[Patch v3] iio: potentiometer: Add driver support for AD5110](https://lore.kernel.org/linux-iio/20210814175607.48399-1-dmugil2000@gmail.com/)

## Acknowledgement
I would like to thank [Darius Berghe](https://github.com/buha) and [Dragos Bogdan](https://github.com/dbogdan) for providing guidance and support throughout my project and also for sponsoring hardware. I would also like to thank [Nuno Sá](https://github.com/nunojsa) and other ADI members for providing suggestions and reviewing the code. Also thanks to Lars, Andy, and Jonathan for helping me to get accept my patch in IIO mailing list and to upstream it. I also like to thank my fellow participants Lucas and Puranjay for sharing their knowledge and their project..
