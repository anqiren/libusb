/*
 * libusb example program to verify data integrity using loopback firmware
 * Copyright (C) 2023 Sylvain Fasel <sylvain@sonatique.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <process.h>

#include "libusb.h"
#include "libusbi.h"

#define EP_DATA_IN	0x81
#define EP_DATA_OUT	0x1

static volatile sig_atomic_t do_exit = 0;
static struct libusb_device_handle *devh = NULL;

static uint64_t expectedValue = 0;

static void LIBUSB_CALL cb_xfr(struct libusb_transfer *xfr)
{
	if (xfr->status != LIBUSB_TRANSFER_COMPLETED) {
		fprintf(stderr, "transfer status %d\n", xfr->status);
		libusb_free_transfer(xfr);
		exit(3);
	}

	if(xfr -> actual_length % 8 != 0)
	{
		fprintf(stderr, "transfer actual_length is not a multiple of 8, but is %d\n", xfr->actual_length);
		libusb_free_transfer(xfr);
		exit(3);
	}

	const uint64_t* ulongPtr = (uint64_t*)xfr->buffer;

	fprintf(stdout, "transfer actual_length = %d (firstValue = %llu) %s\n", 
		xfr->actual_length, 
		*ulongPtr, 
		expectedValue == 0 && *ulongPtr != expectedValue ? "[ignored]" : "");

	for(int i = 0; i < xfr->actual_length / 8; ++i)
	{
		if(*(ulongPtr + i) != expectedValue)
		{
			if(expectedValue != 0)
			{
				fprintf(stderr, "Integrity error! Expected: %llu but got: %llu\n", expectedValue, *(ulongPtr + i));
				exit(3);
			}
		}
		else
		{
			++expectedValue;
		}
	}

	if (libusb_submit_transfer(xfr) < 0) {
		fprintf(stderr, "error re-submitting URB\n");
		exit(1);
	}
}

#define OK_ReadBufferSize (65536)
#define NotOK_ReadBufferSize (2*65536)

static int start_transfer_in()
{
	static uint8_t buf[NotOK_ReadBufferSize];
	static struct libusb_transfer *xfr;
	
	xfr = libusb_alloc_transfer(0);
	if (!xfr) {
		errno = ENOMEM;
		return -1;
	}

	libusb_fill_bulk_transfer(xfr, devh, EP_DATA_IN, buf, sizeof(buf), cb_xfr, NULL, 0);

	return libusb_submit_transfer(xfr);
}

#define WriteBufferSize (65536)
static uint64_t current_write_value = 0;

static void write_data(int ulongCount)
{
	if(ulongCount > WriteBufferSize / 8)
	{
		fprintf(stderr, "ulongCount value larger than permitted by byte buffer size\n");
		exit(3);
	}

	static uint8_t buf[WriteBufferSize];
	static uint64_t* ulongPtr = (uint64_t*)buf;

	for(int i = 0; i < ulongCount; ++i)
	{
		*(ulongPtr + i) = current_write_value++;
	}

	int actualLength;
	int rc = libusb_bulk_transfer(devh, EP_DATA_OUT, buf, ulongCount*8, &actualLength, 2000);
	if(rc != LIBUSB_SUCCESS || actualLength != ulongCount*8)
	{
		fprintf(stderr, "Writing data failed: %s\n", libusb_error_name(rc));
		exit(3);
	}
}

static unsigned continuous_write(void * arg)
{
	UNUSED(arg);

	while (!do_exit) {
		write_data(1000);
	}

	return 0;
}

static void sig_hdlr(int signum)
{
	(void)signum;

	do_exit = 1;
}

int main(void)
{
	int rc;

	(void)signal(SIGINT, sig_hdlr);

	rc = libusb_init_context(/*ctx=*/NULL, /*options=*/NULL, /*num_options=*/0);
	if (rc < 0) {
		fprintf(stderr, "Error initializing libusb: %s\n", libusb_error_name(rc));
		exit(1);
	}

	devh = libusb_open_device_with_vid_pid(NULL, 0x04b4, 0x00F0);
	if (!devh) {
		fprintf(stderr, "Error finding USB device\n");
		goto out;
	}

	rc = libusb_claim_interface(devh, 0);
	if (rc < 0) {
		fprintf(stderr, "Error claiming interface: %s\n", libusb_error_name(rc));
		goto out;
	}

	start_transfer_in();

	_beginthreadex(NULL, 0, continuous_write, NULL, 0, NULL);

	while (!do_exit) {
		rc = libusb_handle_events(NULL);
		if (rc != LIBUSB_SUCCESS)
			break;
	}

	libusb_release_interface(devh, 0);
out:
	if (devh)
		libusb_close(devh);
	libusb_exit(NULL);
	return rc;
}
