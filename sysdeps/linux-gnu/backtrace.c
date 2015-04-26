/*
 * This file is part of mtrace.
 * Copyright (C) 2015 Stefani Seibold <stefani@seibold.net>
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

#include "backtrace.h"
#include "dwarf.h"
#include "task.h"

#include <assert.h>
#include <stdio.h>

int backtrace_init(struct task *task)
{
	assert(task->backtrace == NULL);

	task->backtrace = dwarf_init(task);

	return task->backtrace != NULL;
}

void backtrace_destroy(struct task *task)
{
	if (task->backtrace) {
		dwarf_destroy(task->backtrace);

		task->backtrace = NULL;
	}
}

int backtrace_init_unwind(struct task *task)
{
	assert(task->backtrace);

	return dwarf_init_unwind(task->backtrace);
}

unsigned long backtrace_get_ip(struct task *task)
{
	assert(task->backtrace);

	return dwarf_get_ip(task->backtrace);
}

int backtrace_step(struct task *task)
{
	assert(task->backtrace);

	return dwarf_step(task->backtrace);
}

