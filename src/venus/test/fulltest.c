/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#include <afs/param.h>
#include <afsconfig.h>

RCSID("$Header: /cvs/openafs/src/venus/test/fulltest.c,v 1.4 2001/07/05 15:21:12 shadow Exp $");

#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

main(argc, argv)
int argc;
char **argv; {
    char *dirName;
    char tempName[1024];
    struct stat tstat;
    struct timeval tvp[2];
    int fd1;
    int code;
#ifndef HAVE_GETCWD
    extern char *getwd();
#define getcwd(x,y) getwd(x)
#endif

    /* venus system tester */
    if (argc != 2) return printf("usage: fulltest <dir-to-screw-up>\n");
    dirName = argv[1];
    mkdir(dirName, 0777);
    if (chdir(dirName) < 0) return perror("chdir");
    if (getcwd(tempName, 1024) == 0) {
	return printf("Could not get working dir.\n");
    }
    /* now create some files */
    fd1 = open("hi", O_CREAT | O_TRUNC | O_RDWR, 0666);
    if (fd1 < 0) return perror("open1");
    if (close(fd1) < 0) return perror("close1");
    if (access("hi", 2) < 0) return printf("New file can not be written (access)\n");
    if (chmod ("hi", 0741) < 0) return perror("chmod1");
    if (stat("hi", &tstat) < 0) return perror("stat1");
    if ((tstat.st_mode & 0777) != 0741) return printf("chmod failed to set mode properly\n");
    
    fd1 = open("hi", O_RDWR);
    if (fd1 < 0) return perror("open2");
    if (fchmod(fd1, 0654) < 0) return perror("fchmod");
    if (fstat(fd1, &tstat) < 0) return perror("fstat1");
    if ((tstat.st_mode & 0777) != 0654) return printf("fchmod failed to set mode properly\n");
#if 0
	/* These appear to be defunct routines;
	 * I don't know what, if anything, replaced them */
    if (osi_ExclusiveLockNoBlock(fd1) < 0) return perror("flock1");
    if (osi_UnLock(fd1) < 0) return perror("flock/unlock");
#endif

/* How about shared lock portability? */
    {
	struct flock fl;

	fl.l_type = F_RDLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;

	if (fcntl(fd1, F_SETLK, &fl) == -1) return perror("fcntl1: RDLCK");

	fl.l_type = F_UNLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;

	if (fcntl(fd1, F_SETLK, &fl) == -1) return perror("fcntl2: UNLCK");

	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;

	if (fcntl(fd1, F_SETLK, &fl) == -1) return perror("fcntl3: WRLCK");

	fl.l_type = F_UNLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;

	if (fcntl(fd1, F_SETLK, &fl) == -1) return perror("fcntl4: UNLCK");
    }

    if (fsync(fd1) < 0) return perror("fsync");
    if (write(fd1, "hi\n", 3) != 3) return perror("write");
    if (ftruncate(fd1, 2) < 0) return perror("ftruncate");
    if (close(fd1) < 0) return perror("close2");
    
    fd1 = open("hi", O_RDONLY);
    if (fd1 < 0) return perror("open3");
    if (read(fd1, tempName, 100) != 2) return perror("read2");
    if (close (fd1) < 0) return perror("close3");

    if (link("hi", "bye") < 0) return perror("link");
    if (stat("bye", &tstat) < 0) return perror("link/stat");
    
    if (unlink("bye")<0) return perror("unlink");
    
    if (symlink("hi", "bye") < 0) return perror("symlink");
    if (readlink("bye", tempName, 100) != 2) return perror("readlink");
    if (strncmp(tempName, "hi", 2) != 0) return printf("readlink contents");
    if (mkdir("tdir", 0777) < 0) return perror("mkdir");
    fd1 = open("tdir/fdsa", O_CREAT | O_TRUNC, 0777);
    close(fd1);
    if (rmdir("tdir") == 0) return printf("removed non-empty dir\n");
    if (unlink("tdir/fdsa") < 0) return perror("unlink tdir contents");
    if (rmdir("tdir") < 0) return perror("rmdir");
    
    fd1 = open (".", O_RDONLY);
    if (fd1<0) return perror("open dot");
    if (read(fd1, tempName, 20) < 20) perror("read dir");
    close(fd1);

    fd1 = open("rotest", O_RDWR | O_CREAT, 0444);
    if (fd1<0) return perror("open ronly");
    fchown(fd1, 1, -1);    /* don't check error code, may fail on Ultrix */
    code = write(fd1, "test", 4);
    if (code != 4) {
	printf("rotest short read (%d)\n", code);
	exit(1);
    }
    code = close(fd1);
    if (code) return perror("close ronly");
    code = stat("rotest", &tstat);
    if (code < 0) return perror("stat ronly");
    if (tstat.st_size != 4) {
	printf("rotest short close\n");
	exit(1);
    }
    if (unlink("rotest")<0) return perror("rotest unlink");
    
    if (rename("hi", "bye") < 0) return perror("rename1");
    if (stat("bye", &tstat) < 0) return perror("rename target invisible\n");
    if (stat("hi", &tstat) == 0) return printf("rename source still there\n");
    
#ifndef	AFS_AIX_ENV
/* No truncate(2) on aix so the following are excluded */
    if (truncate("bye", 1) < 0) return perror("truncate");
    if (stat("bye", &tstat) < 0) return perror("truncate zapped");
    if (tstat.st_size != 1) return printf("truncate failed\n");
#endif
    if (utimes("bye", tvp) < 0) return perror("utimes");
    if (unlink("bye") < 0) return perror("unlink bye");

    /* now finish up */
    chdir("..");
    rmdir(dirName);
    printf("Test completed successfully.\n");
}
