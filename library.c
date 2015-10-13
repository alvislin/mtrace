/*
 * This file is part of mtrace.
 * Copyright (C) 2015 Stefani Seibold <stefani@seibold.net>
 *  This file is based on the ltrace source
 *
 * This work was sponsored by Rohde & Schwarz GmbH & Co. KG, Munich/Germany.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>

#include "backend.h"
#include "breakpoint.h"
#include "debug.h"
#include "dict.h"
#include "library.h"
#include "options.h"
#include "report.h"
#include "server.h"
#include "task.h"

struct libref *libref_new(unsigned int type)
{
	struct libref *libref = malloc(sizeof(*libref));

	if (!libref)
		return NULL;

	memset(libref, 0, sizeof(*libref));

	libref->refcnt = 0;
	libref->type = type;

	INIT_LIST_HEAD(&libref->sym_list);

	return libref;
}


void libref_delete(struct libref *libref)
{
	struct list_head *it, *next;

	list_for_each_safe(it, next, &libref->sym_list) {
		struct library_symbol *sym = container_of(it, struct library_symbol, list);

		list_del(&sym->list);
		free(sym);
	}

	if (libref->image_addr)
		munmap(libref->image_addr, libref->load_size);

	free((void *)libref->filename);
	free(libref);
}

static void libref_put(struct libref *libref)
{
	assert(libref->refcnt != 0);

	if (!--libref->refcnt)
		libref_delete(libref);
}

static struct libref *libref_get(struct libref *libref)
{
	assert(libref);

	++libref->refcnt;

	return libref;
}

void libref_set_filename(struct libref *libref, const char *new_name)
{
	free((void *)libref->filename);
	libref->filename = new_name ? strdup(new_name) : NULL;
}

struct library_symbol *library_symbol_new(struct libref *libref, arch_addr_t addr, const struct function *func)
{
	struct library_symbol *libsym = malloc(sizeof(*libsym));

	if (!libsym)
		return NULL;

	libsym->libref = libref;
	libsym->func = func;
	libsym->addr = addr;

	list_add_tail(&libsym->list, &libref->sym_list);

	return libsym;
}

void library_delete(struct task *task, struct library *lib)
{
	if (lib == NULL)
		return;

	struct list_head *it, *next;
	struct libref *libref = lib->libref;

	list_for_each_safe(it, next, &libref->sym_list) {
		struct breakpoint *bp = breakpoint_find(task, container_of(it, struct library_symbol, list)->addr);

		if (bp)
			breakpoint_delete(task, bp);
	}

	list_del(&lib->list);
	free(lib);

	libref_put(libref);
}

struct library_symbol *library_find_symbol(struct libref *libref, arch_addr_t addr)
{
	struct list_head *it;

	list_for_each(it, &libref->sym_list) {
		struct library_symbol *sym = container_of(it, struct library_symbol, list);

		if (sym->addr == addr)
			return sym;
	}
	return NULL;
}

struct library *library_find_with_key(struct list_head *list, arch_addr_t key)
{
	struct list_head *it;

	list_for_each(it, list) {
		struct library *lib = container_of(it, struct library, list);

		if (lib->libref->key == key)
			return lib;
	}
	return NULL;
}

void library_delete_list(struct task *leader, struct list_head *list)
{
	struct list_head *it, *next;

	list_for_each_safe(it, next, list) {
		struct library *lib = container_of(it, struct library, list);
		struct libref *libref = lib->libref;

		debug(DEBUG_FUNCTION, "%s@%#lx", libref->filename, libref->base);

		if (options.verbose > 1)
			fprintf(stderr, "+++ library del pid=%d %s@%#lx %#lx-%#lx +++\n", leader->pid, libref->filename, libref->base, libref->load_addr, libref->load_addr + libref->load_size);

		library_delete(leader, lib);
	}
}

static void cb_breakpoint_for_symbol(struct library_symbol *libsym, void *data)
{
	struct task *task = data;
	arch_addr_t addr = libsym->addr;
	struct breakpoint *bp = breakpoint_find(task, addr);

	if (bp) {
		assert(bp->libsym == NULL);

		bp->libsym = libsym;
		return;
	}
	bp = breakpoint_new(task, addr, libsym, BP_AUTO);
	if (!bp)
		fprintf(stderr, "Couldn't insert breakpoint for %s to %d: %s", libsym->func->name, task->pid, strerror(errno));

	if (server_connected())
		breakpoint_enable(task, bp);
}

static void library_each_symbol(struct libref *libref, void (*cb)(struct library_symbol *, void *), void *data)
{
	struct list_head *it, *next;

	list_for_each_safe(it, next, &libref->sym_list) {
		struct library_symbol *sym = container_of(it, struct library_symbol, list);

		(*cb) (sym, data);
	}
}

static struct library *_library_add(struct task *leader, struct libref *libref)
{
	debug(DEBUG_PROCESS, "%s@%#lx to pid=%d", libref->filename, libref->base, leader->pid);

	assert(leader->leader == leader);

	struct library *lib = malloc(sizeof(*lib));

	if (lib == NULL)
		return NULL;

	memset(lib, 0, sizeof(*lib));

	lib->libref = libref_get(libref);

	list_add_tail(&lib->list, &leader->libraries_list);

	if (options.verbose > 1)
		fprintf(stderr, "+++ library add pid=%d %s@%#lx %#lx-%#lx +++\n", leader->pid, libref->filename, libref->base, libref->load_addr, libref->load_addr + libref->load_size);

	return lib;
}

struct library *library_add(struct task *leader, struct libref *libref)
{
	struct library *lib = _library_add(leader, libref);

	/* Insert breakpoints for all active symbols.  */
	library_each_symbol(libref, cb_breakpoint_for_symbol, leader);

	report_add_map(leader, lib);

	return lib;
}

void library_clear_all(struct task *leader)
{
	library_delete_list(leader, &leader->libraries_list);
}

int library_clone_all(struct task *clone, struct task *leader)
{
	struct list_head *it;

	list_for_each(it, &leader->libraries_list) {
		struct library *lib = container_of(it, struct library, list);

		_library_add(clone, lib->libref);
	}
	return 0;
}

void library_setup(struct task *leader)
{
	INIT_LIST_HEAD(&leader->libraries_list);
}

const char *library_execname(struct task *leader)
{
	if (list_empty(&leader->libraries_list))
		return NULL;

	return container_of(leader->libraries_list.next, struct library, list)->libref->filename;
}

