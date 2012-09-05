/*
 * Copyright (C) 2010-2012 Canonical
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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

#include "fwts.h"
#include "fwts_uefi.h"

/* Old sysfs uefi packed binary blob variables */
typedef struct {
	uint16_t	varname[512];
	uint8_t		guid[16];
	uint64_t	datalen;
	uint8_t		data[1024];
	uint64_t	status;
	uint32_t	attributes;
} __attribute__((packed)) fwts_uefi_sys_fs_var;

/* New efifs variable */
typedef struct {
	uint32_t	attributes;
	uint8_t		data[0];	/* variable length, depends on file size */
} __attribute__((packed)) fwts_uefi_efivars_fs_var;

/* Which interface are we using? */
#define UEFI_IFACE_UNKNOWN 		(0)	/* Not yet known */
#define UEFI_IFACE_NONE			(1)	/* None found */
#define UEFI_IFACE_SYSFS		(2)	/* sysfs */
#define UEFI_IFACE_EFIVARS		(3)	/* efivar fs */

/* File system magic numbers */
#define EFIVARS_FS_MAGIC	0x6165676C
#define SYS_FS_MAGIC		0x62656572

/*
 *  fwts_uefi_get_interface()
 *	find which type of EFI variable file system we are using,
 *	sets path to the name of the file system path, returns
 *	the file system interface (if found).
 */
static int fwts_uefi_get_interface(char **path)
{
	static int  efivars_interface = UEFI_IFACE_UNKNOWN;
	static char efivar_path[4096];

	FILE *fp;
	char fstype[1024];
	struct statfs statbuf;

	if (path == NULL)	/* Sanity check */
		return FWTS_ERROR;

	/* Already discovered, return the cached values */
	if (efivars_interface != UEFI_IFACE_UNKNOWN) {
		*path = efivar_path;
		return efivars_interface;
	}

	*efivar_path = '\0';	/* Assume none */

	/* Scan mounts to see if sysfs or efivar fs is somewhere else */
	if ((fp = fopen("/proc/mounts", "r")) != NULL) {
		while (!feof(fp)) {
			char mount[4096];

			if (fscanf(fp, "%*s %4095s %1023s", mount, fstype) == 2) {
				/* Always try to find the newer interface first */
				if (!strcmp(fstype, "efivars")) {
					strcpy(efivar_path, mount);
					break;
				}
				/* Failing that, look for sysfs, but don't break out
				   the loop as we need to keep on searching for efivar fs */
				if (!strcmp(fstype, "sysfs"))
					strcpy(efivar_path, "/sys/firmware/efi/vars");
			}
		}
	}
	fclose(fp);

	*path = NULL;

	/* No mounted path found, bail out */
	if (*efivar_path == '\0') {
		efivars_interface = UEFI_IFACE_NONE;
		return UEFI_IFACE_NONE;
	}

	/* Can't stat it, bail out */
	if (statfs(efivar_path, &statbuf) < 0) {
		efivars_interface = UEFI_IFACE_NONE;
		return UEFI_IFACE_NONE;
	};

	/* We've now found a valid file system we can use */
	*path = efivar_path;

	if (statbuf.f_type == EFIVARS_FS_MAGIC) {
		efivars_interface = UEFI_IFACE_EFIVARS;
		return UEFI_IFACE_EFIVARS;
	}

	if (statbuf.f_type == SYS_FS_MAGIC) {
		efivars_interface = UEFI_IFACE_SYSFS;
		return UEFI_IFACE_EFIVARS;
	}

	return UEFI_IFACE_UNKNOWN;
}

/*
 *  fwts_uefi_str_to_str16()
 *	convert 8 bit C string to 16 bit sring.
 */
void fwts_uefi_str_to_str16(uint16_t *dst, const size_t len, const char *src)
{
	size_t i = len;

	while ((*src) && (i > 1)) {
		*dst++ = *src++;
		i--;
	}
	*dst = '\0';
}

/*
 *  fwts_uefi_str16_to_str()
 *	convert 16 bit string to 8 bit C string.
 */
void fwts_uefi_str16_to_str(char *dst, const size_t len, const uint16_t *src)
{
	size_t i = len;

	while ((*src) && (i > 1)) {
		*dst++ = *(src++) & 0xff;
		i--;
	}
	*dst = '\0';
}

/*
 *  fwts_uefi_str16len()
 *	16 bit version of strlen()
 */
size_t fwts_uefi_str16len(const uint16_t *str)
{
	int i;

	for (i=0; *str; i++, str++)
		;
	return i;
}

/*
 *  fwts_uefi_get_varname()
 *	fetch the UEFI variable name in terms of a 8 bit C string
 */
void fwts_uefi_get_varname(char *varname, const size_t len, const fwts_uefi_var *var)
{
	fwts_uefi_str16_to_str(varname, len, var->varname);
}

/*
 *  fwts_uefi_get_variable_sys_fs()
 *	fetch a UEFI variable given its name, via sysfs
 */
static int fwts_uefi_get_variable_sys_fs(const char *varname, fwts_uefi_var *var, char *path)
{
	int  fd;
	char filename[PATH_MAX];
	fwts_uefi_sys_fs_var	uefi_sys_fs_var;

	memset(var, 0, sizeof(fwts_uefi_var));

	snprintf(filename, sizeof(filename), "%s/%s/raw_var", path, varname);

	if ((fd = open(filename, O_RDONLY)) < 0)
		return FWTS_ERROR;

	memset(&uefi_sys_fs_var, 0, sizeof(uefi_sys_fs_var));

	/* Read the raw fixed sized data */
	if (read(fd, &uefi_sys_fs_var, sizeof(uefi_sys_fs_var)) != sizeof(uefi_sys_fs_var)) {
		close(fd);
		return FWTS_ERROR;
	}
	close(fd);

	/* Sanity check datalen is OK */
	if (uefi_sys_fs_var.datalen > sizeof(uefi_sys_fs_var.data))
		return FWTS_ERROR;

	/* Allocate space for the variable name */
	var->varname = calloc(1, sizeof(uefi_sys_fs_var.varname));
	if (var->varname == NULL)
		return FWTS_ERROR;

	/* Allocate space for the data */
	var->data = calloc(1, (size_t)uefi_sys_fs_var.datalen);
	if (var->data == NULL) {
		free(var->varname);
		return FWTS_ERROR;
	}

	/* And copy the data */
	memcpy(var->varname, uefi_sys_fs_var.varname, sizeof(uefi_sys_fs_var.varname));
	memcpy(var->data, uefi_sys_fs_var.data, uefi_sys_fs_var.datalen);
	memcpy(var->guid, uefi_sys_fs_var.guid, sizeof(var->guid));
	var->datalen = (size_t)uefi_sys_fs_var.datalen;
	var->attributes = uefi_sys_fs_var.attributes;
	var->status = uefi_sys_fs_var.status;

	return FWTS_OK;
}

/*
 *  fwts_uefi_get_variable_efivars_fs()
 *	fetch a UEFI variable given its name, via efivars fs
 */
static int fwts_uefi_get_variable_efivars_fs(const char *varname, fwts_uefi_var *var, char *path)
{
	int  fd;
	char filename[PATH_MAX];
	struct stat	statbuf;
	fwts_uefi_efivars_fs_var	*efivars_fs_var;
	size_t varname_len = strlen(varname);

	memset(var, 0, sizeof(fwts_uefi_var));

	/* Variable names include the GUID, so must be at least 36 chars long */

	if (varname_len < 36)
		return FWTS_ERROR;

	/* Get the GUID */
	fwts_guid_str_to_buf(varname + varname_len - 36, var->guid, sizeof(var->guid));

	snprintf(filename, sizeof(filename), "%s/%s", path, varname);

	if (stat(filename, &statbuf) < 0)
		return FWTS_ERROR;

	/* Variable name, less the GUID, in 16 bit ints */
	var->varname = calloc(1, (varname_len + 1 - 36)  * sizeof(uint16_t));
	if (var->varname == NULL)
		return FWTS_ERROR;

	/* Convert name to internal 16 bit version */
	fwts_uefi_str_to_str16(var->varname, varname_len - 36, varname);

	/* Need to read the data in one read, so allocate a buffer big enough */
	if ((efivars_fs_var = calloc(1, statbuf.st_size)) == NULL) {
		free(var->varname);
		return FWTS_ERROR;
	}

	if ((fd = open(filename, O_RDONLY)) < 0) {
		free(var->varname);
		free(efivars_fs_var);
		return FWTS_ERROR;
	}

	if (read(fd, efivars_fs_var, statbuf.st_size) != statbuf.st_size) {
		free(var->varname);
		free(efivars_fs_var);
		close(fd);
		return FWTS_ERROR;
	}
	close(fd);

	var->status = 0;

	/*
	 *  UEFI variable data in efifs is:
	 *     4 bytes : attributes
	 *     N bytes : uefi variable contents
 	 */
	var->attributes = efivars_fs_var->attributes;

	var->datalen = statbuf.st_size - 4;
	if ((var->data = calloc(1, var->datalen)) == NULL) {
		free(var->varname);
		free(efivars_fs_var);
		return FWTS_ERROR;
	}
	memcpy(var->data, efivars_fs_var->data, var->datalen);

	free(efivars_fs_var);

	return FWTS_OK;
}

/*
 *  fwts_uefi_get_variable()
 *	fetch a UEFI variable given its name.
 */
int fwts_uefi_get_variable(const char *varname, fwts_uefi_var *var)
{
	char *path;

	if ((!varname) || (!var))	/* Sanity checks */
		return FWTS_ERROR;

	switch (fwts_uefi_get_interface(&path)) {
	case UEFI_IFACE_SYSFS:
		return fwts_uefi_get_variable_sys_fs(varname, var, path);
	case UEFI_IFACE_EFIVARS:
		return fwts_uefi_get_variable_efivars_fs(varname, var, path);
	default:
		return FWTS_ERROR;
	}
}

/*
 *  fwts_uefi_free_variable()
 *	free data and varname associated with a UEFI variable as
 *	fetched by fwts_uefi_get_variable.
 */
void fwts_uefi_free_variable(fwts_uefi_var *var)
{
	free(var->data);
	free(var->varname);
}

static int fwts_uefi_true_filter(const struct dirent *d)
{
	return 1;
}

/*
 *  fwts_uefi_free_variable_names
 *	free the list of uefi variable names
 */
void fwts_uefi_free_variable_names(fwts_list *list)
{
	fwts_list_free_items(list, free);
}

/*
 *  fwts_uefi_get_variable_names
 *	gather a list of all the uefi variable names
 */
int fwts_uefi_get_variable_names(fwts_list *list)
{
	int i, n;
	struct dirent **names = NULL;
	char *path;
	char *name;
	int ret = FWTS_OK;

	fwts_list_init(list);

	switch (fwts_uefi_get_interface(&path)) {
	case UEFI_IFACE_SYSFS:
	case UEFI_IFACE_EFIVARS:
		break;
	default:
		return FWTS_ERROR;
	}

	/* Gather variable names in alphabetical order */
	n = scandir(path, &names, fwts_uefi_true_filter, alphasort);
	if (n < 0)
		return FWTS_ERROR;

	for (i = 0; i < n; i++) {
		if (names[i]->d_name == NULL)
			continue;
		if (!strcmp(names[i]->d_name, "."))
			continue;
		if (!strcmp(names[i]->d_name, ".."))
			continue;

		name = strdup(names[i]->d_name);
		if (name == NULL) {
			ret = FWTS_ERROR;
			fwts_uefi_free_variable_names(list);
			break;
                } else
			fwts_list_append(list, name);
        }

	/* Free dirent names */
 	for (i = 0; i < n; i++)
		free(names[i]);
        free(names);

        return ret;
}

