/*
 * This file is part of mtrace.
 * Copyright (C) 2015 Stefani Seibold <stefani@seibold.net>
 *  This file is based on the ltrace source
 *   Copyright (C) 2007,2011,2012 Petr Machata, Red Hat Inc.
 *   Copyright (C) 2010 Joe Damato
 *   Copyright (C) 1998,2002,2003,2004,2008,2009 Juan Cespedes
 *   Copyright (C) 2006 Ian Wienand
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

#include "config.h"

#define _GNU_SOURCE

#include <asm/unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/prctl.h>

#ifdef HAVE_LIBSELINUX
#include <selinux/selinux.h>
#endif

#include "backend.h"
#include "breakpoint.h"
#include "debug.h"
#include "event.h"
#include "library.h"
#include "options.h"
#include "task.h"

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile pid_t wakeup_pid = -1;

static int inline task_kill(struct task *task, int sig)
{
	errno = 0;

	return syscall(__NR_tgkill, task->leader->pid, task->pid, sig);
}

static int trace_setup(struct task *task, int status, int signum)
{
	int stop_signal;

	if (!WIFSTOPPED(status)) {
		fprintf(stderr, "%s pid=%d not stopped\n", __FUNCTION__, task->pid);
		return -1;
	}

	stop_signal = WSTOPSIG(status);

	if (stop_signal == SIGSTOP)
		task->was_stopped = 1;
	else
	if (stop_signal == signum)
		stop_signal = 0;

	task->event.type = EVENT_SIGNAL;
	task->event.e_un.signum = stop_signal;

	task->traced = 1;
	task->stopped = 1;

	if (task->leader)
		task->leader->threads_stopped++;

	return 0;
}

static int _trace_wait(struct task *task, int signum)
{
	int status;

	if (TEMP_FAILURE_RETRY(waitpid(task->pid, &status, __WALL)) != task->pid) {
		fprintf(stderr, "%s pid=%d %s\n", __FUNCTION__, task->pid, strerror(errno));
		return -1;
	}

	if (WIFEXITED(status))
		return -1;

	return trace_setup(task, status, signum);
}

int trace_wait(struct task *task)
{
	return _trace_wait(task, SIGTRAP);
}

static int child_event(struct task *task, enum event_type ev)
{
	unsigned long data;

	if (ptrace(PTRACE_GETEVENTMSG, task->pid, NULL, &data) == -1) {
		debug(DEBUG_EVENT, "PTRACE_GETEVENTMSG pid=%d %s", task->pid, strerror(errno));
		return -1;
	}

	int pid = data;

	if (!pid2task(pid)) {
		struct task *child = task_new(pid, 0);

		if (!child || _trace_wait(child, SIGTRAP))
			return -1;
	}

	task->event.e_un.newpid = pid;
	task->event.type = ev;
	return 0;
}

static int _process_event(struct task *task, int status)
{
	int stop_signal;

	if (WIFSIGNALED(status)) {
		task->event.type = EVENT_EXIT_SIGNAL;
		task->event.e_un.signum = WTERMSIG(status);
		debug(DEBUG_EVENT, "EXIT_SIGNAL: pid=%d, signum=%d", task->pid, task->event.e_un.signum);
		return 0;
	}

	if (WIFEXITED(status)) {
		task->event.type = EVENT_EXIT;
		task->event.e_un.ret_val = WEXITSTATUS(status);
		debug(DEBUG_EVENT, "EXIT: pid=%d, status=%d", task->pid, task->event.e_un.ret_val);
		return 0;
	}

	if (!WIFSTOPPED(status)) {
		/* should never happen */
		debug(DEBUG_EVENT, "NONE: pid=%d ???", task->pid);
		return -1;
	}

	switch(status >> 16)  {
	case PTRACE_EVENT_VFORK:
		if (child_event(task, EVENT_VFORK))
			return -1;
		debug(DEBUG_EVENT, "VFORK: pid=%d, newpid=%d", task->pid, task->event.e_un.newpid);
		return 0;
	case PTRACE_EVENT_FORK:
		if (child_event(task, EVENT_FORK))
			return -1;
		debug(DEBUG_EVENT, "FORK: pid=%d, newpid=%d", task->pid, task->event.e_un.newpid);
		return 0;
	case PTRACE_EVENT_CLONE:
		if (child_event(task, EVENT_CLONE))
			return -1;
		debug(DEBUG_EVENT, "CLONE: pid=%d, newpid=%d", task->pid, task->event.e_un.newpid);
		return 0;
	case PTRACE_EVENT_EXEC:
		task->event.type = EVENT_EXEC;
		debug(DEBUG_EVENT, "EXEC: pid=%d", task->pid);
		return 0;
	case PTRACE_EVENT_EXIT:
	 {
		unsigned long data;

		if (ptrace(PTRACE_GETEVENTMSG, task->pid, NULL, &data) == -1) {
			debug(DEBUG_EVENT, "PTRACE_GETEVENTMSG pid=%d %s", task->pid, strerror(errno));
			return -1;
		}
		task->event.e_un.ret_val = WEXITSTATUS(data);
		task->event.type = EVENT_ABOUT_EXIT;
		debug(DEBUG_EVENT, "ABOUT_EXIT: pid=%d", task->pid);
		return 0;
	 }
	default:
		break;
	}

	stop_signal = WSTOPSIG(status);

	task->event.type = EVENT_SIGNAL;
	task->event.e_un.signum = stop_signal;

	debug(DEBUG_EVENT, "SIGNAL: pid=%d, signum=%d", task->pid, stop_signal);
	return stop_signal;
}

static void process_event(struct task *task, int status)
{
	int stop_signal;
	struct task *leader = task->leader;
	struct breakpoint *bp = NULL;
	int i;
	arch_addr_t ip;

	task->stopped = 1;
	
	if (leader)
		leader->threads_stopped++;

	stop_signal = _process_event(task, status);

	if (stop_signal == -1) {
		task->event.type = EVENT_NONE;
		continue_task(task, 0);
		return;
	}

	if (stop_signal == 0)
		return;

	if (stop_signal != SIGTRAP && stop_signal != SIGSEGV && stop_signal != SIGILL)
		return;

	if (fetch_context(task) == -1) {
		task->event.type = EVENT_NONE;
		continue_task(task, 0);
		return;
	}

	if (!leader)
		return;

	ip = get_instruction_pointer(task);

	for(i = 0; i < HW_BREAKPOINTS; ++i) {
		if (task->hw_bp[i] && task->hw_bp[i]->addr == ip) {
			bp = task->hw_bp[i];
			break;
		}
	}

	if (bp) {
#if HW_BREAKPOINTS > 0
		assert(bp->type != SW_BP);
		assert(bp->hw_bp_slot == i);
#endif
	}
	else {
		bp = breakpoint_find(leader, get_instruction_pointer(task) - DECR_PC_AFTER_BREAK);
		if (!bp)
			return;
		assert(bp->type == SW_BP);

		set_instruction_pointer(task, bp->addr);
	}
#if 1
	assert(bp->enabled);
#else
	if (bp->enabled)
		return;
#endif

	task->event.type = EVENT_BREAKPOINT;
	task->event.e_un.breakpoint = bp;

	debug(DEBUG_EVENT, "BREAKPOINT: pid=%d, addr=%#lx", task->pid, task->event.e_un.breakpoint->addr);

	return;
}

static void trace_fail_warning(void)
{
#ifdef HAVE_LIBSELINUX
	if (security_get_boolean_active("deny_ptrace") == 1)
		fprintf(stderr,
			"The SELinux boolean 'deny_ptrace' is enabled, which may prevent mtrace\n"
			"from tracing an other task. You can disable this process attach protection by\n"
			"issuing 'setsebool deny_ptrace=0' in the superuser context.\n");
#else
		fprintf(stderr,
			"Could not trace! Maybe the SELinux boolean 'deny_ptrace' is enabled, which may\n"
			"prevent mtrace from tracing an other tasks. Try to disable this process attach\n"
			"protection by issuing 'setsebool deny_ptrace=0' in the superuser context.\n");
#endif
}

void trace_me(void)
{
	debug(DEBUG_PROCESS, "pid=%d", getpid());

	prctl(PR_SET_PDEATHSIG, SIGKILL);

	if (ptrace(PTRACE_TRACEME, 0, 0, 0) == -1) {
		fprintf(stderr, "PTRACE_TRACEME pid=%d %s\n", getpid(), strerror(errno));
		exit(1);
	}
}

static inline int fix_signal(struct task *task, int signum)
{
	if (signum == SIGSTOP && task->was_stopped) {
		task->was_stopped = 0;
		signum = 0;
	}
	return signum;
}

int untrace_task(struct task *task, int signum)
{
	debug(DEBUG_PROCESS, "pid=%d", task->pid);

	if (!task->traced)
		return 0;

	task->traced = 0;
	task->stopped = 0;

	if (task->leader)
		task->leader->threads_stopped--;

	if (ptrace(PTRACE_DETACH, task->pid, 0, fix_signal(task, signum)) == -1) {
		if (errno != ESRCH)
			fprintf(stderr, "PTRACE_DETACH pid=%d %s\n", task->pid, strerror(errno));
		return -1;
	}
	return 0;
}

int trace_attach(struct task *task)
{
	debug(DEBUG_PROCESS, "pid=%d", task->pid);

	if (task->traced)
		return 0;

	if (ptrace(PTRACE_ATTACH, task->pid, 0, 0) == -1) {
		fprintf(stderr, "PTRACE_ATTACH pid=%d %s\n", task->pid, strerror(errno));
		trace_fail_warning();
		return -1;
	}

	if (_trace_wait(task, SIGSTOP))
		return -1;

	return 0;
}

int trace_set_options(struct task *task)
{
	long options = PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK | PTRACE_O_TRACECLONE | PTRACE_O_TRACEEXEC | PTRACE_O_TRACEEXIT;

	debug(DEBUG_PROCESS, "pid=%d", task->pid);

	if (ptrace(PTRACE_SETOPTIONS, task->pid, 0, (void *)options) == -1) {
		fprintf(stderr, "PTRACE_SETOPTIONS pid=%d %s\n", task->pid, strerror(errno));
		return -1;
	}
	return 0;
}

int continue_task(struct task *task, int signum)
{
	debug(DEBUG_PROCESS, "pid=%d", task->pid);

	if (task->leader)
		task->leader->threads_stopped--;
	task->stopped = 0;

	if (ptrace(PTRACE_CONT, task->pid, 0, fix_signal(task, signum)) == -1) {
		fprintf(stderr, "PTRACE_CONT pid=%d %s\n", task->pid, strerror(errno));
		return -1;
	}
	return 0;
}

static void do_stop_cb(struct task *task, void *data)
{
	if (task->stopped)
		return;
#if 0
	int status;
	int pid = TEMP_FAILURE_RETRY(waitpid(task->pid, &status, __WALL | WNOHANG));

	if (pid == task->pid) {
		process_event(task, status);
		queue_event(task);
		return;
	}
#endif
	task->was_stopped = 1;

	task_kill(task, SIGSTOP);
}

void stop_threads(struct task *task)
{
	struct task *leader = task->leader;

	if (!leader)
		return;

	if (leader->threads != leader->threads_stopped) {
		each_task(leader, &do_stop_cb, NULL);

		while (leader->threads != leader->threads_stopped)
			queue_event(wait_event());
	}
}

int handle_singlestep(struct task *task, int (*singlestep)(struct task *task))
{
	int status;
	int stop_signal;

	for(;;) {
		if (singlestep(task) == -1)
			return -1;

		if (TEMP_FAILURE_RETRY(waitpid(task->pid, &status, __WALL)) != task->pid) {
			fprintf(stderr, "%s waitpid pid=%d %s\n", __FUNCTION__,  task->pid, strerror(errno));
			return -1;
		}

		stop_signal = _process_event(task, status);

		if (stop_signal == -1)
			return -1;

		if (stop_signal == SIGTRAP)
			return 0; /* check if was a real breakpoint code there */


		if (!stop_signal) {
			queue_event(task);
			return 0;
		}


		if (fix_signal(task, stop_signal) > 0) {
			queue_event(task);
			return 1;
		}
	}
}

#ifndef ARCH_SINGLESTEP
static int ptrace_singlestep(struct task *task)
{
	if (ptrace(PTRACE_SINGLESTEP, task->pid, 0, 0) == -1) {
		if (errno != ESRCH)
			fprintf(stderr, "%s PTRACE_SINGLESTEP pid=%d %s\n", __FUNCTION__, task->pid, strerror(errno));
		return -1;
	}
	return 0;
}

int do_singlestep(struct task *task)
{
	return handle_singlestep(task, ptrace_singlestep);
}
#endif

struct task *wait_event(void)
{
	struct task *task;
	int status;
	int pid;
	
	pid = waitpid(-1, &status, __WALL);
	if (pid == -1) {
		if (errno != EINTR) {
			if (errno == ECHILD)
				debug(DEBUG_EVENT, "No more traced programs");
			else
				fprintf(stderr, "%s waitpid %s\n", __FUNCTION__, strerror(errno));
		}
		return NULL;
	}

	pthread_mutex_lock(&mutex);
	if (pid == wakeup_pid) {
		pid = 0;
		wakeup_pid = -1;
	}
	pthread_mutex_unlock(&mutex);

	if (!pid)
		return NULL;

	task = pid2task(pid);
	if (!task) {
		task = task_new(pid, 0);

		if (task)
			trace_setup(task, status, SIGTRAP);
		return NULL;
	}

	process_event(task, status);

	return task;
}

void wait_event_wakeup(void)
{
	pid_t pid;

	pthread_mutex_lock(&mutex);
	if (wakeup_pid == -1) {
		pid = vfork();
		if (pid == 0)
			_exit(0);
		wakeup_pid = pid;
	}
	pthread_mutex_unlock(&mutex);
}

ssize_t copy_from_proc(struct task *task, arch_addr_t addr, void *dst, size_t len)
{
	union {
		long a;
		char c[sizeof(long)];
	} a;

	ssize_t num_bytes = 0;
	size_t n = sizeof(a.a);

	errno = 0;

	while (len) {
		a.a = ptrace(PTRACE_PEEKTEXT, task->pid, addr, 0);
		if (a.a == -1 && errno) {
			if (num_bytes && errno == EIO)
				break;
			return -1;
		}

		if (n > len)
			n = len;

		memcpy(dst, a.c, n);

		num_bytes += n;
		len -= n;

		dst += n;
		addr += n;
	}

	return num_bytes;
}

ssize_t copy_to_proc(struct task *task, arch_addr_t addr, const void *src, size_t len)
{
	union {
		long a;
		char c[sizeof(long)];
	} a;

	ssize_t num_bytes = 0;
	size_t n = sizeof(a.a);

	while (len) {
		if (n > len) {
			errno = 0;

			n = len;

			a.a = ptrace(PTRACE_PEEKTEXT, task->pid, addr, 0);
			if (a.a == -1 && errno) {
				if (num_bytes && errno == EIO)
					break;
				return -1;
			}
		}

		memcpy(a.c, src, n);

		a.a = ptrace(PTRACE_POKETEXT, task->pid, addr, a.a);
		if (a.a == -1) {
			if (num_bytes && errno == EIO)
				break;
			return -1;
		}

		num_bytes += n;
		len -= n;

		src += n;
		addr += n;
	}

	return num_bytes;
}

ssize_t copy_from_to_proc(struct task *task, arch_addr_t addr, const void *src, void *dst, size_t len)
{
	union {
		long a;
		char c[sizeof(long)];
	} a;

	ssize_t num_bytes = 0;
	size_t n = sizeof(a.a);

	errno = 0;

	while (len) {
		a.a = ptrace(PTRACE_PEEKTEXT, task->pid, addr, 0);
		if (a.a == -1 && errno) {
			if (num_bytes && errno == EIO)
				break;
			return -1;
		}

		if (n > len)
			n = len;

		memcpy(dst, a.c, n);
		memcpy(a.c, src, n);

		a.a = ptrace(PTRACE_POKETEXT, task->pid, addr, a.a);
		if (a.a == -1) {
			if (num_bytes && errno == EIO)
				break;
			return -1;
		}

		num_bytes += n;
		len -= n;

		src += n;
		dst += n;
		addr += n;
	}

	return num_bytes;
}

ssize_t copy_str_from_proc(struct task *task, arch_addr_t addr, char *dst, size_t len)
{
	union {
		long a;
		char c[sizeof(long)];
	} a;

	ssize_t num_bytes = 0;
	size_t n = sizeof(a.a);
	size_t i;

	errno = 0;

	if (--len < 0)
		return -1;

	while (len) {
		a.a = ptrace(PTRACE_PEEKTEXT, task->pid, addr, 0);
		if (a.a == -1 && errno) {
			if (num_bytes && errno == EIO)
				break;
			return -1;
		}

		if (n > len)
			n = len;

		for(i = 0; i < n; ++i) {
			if (!a.c[i])
				break;
		}

		memcpy(dst, a.c, i);

		num_bytes += i;
		len -= i;

		dst += i;
		addr += i;

		if (i < n)
			break;
	}

	*dst = 0;

	return num_bytes;
}

