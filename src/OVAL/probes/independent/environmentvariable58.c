/**
 * @file   environmentvariable58.c
 * @brief  environmentvariable58 probe
 * @author "Petr Lautrbach" <plautrba@redhat.com>
 *
 *  This probe is able to process a environmentvariable58_object as defined in OVAL 5.8.
 *
 */

/*
 * Copyright 2009-2011 Red Hat Inc., Durham, North Carolina.
 * All Rights Reserved.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors:
 *   Petr Lautrbach <plautrba@redhat.com>
 */

/*
 * environmentvariable58 probe:
 *
 * pid
 * name
 * value
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <dirent.h>

#include "seap.h"
#include "probe-api.h"
#include "probe/entcmp.h"
#include "alloc.h"

#define BUFFER_SIZE 256

extern char **environ;

static int read_environment(SEXP_t *pid_ent, SEXP_t *name_ent, probe_ctx *ctx)
{
	int err = 0, pid, empty = 1, fd;
	size_t env_name_size;
	SEXP_t *env_name, *env_value, *item, *pid_sexp;
	DIR *d;
	struct dirent *d_entry;
	char *buffer = NULL, env_file[256];
	unsigned int buffer_size = 0, read_size = 0;

	d = opendir("/proc");
	if (d == NULL) {
		dE("Can't read /proc: errno=%d, %s.\n", errno, strerror (errno));
		return PROBE_EACCESS;
	}

	if ((buffer = oscap_realloc(NULL, BUFFER_SIZE)) == NULL) {
		closedir(d);
		return PROBE_EFAULT;
	}
	buffer_size = BUFFER_SIZE;

	while ((d_entry = readdir(d))) {
		if (strspn(d_entry->d_name, "0123456789") != strlen(d_entry->d_name))
			continue;

		pid = atoi(d_entry->d_name);
		pid_sexp = SEXP_number_newi_32(pid);

		if (probe_entobj_cmp(pid_ent, pid_sexp) != OVAL_RESULT_TRUE) {
			SEXP_free(pid_sexp);
			continue;
		}
		SEXP_free(pid_sexp);

		sprintf(env_file, "/proc/%d/environ", pid);

		if ((fd = open(env_file, O_RDONLY)) == -1) {

			SEXP_t *r0;

			dE("Can't open \"%s\": errno=%d, %s.\n", env_file, errno, strerror (errno));
			item = probe_item_create(
					OVAL_INDEPENDENT_ENVIRONMENT_VARIABLE58, NULL,
					"pid", OVAL_DATATYPE_INTEGER, pid,
					NULL
			);
			probe_item_setstatus(item, SYSCHAR_STATUS_NOT_COLLECTED);
			probe_item_attr_add(item, "message", r0 = SEXP_string_newf("Can't open \"%s\": errno=%d, %s.", env_file, errno, strerror (errno)));
			probe_item_collect(ctx, item);
			SEXP_free(r0);

			continue;
		}

		*buffer = '\0';

		if ((read_size = read(fd, buffer, buffer_size - 1)) > 0) {
			empty = 0;
		} else {
			close(fd);
                        closedir(d);
			return err;
		}

		buffer[buffer_size - 1] = 0;

		while (! empty) {
			/* we dont have whole var=val string */
			while (strlen(buffer) >= read_size) {
				int s;
				if (read_size + 1 >= buffer_size) {
					buffer_size += BUFFER_SIZE;
					buffer = oscap_realloc(buffer, buffer_size);
				}
				s = read(fd, buffer + read_size, buffer_size - read_size - 1);
				if (s == 0) {
					empty = 1;
					break;
				}
				read_size += s;
				buffer[buffer_size - 1] = 0;
			}

			while (strlen(buffer) < read_size && read_size > 0) {
				char *buffer_split = strchr(buffer, '=');
				if (buffer_split == NULL) {
					/* strange but possible:
					 * $ strings /proc/1218/environ
 					/dev/input/event0 /dev/input/event1 /dev/input/event4 /dev/input/event3
					*/
					read_size -= strlen(buffer);
					memmove(buffer, buffer + strlen(buffer) + 1, read_size--);
					continue;
				}

				env_name_size =  buffer_split - buffer;
				env_name = SEXP_string_new(buffer, env_name_size);
				env_value = SEXP_string_newf("%s", buffer + env_name_size + 1);
				if (probe_entobj_cmp(name_ent, env_name) == OVAL_RESULT_TRUE) {
					item = probe_item_create(
						OVAL_INDEPENDENT_ENVIRONMENT_VARIABLE58, NULL,
						"pid", OVAL_DATATYPE_INTEGER, pid,
						"name",  OVAL_DATATYPE_SEXP, env_name,
						"value", OVAL_DATATYPE_SEXP, env_value,
					      NULL);
					probe_item_collect(ctx, item);
					err = 0;
				}
				SEXP_free(env_name);
				SEXP_free(env_value);

				read_size -= strlen(buffer);
				memmove(buffer, buffer + strlen(buffer) + 1, read_size--);
			}
		}
		close(fd);
	}
	closedir(d);
	oscap_free(buffer);

	return err;
}

int probe_main(probe_ctx *ctx, void *arg)
{
	SEXP_t *probe_in, *name_ent, *pid_ent;
	int pid, err;

	probe_in  = probe_ctx_getobject(ctx);
	name_ent = probe_obj_getent(probe_in, "name", 1);

	if (name_ent == NULL) {
		return PROBE_ENOENT;
	}

	pid_ent = probe_obj_getent(probe_in, "pid", 1);
	if (pid_ent == NULL) {
		return PROBE_ENOENT;
	}

	PROBE_ENT_I32VAL(pid_ent, pid, pid = -1;);

	if (pid == -1) {
		return PROBE_ERANGE;
	}

	if (pid == 0) {
		/* overwrite pid value with actual pid */
		SEXP_t *nref, *nval, *new_pid_ent;

		nref = SEXP_list_first(probe_in);
		nval = SEXP_number_newu_32(getpid());
		new_pid_ent = SEXP_list_new(nref, nval, NULL);
		SEXP_vfree(pid_ent, nref, nval, NULL);
		pid_ent = new_pid_ent;
	}

	err = read_environment(pid_ent, name_ent, ctx);
	SEXP_free(name_ent);
	SEXP_free(pid_ent);

	return err;
}
