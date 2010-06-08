/*
 * Copyright (C) 2006, Intel Corporation
 * Copyright (C) 2010 Canonical
 *
 * This code was originally part of the Linux-ready Firmware Developer Kit
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "fwts.h"

static fwts_list *klog;

#define HPET_REG_SIZE  (0x400)
#define MAX_CLK_PERIOD (100000000)

static uint64	hpet_base_p = 0;
static void     *hpet_base_v = 0;

#if 0
/* check_hpet_base_hpet() -- used to parse the HPET Table for HPET base info */
static void check_hpet_base_hpet(void)
{
        unsigned long address = 0;
        unsigned long size = 0;
	struct hpet_table *table;
	char *table_ptr;

	if (locate_acpi_table("HPET", &address, &size))
		return;

        if (address == 0 || address == -1) 
                return;

        table = (struct hpet_table *) address;

	hpet_base_p = table->base_address.address;
	free((void *) address);
}
#endif

/* check_hpet_base_dsdt() -- used to parse the DSDT for HPET base info */
static void hpet_check_base_acpi_table(fwts_framework *fw, char *table, int which)
{
	char *val, *idx;
	int hpet_found = 0;

	fwts_list *output;
	fwts_list_element *item;

	output = fwts_iasl_disassemble(fw, table, which);
	if (output == NULL)
		return;

	for (item = output->head; item != NULL; item = item->next) {
		if (!hpet_found) {
			if (strstr(fwts_text_list_text(item), "Device (HPET)") != NULL)
				hpet_found = 1;
		} else {
			/* HPET section is found, looking for base */
			val = strstr(fwts_text_list_text(item), "0x");
			if (val != NULL) {
				uint64 address_base;
				idx = index(val, ',');
				if (idx)
					*idx = '\0';

				address_base = strtoul(val, NULL, 0x10);

				if (hpet_base_p != 0) {
					if (hpet_base_p != address_base)
						fwts_framework_failed(fw, 
			     				"Mismatched HPET base between %s (%lx) and the kernel (%lx)",
							table,
			     				(unsigned long)hpet_base_p, (unsigned long)address_base);
					else
						fwts_framework_passed(fw,
							"HPET base matches that between %s and the kernel (%lx)",
							table,
							(unsigned long)hpet_base_p);
					break;
				}
				/*hpet_base_p = address_base;*/
				hpet_found = 0;
			}
		}
	}
	fwts_text_list_free(output);
}


static int hpet_check_init(fwts_framework *fw)
{
	if (fwts_check_root_euid(fw))
		return 1;

	if ((klog = fwts_klog_read()) == NULL) {
		fwts_log_error(fw, "Cannot read kernel log");
		return 1;
	}
	return 0;
}

static int hpet_check_deinit(fwts_framework *fw)
{
	if (klog)
		fwts_text_list_free(klog);

	return 0;
}

static char *hpet_check_headline(void)
{
	return "HPET configuration test";
}

static int hpet_check_test1(fwts_framework *fw)
{
	if (klog == NULL)
		return 1;

	fwts_list_element *item;

	fwts_log_info(fw,
		   "This test checks the HPET PCI BAR for each timer block in the timer.\n"
		   "The base address is passed by the firmware via an ACPI table.\n"
		   "IRQ routing and initialization is also verified by the test.");

	for (item = klog->head; item != NULL; item = item->next) {
		if ((strstr(fwts_text_list_text(item), "ACPI: HPET id:")) != NULL) {
			char *txt = strstr(fwts_text_list_text(item), "base: ");
			if (txt)
				hpet_base_p = strtoul(txt+6,  NULL, 0x10);
			fwts_log_warning(fw, "HPET driver in the kernel is enabled, inaccurate results follow");
			fwts_framework_passed(fw, "Found HPET base %x in kernel log\n", hpet_base_p);
			break;
		}
	}

	return 0;
}

static int hpet_check_test2(fwts_framework *fw)
{
	int fd;
	uint64 hpet_id;
	uint32 clk_period;

	if ((fd = open("/dev/mem", O_RDONLY)) < 0) {
		fwts_log_error(fw, "Cannot open /dev/mem");
		return 1;
	}
	hpet_base_v = 
	    mmap(NULL, HPET_REG_SIZE, PROT_READ, MAP_SHARED, fd,
		 hpet_base_p);

	if (hpet_base_v == NULL) {
		fwts_log_error(fw, "Cannot mmap to /dev/mem");
		return 1;
	}

	hpet_id = *(uint64*) hpet_base_v;

	fwts_log_info(fw, "HPET found, VendorID is: %04X", ((hpet_id & 0xffff0000) >> 16));

	clk_period = hpet_id >> 32;
	if ((clk_period > MAX_CLK_PERIOD) || (clk_period == 0))
		fwts_framework_failed(fw, "Invalid clock period %li, must be non-zero and less than 10^8 fs", clk_period);
	else
		fwts_framework_passed(fw, "Valid clock period %li", clk_period);

	return 0;
}

static int hpet_check_test3(fwts_framework *fw)
{
	int i;

	hpet_check_base_acpi_table(fw, "DSDT", 0);
	for (i=0;i<11;i++) {
		hpet_check_base_acpi_table(fw, "SSDT", i);
	}
	return 0;
}

static fwts_framework_tests hpet_check_tests[] = {
	hpet_check_test1,
	hpet_check_test2,
	hpet_check_test3,
	NULL
};

static fwts_framework_ops hpet_check_ops = {
	hpet_check_headline,
	hpet_check_init,
	hpet_check_deinit,
	hpet_check_tests
};

FRAMEWORK(hpet_check, &hpet_check_ops, TEST_ANYTIME);
