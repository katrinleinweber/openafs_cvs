/*
Copyright 1995 Massachusetts Institute of Technology.  All Rights
Reserved.

You are hereby granted a worldwide, irrevocable, paid-up, right and
license to use, execute, display, modify, copy and distribute MIT's
Modifications, provided that (i) you abide by the terms and conditions
of your Transarc AFS License Agreement, and (ii) you do not use the name
of MIT in any advertising or publicity without the prior written consent
of MIT.  MIT disclaims all liability for your use of MIT's
Modifications.  MIT's Modifications are provided "AS IS" WITHOUT
WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO,
ANY WARRANTY OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR
NONINFRINGEMENT.
*/

/*
 * OpenBSD specific assistance routines & VFS ops
 * Original NetBSD version for Transarc afs by John Kohl <jtk@MIT.EDU>
 * OpenBSD version by Jim Rees <rees@umich.edu>
 *
 * $Id: osi_vfsops.c,v 1.3 2002/11/05 22:37:53 rees Exp $
 */

/*
 * Some code cribbed from ffs_vfsops and other NetBSD sources, which
 * are marked:
 */
/*
 * Copyright (c) 1989, 1991, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <afsconfig.h>
#include "afs/param.h"

RCSID("$Header: /cvs/openafs/src/afs/OBSD/osi_vfsops.c,v 1.3 2002/11/05 22:37:53 rees Exp $");

#include "afs/sysincludes.h"	/* Standard vendor system headers */
#include "afs/afsincludes.h"	/* Afs-based standard headers */
#include "afs/afs_stats.h" /* statistics */

#include <sys/conf.h>
#include <sys/exec.h>
#include <sys/lkm.h>
#include <sys/namei.h>
#include <sys/syscall.h>
#include <sys/syscallargs.h>

#define NBSD_DONTFOLLOW_LINK 0
#define NBSD_FOLLOW_LINK 1
static int lkmid = -1;
static int afs_badcall(struct proc *p, void *xx, register_t *yy);

char afs_NetBSD_osname[] = "OpenBSD";
struct osi_vfs *afs_globalVFS;
struct vcache *afs_globalVp;

int afs_quotactl();
int afs_fhtovp();
int afs_vptofh();
int afsinit();
int afs_start();
int afs_mount();
int afs_unmount();
int afs_root();
int afs_statfs();
int afs_sync();
int afs_vget();
int afs_sysctl();
int afs_checkexp();

struct vfsops afs_vfsops = {
    afs_mount,
    afs_start,
    afs_unmount,
    afs_root,
    afs_quotactl,
    afs_statfs,
    afs_sync,
    afs_vget,
    afs_fhtovp,
    afs_vptofh,
    afsinit,
    afs_sysctl,
    afs_checkexp,
};

int
afs_nbsd_lookupname(char *fnamep,
		    enum uio_seg segflg,
		    int followlink,
		    struct vnode **dirvpp,
		    struct vnode **compvpp)
{
    struct nameidata nd;
    int niflag;
    int error;

    /*
     * Lookup pathname "fnamep", returning parent directory in
     * *dirvpp (if non-null) and leaf in *compvpp.  segflg says whether the
     * pathname is user or system space.
     */
    /* XXX LOCKLEAF ? */
    niflag =  (followlink == NBSD_FOLLOW_LINK) ? FOLLOW : NOFOLLOW;
    if (dirvpp)
	niflag |= WANTPARENT;		/* XXX LOCKPARENT? */
    NDINIT(&nd, LOOKUP,
	   niflag,
	   segflg,
	   fnamep, osi_curproc());
    if ((error = namei(&nd)))
	return error;
    *compvpp = nd.ni_vp;
    if (dirvpp)
	*dirvpp = nd.ni_dvp;
    return error;
}

#if 0
int
afs_rdwr(struct vnode *vp, struct uio *uiop, enum uio_rw op,
	      int flags, struct AFS_UCRED *cred)
{
    uiop->uio_rw = op;
    if (op == UIO_READ)
	return VOP_READ(vp, uiop, flags, cred);
    if (op == UIO_WRITE)
	return VOP_WRITE(vp, uiop, flags, cred);
#ifdef DIAGNOSTIC
    panic("afs_rdwr mode");
#endif
    return EINVAL;
}
#endif

int
afs_quotactl()
{
    return EOPNOTSUPP;
}

int
afs_sysctl()
{
    return EOPNOTSUPP;
}

int
afs_checkexp()
{
	return EOPNOTSUPP;
}

int
afs_fhtovp(mp, fhp, vpp)
struct mount *mp;
struct fid *fhp;
struct vnode **vpp;
{

    return (EINVAL);
}

int
afs_vptofh(vp, fhp)
struct vnode *vp;
struct fid *fhp;
{

    return (EINVAL);
}

int
afs_start(mp, flags, p)
struct mount *mp;
int flags;
struct proc *p;
{
    return (0);				/* nothing to do. ? */
}

int
afs_mount(mp, path, data, ndp, p)
register struct mount *mp;
char *path;
caddr_t data;
struct nameidata *ndp;
struct proc *p;
{
    /* ndp contains the mounted-from device.  Just ignore it.
       we also don't care about our proc struct. */
    int size;

    if (mp->mnt_flag & MNT_UPDATE)
	return EINVAL;

    if (afs_globalVFS) {
	/* Don't allow remounts */
	return EBUSY;
    }

    AFS_GLOCK();
    AFS_STATCNT(afs_mount);

#ifdef DISCONN
    /* initialize the vcache entries before we start using them */

    /* XXX find a better place for this if possible  */
    init_vcache_entries();
#endif
    afs_globalVFS = mp;
    mp->osi_vfs_bsize = 8192;
    mp->osi_vfs_fsid.val[0] = AFS_VFSMAGIC; /* magic */
    mp->osi_vfs_fsid.val[1] = (int) AFS_VFSFSID; 

    AFS_GUNLOCK();

    (void) copyinstr(path, mp->mnt_stat.f_mntonname, MNAMELEN-1, &size);
    bzero(mp->mnt_stat.f_mntonname + size, MNAMELEN - size);
    bzero(mp->mnt_stat.f_mntfromname, MNAMELEN);
    strcpy(mp->mnt_stat.f_mntfromname, "AFS");
    /* null terminated string "AFS" will fit, just leave it be. */
    strcpy(mp->mnt_stat.f_fstypename, MOUNT_AFS);
    (void) afs_statfs(mp, &mp->mnt_stat);

    return 0;
}

int
afs_unmount(afsp, flags, p)
struct mount *afsp;
int flags;
struct proc *p;
{
    int err;
    extern int sys_ioctl(), sys_setgroups();

    err = afs_unmount(afsp, flags);
    if (err == 0) {
	/* give up syscall entries for ioctl & setgroups, which we've stolen */
	sysent[SYS_ioctl].sy_call = sys_ioctl;
	sysent[SYS_setgroups].sy_call = sys_setgroups;
	/* give up the stolen syscall entry */
	sysent[AFS_SYSCALL].sy_narg = 0;
	sysent[AFS_SYSCALL].sy_argsize = 0;
	sysent[AFS_SYSCALL].sy_call = afs_badcall;
	printf("AFS unmounted--use `/sbin/modunload -i %d' to unload before restarting AFS\n", lkmid);
    }
    return err;
}

static int
afs_badcall(struct proc *p, void *xx, register_t *yy)
{
    return ENOSYS;
}

void
afs_nbsd_getnewvnode(struct vcache *tvc)
{
    while (getnewvnode(VT_AFS, afs_globalVFS, afs_vnodeop_p, &tvc->v)) {
	/* no vnodes available, force an alloc (limits be damned)! */
	desiredvnodes++;
    }
    tvc->v->v_data = (void *)tvc;
}

int
afs_root(struct mount *mp,
	      struct vnode **vpp)
{
    struct vrequest treq;
    struct vcache *tvp;
    int code;

    AFS_STATCNT(afs_root);

#ifdef  AFS_GLOBAL_SUNLOCK
    mutex_enter(&afs_global_lock);
#endif
    if (!(code = afs_InitReq(&treq, osi_curcred())) &&
	!(code = afs_CheckInit())) {
	tvp = afs_GetVCache(&afs_rootFid, &treq, NULL, NULL);
	if (tvp) {
printf("tvp %x %d\n", tvp, AFSTOV(tvp)->v_usecount);
	    /* There is really no reason to over-hold this bugger--it's held
	       by the root filesystem reference. */
	    if (afs_globalVp != tvp) {
		if (afs_globalVp)
printf("afs_globalVp %x %d\n", afs_globalVp, AFSTOV(afs_globalVp)->v_usecount);
/*		    AFS_RELE(AFSTOV(afs_globalVp));*/
		afs_globalVp = tvp;
		AFS_HOLD(AFSTOV(afs_globalVp));
	    }
	    AFSTOV(tvp)->v_flag |= VROOT;
	    afs_globalVFS = mp;
	    *vpp = AFSTOV(tvp);
	} else
	    code = ENOENT;
    }
#ifdef  AFS_GLOBAL_SUNLOCK
    mutex_exit(&afs_global_lock);
#endif

    if (!code)
	vn_lock(*vpp, LK_EXCLUSIVE | LK_RETRY, curproc); /* return it locked */
    return code;
}

int
afs_statfs(struct osi_vfs *afsp, struct statfs *abp)
{
    AFS_STATCNT(afs_statfs);
#ifdef  AFS_GLOBAL_SUNLOCK
    mutex_enter(&afs_global_lock);
#endif
    abp->f_bsize = afsp->osi_vfs_bsize;
    /* Fake a high number below to satisfy programs that use the ustat (for AIX), or statfs (for the rest) call to make sure that there's enough space in the device partition before storing something there (like ed(1)) */
    abp->f_blocks = abp->f_bfree = abp->f_bavail = abp->f_files = abp->f_ffree  = 9000000; /* XXX */
    abp->f_fsid.val[0] = AFS_VFSMAGIC; /* magic */
    abp->f_fsid.val[1] = (int) AFS_VFSFSID;
#ifdef  AFS_GLOBAL_SUNLOCK
    mutex_exit(&afs_global_lock);
#endif
    return 0;
}

int
afs_sync(struct osi_vfs *afsp)
{
    AFS_STATCNT(afs_sync);
#if defined(DISCONN) && !defined(AFS_OBSD_ENV)
    /* Can't do this in OpenBSD 2.7, it faults when called from apm_suspend() */
    store_dirty_vcaches();
#endif
    return 0;
}

void
afs_nbsd_ref(struct vnode *vp)
{
    if (vp->v_usecount == 0) {
	vprint("holding unheld node", vp);
	panic("afs_ref");
    }
    VREF(vp);
}

void
afs_nbsd_rele(struct vnode *vp)
{
    if (vp->v_usecount <= 0) {
	vprint("rele'ing unheld node", vp);
	panic("afs_rele");
    }
    vrele(vp);
}

int
afs_vget(vp, lfl)
struct vnode *vp;
int lfl;
{
    int error;

    if (vp->v_usecount < 0) {
	vprint("bad usecount", vp);
	panic("afs_vget");
    }
    error = vget(vp, lfl, curproc);
    if (!error)
	insmntque(vp, afs_globalVFS);	/* take off free list */
    return error;
}

extern struct vfsops afs_vfsops;
extern struct vnodeopv_desc afs_vnodeop_opv_desc;

static struct vfsconf afs_vfsconf = {
    &afs_vfsops,
    "afs",
    0,
    0,
    0,
    NULL,
    NULL,
};

MOD_VFS("afs", 0, &afs_vfsconf);

static char afsgenmem[] = "afsgenmem";
static char afsfidmem[] = "afsfidmem";
static char afsbhdrmem[] = "afsbhdrmem";
static char afsbfrmem[] = "afsbfrmem";

int
afsinit()
{
    extern int afs3_syscall(), afs_xioctl(), Afs_xsetgroups(), afs_xflock();

    sysent[AFS_SYSCALL].sy_call = afs3_syscall;
    sysent[AFS_SYSCALL].sy_narg = 6;
    sysent[AFS_SYSCALL].sy_argsize = 6*sizeof(long);
    sysent[54].sy_call = afs_xioctl;
    sysent[80].sy_call = Afs_xsetgroups;

    return 0;
}

int
afs_vfs_load(struct lkm_table *lkmtp,
	     int cmd)
{
    extern char *memname[];

    vfs_opv_init_explicit(&afs_vnodeop_opv_desc);
    vfs_opv_init_default(&afs_vnodeop_opv_desc);
    if (memname[M_AFSGENERIC] == NULL)
	memname[M_AFSGENERIC] = afsgenmem;
    if (memname[M_AFSFID] == NULL)
	memname[M_AFSFID] = afsfidmem;
    if (memname[M_AFSBUFHDR] == NULL)
	memname[M_AFSBUFHDR] = afsbhdrmem;
    if (memname[M_AFSBUFFER] == NULL)
	memname[M_AFSBUFFER] = afsbfrmem;
    lkmid = lkmtp->id;
    printf("OpenAFS ($Revision: 1.3 $) lkm loaded\n");
    return 0;
}

int
afs_vfs_unload(struct lkm_table *lktmp,
	     int cmd)
{
    extern char *memname[];
    extern int sys_lkmnosys();

    if (afs_globalVp)
	return EBUSY;
    if (sysent[SYS_ioctl].sy_call != sys_ioctl)
	return EBUSY;

    if (memname[M_AFSGENERIC] == afsgenmem)
	memname[M_AFSGENERIC] = NULL;
    if (memname[M_AFSFID] == afsfidmem)
	memname[M_AFSFID] = NULL;
    if (memname[M_AFSBUFHDR] == afsbhdrmem)
	memname[M_AFSBUFHDR] = NULL;
    if (memname[M_AFSBUFFER] == afsbfrmem)
	memname[M_AFSBUFFER] = NULL;

    sysent[AFS_SYSCALL].sy_call = sys_lkmnosys;
    printf("OpenAFS unloaded\n");
    return 0;
}



int
afsmodload(struct lkm_table *lkmtp,
	   int cmd,
	   int ver)
{
    extern int sys_lkmnosys();

    if (cmd == LKM_E_LOAD) {
	if (strcmp(ostype,afs_NetBSD_osname)) {
	    printf("This is %s version %s\n", ostype, osrelease);
	    printf("This version of AFS is only for %s\n",
		   afs_NetBSD_osname);
/*	    return EPROGMISMATCH;*/
	}
	if (sysent[AFS_SYSCALL].sy_call != sys_lkmnosys) {
	    printf("AFS must be loaded with syscall %d assigned to sys_lkmnosys\nIs AFS already loaded?\n",
		   AFS_SYSCALL);
	    return EINVAL;
	}
    }
    DISPATCH(lkmtp,cmd,ver,afs_vfs_load,afs_vfs_unload,lkm_nofunc);
}
