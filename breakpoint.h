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

#ifndef _INC_BREAKPOINT_H
#define _INC_BREAKPOINT_H

#include <stdlib.h>

#include "arch.h"
#include "list.h"
#include "sysdep.h"
#include "forward.h"

#define BP_REORDER_THRESHOLD	1024U

#define BP_SW		0
#define BP_HW_SCRATCH	1
#define BP_HW		2
#define BP_AUTO		3

struct breakpoint {
#if HW_BREAKPOINTS > 1
	unsigned int hwcnt;
#endif
	unsigned int refcnt;
	unsigned int count;

	unsigned int ext:8;
	unsigned int type:2;
	unsigned int enabled:1;
	unsigned int locked:1;
	unsigned int deleted:1;
	unsigned int hw:1;
	union {
		unsigned char orig_value[BREAKPOINT_LENGTH];
#if HW_BREAKPOINTS > 0
		unsigned int hw_bp_slot;
#endif
	};

	int (*on_hit)(struct task *task, struct breakpoint *bp);
	arch_addr_t addr;
	struct library_symbol *libsym;
	struct list_head link_list;
};

/* setup the basic breakpoint support for a given leader */
void breakpoint_setup(struct task *leader);

/* Call on-hit handler of BP, if any is set.  */
int breakpoint_on_hit(struct task *task, struct breakpoint *bp);

/* get a new breakpoint structure. */
struct breakpoint *breakpoint_new(struct task *task, arch_addr_t addr, struct library_symbol *libsym, int type);

/* get a new extended breakpoint structure . */
struct breakpoint *breakpoint_new_ext(struct task *task, arch_addr_t addr, struct library_symbol *libsym, int type, size_t ext);

/* insert a new breakpoint structure if necessary and turn the breakpoint on */
struct breakpoint *breakpoint_insert(struct task *task, arch_addr_t addr, struct library_symbol *libsym, int type);

/* delete a breakpoint in task */
void breakpoint_delete(struct task *task, struct breakpoint *bp);

/* Enable breakpoint in task */
void breakpoint_enable(struct task *task, struct breakpoint *bp);

/* Disable task breakpoint task */
void breakpoint_disable(struct task *task, struct breakpoint *bp);

void breakpoint_enable_all(struct task *leader);
void breakpoint_disable_all(struct task *leader);
void breakpoint_enable_all_nonlocked(struct task *leader);
void breakpoint_disable_all_nonlocked(struct task *leader);
void breakpoint_clear_all(struct task *leader);
int breakpoint_clone_all(struct task *clone, struct task *leader);

struct breakpoint *breakpoint_find(struct task *leader, arch_addr_t addr);

#if HW_BREAKPOINTS > 0
void enable_scratch_hw_bp(struct task *task, struct breakpoint *bp);
void disable_scratch_hw_bp(struct task *task, struct breakpoint *bp);
#else
static inline void enable_scratch_hw_bp(struct task *task, struct breakpoint *bp)
{
}

static inline void disable_scratch_hw_bp(struct task *task, struct breakpoint *bp)
{
}
#endif

#if HW_BREAKPOINTS > 1
void reorder_hw_bp(struct task *task);

void breakpoint_hw_clone(struct task *task);
void breakpoint_hw_destroy(struct task *task);
#else
static inline void breakpoint_hw_clone(struct task *task)
{
}

static inline void breakpoint_hw_destroy(struct task *task)
{
}
#endif

struct breakpoint *breakpoint_get(struct breakpoint *bp);

int breakpoint_put(struct breakpoint *bp);

#endif

