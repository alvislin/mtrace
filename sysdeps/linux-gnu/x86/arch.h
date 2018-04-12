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

#ifndef _INC_SYSDEPS_LINUX_GNU_X86_ARCH_H
#define _INC_SYSDEPS_LINUX_GNU_X86_ARCH_H

#include <elf.h>
#include <stddef.h>
#include <sys/user.h>
#include <sys/ptrace.h>
#include <linux/unistd.h>

#define BREAKPOINT_VALUE { 0xcc }
#define BREAKPOINT_LENGTH 1
#define DECR_PC_AFTER_BREAK 1
#define ARCH_ENDIAN_LITTLE

#define MT_ELFCLASS	ELFCLASS32
#define MT_ELF_MACHINE	EM_386

#ifdef __x86_64__
#define MT_ELFCLASS2	ELFCLASS64
#define MT_ELF_MACHINE2	EM_X86_64

#ifndef __NR_process_vm_readv
#define __NR_process_vm_readv 310
#endif

#else /* __x86_64__ */

#ifndef __NR_process_vm_readv
#define __NR_process_vm_readv 347
#endif

#endif /* __x86_64__ */

#define GUESS_CALLER

#define HW_BREAKPOINTS	4

#define TASK_HAVE_PROCESS_DATA

struct context {
	struct user_regs_struct iregs;
};

struct arch_task_data {
	unsigned long dr7;
	unsigned long hw_bp[HW_BREAKPOINTS];
};

#endif

