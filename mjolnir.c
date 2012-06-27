/*
 * mjolnir - simple and portable shredder (secure file eraser)
 * Copyright (c) 2010-2012 Guillermo Ramos Gutiérrez <0xwille@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ''AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL COPYRIGHT HOLDERS OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#define _XOPEN_SOURCE 600	// SUSv3 - required by fsync(2) & sigaction(2)

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define BUFLEN		0x80000		// randbuf length (bytes)
#define LOOPS		4			// number of times the file is overwritten
#define BARLEN		40			// length of the progress bar

// http://www.gnu.org/software/libc/manual/html_mono/libc.html#Limits-for-Files
#ifndef PATH_MAX
# define PATH_MAX FILENAME_MAX
#endif


static int interrupted = 0;
static int recursive = 0;

static int shred(char *path);	// recursive; must declare it here


/* All-in-one signal handler */
static void sighandler(int sig)
{
	switch (sig) {
	case SIGINT:
		interrupted = 1;
		break;
	}
}


/* Unified error message format */
static void err(char *path)
{
	fprintf(stderr, "[-] ERROR %d with file '%s': %s\n",
			errno, path, strerror(errno));
}


/* Print program usage and exit */
static void usage(char *path, int retval)
{
	printf("Usage: %s [-hr] [FILE]...\n"
		"Securely erase FILEs so that they can hardly be recovered\n"
		"\nOPTIONS:\n-h\n\tshow this help\n-r\n-R\n"
		"\trecursive mode (erase directories with all their content)\n", path);
	exit(retval);
}


/* Print a progress bar showing how's the shredding going by the moment */
static void show_progress(int jump)
{
	float jump_len = (float)BARLEN / LOOPS;
	int i;

	printf("\r\t[");

	for (i = 0; i < round(jump * jump_len); i++)
		putchar('#');
	for (i = 0; i < BARLEN - round(jump * jump_len); i++)
		putchar('-');

	switch (jump % 4) {
	case 0:
		printf("] | ");
		break;
	case 1:
		printf("] / ");
		break;
	case 2:
		printf("] - ");
		break;
	case 3:
		printf("] \\ ");
	}

	fflush(stdout);
}


/* Overwrites file with 1's, 0's, and random characters LOOPS times. */
static int shred_file(char *path)
{
	struct sigaction sact, soact;	// signal control structs
	void *map;						// mmap'ed file
	char randbuf[BUFLEN];			// buffer with random data
	unsigned tofill;				// useful part of the randbuf
	int remaining, offset;			// controlling randbuf memcpy
	size_t len;						// file length
	int fd;
	unsigned i, j;

	if ((fd = open(path, O_RDWR)) == -1)
		return -1;
	if ((len = lseek(fd, 0, SEEK_END)) == 0) {
		printf("[+] File '%s' has length 0, skipped\n", path);
		return 0;
	}

	// Let's zero these garbagish structs
	memset(&sact, 0, sizeof(struct sigaction));
	memset(&soact, 0, sizeof(struct sigaction));

	// Create memory map of target file
	map = mmap(NULL, len, PROT_WRITE, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED)
		return -1;

	// Set up the random buffer
	tofill = BUFLEN < len ? BUFLEN : len;
	for (i = 0; i < tofill; i++)
		randbuf[i] = rand() % 255;

	// Set the new signal behavior and save the previous one for later restore
	sact.sa_handler = sighandler;
	sigaction(SIGINT, &sact, &soact);

	printf("[+] Shredding '%s'...\n", path);
	show_progress(0);

	for (i = 0; i < LOOPS; i++) {

		// If received SIGINT during last loop, abort program
		if (interrupted) {
			sigaction(SIGINT, &soact, &sact);
			printf("\tSIGINT caught, aborting...\n");
			munmap(map, len);
			exit(0);
		}

		// Switch between writing \x00, \xFF and copying random data
		if (i % 3 == 0) {
			memset(map, 0, len);
		} else if (i % 3 == 1) {
			memset(map, 255, len);
		} else {

			/*
			 * With random data, we must be extra careful. Since the buffer
			 * and the file are nearly always going to have different sizes, we
			 * will write the buffer either several times or not even one. The
			 * remaining and offset variables are used to control what has
			 * already been overwritten so that we fill exactly the entire file
			 * without overflowing it.
			 */
			remaining = BUFLEN;
			offset = 0;

			while (remaining == BUFLEN) {
				if (((signed)len - offset) < remaining)
					remaining = len-offset;
				memcpy((char *)map + offset, randbuf, remaining);
				offset += remaining;
			}

			/*
			 * Only revert random buffer if it's not the last time the file is
			 * going to be filled with random data
			 */
			if (i < LOOPS-2)
				for (j = 0; j < tofill; j++)
					randbuf[j] = ~randbuf[j];
		}

		fsync(fd);				// Apply the changes from map into the HDD
		show_progress(i+1);		// Update progress bar
	}

	putchar('\n');
	munmap(map, len);
	close(fd);

	return 0;
}


/* Return true (!= 0) if path is a file, false otherwise */
static int is_dir(char *path)
{
	struct stat s;
	lstat(path, &s);
	return S_ISDIR(s.st_mode);
}


/* Rename path (file/dir) several times and unlink/rmdir it afterwards */
static int rename_del(char *path)
{
	int pathlen = strlen(path);
	char *old_path = path;
	char new_path[pathlen];
	char *p = strrchr(path, '/');
	int file_offset = p ? (p - path + 1) : 0;
	int retval = 0;
	int i;


	if (is_dir(path)) {
		printf("[+] Deleting %s...\n", path);
		return rmdir(path);
	} else {
		strncpy(new_path, old_path, pathlen+1);	// pathlen+1: copy the '\0' too

		while ((pathlen-file_offset) > 1) {

			// random new file name (FN -> new_path[file_offset..pathlen-1])
			for (i = file_offset; i < pathlen; i++)
				new_path[i] = (rand() % 26) + 'a';
			new_path[--pathlen] = '\0';		// reduce file length by 1 each time

			if ((retval = rename(old_path, new_path)))
				return retval;

			strncpy(old_path, new_path, pathlen+1);		// again, copy the '\0'
		}

		return unlink(new_path);
	}
}


/* Recursively shred all the files inside target dir */
static int shred_dir(char *path)
{
	char *filename = malloc(PATH_MAX * sizeof(char));
	struct dirent *ent;
	int retval = 0;
	DIR *d;

	if ((d = opendir(path)) == NULL)
		return -1;

	while ((ent = readdir(d)) != NULL) {
		snprintf(filename, PATH_MAX, "%s/%s", path, ent->d_name);
		if (strcmp(ent->d_name, ".") && strcmp(ent->d_name, ".."))
			retval |= shred(filename);
	}

	free(filename);

	return retval|closedir(d);
}


/* Detect path's type and call the apropiate functions to shred it */
static int shred(char *path)
{
	int retval = 0;

	if (access(path, F_OK) == -1)
		err(path);
	else if (is_dir(path)) {
		if (recursive) {
			if ((retval = shred_dir(path)) || (retval = rename_del(path)))
				err(path);
		} else {
			printf("[*] Omitting directory '%s' (-r to shred recursively)\n",
					path);
		}
	} else {
		if ((retval = shred_file(path)) || (retval = rename_del(path)))
			err(path);
	}

	return retval;
}


int main(int argc, char **argv)
{
	int opt, i;
	int retval = 0;

	if (argc < 2 || !strcmp(argv[1], "--help"))
		usage(*argv, 0);

	while ((opt = getopt(argc, argv, "hrR")) != -1)
		switch (opt) {
		case 'h':
			usage(*argv, 0);
			break;
		case 'r':
		case 'R':
			recursive = 1;
			break;
		case '?':
		default:
			usage(*argv, EINVAL);
		}

	srand((unsigned)(time(NULL) * (time_t)&i));	// ASLR is our friend }:D

	for (i = optind; i < argc; i++)
		retval |= shred(argv[i]);

	return retval;
}
