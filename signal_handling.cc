/*
 * Copyleft Mirsad Todorovac 2021
 *
 * Preload important files in memory and periodically page them in
 *
 * Copying by Gnu General Public License version 3 or later
 *
 * Use, compiling, modifying and using in packages, free and commercial, allowed as long
 * as the above copyright notice is preserved.
 *
 */

/* File: signal_handling.cc, spawned from mmap-preload.cc on 2021-10-24 00:22
 */
// mtodorov, 2021-10-23 23:54 v0.03.00 Classes version now apparently works (Hooray!).
// mtodorov, 2021-10-23       v0.02.02 Enabled for (stdin) list of arguments. Good for piping from find or xargs.
//                                         FIXME: Lists are not released on fatal errors.
// mtodorov, 2021-10-23       v0.02.01 Fixed name printing for file list arguments.
// mtodorov, 2021-10-23       v0.02.00 Added file list arguments such as result from find.
//                                         Regression: filename print doesn't work with file list. Must strdup (FIXME: FIXED).
// mtodorov, 2021-10-23       v0.01.05 Some pretty verbose output.
// mtodorov, 2021-10-23       v0.01.04 Fixed SEGFAULT in ReleaseMappedRegions().
// mtodorov, 2021-10-23       v0.01.03 added malloc() security to list allocation in RegisterRegion().
//                                         Regression: introduced core dump after unmap() (FIXME: FIXED).
//                                         FOUND: pointer set to NULL before used.
// mtodorov, 2021-10-23       v0.01.02 SIGHUP reload bug fixed.
// mtodorov, 2021-10-23       v0.01.01 SIGHUP reload apparently works.
//                                         - after first reload only one m1 test file was mapped (FIXME: FIXED).
//                                         - it appears only last mapped file from the list remains mapped.
//                                         FOUND: head pointer was left pointing at the unallocated list
// mtodorov, 2021-10-23       v0.01.00 allowed indirect mmaping over the argument list list (TESTED).
//                                         (will need for implementing SIGHUP reload)
// mtodorov, 2021-10-23       v0.00.08 implemented locking on SIGUSR1 and unlocking on SIGUSR2 (TESTED).
// mtodorov, 2021-10-23       v0.00.07 implemented wait_seconds arg parm (TESTED).
// mtodorov, 2021-10-23       v0.00.06 implemented signal handling for SIGINT and SIGTERM (TESTED).
// mtodorov, 2021-10-23       v0.00.05 implemented mmaped region locking (TESTED).
// mtodorov, 2021-10-23       v0.00.04 implemented arguments, options and long options.
// mtodorov, 2021-10-23       v0.00.03 rearranged for some modularity to enable planned additional functionality
// mtodorov, 2021-10-23       v0.00.02 made error messages more elegant
// mtodorov, 2021-09-30       v0.00.01 Preload mmaped files from apache2 config.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/fcntl.h>
#include <sys/param.h>

#include "mmap-preload.h"
#include "signal_handling.h"

volatile enum async_states async_state = STATE_OK;

void
sigterm_handler (int signum)
{
	if (verbose) write (1, "Received SIGTERM.\n", 18);
	async_state = STATE_TERM;
}

void
sigusr1_handler (int signum)
{
	if (verbose) write (1, "Received SIGUSR1.\n", 18);
	async_state = STATE_LOCK;
}

void
sigusr2_handler (int signum)
{
	if (verbose) write (1, "Received SIGUSR2.\n", 18);
	async_state = STATE_UNLOCK;
}

void
sigint_handler (int signum)
{
	async_state = STATE_INT;
}

void
sighup_handler (int signum)
{
	if (verbose) write (1, "Received SIGHUP.\n", 17);
	async_state = STATE_RELOAD;
}

struct sigaction new_sigterm_action, old_sigterm_action;
struct sigaction new_sigint_action, old_sigint_action;
struct sigaction new_sighup_action, old_sighup_action;
struct sigaction new_sigusr1_action, old_sigusr1_action;
struct sigaction new_sigusr2_action, old_sigusr2_action;

void init_signal_handlers (void)
{
	new_sigterm_action.sa_handler = sigterm_handler;
	new_sigterm_action.sa_flags = 0;
	sigemptyset (&new_sigterm_action.sa_mask);
	sigaction (SIGTERM, &new_sigterm_action, &old_sigterm_action);

	new_sigint_action.sa_handler = sigint_handler;
	new_sigint_action.sa_flags = 0;
	sigemptyset (&new_sigint_action.sa_mask);
	sigaction (SIGINT, &new_sigint_action, &old_sigint_action);

	new_sighup_action.sa_handler = sighup_handler;
	new_sighup_action.sa_flags = 0;
	sigemptyset (&new_sighup_action.sa_mask);
	sigaction (SIGHUP, &new_sighup_action, &old_sighup_action);

	new_sigusr1_action.sa_handler = sigusr1_handler;
	new_sigusr1_action.sa_flags = 0;
	sigemptyset (&new_sigusr1_action.sa_mask);
	sigaction (SIGUSR1, &new_sigusr1_action, &old_sigusr1_action);

	new_sigusr2_action.sa_handler = sigusr2_handler;
	new_sigusr2_action.sa_flags = 0;
	sigemptyset (&new_sigusr2_action.sa_mask);
	sigaction (SIGUSR2, &new_sigusr2_action, &old_sigusr2_action);
}

