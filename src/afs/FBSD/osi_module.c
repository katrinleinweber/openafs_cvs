/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */


#include <afsconfig.h>
#include <afs/param.h>

RCSID
    ("$Header: /cvs/openafs/src/afs/FBSD/osi_module.c,v 1.6 2004/10/27 19:46:54 rees Exp $");

#include <afs/sysincludes.h>
#include <afsincludes.h>
#include <sys/module.h>
#include <sys/sysproto.h>
#include <sys/syscall.h>
#include <sys/sysent.h>

extern struct vfsops afs_vfsops;
extern struct vnodeopv_desc afs_vnodeop_opv_desc;
extern struct mount *afs_globalVFS;
static struct vfsconf afs_vfsconf;

MALLOC_DEFINE(M_AFS, "afsmisc", "memory used by the AFS filesystem");

extern int afs3_syscall();
extern int Afs_xsetgroups();
extern int afs_xioctl();

int
afs_module_handler(module_t mod, int what, void *arg)
{
    static sy_call_t *old_handler;
    static int inited = 0;
    int error = 0;

    switch (what) {
    case MOD_LOAD:
	if (inited) {
	    printf("afs cannot be MOD_LOAD'd more than once\n");
	    error = EBUSY;
	    break;
	}
	if (sysent[AFS_SYSCALL].sy_call != nosys
	    && sysent[AFS_SYSCALL].sy_call != lkmnosys) {
	    printf("AFS_SYSCALL in use. aborting\n");
	    error = EBUSY;
	    break;
	}
	memset(&afs_vfsconf, 0, sizeof(struct vfsconf));
#ifdef AFS_FBSD53_ENV
	afs_vfsconf.vfc_version = VFS_VERSION;
#endif
	strcpy(afs_vfsconf.vfc_name, "AFS");
	afs_vfsconf.vfc_vfsops = &afs_vfsops;
	afs_vfsconf.vfc_typenum = -1;	/* set by vfs_register */
	afs_vfsconf.vfc_flags = VFCF_NETWORK;
	if ((error = vfs_register(&afs_vfsconf)) != 0)
	    break;
	vfs_add_vnodeops(&afs_vnodeop_opv_desc);
	osi_Init();
#if 0
	sysent[SYS_setgroups].sy_call = Afs_xsetgroups;
	sysent[SYS_ioctl].sy_call = afs_xioctl;
#endif
	old_handler = sysent[AFS_SYSCALL].sy_call;
	sysent[AFS_SYSCALL].sy_call = afs3_syscall;
	sysent[AFS_SYSCALL].sy_narg = 5;
	inited = 1;
	break;
    case MOD_UNLOAD:
#ifndef RXK_LISTENER_ENV
	/* shutdown is incomplete unless RXK_LISTENER_ENV */
	printf("afs: I can't be unloaded yet\n");
	return -1;
#endif
	if (!inited) {
	    error = 0;
	    break;
	}
	if (afs_globalVFS) {
	    error = EBUSY;
	    break;
	}
	if ((error = vfs_unregister(&afs_vfsconf)) != 0) {
	    break;
	}
	vfs_rm_vnodeops(&afs_vnodeop_opv_desc);
#if 0
	sysent[SYS_ioctl].sy_call = ioctl;
	sysent[SYS_setgroups].sy_call = setgroups;
#endif
	sysent[AFS_SYSCALL].sy_narg = 0;
	sysent[AFS_SYSCALL].sy_call = old_handler;
	break;
    }

    return (error);
}


static moduledata_t afs_mod = {
    "afs",
    afs_module_handler,
    &afs_mod
};

DECLARE_MODULE(afs, afs_mod, SI_SUB_VFS, SI_ORDER_MIDDLE);
