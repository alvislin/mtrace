/*
 * This file is part of mtrace-ng.
 * Copyright (C) 2018 Stefani Seibold <stefani@seibold.net>
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

#ifndef _INC_MTRACE_ELF_H
#define _INC_MTRACE_ELF_H

#include <gelf.h>
#include <stdlib.h>

#include "forward.h"
#include "sysdep.h"

int elf_read_library(struct task *task, struct libref *libref, const char *filename, GElf_Addr bias);

/* Create a library object representing the main binary. */
struct libref *elf_read_main_binary(struct task *task, int was_attached);

int mte_cmp_machine(struct mt_elf *mte, Elf64_Half type);

#endif

