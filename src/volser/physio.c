/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#include <afs/param.h>
#include <sys/types.h>
#ifdef AFS_NT40_ENV
#include <fcntl.h>
#else
#include <sys/file.h>
#include <netinet/in.h>
#include <unistd.h>
#endif
#ifdef	AFS_SUN5_ENV
#include <sys/fcntl.h>
#endif
#include <errno.h>
#include <rx/xdr.h>
#include <rx/rx.h>
#include <stdio.h>
#include <afs/afsint.h>
#include <afs/nfs.h>
#include <afs/assert.h>
#include <afs/dir.h>
#include <afs/ihandle.h>
#include "vol.h"

/* returns 0 on success, errno on failure */
int ReallyRead (file, block, data)
DirHandle     *	file;
int 		block;
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
    if (FDH_SEEK(fdP, block*AFS_PAGESIZE, SEEK_SET) < 0) {
	code = errno;
	FDH_REALLYCLOSE(fdP);
	return code;
    }
    code = FDH_READ(fdP, data, AFS_PAGESIZE);
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
int 		block;
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
    if (FDH_SEEK(fdP, block*AFS_PAGESIZE, SEEK_SET) < 0) {
	code = errno;
	FDH_REALLYCLOSE(fdP);
	return code;
    }
    code = FDH_WRITE(fdP, data, AFS_PAGESIZE);
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
afs_int32 device;
{
    private SalvageCacheCheck = 1;
    bzero(dir, sizeof(DirHandle));

    dir->dirh_volume = volume;
    dir->dirh_device = device;
    dir->dirh_inode = inode;
    IH_INIT(dir->dirh_handle, device, volume, inode);

    /* Always re-read for a new dirhandle */
    dir->dirh_cacheCheck = SalvageCacheCheck++;	
}

FidZap (file)
DirHandle     *	file;

{
    IH_RELEASE(file->dirh_handle);
    bzero(file, sizeof(DirHandle));
}

FidZero (file)
DirHandle     *	file;

{
    bzero(file, sizeof(DirHandle));
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
