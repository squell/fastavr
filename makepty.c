#define _XOPEN_SOURCE 600
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

/* this opens /dev/ptmx to get a master/slave pair, so we don't need 'socat' */

const char* make_stdin_pty(void)
{
	int fd = posix_openpt(O_RDWR);
	grantpt(fd);
	unlockpt(fd);
	dup2(fd, STDIN_FILENO);
	dup2(fd, STDOUT_FILENO);
	return ptsname(fd);
}
