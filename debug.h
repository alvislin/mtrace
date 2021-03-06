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

#ifndef _INC_DEBUG_H
#define _INC_DEBUG_H

#include "config.h"

/* debug levels:
 */
enum {
	DEBUG_TRACE = 1,
	DEBUG_DWARF = 2,
	DEBUG_EVENT = 4,
	DEBUG_PROCESS = 8,
	DEBUG_FUNCTION = 16,
};

#ifdef DEBUG
void _debug(int level, const char *file, const char *function, int line, const char *fmt, ...) __attribute__ ((format(printf, 5, 6)));
#else
static inline void _debug(int level, const char *file, const char *function, int line, const char *fmt, ...)
{
	(void)level;
	(void)file;
	(void)function;
	(void)line;
	(void)fmt;
}
#endif

#define debug(level, expr...) _debug(level, __FILE__, __FUNCTION__, __LINE__, expr)

#endif

