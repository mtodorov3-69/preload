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

// mtodorov, 2022-10-28 11:50 v0.05.01 automatic reloading of modified files
//                                     allow space in filenames in file lists (but not apache2 conf MMapFile)
// mtodorov, 2021-11-04 16:35 v0.04.05 fixed bug with leaking file descriptors in memfree.cc
// mtodorov, 2021-11-04 12:20 v0.04.01 avoided calling system to lock what is already locked
//                                         and unlock what isn't.
// mtodorov, 2021-11-04 10:30 v0.04.00 added some memory safeguards (BETA).
// mtodorov, 2021-11-04 08:43 v0.03.07 some class declaration hardening.
// mtodorov, 2021-11-04 08:22 v0.03.05 Added total preaload limit 1 GiB (security DoS safeguarding).
// mtodorov, 2021-11-04 07:53 v0.03.04 Added preaload limit 64M (security DoS safeguarding).
// mtodorov, 2021-10-24 00:37 v0.03.03 Separation into modules finished.
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
#include <getopt.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/fcntl.h>
#include <sys/param.h>

#include "mmap-preload.h"
#include "argument_list.h"
#include "mapped_file.h"
#include "signal_handling.h"
#include "my_aux.h"

bool	debug		= false;
int	verbose		= 1;
bool	region_lock	= false;
bool    lock_on_fault	= false;
bool	exit_on_errors	= true;
bool	unload_on_exit	= true;
int	wait_seconds	= 60;
size_t	safety_mmap_limit = 64 * 1024 * 1024; // 64 M, we want to speed up the systemby preloading files,
					     //     and not bring it down on its knees
size_t	total_memory_use_limit = 1024 * 1024 * 1024; // 1 GiB, this should be enough for a smaller webiste,
size_t	memory_free_safeguard = 256 * 1024;  // 256 MiB

argument_file *argFileList = NULL;
						     //    we are not making a DoS tool!!!
void main_cleanup (void)
{
	if (verbose)
		fprintf (stderr, "Entered cleanup procedure.\n");
	if (argFileList)    { delete argFileList; argFileList = NULL; }
	if (mappedFileList) {
		mappedFileList->ReleaseMappedRegions();
		delete mappedFileList;
		mappedFileList = NULL;
	}
	if (verbose)
		fprintf (stderr, "Done cleanup.\n");
}

int main (int argc, char **argv)
{
	int  c;
	int  digit_optind = 0;

	init_signal_handlers();
	atexit (main_cleanup);

	argFileList = new argument_file();
	mappedFileList = new mapped_file_list();

	while (1) {
	    int this_option_optind = optind ? optind : 1;
	    int option_index = 0;
	    static struct option long_options[] = {
		{"config-file",		required_argument,	0, 'c'},
		{"file-list",		required_argument,	0, 'L'},
		{"file",		required_argument,	0, 'f'},
		{"verbose",		no_argument,		0, 'v'},
		{"silent",		no_argument,		0, 's'},
		{"ignore-errors",	no_argument,		0, 'i'},
		{"exit-on-errors",	no_argument,		0, 'e'},
		{"memory-lock",		no_argument,		0, 'l'},
		{"unload-on-exit",	no_argument,		0, 'u'},
		{"wait",		required_argument,	0, 'w'},
		{0,			0,			0,  0 }
	    };

	    c = getopt_long(argc, argv, "c:f:eilL:svw:u",
		     long_options, &option_index);
	    if (c == -1)
		break;

	    if (debug) fprintf(stderr, "getopt returned %c\n", c);

	    switch (c) {
	        case 0:
		    printf("option %s", long_options[option_index].name);
		    if (optarg)
			printf(" with arg %s", optarg);
		    printf("\n");
		    break;

		case 'c':
		    if (verbose) fprintf(stderr, "Adding config file \"%s\" to list\n", optarg);
		    argFileList->AddArgumentConfFile (optarg);
		    break;

		case 'f':
		    if (verbose) fprintf(stderr, "Adding file \"%s\" to list\n", optarg);
		    argFileList->AddArgumentFile (optarg);
		    break;

		case 'L':
		    if (verbose) fprintf(stderr, "Adding file list \"%s\" to list\n",
						 strcmp (optarg, "-") ? optarg : "(stdin)");
		    argFileList->AddArgumentFileList (optarg);
		    break;

		case 'w':
		    if (verbose) fprintf(stderr, "Wait period = %s\n", optarg);
		    if (strspn (optarg, "0123456789") == strlen (optarg))
			wait_seconds = atoi (optarg);
		    else {
			fprintf (stderr, "%s: illegal argument. Decimal number expected (seconds).\n", optarg);
			exit (EXIT_FAILURE);
		    }
		    break;

		case 'v':
		    verbose ++;
		    break;

		case 's':
		    verbose = 0;
		    break;

		case 'd':
		    debug = true;
		    break;

		case 'l':
		    region_lock = true;
		    break;

		case 'u':
		    unload_on_exit = true;
		    break;

		case 'e':
		    exit_on_errors = true;
		    break;

		case 'i':
		    exit_on_errors = false;
		    break;

                default:
                    printf("?? getopt returned character code 0%o ??\n", c);
            }
	}

	if (optind < argc) {
	    if (debug) fprintf (stderr, "non-option arguments:\n");
	    while (optind < argc) {
		char *filename = argv[optind++];
		if (verbose) fprintf(stderr, "Adding file %s to list\n", filename);
		argFileList->AddArgumentFile (filename);
	    }
	}

	argFileList->MMapFromArgumentList();

	fprintf (stderr, "Total mmaped files: %d\n", mappedFileList->TotFiles());
	fprintf (stderr, "Total file length: %lu KiB\n", mappedFileList->TotLen() / 1024);
	fprintf (stderr, "Total mmaped memory: %lu KiB\n", mappedFileList->TotAlloc() / 1024);

	while (FOREVER) {
	    if (verbose) fprintf (stderr, "State = %d\n", async_state);

	    if (async_state == STATE_OK)
		sleep (wait_seconds);
	
	    switch (async_state) {
		case STATE_OK:
		    if (verbose) fprintf (stderr, "Mincore=%d KiB, Preloading files ... \n", mappedFileList->Mincore() * 4);
		    mappedFileList->RefreshMappedRegions();
		    if (verbose) fprintf (stderr, "Mincore=%d KiB, done.\n", mappedFileList->Mincore() * 4);
		    break;

		case STATE_TERM:
		case STATE_INT:
		    if (verbose) fprintf (stderr, "Releasing mmaped files ... \n");
		    mappedFileList->ReleaseMappedRegions();
		    if (verbose) fprintf (stderr, "done.\n");
		    if (argFileList) {
			delete argFileList;
			argFileList = NULL;
		    }
		    exit (0);
		    break;

		case STATE_LOCK:
		    mappedFileList->LockMappedRegions();
		    async_state = STATE_OK;
		    break;

		case STATE_UNLOCK:
		    mappedFileList->UnlockMappedRegions();
		    async_state = STATE_OK;
		    break;

		case STATE_RELOAD:
		    fprintf (stderr, "Reloading ...\n");
		    mappedFileList->ReleaseMappedRegions();
		    argFileList->MMapFromArgumentList();
		    fprintf (stderr, "Done reloading.\n");
		    async_state = STATE_OK;
		    break;

		default:
		    fprintf (stderr, "Uknown state: %d\n", async_state);
		    break;
	    }
	
	    if (async_state == STATE_OK)
		sleep (wait_seconds);

	}

}

