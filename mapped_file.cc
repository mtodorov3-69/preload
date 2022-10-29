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

/*
 * File: mapped_file.cc, spawened from mmap-reload.cc on 2021-10-23 23:12
 *
 */

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
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/fcntl.h>
#include <sys/param.h>

#include "mmap-preload.h"
#include "argument_list.h"
#include "mapped_file.h"

size_t page_sz = sysconf(_SC_PAGESIZE);

mapped_file_list *mappedFileList = NULL;

mapped_file::mapped_file (char *filename, void *mem_addr, size_t segment_size, struct stat *st_buf, bool locked)
{
	this->addr = mem_addr;
	this->len  = segment_size;
	this->mapped = true;
	this->locked = locked;
	this->filename = strdup (filename);
	if (this->filename == NULL)
	    FATALMSG_AND_EXIT("malloc", filename, EXIT_FAIL_ENOMEM);
	this->prev = NULL;
	this->next = NULL;
	this->last_stat = *st_buf;
}

mapped_file::~mapped_file (void)
{
	// this->UnRegister();
}

void mapped_file::UnRegister (void)
{
	if (this->locked) this->UnLock();
	if (this->mapped) {
	    if (verbose) fprintf (stderr, "Unmapping region \"%s\" (%p, %lu)\n", this->filename, this->addr, this->len);
	    if (munmap (this->addr, this->len) == -1)
		WARNMSG ("munmap", this->filename); // this is bad, but try to unmap as much as possible
	}
	this->addr = NULL;
	this->len  = 0;
	free (this->filename);
	this->filename = NULL;
	if (this->prev)
		this->prev->next = this->next;
	if (this->next)
		this->next->prev = this->prev;
	this->prev = NULL;
	this->next = NULL;
}

bool mapped_file::Lock()
{
	if (!this->locked) {
	    if (verbose) fprintf (stderr, "Locking this \"%s\" (%p, %lu)\n", this->filename, this->addr, this->len);
	    if (mlock (this->addr, this->len) == -1) {
		WARNMSG ("mlock", this->filename); // non fatal
	    } else
		this->locked = true;
	}
	return this->locked;
}

bool mapped_file::UnLock()
{
	if (this->locked) {
	    if (verbose) fprintf (stderr, "Unlocking region \"%s\" (%p, %lu)\n", this->filename, this->addr, this->len);
	    if (munlock (this->addr, this->len) == -1) {
		WARNMSG ("munlock", this->filename); // non fatal
	    } else {
		this->locked = false;
	    }
	}
	return !this->locked;
}

bool mapped_file::UnMap()
{
	if (this->locked)
	    this->UnLock();
	if (this->mapped) {
	    if (verbose) fprintf (stderr, "Unmapping region \"%s\" (%p, %lu)\n", this->filename, this->addr, this->len);
	    if (munmap (this->addr, this->len) == -1) {
		WARNMSG ("munmap", this->filename); // this is bad, but try to unmap as much as possible
	    } else
		this->mapped = false;
	}
	return !this->mapped;
}

void mapped_file::Refresh(void)
{
	volatile unsigned char c;
        mapped_file *p = this;
        char *pchar = (char *)p->addr;

        if (verbose) fprintf (stderr, "Refreshing \"%s\" (%p, %lu)\n", p->filename, p->addr, p->len);
        while (pchar - (char *)p->addr <= p->len)
        {
            c = (c + *pchar) % 256;
            pchar += page_sz;
        }
}

int mapped_file::Mincore()
{
	size_t page_sz = sysconf (_SC_PAGESIZE);
	int npages = (this->len + page_sz - 1)/ page_sz, npages_incore = 0;
	unsigned char *vec = (unsigned char *) calloc (npages, 1);

	if (mincore (this->addr, this->len, vec) == -1)
		WARNMSG("mincore", this->filename);
	else {
	    for (int i = 0; i < npages; i++)
		npages_incore += vec[i] & 0x00000001;
	}

	free (vec);
	if (verbose) fprintf (stderr, "Mincore: len = %ld, npages_incore = %d\n", this->len, npages_incore);
	return npages_incore;
}

mapped_file * mapped_file_list::RegisterRegion(mapped_file *mappedFile)
{
	if (head == NULL) {
	    head = mappedFile;
	    current = head;
	} else {
	    current->next = mappedFile;
	    mappedFile->prev = current;
	    current = current->next;
	}
	if (current == NULL)
	    FATALMSG_AND_EXIT("malloc", mappedFile->filename, EXIT_FAIL_ENOMEM);

	if (verbose) fprintf (stderr, "Registered region \"%s\" (%p, %lu)\n", mappedFile->filename, mappedFile->addr, mappedFile->len);

	tot_len += mappedFile->len;
	tot_alloc += (mappedFile->len / page_sz + 1) * page_sz;
	tot_files ++;

	return current;
}

mapped_file * mapped_file_list::RegisterRegion(char *filename, void *mem_addr, size_t segment_size, struct stat *st_buf, bool locked)
{
	mapped_file *mappedFile = new mapped_file (filename, mem_addr, segment_size, st_buf, locked);

	return this->RegisterRegion (mappedFile);
}

void mapped_file_list::RefreshMappedRegions(void)
{
	volatile unsigned char   c;
	struct stat stat_buf;
	mapped_file *current = head, *newMapping;

	while (current != NULL) {
	    mapped_file *next = current->next;
	    if (stat (current->filename, &stat_buf) == -1)
	    {
		WARNMSG("stat", current->filename);
		current->UnRegister();
	    } else if (current->last_stat.st_size         != stat_buf.st_size         ||
		       current->last_stat.st_mtim.tv_sec  != stat_buf.st_mtim.tv_sec  ||
		       current->last_stat.st_mtim.tv_nsec != stat_buf.st_mtim.tv_nsec ||
		       current->last_stat.st_ctim.tv_sec  != stat_buf.st_ctim.tv_sec  ||
		       current->last_stat.st_ctim.tv_nsec != stat_buf.st_ctim.tv_nsec)
	    {
		WARNMSG("stat changed, reloading", current->filename);
		char *filename;
		if (!(filename = strdup (current->filename)))
			FATALMSG_AND_EXIT("malloc", filename, EXIT_FAIL_ENOMEM);
		current->UnRegister();
		delete current;
		if (newMapping = MMapFile (filename))
		    Register (this, newMapping);
		free (filename);
	    } else {
		current->Refresh();
	    }
	    current = next;
	}
}

/*
void mapped_file_list::RefreshMappedRegions (void)
{

	// The page is mapped successfully, now pagefault it in to actually preload file.
	mapped_file *region = head;

	while (region != NULL) {
	    region->Refresh();
	    region = region -> next;
	}
}
*/

void mapped_file_list::LockMappedRegions (void) {
	// called at termination signal, so it should be async-safe
	mapped_file *region = head;

	while (region != NULL) {
	    if (!region->locked)
		region->Lock();
	    region = region->next;
	}
}

void mapped_file_list::UnlockMappedRegions (void) {
	// called at termination signal, so it should be async-safe
	mapped_file *region = head;

	while (region != NULL) {
	    if (region->locked)
		region->UnLock();
	    region = region->next;
	}
}

int mapped_file_list::Mincore (void) {
	// called at termination signal, so it should be async-safe
	mapped_file *region = this->head;
	int npages_incore_tot = 0;

	while (region != NULL) {
	    if (region->mapped)
		npages_incore_tot += region->Mincore();
	    region = region->next;
	}

	return npages_incore_tot;
}

void mapped_file_list::ReleaseMappedRegions (void) {
	// called at termination signal, so it should be async-safe
	mapped_file *region = head;
	head = NULL;

	while (region != NULL) {
	    if (region->locked)
		region->UnLock();
	    if (region->mapped)
		region->UnMap();
	    tot_alloc -= region->len;
	    tot_files --;
	    mapped_file * p = region;
	    region = region->next;
	    delete p;
	}
}

class mapped_file * MMapFile (char *filename) {

	// mmap filename in memory, do the boring stuff

	struct stat st_buf;
	int 	     fd;
	void        *maddr;
	size_t	     segment_size;
	bool	     locked;

	if ((fd = open (filename, O_RDONLY)) == -1) {
	    ERRMSG ("open", filename);
	    return NULL;
	}

	if (fstat (fd, &st_buf) == -1) {
	    ERRMSG ("fstat", filename);
	    return NULL;
	}

	segment_size = st_buf.st_size;

	if (verbose) {
		fprintf (stderr, "MMapping \"%s\" (%lu bytes, memory use %lu KiB): ", filename, segment_size, (segment_size / page_sz + 1) * page_sz / 1024);
	}

	if (segment_size == 0) {
	    if (verbose) fprintf (stderr, "Warning: File size of \"%s\" is zero (0). Not mapped.\n", filename);
	    return NULL; // let's not complicate mapping from find output, silently ignore
	} else if (safety_mmap_limit && segment_size > safety_mmap_limit) {
	    segment_size = safety_mmap_limit;
	    fprintf (stderr, "MMapping \"%s\": mmapping safety limit exceeded, mapped only first (%lu bytes, memory use %lu KiB): ",
			     filename, segment_size, (segment_size / page_sz + 1) * page_sz / 1024);
	}

	if ((maddr = mmap (0, segment_size, PROT_READ, MAP_SHARED, fd, 0)) == MAP_FAILED) {
	    ERRMSG ("mmap", filename);
	    return NULL;
	}

	if (verbose) printf ("done\n");

	if (close (fd) == -1) {
	    ERRMSG ("close", filename);   // non fatal
	}

	if (madvise (maddr, segment_size, MADV_WILLNEED) == -1) {
	    ERRMSG ("madvise", filename); // non fatal
	}

	locked = false;

	if (region_lock) {
	    int retcode;

	    if (verbose >= 2) fprintf(stderr, "Locking region (%p, %lu)\n", maddr, segment_size);
	    switch (lock_on_fault) {
	    	case true:  retcode = mlock2 (maddr, segment_size, MLOCK_ONFAULT); break;
	    	case false: retcode = mlock (maddr, segment_size); break;
	    }
	    if (retcode == -1) {
		locked = false;
		ERRMSG("mlock", filename);  // non fatal
	    } else
		locked = true;
		
	}

	return new mapped_file (filename, maddr, segment_size, &st_buf, locked);
}

class mapped_file * MMapFile (mapped_file_list *mappedFileList, char *filename) {
	mapped_file *newMapping = MMapFile (filename), *ret;

	if (newMapping)
	    ret = mappedFileList->RegisterRegion(newMapping);
	else
	    ret = NULL;

	return ret;
}

class mapped_file * Register (mapped_file_list *mappedFileList, char *filename) {
	return MMapFile (mappedFileList, filename);
}

class mapped_file * Register (mapped_file_list *mappedFileList, mapped_file *newMapping) {
	return mappedFileList->RegisterRegion(newMapping);
}

