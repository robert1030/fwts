/*
 * Copyright (C) 2015-2020 Canonical
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
#include "fwts.h"

#if defined(FWTS_HAS_ACPI)

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <string.h>

static fwts_acpi_table_info *table;

static int xsdt_init(fwts_framework *fw)
{
	if (fwts_acpi_find_table(fw, "XSDT", 0, &table) != FWTS_OK) {
		fwts_log_error(fw, "Cannot read ACPI tables.");
		return FWTS_ERROR;
	}
	if (table == NULL || (table && table->length == 0)) {
		if (fw->flags & FWTS_FLAG_TEST_SBBR) {
			fwts_log_error(fw, "ACPI XSDT table does not exist");
			return FWTS_ERROR;
		} else {
			fwts_log_error(fw, "ACPI XSDT table does not exist, skipping test");
			return FWTS_SKIP;
		}
	}
	return FWTS_OK;
}

/*
 *  XSDT Extended System Description Table
 */
static int xsdt_test1(fwts_framework *fw)
{
	fwts_acpi_table_xsdt *xsdt = (fwts_acpi_table_xsdt*)table->data;
	size_t i, n;
	bool passed = true;

	n = (table->length - sizeof(fwts_acpi_table_header)) / sizeof(uint64_t);
	for (i = 0; i < n; i++) {
		if (xsdt->entries[i] == 0) {
			passed = false;
			if (fw->flags & FWTS_FLAG_TEST_SBBR) {
				fwts_failed(fw, LOG_LEVEL_CRITICAL,
					"XSDTEntryNull",
					"XSDT Entry %zd is null, should not be non-zero.", i);
			}
			else {
				fwts_failed(fw, LOG_LEVEL_MEDIUM,
					"XSDTEntryNull",
					"XSDT Entry %zd is null, should not be non-zero.", i);
			}
			fwts_advice(fw,
				"A XSDT pointer is null and therefore erroneously "
				"points to an invalid 64 bit ACPI table header. "
				"At worse this will cause the kernel to oops, at "
				"best the kernel may ignore this.  However, it "
				"should be fixed where possible.");
		}
	}
	if (passed) {
		if (fw->flags & FWTS_FLAG_TEST_SBBR) {
			fwts_passed(fw, "XSDT is present, pointed at by XsdrAddress=0x%" PRIx64
				" and contain valid pointers to %d other ACPI tables mandated by SBBR",
				 xsdt->entries[0], (int)n);
		} else
			fwts_passed(fw, "No issues found in XSDT table.");
	}

	return FWTS_OK;
}

static fwts_framework_minor_test xsdt_tests[] = {
	{ xsdt_test1, "XSDT Extended System Description Table test." },
	{ NULL, NULL }
};

static fwts_framework_ops xsdt_ops = {
	.description = "XSDT Extended System Description Table test.",
	.init        = xsdt_init,
	.minor_tests = xsdt_tests
};

FWTS_REGISTER("xsdt", &xsdt_ops, FWTS_TEST_ANYTIME, FWTS_FLAG_BATCH | FWTS_FLAG_TEST_ACPI | FWTS_FLAG_TEST_SBBR)

#endif
