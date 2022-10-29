
/*
 * Tool for checking free memory on Linux
 *
 * Copyleft (C) Mirsad Todorovac 2021-11-04
 *
 * function to get actual free memory on a Linux compatible system
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "my_aux.h"

extern  bool	debug;

#ifdef DEBUG
bool	debug = true;
#endif

int	getMemorySystemParameter (const char * const systemParm)
{
	char	 *parameter;
	unsigned  value;
	int	  retval;
	int 	  n;
	char	 *line = NULL;
	size_t	  sz = 0;

	FILE     *fp = fopen ("/proc/meminfo", "r");
	if (fp == NULL)
		return -1;

	retval = -1;

	while (getline (&line, &sz, fp) != -1)
	{
		n = sscanf (line, "%m[A-Za-z0-9]: %d%*s\n", &parameter, &value);
		if (debug) fprintf (stderr, "n = %d, Parameter = '%s', value = %d\n", n, parameter, value);
		if (strcmp (parameter, systemParm) == 0)
		{
			if (debug) fprintf (stderr, "Equal\n");
			free (parameter);
			retval = value;
			break;
		}
		if (debug) fprintf (stderr, "Not Equal\n");
		free (parameter);
	}

	free (line);
	fclose (fp);
	return retval;
}

size_t	totalMemFreeOnSystem (void)
{
	return (getMemorySystemParameter ("MemFree"));
}

#ifdef DEBUG

int main (int argc, char *argv[])
{
	printf ("MemFree: %lu\n", totalMemFreeOnSystem());
}

#endif

