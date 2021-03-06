/****************************************************************************
 *
 *   Copyright (c) 2012-2016 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/**
 * @file main.cpp
 *
 * Driver for the Invensense mpu9250 connected via I2C or SPI.
 *
 * @authors Andrew Tridgell
 *          Robert Dickenson
 *
 * based on the mpu6000 driver
 */

#include <px4_config.h>

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <px4_getopt.h>

#include <perf/perf_counter.h>
#include <systemlib/err.h>
#include <systemlib/conversions.h>

#include <board_config.h>
#include <drivers/drv_hrt.h>

#include <drivers/device/spi.h>
#include <drivers/device/ringbuffer.h>
#include <drivers/device/integrator.h>
#include <drivers/drv_accel.h>
#include <drivers/drv_gyro.h>
#include <drivers/drv_mag.h>
#include <mathlib/math/filter/LowPassFilter2p.hpp>
#include <lib/conversion/rotation.h>

#include "mpu9250.h"

#define MPU_DEVICE_PATH_ACCEL		"/dev/mpu9250_accel"
#define MPU_DEVICE_PATH_GYRO		"/dev/mpu9250_gyro"
#define MPU_DEVICE_PATH_MAG		"/dev/mpu9250_mag"

#define MPU_DEVICE_PATH_ACCEL_1		"/dev/mpu9250_accel1"
#define MPU_DEVICE_PATH_GYRO_1		"/dev/mpu9250_gyro1"
#define MPU_DEVICE_PATH_MAG_1		"/dev/mpu9250_mag1"

#define MPU_DEVICE_PATH_ACCEL_EXT	"/dev/mpu9250_accel_ext"
#define MPU_DEVICE_PATH_GYRO_EXT	"/dev/mpu9250_gyro_ext"
#define MPU_DEVICE_PATH_MAG_EXT 	"/dev/mpu9250_mag_ext"

#define MPU_DEVICE_PATH_ACCEL_EXT1	"/dev/mpu9250_accel_ext1"
#define MPU_DEVICE_PATH_GYRO_EXT1	"/dev/mpu9250_gyro_ext1"
#define MPU_DEVICE_PATH_MAG_EXT1 	"/dev/mpu9250_mag_ext1"

#define MPU_DEVICE_PATH_ACCEL_EXT2	"/dev/mpu9250_accel_ext2"
#define MPU_DEVICE_PATH_GYRO_EXT2	"/dev/mpu9250_gyro_ext2"
#define MPU_DEVICE_PATH_MAG_EXT2	"/dev/mpu9250_mag_ext2"

#define MPU_DEVICE_PATH_MPU6500_ACCEL       "/dev/mpu6500_accel"
#define MPU_DEVICE_PATH_MPU6500_GYRO        "/dev/mpu6500_gyro"
#define MPU_DEVICE_PATH_MPU6500_MAG         "/dev/mpu6500_mag"

#define MPU_DEVICE_PATH_MPU6500_ACCEL_1     "/dev/mpu6500_accel1"
#define MPU_DEVICE_PATH_MPU6500_GYRO_1      "/dev/mpu6500_gyro1"
#define MPU_DEVICE_PATH_MPU6500_MAG_1       "/dev/mpu6500_mag1"

#define MPU_DEVICE_PATH_MPU6500_ACCEL_EXT   "/dev/mpu6500_accel_ext"
#define MPU_DEVICE_PATH_MPU6500_GYRO_EXT    "/dev/mpu6500_gyro_ext"
#define MPU_DEVICE_PATH_MPU6500_MAG_EXT     "/dev/mpu6500_mag_ext"

#define MPU_DEVICE_PATH_ICM_ACCEL_EXT  "/dev/mpu9250_icm_accel_ext"
#define MPU_DEVICE_PATH_ICM_GYRO_EXT   "/dev/mpu9250_icm_gyro_ext"
#define MPU_DEVICE_PATH_ICM_MAG_EXT    "/dev/mpu9250_icm_mag_ext"

#define MPU_DEVICE_PATH_ICM_ACCEL_EXT1	"/dev/mpu9250_icm_accel_ext1"
#define MPU_DEVICE_PATH_ICM_GYRO_EXT1	"/dev/mpu9250_icm_gyro_ext1"
#define MPU_DEVICE_PATH_ICM_MAG_EXT1 	"/dev/mpu9250_icm_mag_ext1"

#define MPU_DEVICE_PATH_ICM_ACCEL_EXT2	"/dev/mpu9250_icm_accel_ext2"
#define MPU_DEVICE_PATH_ICM_GYRO_EXT2	"/dev/mpu9250_icm_gyro_ext2"
#define MPU_DEVICE_PATH_ICM_MAG_EXT2	"/dev/mpu9250_icm_mag_ext2"

/** driver 'main' command */
extern "C" { __EXPORT int mpu9250_main(int argc, char *argv[]); }

enum MPU9250_BUS {
	MPU9250_BUS_ALL = 0,
	MPU9250_BUS_I2C_INTERNAL,
	MPU9250_BUS_I2C_EXTERNAL,
	MPU9250_BUS_SPI_INTERNAL,
	MPU9250_BUS_SPI_INTERNAL2,
	MPU9250_BUS_SPI_EXTERNAL
};

/**
 * Local functions in support of the shell command.
 */
namespace mpu9250
{

/*
  list of supported bus configurations
 */

struct mpu9250_bus_option {
	enum MPU9250_BUS busid;
	const char *accelpath;
	const char *gyropath;
	const char *magpath;
	MPU9250_constructor interface_constructor;
	bool magpassthrough;
	uint8_t busnum;
	uint32_t address;
	MPU9250	*dev;
} bus_options[] = {
#if defined (USE_I2C)
#  if defined(PX4_I2C_BUS_ONBOARD) && defined(PX4_I2C_OBDEV_MPU9250)
	{ MPU9250_BUS_I2C_INTERNAL, MPU_DEVICE_PATH_ACCEL, MPU_DEVICE_PATH_GYRO, MPU_DEVICE_PATH_MAG,  &MPU9250_I2C_interface, false, PX4_I2C_BUS_ONBOARD, PX4_I2C_OBDEV_MPU9250, NULL },
	{ MPU9250_BUS_I2C_INTERNAL, MPU_DEVICE_PATH_ACCEL, MPU_DEVICE_PATH_GYRO, MPU_DEVICE_PATH_MAG,  &MPU9250_I2C_interface, false, PX4_I2C_BUS_ONBOARD, PX4_I2C_OBDEV_MPU9250, NULL },
#  endif
#  if defined(PX4_I2C_BUS_EXPANSION)
#  if defined(PX4_I2C_OBDEV_MPU9250)
	{ MPU9250_BUS_I2C_EXTERNAL, MPU_DEVICE_PATH_ACCEL_EXT, MPU_DEVICE_PATH_GYRO_EXT, MPU_DEVICE_PATH_MAG_EXT, &MPU9250_I2C_interface, false, PX4_I2C_BUS_EXPANSION, PX4_I2C_OBDEV_MPU9250, NULL },
	{ MPU9250_BUS_I2C_EXTERNAL, MPU_DEVICE_PATH_ACCEL_EXT, MPU_DEVICE_PATH_GYRO_EXT, MPU_DEVICE_PATH_MAG_EXT, &MPU9250_I2C_interface, false, PX4_I2C_BUS_EXPANSION, PX4_I2C_OBDEV_MPU9250, NULL },
#  endif
	{ MPU9250_BUS_I2C_EXTERNAL, MPU_DEVICE_PATH_ICM_ACCEL_EXT, MPU_DEVICE_PATH_ICM_GYRO_EXT, MPU_DEVICE_PATH_ICM_MAG_EXT, &MPU9250_I2C_interface, false, PX4_I2C_BUS_EXPANSION, PX4_I2C_EXT_ICM20948_1, NULL },
#endif
#  if defined(PX4_I2C_BUS_EXPANSION1) && defined(PX4_I2C_OBDEV_MPU9250)
	{ MPU9250_BUS_I2C_EXTERNAL, MPU_DEVICE_PATH_ACCEL_EXT1, MPU_DEVICE_PATH_GYRO_EXT1, MPU_DEVICE_PATH_MAG_EXT1, &MPU9250_I2C_interface, false, PX4_I2C_BUS_EXPANSION1, PX4_I2C_OBDEV_MPU9250, NULL },
#  endif
#  if defined(PX4_I2C_BUS_EXPANSION2) && defined(PX4_I2C_OBDEV_MPU9250)
	{ MPU9250_BUS_I2C_EXTERNAL, MPU_DEVICE_PATH_ACCEL_EXT2, MPU_DEVICE_PATH_GYRO_EXT2, MPU_DEVICE_PATH_MAG_EXT2, &MPU9250_I2C_interface, false, PX4_I2C_BUS_EXPANSION2, PX4_I2C_OBDEV_MPU9250, NULL },
#  endif
#endif
#ifdef PX4_SPIDEV_MPU
	{ MPU9250_BUS_SPI_INTERNAL, MPU_DEVICE_PATH_ACCEL, MPU_DEVICE_PATH_GYRO, MPU_DEVICE_PATH_MAG, &MPU9250_SPI_interface, true, PX4_SPI_BUS_SENSORS, PX4_SPIDEV_MPU, NULL },
	{ MPU9250_BUS_SPI_INTERNAL, MPU_DEVICE_PATH_MPU6500_ACCEL, MPU_DEVICE_PATH_MPU6500_GYRO, MPU_DEVICE_PATH_MPU6500_MAG, &MPU9250_SPI_interface, true, PX4_SPI_BUS_SENSORS, PX4_SPIDEV_MPU, NULL },
#endif
#ifdef PX4_SPIDEV_MPU2
	{ MPU9250_BUS_SPI_INTERNAL2, MPU_DEVICE_PATH_ACCEL_1, MPU_DEVICE_PATH_GYRO_1, MPU_DEVICE_PATH_MAG_1, &MPU9250_SPI_interface, true, PX4_SPI_BUS_SENSORS, PX4_SPIDEV_MPU2, NULL },
	{ MPU9250_BUS_SPI_INTERNAL2, MPU_DEVICE_PATH_MPU6500_ACCEL_1, MPU_DEVICE_PATH_MPU6500_GYRO_1, MPU_DEVICE_PATH_MPU6500_MAG_1, &MPU9250_SPI_interface, true, PX4_SPI_BUS_SENSORS, PX4_SPIDEV_MPU2, NULL },
#endif
#if defined(PX4_SPI_BUS_EXT) && defined(PX4_SPIDEV_EXT_MPU)
	{ MPU9250_BUS_SPI_EXTERNAL, MPU_DEVICE_PATH_ACCEL_EXT, MPU_DEVICE_PATH_GYRO_EXT, MPU_DEVICE_PATH_MAG_EXT, &MPU9250_SPI_interface, true, PX4_SPI_BUS_EXT, PX4_SPIDEV_EXT_MPU, NULL },
	{ MPU9250_BUS_SPI_EXTERNAL, MPU_DEVICE_PATH_MPU6500_ACCEL_EXT, MPU_DEVICE_PATH_MPU6500_GYRO_EXT, MPU_DEVICE_PATH_MPU6500_MAG_EXT, &MPU9250_SPI_interface, true, PX4_SPI_BUS_EXT, PX4_SPIDEV_EXT_MPU, NULL },
#endif
};

#define NUM_BUS_OPTIONS (sizeof(bus_options)/sizeof(bus_options[0]))


void	start(enum MPU9250_BUS busid, enum Rotation rotation, bool external_bus, bool magnetometer_only);
bool	start_bus(struct mpu9250_bus_option &bus, enum Rotation rotation, bool external_bus, bool magnetometer_only);
struct mpu9250_bus_option &find_bus(enum MPU9250_BUS busid);
void	stop(enum MPU9250_BUS busid);
void	reset(enum MPU9250_BUS busid);
void	info(enum MPU9250_BUS busid);
void	usage();

/**
 * find a bus structure for a busid
 */
struct mpu9250_bus_option &find_bus(enum MPU9250_BUS busid)
{
	for (uint8_t i = 0; i < NUM_BUS_OPTIONS; i++) {
		if ((busid == MPU9250_BUS_ALL ||
		     busid == bus_options[i].busid) && bus_options[i].dev != NULL) {
			return bus_options[i];
		}
	}

	errx(1, "bus %u not started", (unsigned)busid);
}

/**
 * start driver for a specific bus option
 */
bool
start_bus(struct mpu9250_bus_option &bus, enum Rotation rotation, bool external, bool magnetometer_only)
{
	int fd = -1;

	PX4_INFO("Bus probed: %d", bus.busid);

	if (bus.dev != nullptr) {
		warnx("%s SPI not available", external ? "External" : "Internal");
		return false;
	}

	device::Device *interface = bus.interface_constructor(bus.busnum, bus.address, external);

	if (interface == nullptr) {
		warnx("no device on bus %u", (unsigned)bus.busid);
		return false;
	}

	if (interface->init() != OK) {
		delete interface;
		warnx("no device on bus %u", (unsigned)bus.busid);
		return false;
	}

	device::Device *mag_interface = nullptr;

#ifdef USE_I2C
	/* For i2c interfaces, connect to the magnetomer directly */
	bool is_i2c = bus.busid == MPU9250_BUS_I2C_INTERNAL || bus.busid == MPU9250_BUS_I2C_EXTERNAL;

	if (is_i2c) {
		mag_interface = AK8963_I2C_interface(bus.busnum, external);
	}

#endif

	bus.dev = new MPU9250(interface, mag_interface, bus.accelpath, bus.gyropath, bus.magpath, rotation,
			      magnetometer_only);

	if (bus.dev == nullptr) {
		delete interface;

		if (mag_interface != nullptr) {
			delete mag_interface;
		}

		return false;
	}

	if (OK != bus.dev->init()) {
		goto fail;
	}

	fd = open(bus.accelpath, O_RDONLY);

	if (fd < 0) {
		PX4_INFO("ioctl failed");
		goto fail;
	}

	if (ioctl(fd, SENSORIOCSPOLLRATE, SENSOR_POLLRATE_DEFAULT) < 0) {
		goto fail;
	}


	close(fd);

	return true;

fail:

	if (fd >= 0) {
		close(fd);
	}

	if (bus.dev != nullptr) {
		delete (bus.dev);
		bus.dev = nullptr;
	}

	errx(1, "driver start failed");
}

/**
 * Start the driver.
 *
 * This function only returns if the driver is up and running
 * or failed to detect the sensor.
 */
void
start(enum MPU9250_BUS busid, enum Rotation rotation, bool external, bool magnetometer_only)
{

	bool started = false;

	for (unsigned i = 0; i < NUM_BUS_OPTIONS; i++) {
		if (busid == MPU9250_BUS_ALL && bus_options[i].dev != NULL) {
			// this device is already started
			continue;
		}

		if (busid != MPU9250_BUS_ALL && bus_options[i].busid != busid) {
			// not the one that is asked for
			continue;
		}

		started |= start_bus(bus_options[i], rotation, external, magnetometer_only);

		if (started) { break; }
	}

	exit(started ? 0 : 1);

}

void
stop(enum MPU9250_BUS busid)
{
	struct mpu9250_bus_option &bus = find_bus(busid);


	if (bus.dev != nullptr) {
		delete bus.dev;
		bus.dev = nullptr;

	} else {
		/* warn, but not an error */
		warnx("already stopped.");
	}

	exit(0);
}

/**
 * Reset the driver.
 */
void
reset(enum MPU9250_BUS busid)
{
	struct mpu9250_bus_option &bus = find_bus(busid);
	int fd = open(bus.accelpath, O_RDONLY);

	if (fd < 0) {
		err(1, "failed ");
	}

	if (ioctl(fd, SENSORIOCRESET, 0) < 0) {
		err(1, "driver reset failed");
	}

	if (ioctl(fd, SENSORIOCSPOLLRATE, SENSOR_POLLRATE_DEFAULT) < 0) {
		err(1, "driver poll restart failed");
	}

	close(fd);

	exit(0);
}

/**
 * Print a little info about the driver.
 */
void
info(enum MPU9250_BUS busid)
{
	struct mpu9250_bus_option &bus = find_bus(busid);


	if (bus.dev == nullptr) {
		errx(1, "driver not running");
	}

	printf("state @ %p\n", bus.dev);
	bus.dev->print_info();

	exit(0);
}

void
usage()
{
	PX4_INFO("missing command: try 'start', 'info', 'test', 'stop',\n'reset', 'regdump', 'testerror'");
	PX4_INFO("options:");
	PX4_INFO("    -X    (i2c external bus)");
	PX4_INFO("    -I    (i2c internal bus)");
	PX4_INFO("    -s    (spi internal bus)");
	PX4_INFO("    -S    (spi external bus)");
	PX4_INFO("    -t    (spi internal bus, 2nd instance)");
	PX4_INFO("    -R rotation");
	PX4_INFO("    -M only enable magnetometer, accel/gyro disabled - not av. on MPU6500");
}

} // namespace

int
mpu9250_main(int argc, char *argv[])
{
	int myoptind = 1;
	int ch;
	const char *myoptarg = nullptr;

	enum MPU9250_BUS busid = MPU9250_BUS_ALL;
	enum Rotation rotation = ROTATION_NONE;
	bool magnetometer_only = false;

	while ((ch = px4_getopt(argc, argv, "XISstMR:", &myoptind, &myoptarg)) != EOF) {
		switch (ch) {
		case 'X':
			busid = MPU9250_BUS_I2C_EXTERNAL;
			break;

		case 'I':
			busid = MPU9250_BUS_I2C_INTERNAL;
			break;

		case 'S':
			busid = MPU9250_BUS_SPI_EXTERNAL;
			break;

		case 's':
			busid = MPU9250_BUS_SPI_INTERNAL;
			break;

		case 't':
			busid = MPU9250_BUS_SPI_INTERNAL2;
			break;

		case 'R':
			rotation = (enum Rotation)atoi(myoptarg);
			break;

		case 'M':
			magnetometer_only = true;
			break;

		default:
			mpu9250::usage();
			return 0;
		}
	}

	if (myoptind >= argc) {
		mpu9250::usage();
		return -1;
	}

	bool external = busid == MPU9250_BUS_I2C_EXTERNAL || busid == MPU9250_BUS_SPI_EXTERNAL;
	const char *verb = argv[myoptind];

	/*
	 * Start/load the driver.
	 */
	if (!strcmp(verb, "start")) {
		mpu9250::start(busid, rotation, external, magnetometer_only);
	}

	if (!strcmp(verb, "stop")) {
		mpu9250::stop(busid);
	}

	/*
	 * Reset the driver.
	 */
	if (!strcmp(verb, "reset")) {
		mpu9250::reset(busid);
	}

	/*
	 * Print driver information.
	 */
	if (!strcmp(verb, "info")) {
		mpu9250::info(busid);
	}

	mpu9250::usage();
	return 0;
}
