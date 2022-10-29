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

/* argument_list.cc
 * spawned from mmap-reload.cc on 2021-10-23 22:59 CET DST.
 */

// mtodorov, 2021-10-24 00:00 v0.03.01 class spawn, cleaned up a bit.
// mtodorov, 2021-10-23 v0.02.02 Enabled for (stdin) list of arguments. Good for piping from find or xargs.
//                                   FIXME: Lists are not released on fatal errors.
// mtodorov, 2021-10-23 v0.02.01 Fixed name printing for file list arguments.
// mtodorov, 2021-10-23 v0.02.00 Added file list arguments such as result from find.
//                                   Regression: filename print doesn't work with file list. Must strdup (FIXME: FIXED).
// mtodorov, 2021-10-23 v0.01.05 Some pretty verbose output.
// mtodorov, 2021-10-23 v0.01.04 Fixed SEGFAULT in ReleaseMappedRegions().
// mtodorov, 2021-10-23 v0.01.03 added malloc() security to list allocation in RegisterRegion().
//                                   Regression: introduced core dump after unmap() (FIXME: FIXED).
//                                   FOUND: pointer set to NULL before used.
// mtodorov, 2021-10-23 v0.01.02 SIGHUP reload bug fixed.
// mtodorov, 2021-10-23 v0.01.01 SIGHUP reload apparently works.
//                                   - after first reload only one m1 test file was mapped (FIXME: FIXED).
//                                   - it appears only last mapped file from the list remains mapped.
//                                   FOUND: head pointer was left pointing at the unallocated list
// mtodorov, 2021-10-23 v0.01.00 allowed indirect mmaping over the argument list list (TESTED).
//                                   (will need for implementing SIGHUP reload)
// mtodorov, 2021-10-23 v0.00.08 implemented locking on SIGUSR1 and unlocking on SIGUSR2 (TESTED).
// mtodorov, 2021-10-23 v0.00.07 implemented wait_seconds arg parm (TESTED).
// mtodorov, 2021-10-23 v0.00.06 implemented signal handling for SIGINT and SIGTERM (TESTED).
// mtodorov, 2021-10-23 v0.00.05 implemented mmaped region locking (TESTED).
// mtodorov, 2021-10-23 v0.00.04 implemented arguments, options and long options.
// mtodorov, 2021-10-23 v0.00.03 rearranged for some modularity to enable planned additional functionality
// mtodorov, 2021-10-23 v0.00.02 made error messages more elegant
// mtodorov, 2021-09-30 v0.00.01 Preload mmaped files from apache2 config.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/param.h>

#include "mmap-preload.h"
#include "argument_list.h"
#include "mapped_file.h"
#include "my_aux.h"

void argument_file::AddArgumentToList (char *filename, enum arg_type arg_type)
{
	if (head == NULL) {
	    head = (argument_file *) malloc (sizeof (argument_file));
	    current = head;
	} else {
	    current->next = (argument_file *) malloc (sizeof (argument_file));
	    current = current->next;
	}
	if (current == NULL)
	    FATALMSG_AND_EXIT("malloc", filename, EXIT_FAIL_ENOMEM);
	current->next = NULL;
	current->filename = strdup (filename);
	if (current->filename == NULL)
	    FATALMSG_AND_EXIT("malloc", filename, EXIT_FAIL_ENOMEM);
	current->arg_type = arg_type;
}

void argument_file::DeleteList(void)
{
	argument_file *p = head;
	head = NULL;
	current = NULL;

	while (p != NULL)
	{
	    if (p->filename) {
		free (p->filename);
		p->filename = NULL;
	    }
	    argument_file *tmp = p;
	    p = p->next;
	    tmp->next = NULL;  // some use-after-free prevention
	    free (tmp);
	}
}

void argument_file::AddArgumentFile (char *filename)
{
	AddArgumentToList (filename, ARG_FILE);
}

void argument_file::AddArgumentFileList (char *filename)
{
	AddArgumentToList (filename, ARG_FILELIST);
}
	

void argument_file::AddArgumentConfFile (char *filename)
{
	AddArgumentToList (filename, ARG_CONFFILE);
}

int MMapFromFileList (char *file_list_name)
{
	FILE *fp;
	char *filename = NULL;
	size_t len = 0;
	int cnt = 0;
	bool do_close = true;
	int n;

	if (strcmp (file_list_name, "-") == 0) {
	    fp = stdin;
	    do_close = false;
	} else if ((fp = fopen (file_list_name, "r")) == NULL) {
	    FATALMSG_AND_EXIT("open", file_list_name, EXIT_FAIL_NOARG);
	}

	while ((n = getline (&filename, &len, fp)) != -1) {
	    if (filename[n - 1] == '\n')
		filename[n - 1] = '\0';
	    if (MMapFile (mappedFileList, filename))
		cnt ++;
	}

	if (do_close)
	    fclose (fp);

	return cnt;

}

int MMapFromApacheConfigFile (char *config_file_name)
{
	FILE *fp;
	char command [1024];
	char filename [MAXPATHLEN+1];
	int cnt = 0;

	if ((fp = fopen (config_file_name, "r")) == NULL) {
	    FATALMSG_AND_EXIT("open", config_file_name, EXIT_FAIL_NOARG);
	}

	while (fscanf (fp, "%s %s\n", command, filename) == 2) {
	    if (strcmp (command, "MMapFile") == 0) {
		if (MMapFile (mappedFileList, filename)) cnt ++;
	    }
	}

	fclose (fp);

	return cnt;

}


int argument_file::MMapFromArgumentList ()
{
	argument_file *p = head;
	int cnt = 0;
	int tot_files = 0;
	size_t tot_alloc = 0;
	size_t sys_mem_free;

	for ( ; p != NULL ; p = p->next )
	{
	    switch (p->arg_type) {
		case ARG_FILE:
		    if (verbose) fprintf(stderr, "Mapping file \"%s\".\n", p->filename);
		    if (MMapFile (mappedFileList, p->filename)) tot_files ++;
		    break;
		case ARG_FILELIST:
		    if (verbose) fprintf(stderr, "Mapping from file list \"%s\".\n", p->filename);
		    tot_files += MMapFromFileList (p->filename);
		    break;
		case ARG_CONFFILE:
		    if (verbose) fprintf(stderr, "Mapping from config file \"%s\".\n", p->filename);
		    tot_files += MMapFromApacheConfigFile (p->filename);
		    break;
		default:
		    fprintf (stderr, "Bug: Unknown argument type in list (%d).\n", p->arg_type);
		    exit(EXIT_FAIL);
		    break;
	    }
	    cnt ++;
	    tot_alloc = mappedFileList->TotAlloc();
	    if (tot_alloc > total_memory_use_limit) {
		if (verbose) fprintf(stderr, "Warning: total memory use limit exceeded (%lu > %lu). Ignoring further requests.\n",
						tot_alloc, total_memory_use_limit);
		break;
	    }
	    sys_mem_free = getMemorySystemParameter ("MemFree");
	    if (sys_mem_free < memory_free_safeguard) {
		if (verbose) fprintf(stderr, "Warning: system free memory low (%lu KiB). Ignoring further requests.\n",
						sys_mem_free);
		break;
	    }
	}

	return cnt;
}

