/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

/*
	System:		VICE-TWO
	Module:		physio.c
	Institution:	The Information Technology Center, Carnegie-Mellon University

 */

#include <afsconfig.h>
#include <afs/param.h>

RCSID("$Header: /cvs/openafs/src/vol/physio.c,v 1.7 2003/01/07 23:38:30 shadow Exp $");

#include <stdio.h>
#include <sys/types.h>
#include <errno.h>
#ifdef AFS_NT40_ENV
#include <fcntl.h>
#else
#include <sys/file.h>
#include <unistd.h>
#ifdef HAVE_STRING_H
#include <string.h>
#else
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#endif
#ifdef	AFS_SUN5_ENV
#include <sys/fcntl.h>
#endif
#endif
#include <rx/xdr.h>
#include <afs/afsint.h>
#include <errno.h>
#include <afs/afssyscalls.h>
#include "ihandle.h"
#include "nfs.h"
#include "salvage.h"
#include "afs/assert.h"
#include "afs/dir.h"

/* returns 0 on success, errno on failure */
int ReallyRead (file, block, data)
DirHandle     *	file;
afs_size_t	block;
char	      *	data;
{
    FdHandle_t *fdP;
    int code;
    errno = 0;
    fdP = IH_OPEN(file->dirh_handle);
    if (fdP == NULL) {
	code = errno;
	return code;
    }
    if (FDH_SEEK(fdP, (afs_size_t)(block*AFS_PAGESIZE), SEEK_SET) < 0) {
	code = errno;
	FDH_REALLYCLOSE(fdP);
	return code;
    }
    code = FDH_READ(fdP, data, (afs_size_t) AFS_PAGESIZE);
    if (code != AFS_PAGESIZE) {
	if (code < 0)
	    code = errno;
	else 
	    code = EIO;
	FDH_REALLYCLOSE(fdP);
	return code;
    }
    FDH_CLOSE(fdP);
    return 0;
}

/* returns 0 on success, errno on failure */
int ReallyWrite (file, block, data)
DirHandle     *	file;
afs_size_t	block;
char	      *	data;
{
    FdHandle_t *fdP;
    extern int VolumeChanged;
    int code;

    errno = 0;

    fdP = IH_OPEN(file->dirh_handle);
    if (fdP == NULL) {
	code = errno;
	return code;
    }
    if (FDH_SEEK(fdP, (afs_size_t)(block*AFS_PAGESIZE), SEEK_SET) < 0) {
	code = errno;
	FDH_REALLYCLOSE(fdP);
	return code;
    }
    code = FDH_WRITE(fdP, data, (afs_size_t) AFS_PAGESIZE);
    if (code != AFS_PAGESIZE) {
	if (code < 0)
	    code = errno;
	else 
	    code = EIO;
	FDH_REALLYCLOSE(fdP);
	return code;
    }
    FDH_CLOSE(fdP);
    VolumeChanged = 1;
    return 0;
}

/* SetSalvageDirHandle:
 * Create a handle to a directory entry and reference it (IH_INIT).
 * The handle needs to be dereferenced with the FidZap() routine.
 */
SetSalvageDirHandle(dir, volume, device, inode)
DirHandle *dir;
afs_int32 volume;
Inode inode;
Device device;
{
    static SalvageCacheCheck = 1;
    memset(dir, 0, sizeof(DirHandle));

    dir->dirh_device = device;
    dir->dirh_volume = volume;
    dir->dirh_inode = inode;
    IH_INIT(dir->dirh_handle, device, volume,  inode);
    /* Always re-read for a new dirhandle */
    dir->dirh_cacheCheck = SalvageCacheCheck++;
}

FidZap (file)
DirHandle     *	file;

{
    IH_RELEASE(file->dirh_handle);
    memset(file, 0, sizeof(DirHandle));
}

FidZero (file)
DirHandle     *	file;

{
    memset(file, 0, sizeof(DirHandle));
}

FidEq (afile, bfile)
DirHandle      * afile;
DirHandle      * bfile;

{
    if (afile->dirh_volume != bfile->dirh_volume) return 0;
    if (afile->dirh_device != bfile->dirh_device) return 0;
    if (afile->dirh_cacheCheck != bfile->dirh_cacheCheck) return 0;
    if (afile->dirh_inode != bfile->dirh_inode) return 0;
    return 1;
}

FidVolEq (afile, vid)
DirHandle      * afile;
afs_int32            vid;

{
    if (afile->dirh_volume != vid) return 0;
    return 1;
}

FidCpy (tofile, fromfile)
DirHandle      * tofile;
DirHandle      * fromfile;

{
    *tofile = *fromfile;
    IH_COPY(tofile->dirh_handle, fromfile->dirh_handle);
}

Die (msg)
char  * msg;

{
    printf("%s\n",msg);
    assert(1==2);
}

