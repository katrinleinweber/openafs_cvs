/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 *
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#include <afsconfig.h>
#include "afs/param.h"

RCSID
    ("$Header: /cvs/openafs/src/afs/afs_pioctl.c,v 1.110.2.22 2008/11/29 18:20:24 shadow Exp $");

#include "afs/sysincludes.h"	/* Standard vendor system headers */
#ifdef AFS_OBSD_ENV
#include "h/syscallargs.h"
#endif
#ifdef AFS_FBSD50_ENV
#include "h/sysproto.h"
#endif
#include "afsincludes.h"	/* Afs-based standard headers */
#include "afs/afs_stats.h"	/* afs statistics */
#include "afs/vice.h"
#include "afs/afs_bypasscache.h"
#include "rx/rx_globals.h"

struct VenusFid afs_rootFid;
afs_int32 afs_waitForever = 0;
short afs_waitForeverCount = 0;
afs_int32 afs_showflags = GAGUSER | GAGCONSOLE;	/* show all messages */

#ifdef AFS_DISCON_ENV
afs_int32 afs_is_disconnected;
afs_int32 afs_is_logging;
afs_int32 afs_is_discon_rw;
/* On reconnection, turn this knob on until it finishes,
 * then turn it off.
 */
afs_int32 afs_in_sync = 0;
#endif

/*!
 * \defgroup pioctl Path IOCTL functions
 *
 * DECL_PIOCTL is a macro defined to contain the following parameters for functions:
 *
 * \param[in] avc	the AFS vcache structure in use by pioctl
 * \param[in] afun	not in use
 * \param[in] areq	the AFS vrequest structure
 * \param[in] ain	as defined by the function
 * \param[in] aout	as defined by the function
 * \param[in] ainSize	size of ain
 * \param[in] aoutSize	size of aout
 * \param[in] acred	UNIX credentials structure underlying the operation
 */

#define DECL_PIOCTL(x) static int x(struct vcache *avc, int afun, struct vrequest *areq, \
	char *ain, char *aout, afs_int32 ainSize, afs_int32 *aoutSize, \
	struct AFS_UCRED **acred)

/* Prototypes for pioctl routines */
DECL_PIOCTL(PGetFID);
DECL_PIOCTL(PSetAcl);
DECL_PIOCTL(PStoreBehind);
DECL_PIOCTL(PGCPAGs);
DECL_PIOCTL(PGetAcl);
DECL_PIOCTL(PNoop);
DECL_PIOCTL(PBogus);
DECL_PIOCTL(PGetFileCell);
DECL_PIOCTL(PGetWSCell);
DECL_PIOCTL(PGetUserCell);
DECL_PIOCTL(PSetTokens);
DECL_PIOCTL(PGetVolumeStatus);
DECL_PIOCTL(PSetVolumeStatus);
DECL_PIOCTL(PFlush);
DECL_PIOCTL(PNewStatMount);
DECL_PIOCTL(PGetTokens);
DECL_PIOCTL(PUnlog);
DECL_PIOCTL(PMariner);
DECL_PIOCTL(PCheckServers);
DECL_PIOCTL(PCheckVolNames);
DECL_PIOCTL(PCheckAuth);
DECL_PIOCTL(PFindVolume);
DECL_PIOCTL(PViceAccess);
DECL_PIOCTL(PSetCacheSize);
DECL_PIOCTL(PGetCacheSize);
DECL_PIOCTL(PRemoveCallBack);
DECL_PIOCTL(PNewCell);
DECL_PIOCTL(PNewAlias);
DECL_PIOCTL(PListCells);
DECL_PIOCTL(PListAliases);
DECL_PIOCTL(PRemoveMount);
DECL_PIOCTL(PVenusLogging);
DECL_PIOCTL(PGetCellStatus);
DECL_PIOCTL(PSetCellStatus);
DECL_PIOCTL(PFlushVolumeData);
DECL_PIOCTL(PGetVnodeXStatus);
DECL_PIOCTL(PGetVnodeXStatus2);
DECL_PIOCTL(PSetSysName);
DECL_PIOCTL(PSetSPrefs);
DECL_PIOCTL(PSetSPrefs33);
DECL_PIOCTL(PGetSPrefs);
DECL_PIOCTL(PExportAfs);
DECL_PIOCTL(PGag);
DECL_PIOCTL(PTwiddleRx);
DECL_PIOCTL(PGetInitParams);
DECL_PIOCTL(PGetRxkcrypt);
DECL_PIOCTL(PSetRxkcrypt);
DECL_PIOCTL(PGetCPrefs);
DECL_PIOCTL(PSetCPrefs);
DECL_PIOCTL(PFlushMount);
DECL_PIOCTL(PRxStatProc);
DECL_PIOCTL(PRxStatPeer);
DECL_PIOCTL(PPrefetchFromTape);
DECL_PIOCTL(PResidencyCmd);
DECL_PIOCTL(PCallBackAddr);
DECL_PIOCTL(PDiscon);
DECL_PIOCTL(PNFSNukeCreds);
DECL_PIOCTL(PNewUuid);
DECL_PIOCTL(PPrecache); 
#if defined(AFS_CACHE_BYPASS)
DECL_PIOCTL(PSetCachingThreshold);
DECL_PIOCTL(PSetCachingBlkSize);
#endif

/*
 * A macro that says whether we're going to need HandleClientContext().
 * This is currently used only by the nfs translator.
 */
#if !defined(AFS_NONFSTRANS) || defined(AFS_AIX_IAUTH_ENV)
#define AFS_NEED_CLIENTCONTEXT
#endif

/* Prototypes for private routines */
#ifdef AFS_NEED_CLIENTCONTEXT
static int HandleClientContext(struct afs_ioctl *ablob, int *com,
			       struct AFS_UCRED **acred,
			       struct AFS_UCRED *credp);
#endif
int HandleIoctl(register struct vcache *avc, register afs_int32 acom,
		struct afs_ioctl *adata);
int afs_HandlePioctl(struct vnode *avp, afs_int32 acom,
		     register struct afs_ioctl *ablob, int afollow,
		     struct AFS_UCRED **acred);
static int Prefetch(char *apath, struct afs_ioctl *adata, int afollow,
		    struct AFS_UCRED *acred);

typedef int (*pioctlFunction) (struct vcache *, int, struct vrequest *,
			       char *, char *, afs_int32, afs_int32 *,
			       struct AFS_UCRED **);

static pioctlFunction VpioctlSw[] = {
    PBogus,			/* 0 */
	PSetAcl,		/* 1 */
	PGetAcl,		/* 2 */
	PSetTokens,		/* 3 */
	PGetVolumeStatus,	/* 4 */
	PSetVolumeStatus,	/* 5 */
	PFlush,			/* 6 */
	PBogus,			/* 7 */
	PGetTokens,		/* 8 */
	PUnlog,			/* 9 */
	PCheckServers,		/* 10 */
	PCheckVolNames,		/* 11 */
	PCheckAuth,		/* 12 */
	PBogus,			/* 13 -- used to be quick check time */
	PFindVolume,		/* 14 */
	PBogus,			/* 15 -- prefetch is now special-cased; see pioctl code! */
	PBogus,			/* 16 -- used to be testing code */
	PNoop,			/* 17 -- used to be enable group */
	PNoop,			/* 18 -- used to be disable group */
	PBogus,			/* 19 -- used to be list group */
	PViceAccess,		/* 20 */
	PUnlog,			/* 21 -- unlog *is* unpag in this system */
	PGetFID,		/* 22 -- get file ID */
	PBogus,			/* 23 -- used to be waitforever */
	PSetCacheSize,		/* 24 */
	PRemoveCallBack,	/* 25 -- flush only the callback */
	PNewCell,		/* 26 */
	PListCells,		/* 27 */
	PRemoveMount,		/* 28 -- delete mount point */
	PNewStatMount,		/* 29 -- new style mount point stat */
	PGetFileCell,		/* 30 -- get cell name for input file */
	PGetWSCell,		/* 31 -- get cell name for workstation */
	PMariner,		/* 32 - set/get mariner host */
	PGetUserCell,		/* 33 -- get cell name for user */
	PVenusLogging,		/* 34 -- Enable/Disable logging */
	PGetCellStatus,		/* 35 */
	PSetCellStatus,		/* 36 */
	PFlushVolumeData,	/* 37 -- flush all data from a volume */
	PSetSysName,		/* 38 - Set system name */
	PExportAfs,		/* 39 - Export Afs to remote nfs clients */
	PGetCacheSize,		/* 40 - get cache size and usage */
	PGetVnodeXStatus,	/* 41 - get vcache's special status */
	PSetSPrefs33,		/* 42 - Set CM Server preferences... */
	PGetSPrefs,		/* 43 - Get CM Server preferences... */
	PGag,			/* 44 - turn off/on all CM messages */
	PTwiddleRx,		/* 45 - adjust some RX params       */
	PSetSPrefs,		/* 46 - Set CM Server preferences... */
	PStoreBehind,		/* 47 - set degree of store behind to be done */
	PGCPAGs,		/* 48 - disable automatic pag gc-ing */
	PGetInitParams,		/* 49 - get initial cm params */
	PGetCPrefs,		/* 50 - get client interface addresses */
	PSetCPrefs,		/* 51 - set client interface addresses */
	PFlushMount,		/* 52 - flush mount symlink data */
	PRxStatProc,		/* 53 - control process RX statistics */
	PRxStatPeer,		/* 54 - control peer RX statistics */
	PGetRxkcrypt,		/* 55 -- Get rxkad encryption flag */
	PSetRxkcrypt,		/* 56 -- Set rxkad encryption flag */
	PBogus,			/* 57 -- arla: set file prio */
	PBogus,			/* 58 -- arla: fallback getfh */
	PBogus,			/* 59 -- arla: fallback fhopen */
	PBogus,			/* 60 -- arla: controls xfsdebug */
	PBogus,			/* 61 -- arla: controls arla debug */
	PBogus,			/* 62 -- arla: debug interface */
	PBogus,			/* 63 -- arla: print xfs status */
	PBogus,			/* 64 -- arla: force cache check */
	PBogus,			/* 65 -- arla: break callback */
	PPrefetchFromTape,	/* 66 -- MR-AFS: prefetch file from tape */
	PResidencyCmd,		/* 67 -- MR-AFS: generic commnd interface */
	PBogus,			/* 68 -- arla: fetch stats */
	PGetVnodeXStatus2,	/* 69 - get caller access and some vcache status */
};

static pioctlFunction CpioctlSw[] = {
    PBogus,			/* 0 */
	PNewAlias,		/* 1 -- create new cell alias */
	PListAliases,		/* 2 -- list cell aliases */
	PCallBackAddr,		/* 3 -- request addr for callback rxcon */
    PBogus,			/* 4 */
    PDiscon,			/* 5 */
    PBogus,			/* 6 */
    PBogus,			/* 7 */
    PBogus,			/* 8 */
    PNewUuid,                   /* 9 */ 
    PBogus,                     /* 0 */
    PBogus,                     /* 0 */
    PPrecache,                  /* 12 */
};

static int (*(OpioctlSw[])) () = {
    PBogus,			/* 0 */
    PNFSNukeCreds,		/* 1 -- nuke all creds for NFS client */
#if defined(AFS_CACHE_BYPASS)
    PSetCachingThreshold        /* 2 -- get/set cache-bypass size threshold */
#else
    PNoop                       /* 2 -- get/set cache-bypass size threshold */
#endif
};

#define PSetClientContext 99	/*  Special pioctl to setup caller's creds  */
int afs_nobody = NFS_NOBODY;

int
HandleIoctl(register struct vcache *avc, register afs_int32 acom,
	    struct afs_ioctl *adata)
{
    register afs_int32 code;

    code = 0;
    AFS_STATCNT(HandleIoctl);

    switch (acom & 0xff) {
    case 1:
	avc->states |= CSafeStore;
	avc->asynchrony = 0;
	break;

	/* case 2 used to be abort store, but this is no longer provided,
	 * since it is impossible to implement under normal Unix.
	 */

    case 3:{
	    /* return the name of the cell this file is open on */
	    register struct cell *tcell;
	    register afs_int32 i;

	    tcell = afs_GetCell(avc->fid.Cell, READ_LOCK);
	    if (tcell) {
		i = strlen(tcell->cellName) + 1;	/* bytes to copy out */

		if (i > adata->out_size) {
		    /* 0 means we're not interested in the output */
		    if (adata->out_size != 0)
			code = EFAULT;
		} else {
		    /* do the copy */
		    AFS_COPYOUT(tcell->cellName, adata->out, i, code);
		}
		afs_PutCell(tcell, READ_LOCK);
	    } else
		code = ENOTTY;
	}
	break;

    case 49:			/* VIOC_GETINITPARAMS */
	if (adata->out_size < sizeof(struct cm_initparams)) {
	    code = EFAULT;
	} else {
	    AFS_COPYOUT(&cm_initParams, adata->out,
			sizeof(struct cm_initparams), code);
	}
	break;

    default:

	code = EINVAL;
#ifdef AFS_AIX51_ENV
	code = ENOSYS;
#endif
	break;
    }
    return code;		/* so far, none implemented */
}


#ifdef	AFS_AIX_ENV
/* For aix we don't temporarily bypass ioctl(2) but rather do our
 * thing directly in the vnode layer call, VNOP_IOCTL; thus afs_ioctl
 * is now called from afs_gn_ioctl.
 */
int
afs_ioctl(struct vcache *tvc, int cmd, int arg)
{
    struct afs_ioctl data;
    int error = 0;

    AFS_STATCNT(afs_ioctl);
    if (((cmd >> 8) & 0xff) == 'V') {
	/* This is a VICEIOCTL call */
	AFS_COPYIN(arg, (caddr_t) & data, sizeof(data), error);
	if (error)
	    return (error);
	error = HandleIoctl(tvc, cmd, &data);
	return (error);
    } else {
	/* No-op call; just return. */
	return (ENOTTY);
    }
}
#endif /* AFS_AIX_ENV */

#if defined(AFS_SGI_ENV)
afs_ioctl(OSI_VN_DECL(tvc), int cmd, void *arg, int flag, cred_t * cr,
	  rval_t * rvalp
#ifdef AFS_SGI65_ENV
	  , struct vopbd * vbds
#endif
    )
{
    struct afs_ioctl data;
    int error = 0;
    int locked;

    OSI_VN_CONVERT(tvc);

    AFS_STATCNT(afs_ioctl);
    if (((cmd >> 8) & 0xff) == 'V') {
	/* This is a VICEIOCTL call */
	error = copyin_afs_ioctl(arg, &data);
	if (error)
	    return (error);
	locked = ISAFS_GLOCK();
	if (!locked)
	    AFS_GLOCK();
	error = HandleIoctl(tvc, cmd, &data);
	if (!locked)
	    AFS_GUNLOCK();
	return (error);
    } else {
	/* No-op call; just return. */
	return (ENOTTY);
    }
}
#endif /* AFS_SGI_ENV */

/* unlike most calls here, this one uses u.u_error to return error conditions,
   since this is really an intercepted chapter 2 call, rather than a vnode
   interface call.
   */
/* AFS_HPUX102 and up uses VNODE ioctl instead */
#if !defined(AFS_HPUX102_ENV) && !defined(AFS_DARWIN80_ENV)
#if !defined(AFS_SGI_ENV)
#ifdef	AFS_AIX32_ENV
#ifdef AFS_AIX51_ENV
#ifdef __64BIT__
int
kioctl(int fdes, int com, caddr_t arg, caddr_t ext, caddr_t arg2, 
	   caddr_t arg3)
#else /* __64BIT__ */
int
kioctl32(int fdes, int com, caddr_t arg, caddr_t ext, caddr_t arg2, 
	     caddr_t arg3)
#endif /* __64BIT__ */
#else
int
kioctl(int fdes, int com, caddr_t arg, caddr_t ext)
#endif
{
    struct a {
	int fd, com;
	caddr_t arg, ext;
#ifdef AFS_AIX51_ENV
	caddr_t arg2, arg3;
#endif
    } u_uap, *uap = &u_uap;
#else
#if defined(AFS_SUN5_ENV)

struct afs_ioctl_sys {
    int fd;
    int com;
    int arg;
};

int 
afs_xioctl(struct afs_ioctl_sys *uap, rval_t *rvp)
{
#elif defined(AFS_OSF_ENV)
int 
afs_xioctl(struct proc *p, void *args, long *retval)
{
    struct a {
	long fd;
	u_long com;
	caddr_t arg;
    } *uap = (struct a *)args;
#elif defined(AFS_FBSD50_ENV)
#define arg data
int
afs_xioctl(struct thread *td, register struct ioctl_args *uap, 
	   register_t *retval)
{
    struct proc *p = td->td_proc;
#elif defined(AFS_DARWIN_ENV) || defined(AFS_XBSD_ENV)
struct ioctl_args {
    int fd;
    u_long com;
    caddr_t arg;
};

int
afs_xioctl(struct proc *p, register struct ioctl_args *uap, register_t *retval)
{
#elif defined(AFS_LINUX22_ENV)
struct afs_ioctl_sys {
    unsigned int com;
    unsigned long arg;
};
int
afs_xioctl(struct inode *ip, struct file *fp, unsigned int com,
	   unsigned long arg)
{
    struct afs_ioctl_sys ua, *uap = &ua;
#else
int
afs_xioctl(void)
{
    register struct a {
	int fd;
	int com;
	caddr_t arg;
    } *uap = (struct a *)u.u_ap;
#endif /* AFS_SUN5_ENV */
#endif
#if defined(AFS_AIX32_ENV) || defined(AFS_SUN5_ENV) || defined(AFS_OSF_ENV) || defined(AFS_DARWIN_ENV)
    struct file *fd;
#elif !defined(AFS_LINUX22_ENV)
    register struct file *fd;
#endif
#if defined(AFS_XBSD_ENV)
    register struct filedesc *fdp;
#endif
    register struct vcache *tvc;
    register int ioctlDone = 0, code = 0;

    AFS_STATCNT(afs_xioctl);
#if defined(AFS_DARWIN_ENV)
    if ((code = fdgetf(p, uap->fd, &fd)))
	return code;
#elif defined(AFS_XBSD_ENV)
    fdp = p->p_fd;
    if ((u_int) uap->fd >= fdp->fd_nfiles
	|| (fd = fdp->fd_ofiles[uap->fd]) == NULL)
	return EBADF;
    if ((fd->f_flag & (FREAD | FWRITE)) == 0)
	return EBADF;
#elif defined(AFS_LINUX22_ENV)
    ua.com = com;
    ua.arg = arg;
#elif defined(AFS_AIX32_ENV)
    uap->fd = fdes;
    uap->com = com;
    uap->arg = arg;
#ifdef AFS_AIX51_ENV
    uap->arg2 = arg2;
    uap->arg3 = arg3;
#endif
    if (setuerror(getf(uap->fd, &fd))) {
	return -1;
    }
#elif defined(AFS_OSF_ENV)
    fd = NULL;
    if (code = getf(&fd, uap->fd, FILE_FLAGS_NULL, &u.u_file_state))
	return code;
#elif defined(AFS_SUN5_ENV)
# if defined(AFS_SUN57_ENV)
    fd = getf(uap->fd);
    if (!fd)
	return (EBADF);
# elif defined(AFS_SUN54_ENV)
    fd = GETF(uap->fd);
    if (!fd)
	return (EBADF);
# else
    if (code = getf(uap->fd, &fd)) {
	return (code);
    }
# endif	/* AFS_SUN57_ENV */
#else
    fd = getf(uap->fd);
    if (!fd)
	return (EBADF);
#endif
    /* first determine whether this is any sort of vnode */
#if defined(AFS_LINUX22_ENV)
    tvc = VTOAFS(ip);
    {
#else
#ifdef AFS_SUN5_ENV
    if (fd->f_vnode->v_type == VREG || fd->f_vnode->v_type == VDIR) {
#else
    if (fd->f_type == DTYPE_VNODE) {
#endif
	/* good, this is a vnode; next see if it is an AFS vnode */
#if	defined(AFS_AIX32_ENV) || defined(AFS_SUN5_ENV)
	tvc = VTOAFS(fd->f_vnode);	/* valid, given a vnode */
#elif defined(AFS_OBSD_ENV)
	tvc =
	    IsAfsVnode((struct vnode *)fd->
		       f_data) ? VTOAFS((struct vnode *)fd->f_data) : NULL;
#else
	tvc = VTOAFS((struct vnode *)fd->f_data);	/* valid, given a vnode */
#endif
#endif /* AFS_LINUX22_ENV */
	if (tvc && IsAfsVnode(AFSTOV(tvc))) {
	    /* This is an AFS vnode */
	    if (((uap->com >> 8) & 0xff) == 'V') {
		register struct afs_ioctl *datap;
		AFS_GLOCK();
		datap =
		    (struct afs_ioctl *)osi_AllocSmallSpace(AFS_SMALLOCSIZ);
		code=copyin_afs_ioctl((char *)uap->arg, datap);
		if (code) {
		    osi_FreeSmallSpace(datap);
		    AFS_GUNLOCK();
#if defined(AFS_DARWIN_ENV) || defined(AFS_XBSD_ENV)
		    return code;
#else
#if	defined(AFS_SUN5_ENV)
#ifdef	AFS_SUN54_ENV
		    releasef(uap->fd);
#else
		    releasef(fd);
#endif
		    return (EFAULT);
#else
#ifdef	AFS_OSF_ENV
#ifdef	AFS_OSF30_ENV
		    FP_UNREF_ALWAYS(fd);
#else
		    FP_UNREF(fd);
#endif
		    return code;
#else /* AFS_OSF_ENV */
#ifdef	AFS_AIX41_ENV
		    ufdrele(uap->fd);
#endif
#ifdef AFS_LINUX22_ENV
		    return -code;
#else
		    setuerror(code);
		    return;
#endif
#endif
#endif
#endif
		}
		code = HandleIoctl(tvc, uap->com, datap);
		osi_FreeSmallSpace(datap);
		AFS_GUNLOCK();
		ioctlDone = 1;
#ifdef	AFS_AIX41_ENV
		ufdrele(uap->fd);
#endif
#ifdef	AFS_OSF_ENV
#ifdef	AFS_OSF30_ENV
		FP_UNREF_ALWAYS(fd);
#else
		FP_UNREF(fd);
#endif
#endif
	    }
#if defined(AFS_LINUX22_ENV)
	    else
		code = EINVAL;
#endif
	}
    }

    if (!ioctlDone) {
#ifdef	AFS_AIX41_ENV
	ufdrele(uap->fd);
#ifdef AFS_AIX51_ENV
#ifdef __64BIT__
	code = okioctl(fdes, com, arg, ext, arg2, arg3);
#else /* __64BIT__ */
	code = okioctl32(fdes, com, arg, ext, arg2, arg3);
#endif /* __64BIT__ */
#else /* !AFS_AIX51_ENV */
	code = okioctl(fdes, com, arg, ext);
#endif /* AFS_AIX51_ENV */
	return code;
#else /* !AFS_AIX41_ENV */
#ifdef	AFS_AIX32_ENV
	okioctl(fdes, com, arg, ext);
#elif defined(AFS_SUN5_ENV)
#if defined(AFS_SUN57_ENV)
	releasef(uap->fd);
#elif defined(AFS_SUN54_ENV)
	RELEASEF(uap->fd);
#else
	releasef(fd);
#endif
	code = ioctl(uap, rvp);
#elif defined(AFS_FBSD50_ENV)
	return ioctl(td, uap);
#elif defined(AFS_FBSD_ENV)
	return ioctl(p, uap);
#elif defined(AFS_OBSD_ENV)
	code = sys_ioctl(p, uap, retval);
#elif defined(AFS_DARWIN_ENV)
	return ioctl(p, uap, retval);
#elif defined(AFS_OSF_ENV)
	code = ioctl(p, args, retval);
#ifdef	AFS_OSF30_ENV
	FP_UNREF_ALWAYS(fd);
#else
	FP_UNREF(fd);
#endif
	return code;
#elif !defined(AFS_LINUX22_ENV)
	ioctl();
#endif
#endif
    }
#ifdef	AFS_SUN5_ENV
    if (ioctlDone)
#ifdef	AFS_SUN54_ENV
	releasef(uap->fd);
#else
	releasef(fd);
#endif
    return (code);
#else
#ifdef AFS_LINUX22_ENV
    return -code;
#else
#if defined(KERNEL_HAVE_UERROR)
    if (!getuerror())
	setuerror(code);
#if	defined(AFS_AIX32_ENV) && !defined(AFS_AIX41_ENV)
    return (getuerror()? -1 : u.u_ioctlrv);
#else
    return getuerror()? -1 : 0;
#endif
#endif
#endif /* AFS_LINUX22_ENV */
#endif /* AFS_SUN5_ENV */
#if defined(AFS_OSF_ENV) || defined(AFS_DARWIN_ENV) || defined(AFS_XBSD_ENV)
    return (code);
#endif
}
#endif /* AFS_SGI_ENV */
#endif /* AFS_HPUX102_ENV */

#if defined(AFS_SGI_ENV)
  /* "pioctl" system call entry point; just pass argument to the parameterized
   * call below */
struct pioctlargs {
    char *path;
    sysarg_t cmd;
    caddr_t cmarg;
    sysarg_t follow;
};
int
afs_pioctl(struct pioctlargs *uap, rval_t * rvp)
{
    int code;

    AFS_STATCNT(afs_pioctl);
    AFS_GLOCK();
    code = afs_syscall_pioctl(uap->path, uap->cmd, uap->cmarg, uap->follow);
    AFS_GUNLOCK();
#ifdef AFS_SGI64_ENV
    return code;
#else
    return u.u_error;
#endif
}

#elif defined(AFS_OSF_ENV)
afs_pioctl(struct proc *p, void *args, int *retval)
{
    struct a {
	char *path;
	int cmd;
	caddr_t cmarg;
	int follow;
    } *uap = (struct a *)args;

    AFS_STATCNT(afs_pioctl);
    return (afs_syscall_pioctl(uap->path, uap->cmd, uap->cmarg, uap->follow));
}

#elif defined(AFS_FBSD50_ENV)
int
afs_pioctl(struct thread *td, void *args, int *retval)
{
    struct a {
	char *path;
	int cmd;
	caddr_t cmarg;
	int follow;
    } *uap = (struct a *)args;

    AFS_STATCNT(afs_pioctl);
    return (afs_syscall_pioctl
	    (uap->path, uap->cmd, uap->cmarg, uap->follow, td->td_ucred));
}

#elif defined(AFS_DARWIN_ENV) || defined(AFS_XBSD_ENV)
int
afs_pioctl(struct proc *p, void *args, int *retval)
{
    struct a {
	char *path;
	int cmd;
	caddr_t cmarg;
	int follow;
    } *uap = (struct a *)args;

    AFS_STATCNT(afs_pioctl);
#ifdef AFS_DARWIN80_ENV
    return (afs_syscall_pioctl
	    (uap->path, uap->cmd, uap->cmarg, uap->follow,
	     kauth_cred_get()));
#else
    return (afs_syscall_pioctl
	    (uap->path, uap->cmd, uap->cmarg, uap->follow,
	     p->p_cred->pc_ucred));
#endif
}

#endif

/* macro to avoid adding any more #ifdef's to pioctl code. */
#if defined(AFS_LINUX22_ENV) || defined(AFS_AIX41_ENV)
#define PIOCTL_FREE_CRED() crfree(credp)
#else
#define PIOCTL_FREE_CRED()
#endif

int
#ifdef	AFS_SUN5_ENV
afs_syscall_pioctl(char *path, unsigned int com, caddr_t cmarg, int follow, 
		   rval_t *vvp, struct AFS_UCRED *credp)
#else
#if defined(AFS_DARWIN_ENV) || defined(AFS_XBSD_ENV)
afs_syscall_pioctl(char *path, unsigned int com, caddr_t cmarg, int follow, 
		   struct AFS_UCRED *credp)
#else
afs_syscall_pioctl(char *path, unsigned int com, caddr_t cmarg, int follow)
#endif
#endif
{
    struct afs_ioctl data;
#ifdef AFS_NEED_CLIENTCONTEXT
    struct AFS_UCRED *tmpcred = NULL;
#endif
    struct AFS_UCRED *foreigncreds = NULL;
    register afs_int32 code = 0;
    struct vnode *vp = NULL;
#ifdef	AFS_AIX41_ENV
    struct ucred *credp = crref();	/* don't free until done! */
#endif
#ifdef AFS_LINUX22_ENV
    cred_t *credp = crref();	/* don't free until done! */
    struct dentry *dp;
#endif

    AFS_STATCNT(afs_syscall_pioctl);
    if (follow)
	follow = 1;		/* compat. with old venus */
    code = copyin_afs_ioctl(cmarg, &data);
    if (code) {
	PIOCTL_FREE_CRED();
#if defined(KERNEL_HAVE_UERROR)
	setuerror(code);
#endif
	return (code);
    }
    if ((com & 0xff) == PSetClientContext) {
#ifdef AFS_NEED_CLIENTCONTEXT
#if defined(AFS_SUN5_ENV) || defined(AFS_AIX41_ENV) || defined(AFS_LINUX22_ENV)
	code = HandleClientContext(&data, &com, &foreigncreds, credp);
#else
	code = HandleClientContext(&data, &com, &foreigncreds, osi_curcred());
#endif
	if (code) {
	    if (foreigncreds) {
		crfree(foreigncreds);
	    }
	    PIOCTL_FREE_CRED();
#if defined(KERNEL_HAVE_UERROR)
	    return (setuerror(code), code);
#else
	    return (code);
#endif
	}
#else /* AFS_NEED_CLIENTCONTEXT */
	return EINVAL;
#endif /* AFS_NEED_CLIENTCONTEXT */
    }
#ifdef AFS_NEED_CLIENTCONTEXT
    if (foreigncreds) {
	/*
	 * We could have done without temporary setting the u.u_cred below
	 * (foreigncreds could be passed as param the pioctl modules)
	 * but calls such as afs_osi_suser() doesn't allow that since it
	 * references u.u_cred directly.  We could, of course, do something
	 * like afs_osi_suser(cred) which, I think, is better since it
	 * generalizes and supports multi cred environments...
	 */
#if defined(AFS_SUN5_ENV) || defined(AFS_LINUX22_ENV)
	tmpcred = credp;
	credp = foreigncreds;
#elif defined(AFS_AIX41_ENV)
	tmpcred = crref();	/* XXX */
	crset(foreigncreds);
#elif defined(AFS_HPUX101_ENV)
	tmpcred = p_cred(u.u_procp);
	set_p_cred(u.u_procp, foreigncreds);
#elif defined(AFS_SGI_ENV)
	tmpcred = OSI_GET_CURRENT_CRED();
	OSI_SET_CURRENT_CRED(foreigncreds);
#else
	tmpcred = u.u_cred;
	u.u_cred = foreigncreds;
#endif
    }
#endif /* AFS_NEED_CLIENTCONTEXT */
    if ((com & 0xff) == 15) {
	/* special case prefetch so entire pathname eval occurs in helper process.
	 * otherwise, the pioctl call is essentially useless */
#if	defined(AFS_SUN5_ENV) || defined(AFS_AIX41_ENV) || defined(AFS_LINUX22_ENV) || defined(AFS_DARWIN_ENV) || defined(AFS_XBSD_ENV)
	code =
	    Prefetch(path, &data, follow,
		     foreigncreds ? foreigncreds : credp);
#else
	code = Prefetch(path, &data, follow, osi_curcred());
#endif
	vp = NULL;
#if defined(KERNEL_HAVE_UERROR)
	setuerror(code);
#endif
	goto rescred;
    }
    if (path) {
	AFS_GUNLOCK();
#ifdef	AFS_AIX41_ENV
	code =
	    lookupname(path, USR, follow, NULL, &vp,
		       foreigncreds ? foreigncreds : credp);
#else
#ifdef AFS_LINUX22_ENV
	code = gop_lookupname(path, AFS_UIOUSER, follow, &dp);
	if (!code)
	    vp = (struct vnode *)dp->d_inode;
#else
	code = gop_lookupname(path, AFS_UIOUSER, follow, &vp);
#endif /* AFS_LINUX22_ENV */
#endif /* AFS_AIX41_ENV */
	AFS_GLOCK();
	if (code) {
	    vp = NULL;
#if defined(KERNEL_HAVE_UERROR)
	    setuerror(code);
#endif
	    goto rescred;
	}
    } else
	vp = NULL;

#if defined(AFS_SUN510_ENV)
    if (vp && !IsAfsVnode(vp)) {
	struct vnode *realvp;
	if
#ifdef AFS_SUN511_ENV
          (VOP_REALVP(vp, &realvp, NULL) == 0) 
#else
	  (VOP_REALVP(vp, &realvp) == 0) 
#endif
{
	    struct vnode *oldvp = vp;
	    
	    VN_HOLD(realvp);
	    vp = realvp;
	    AFS_RELE(oldvp);
	}
    }
#endif
    /* now make the call if we were passed no file, or were passed an AFS file */
    if (!vp || IsAfsVnode(vp)) {
#if defined(AFS_SUN5_ENV)
	code = afs_HandlePioctl(vp, com, &data, follow, &credp);
#elif defined(AFS_AIX41_ENV)
	{
	    struct ucred *cred1, *cred2;

	    if (foreigncreds) {
		cred1 = cred2 = foreigncreds;
	    } else {
		cred1 = cred2 = credp;
	    }
	    code = afs_HandlePioctl(vp, com, &data, follow, &cred1);
	    if (cred1 != cred2) {
		/* something changed the creds */
		crset(cred1);
	    }
	}
#elif defined(AFS_HPUX101_ENV)
	{
	    struct ucred *cred = p_cred(u.u_procp);
	    code = afs_HandlePioctl(vp, com, &data, follow, &cred);
	}
#elif defined(AFS_SGI_ENV)
	{
	    struct cred *credp;
	    credp = OSI_GET_CURRENT_CRED();
	    code = afs_HandlePioctl(vp, com, &data, follow, &credp);
	}
#elif defined(AFS_LINUX22_ENV) || defined(AFS_DARWIN_ENV) || defined(AFS_XBSD_ENV)
	code = afs_HandlePioctl(vp, com, &data, follow, &credp);
#else
	code = afs_HandlePioctl(vp, com, &data, follow, &u.u_cred);
#endif
    } else {
#if defined(KERNEL_HAVE_UERROR)
	setuerror(EINVAL);
#else
	code = EINVAL;		/* not in /afs */
#endif
    }

  rescred:
#if defined(AFS_NEED_CLIENTCONTEXT)
    if (foreigncreds) {
#ifdef	AFS_AIX41_ENV
	crset(tmpcred);		/* restore original credentials */
#else
#if	defined(AFS_HPUX101_ENV)
	set_p_cred(u.u_procp, tmpcred);	/* restore original credentials */
#elif	defined(AFS_SGI_ENV)
	OSI_SET_CURRENT_CRED(tmpcred);	/* restore original credentials */
#elif	defined(AFS_SUN5_ENV) || defined(AFS_LINUX22_ENV)
	credp = tmpcred;		/* restore original credentials */
#else
	osi_curcred() = tmpcred;	/* restore original credentials */
#endif /* AFS_HPUX101_ENV */
	crfree(foreigncreds);
#endif /* AIX41 */
    }
#endif /* AFS_NEED_CLIENTCONTEXT */
    if (vp) {
#ifdef AFS_LINUX22_ENV
	dput(dp);
#else
	AFS_RELE(vp);		/* put vnode back */
#endif
    }
    PIOCTL_FREE_CRED();
#if defined(KERNEL_HAVE_UERROR)
    if (!getuerror())
	setuerror(code);
    return (getuerror());
#else
    return (code);
#endif
}

#define MAXPIOCTLTOKENLEN \
(3*sizeof(afs_int32)+MAXKTCTICKETLEN+sizeof(struct ClearToken)+MAXKTCREALMLEN)

int
afs_HandlePioctl(struct vnode *avp, afs_int32 acom,
		 register struct afs_ioctl *ablob, int afollow,
		 struct AFS_UCRED **acred)
{
    struct vcache *avc;
    struct vrequest treq;
    register afs_int32 code;
    register afs_int32 function, device;
    afs_int32 inSize, outSize, outSizeMax;
    char *inData, *outData;
    pioctlFunction *pioctlSw;
    int pioctlSwSize;
    struct afs_fakestat_state fakestate;

    avc = avp ? VTOAFS(avp) : NULL;
    afs_Trace3(afs_iclSetp, CM_TRACE_PIOCTL, ICL_TYPE_INT32, acom & 0xff,
	       ICL_TYPE_POINTER, avc, ICL_TYPE_INT32, afollow);
    AFS_STATCNT(HandlePioctl);
    if ((code = afs_InitReq(&treq, *acred)))
	return code;
    afs_InitFakeStat(&fakestate);
    if (avc) {
	code = afs_EvalFakeStat(&avc, &fakestate, &treq);
	if (code) {
	    afs_PutFakeStat(&fakestate);
	    return code;
	}
    }
    device = (acom & 0xff00) >> 8;
    switch (device) {
    case 'V':			/* Original pioctls */
	pioctlSw = VpioctlSw;
	pioctlSwSize = sizeof(VpioctlSw);
	break;
    case 'C':			/* Coordinated/common pioctls */
	pioctlSw = CpioctlSw;
	pioctlSwSize = sizeof(CpioctlSw);
	break;
    case 'O':			/* Coordinated/common pioctls */
	pioctlSw = OpioctlSw;
	pioctlSwSize = sizeof(OpioctlSw);
	break;
    default:
	afs_PutFakeStat(&fakestate);
	return EINVAL;
    }
    function = acom & 0xff;
    if (function >= (pioctlSwSize / sizeof(char *))) {
	afs_PutFakeStat(&fakestate);
	return EINVAL;		/* out of range */
    }
    inSize = ablob->in_size;

    /* Do all range checking before continuing */
    if (inSize > MAXPIOCTLTOKENLEN || inSize < 0 || ablob->out_size < 0)
	return E2BIG;

    /* Note that we use osi_Alloc for large allocs and osi_AllocLargeSpace for small ones */
    if (inSize > AFS_LRALLOCSIZ) {
	inData = osi_Alloc(inSize + 1);
    } else {
	inData = osi_AllocLargeSpace(AFS_LRALLOCSIZ);
    }
    if (!inData)
	return ENOMEM;
    if (inSize > 0) {
	AFS_COPYIN(ablob->in, inData, inSize, code);
	inData[inSize] = '\0';
    } else
	code = 0;
    if (code) {
	if (inSize > AFS_LRALLOCSIZ) {
	    osi_Free(inData, inSize + 1);
	} else {
	    osi_FreeLargeSpace(inData);
	}
	afs_PutFakeStat(&fakestate);
	return code;
    }
    if (function == 8 && device == 'V') {	/* PGetTokens */
	outSizeMax = MAXPIOCTLTOKENLEN;
	outData = osi_Alloc(outSizeMax);
    } else {
	outSizeMax = AFS_LRALLOCSIZ;
	outData = osi_AllocLargeSpace(AFS_LRALLOCSIZ);
    }
    if (!outData) {
	if (inSize > AFS_LRALLOCSIZ) {
	    osi_Free(inData, inSize + 1);
	} else {
	    osi_FreeLargeSpace(inData);
	}
	afs_PutFakeStat(&fakestate);
	return ENOMEM;
    }
    outSize = 0;
    code =
	(*pioctlSw[function]) (avc, function, &treq, inData, outData, inSize,
			       &outSize, acred);
    if (inSize > AFS_LRALLOCSIZ) {
	osi_Free(inData, inSize + 1);
    } else {
	osi_FreeLargeSpace(inData);
    }
    if (code == 0 && ablob->out_size > 0) {
	if (outSize > ablob->out_size) {
	    code = E2BIG;	/* data wont fit in user buffer */
	} else if (outSize) {
	    AFS_COPYOUT(outData, ablob->out, outSize, code);
	}
    }
    if (outSizeMax > AFS_LRALLOCSIZ) {
	osi_Free(outData, outSizeMax);
    } else {
	osi_FreeLargeSpace(outData);
    }
    afs_PutFakeStat(&fakestate);
    return afs_CheckCode(code, &treq, 41);
}

/*!
 * VIOCETFID (22) - Get file ID quickly
 *
 * \ingroup pioctl
 *
 * \param[in] ain	not in use
 * \param[out] aout	not in use
 *
 * \retval EINVAL	Error if some of the initial arguments aren't set
 *
 * \post get the file id of some file
 */
DECL_PIOCTL(PGetFID)
{
    AFS_STATCNT(PGetFID);
    if (!avc)
	return EINVAL;
    memcpy(aout, (char *)&avc->fid, sizeof(struct VenusFid));
    *aoutSize = sizeof(struct VenusFid);
    return 0;
}

/*!
 * VIOCSETAL (1) - Set access control list
 *
 * \ingroup pioctl
 *
 * \param[in] ain	the ACL being set
 * \param[out] aout	the ACL being set returned
 *
 * \retval EINVAL	Error if some of the standard args aren't set
 *
 * \post Changed ACL, via direct writing to the wire
 */
int dummy_PSetAcl(char *ain, char *aout)
{
    return 0;
}

DECL_PIOCTL(PSetAcl)
{
    register afs_int32 code;
    struct conn *tconn;
    struct AFSOpaque acl;
    struct AFSVolSync tsync;
    struct AFSFetchStatus OutStatus;
    XSTATS_DECLS;

    AFS_STATCNT(PSetAcl);
    if (!avc)
	return EINVAL;
    if ((acl.AFSOpaque_len = strlen(ain) + 1) > 1024 /* AFSOPAQUEMAX */)
	return EINVAL;

    acl.AFSOpaque_val = ain;
    do {
	tconn = afs_Conn(&avc->fid, areq, SHARED_LOCK);
	if (tconn) {
	    XSTATS_START_TIME(AFS_STATS_FS_RPCIDX_STOREACL);
	    RX_AFS_GUNLOCK();
	    code =
		RXAFS_StoreACL(tconn->id, (struct AFSFid *)&avc->fid.Fid,
			       &acl, &OutStatus, &tsync);
	    RX_AFS_GLOCK();
	    XSTATS_END_TIME;
	} else
	    code = -1;
    } while (afs_Analyze
	     (tconn, code, &avc->fid, areq, AFS_STATS_FS_RPCIDX_STOREACL,
	      SHARED_LOCK, NULL));

    /* now we've forgotten all of the access info */
    ObtainWriteLock(&afs_xcbhash, 455);
    avc->callback = 0;
    afs_DequeueCallback(avc);
    avc->states &= ~(CStatd | CUnique);
    ReleaseWriteLock(&afs_xcbhash);
    if (avc->fid.Fid.Vnode & 1 || (vType(avc) == VDIR))
	osi_dnlc_purgedp(avc);
    return code;
}

int afs_defaultAsynchrony = 0;

/*!
 * VIOC_STOREBEHIND (47) Adjust store asynchrony
 *
 * \ingroup pioctl
 *
 * \param[in] ain	sbstruct (store behind structure) input
 * \param[out] aout	resulting sbstruct
 *
 * \retval EPERM	Error if the user doesn't have super-user credentials
 * \retval EACCES	Error if there isn't enough access to not check the mode bits
 *
 * \post sets asynchrony based on a file, from a struct sbstruct "I THINK"
 */
DECL_PIOCTL(PStoreBehind)
{
    afs_int32 code = 0;
    struct sbstruct *sbr;

    sbr = (struct sbstruct *)ain;
    if (sbr->sb_default != -1) {
	if (afs_osi_suser(*acred))
	    afs_defaultAsynchrony = sbr->sb_default;
	else
	    code = EPERM;
    }

    if (avc && (sbr->sb_thisfile != -1)) {
	if (afs_AccessOK
	    (avc, PRSFS_WRITE | PRSFS_ADMINISTER, areq, DONT_CHECK_MODE_BITS))
	    avc->asynchrony = sbr->sb_thisfile;
	else
	    code = EACCES;
    }

    *aoutSize = sizeof(struct sbstruct);
    sbr = (struct sbstruct *)aout;
    sbr->sb_default = afs_defaultAsynchrony;
    if (avc) {
	sbr->sb_thisfile = avc->asynchrony;
    }

    return code;
}

/*!
 * VIOC_GCPAGS (48) - Disable automatic PAG gc'ing
 *
 * \ingroup pioctl
 *
 * \param[in] ain	not in use
 * \param[out] aout	not in use
 *
 * \retval EACCES	Error if the user doesn't have super-user credentials
 *
 * \post set the gcpags to GCPAGS_USERDISABLED
 */
DECL_PIOCTL(PGCPAGs)
{
    if (!afs_osi_suser(*acred)) {
	return EACCES;
    }
    afs_gcpags = AFS_GCPAGS_USERDISABLED;
    return 0;
}

/*!
 * VIOCGETAL (2) - Get access control list
 *
 * \ingroup pioctl
 *
 * \param[in] ain	not in use
 * \param[out] aout	the ACL
 *
 * \retval EINVAL	Error if some of the standard args aren't set
 * \retval ERANGE	Error if the vnode of the file id is too large
 * \retval -1		Error if getting the ACL failed
 *
 * \post Obtain the ACL, based on file ID
 *
 * \notes there is a hack to tell which type of ACL is being returned, checks the top 2-bytes to judge what type of ACL it is, only for dfs xlat or ACLs
 */
DECL_PIOCTL(PGetAcl)
{
    struct AFSOpaque acl;
    struct AFSVolSync tsync;
    struct AFSFetchStatus OutStatus;
    afs_int32 code;
    struct conn *tconn;
    struct AFSFid Fid;
    XSTATS_DECLS;

    AFS_STATCNT(PGetAcl);
    if (!avc)
	return EINVAL;
    Fid.Volume = avc->fid.Fid.Volume;
    Fid.Vnode = avc->fid.Fid.Vnode;
    Fid.Unique = avc->fid.Fid.Unique;
    if (avc->states & CForeign) {
	/*
	 * For a dfs xlator acl we have a special hack so that the
	 * xlator will distinguish which type of acl will return. So
	 * we currently use the top 2-bytes (vals 0-4) to tell which
	 * type of acl to bring back. Horrible hack but this will
	 * cause the least number of changes to code size and interfaces.
	 */
	if (Fid.Vnode & 0xc0000000)
	    return ERANGE;
	Fid.Vnode |= (ainSize << 30);
    }
    acl.AFSOpaque_val = aout;
    do {
	tconn = afs_Conn(&avc->fid, areq, SHARED_LOCK);
	if (tconn) {
	    *aout = 0;
	    XSTATS_START_TIME(AFS_STATS_FS_RPCIDX_FETCHACL);
	    RX_AFS_GUNLOCK();
	    code = RXAFS_FetchACL(tconn->id, &Fid, &acl, &OutStatus, &tsync);
	    RX_AFS_GLOCK();
	    XSTATS_END_TIME;
	} else
	    code = -1;
    } while (afs_Analyze
	     (tconn, code, &avc->fid, areq, AFS_STATS_FS_RPCIDX_FETCHACL,
	      SHARED_LOCK, NULL));

    if (code == 0) {
	*aoutSize = (acl.AFSOpaque_len == 0 ? 1 : acl.AFSOpaque_len);
    }
    return code;
}

/*!
 * PNoop returns success.  Used for functions which are not implemented or are no longer in use.
 *
 * \ingroup pioctl
 *
 * \notes Functions involved in this: 17 (VIOCENGROUP) -- used to be enable group; 18 (VIOCDISGROUP) -- used to be disable group; 2 (?) -- get/set cache-bypass size threshold
 */
DECL_PIOCTL(PNoop)
{
    AFS_STATCNT(PNoop);
    return 0;
}

/*!
 * PBogus returns fail.  Used for functions which are not implemented or are no longer in use.
 *
 * \ingroup pioctl
 *
 * \retval EINVAL	Error if some of the standard args aren't set
 *
 * \notes Functions involved in this: 0 (?); 4 (?); 6 (?); 7 (VIOCSTAT); 8 (?); 13 (VIOCGETTIME) -- used to be quick check time; 15 (VIOCPREFETCH) -- prefetch is now special-cased; see pioctl code!; 16 (VIOCNOP) -- used to be testing code; 19 (VIOCLISTGROUPS) -- used to be list group; 23 (VIOCWAITFOREVER) -- used to be waitforever; 57 (VIOC_FPRIOSTATUS) -- arla: set file prio; 58 (VIOC_FHGET) -- arla: fallback getfh; 59 (VIOC_FHOPEN) -- arla: fallback fhopen; 60 (VIOC_XFSDEBUG) -- arla: controls xfsdebug; 61 (VIOC_ARLADEBUG) -- arla: controls arla debug; 62 (VIOC_AVIATOR) -- arla: debug interface; 63 (VIOC_XFSDEBUG_PRINT) -- arla: print xfs status; 64 (VIOC_CALCULATE_CACHE) -- arla: force cache check; 65 (VIOC_BREAKCELLBACK) -- arla: break callback; 68 (?) -- arla: fetch stats;
 */
DECL_PIOCTL(PBogus)
{
    AFS_STATCNT(PBogus);
    return EINVAL;
}

/*!
 * VIOC_FILE_CELL_NAME (30) - Get cell in which file lives
 *
 * \ingroup pioctl
 *
 * \param[in] ain	not in use (avc used to pass in file id)
 * \param[out] aout	cell name
 *
 * \retval EINVAL	Error if some of the standard args aren't set
 * \retval ESRCH	Error if the file isn't part of a cell
 *
 * \post Get a cell based on a passed in file id
 */
DECL_PIOCTL(PGetFileCell)
{
    register struct cell *tcell;

    AFS_STATCNT(PGetFileCell);
    if (!avc)
	return EINVAL;
    tcell = afs_GetCell(avc->fid.Cell, READ_LOCK);
    if (!tcell)
	return ESRCH;
    strcpy(aout, tcell->cellName);
    afs_PutCell(tcell, READ_LOCK);
    *aoutSize = strlen(aout) + 1;
    return 0;
}

/*!
 * VIOC_GET_WS_CELL (31) - Get cell in which workstation lives
 *
 * \ingroup pioctl
 *
 * \param[in] ain	not in use
 * \param[out] aout	cell name
 *
 * \retval EIO		Error if the afs daemon hasn't started yet
 * \retval ESRCH	Error if the machine isn't part of a cell, for whatever reason
 *
 * \post Get the primary cell that the machine is a part of.
 */
DECL_PIOCTL(PGetWSCell)
{
    struct cell *tcell = NULL;

    AFS_STATCNT(PGetWSCell);
    if (!afs_resourceinit_flag)	/* afs daemons haven't started yet */
	return EIO;		/* Inappropriate ioctl for device */

    tcell = afs_GetPrimaryCell(READ_LOCK);
    if (!tcell)			/* no primary cell? */
	return ESRCH;
    strcpy(aout, tcell->cellName);
    *aoutSize = strlen(aout) + 1;
    afs_PutCell(tcell, READ_LOCK);
    return 0;
}

/*!
 * VIOC_GET_PRIMARY_CELL (33) - Get primary cell for caller
 *
 * \ingroup pioctl
 *
 * \param[in] ain	not in use (user id found via areq)
 * \param[out] aout	cell name
 *
 * \retval ESRCH	Error if the user id doesn't have a primary cell specified
 *
 * \post Get the primary cell for a certain user, based on the user's uid
 */
DECL_PIOCTL(PGetUserCell)
{
    register afs_int32 i;
    register struct unixuser *tu;
    register struct cell *tcell;

    AFS_STATCNT(PGetUserCell);
    if (!afs_resourceinit_flag)	/* afs daemons haven't started yet */
	return EIO;		/* Inappropriate ioctl for device */

    /* return the cell name of the primary cell for this user */
    i = UHash(areq->uid);
    ObtainWriteLock(&afs_xuser, 224);
    for (tu = afs_users[i]; tu; tu = tu->next) {
	if (tu->uid == areq->uid && (tu->states & UPrimary)) {
	    tu->refCount++;
	    ReleaseWriteLock(&afs_xuser);
	    break;
	}
    }
    if (tu) {
	tcell = afs_GetCell(tu->cell, READ_LOCK);
	afs_PutUser(tu, WRITE_LOCK);
	if (!tcell)
	    return ESRCH;
	else {
	    strcpy(aout, tcell->cellName);
	    afs_PutCell(tcell, READ_LOCK);
	    *aoutSize = strlen(aout) + 1;	/* 1 for the null */
	}
    } else {
	ReleaseWriteLock(&afs_xuser);
	*aout = 0;
	*aoutSize = 1;
    }
    return 0;
}

/*!
 * VIOCSETTOK (3) - Set authentication tokens
 *
 * \ingroup pioctl
 *
 * \param[in] ain	the krb tickets from which to set the afs tokens
 * \param[out] aout	not in use
 *
 * \retval EINVAL	Error if the ticket is either too long or too short
 * \retval EIO		Error if the AFS initState is below 101
 * \retval ESRCH	Error if the cell for which the Token is being set can't be found
 *
 * \post Set the Tokens for a specific cell name, unless there is none set, then default to primary
 *
 */
DECL_PIOCTL(PSetTokens)
{
    afs_int32 i;
    register struct unixuser *tu;
    struct ClearToken clear;
    register struct cell *tcell;
    char *stp;
    int stLen;
    struct vrequest treq;
    afs_int32 flag, set_parent_pag = 0;

    AFS_STATCNT(PSetTokens);
    if (!afs_resourceinit_flag) {
	return EIO;
    }
    memcpy((char *)&i, ain, sizeof(afs_int32));
    ain += sizeof(afs_int32);
    stp = ain;			/* remember where the ticket is */
    if (i < 0 || i > MAXKTCTICKETLEN)
	return EINVAL;		/* malloc may fail */
    stLen = i;
    ain += i;			/* skip over ticket */
    memcpy((char *)&i, ain, sizeof(afs_int32));
    ain += sizeof(afs_int32);
    if (i != sizeof(struct ClearToken)) {
	return EINVAL;
    }
    memcpy((char *)&clear, ain, sizeof(struct ClearToken));
    if (clear.AuthHandle == -1)
	clear.AuthHandle = 999;	/* more rxvab compat stuff */
    ain += sizeof(struct ClearToken);
    if (ainSize != 2 * sizeof(afs_int32) + stLen + sizeof(struct ClearToken)) {
	/* still stuff left?  we've got primary flag and cell name.  Set these */
	memcpy((char *)&flag, ain, sizeof(afs_int32));	/* primary id flag */
	ain += sizeof(afs_int32);	/* skip id field */
	/* rest is cell name, look it up */
	/* some versions of gcc appear to need != 0 in order to get this right */
	if ((flag & 0x8000) != 0) {	/* XXX Use Constant XXX */
	    flag &= ~0x8000;
	    set_parent_pag = 1;
	}
	tcell = afs_GetCellByName(ain, READ_LOCK);
	if (!tcell)
	    goto nocell;
    } else {
	/* default to primary cell, primary id */
	flag = 1;		/* primary id */
	tcell = afs_GetPrimaryCell(READ_LOCK);
	if (!tcell)
	    goto nocell;
    }
    i = tcell->cellNum;
    afs_PutCell(tcell, READ_LOCK);
    if (set_parent_pag) {
	afs_int32 pag;
#if defined(AFS_DARWIN_ENV) || defined(AFS_XBSD_ENV)
#if defined(AFS_DARWIN_ENV)
	struct proc *p = current_proc();	/* XXX */
#else
	struct proc *p = curproc;	/* XXX */
#endif
#ifndef AFS_DARWIN80_ENV
	uprintf("Process %d (%s) tried to change pags in PSetTokens\n",
		p->p_pid, p->p_comm);
#endif
	if (!setpag(p, acred, -1, &pag, 1)) {
#else
#ifdef	AFS_OSF_ENV
	if (!setpag(u.u_procp, acred, -1, &pag, 1)) {	/* XXX u.u_procp is a no-op XXX */
#else
	if (!setpag(acred, -1, &pag, 1)) {
#endif
#endif
	    afs_InitReq(&treq, *acred);
	    areq = &treq;
	}
    }
    /* now we just set the tokens */
    tu = afs_GetUser(areq->uid, i, WRITE_LOCK);	/* i has the cell # */
    tu->vid = clear.ViceId;
    if (tu->stp != NULL) {
	afs_osi_Free(tu->stp, tu->stLen);
    }
    tu->stp = (char *)afs_osi_Alloc(stLen);
    if (tu->stp == NULL) {
	return ENOMEM;
    }
    tu->stLen = stLen;
    memcpy(tu->stp, stp, stLen);
    tu->ct = clear;
#ifndef AFS_NOSTATS
    afs_stats_cmfullperf.authent.TicketUpdates++;
    afs_ComputePAGStats();
#endif /* AFS_NOSTATS */
    tu->states |= UHasTokens;
    tu->states &= ~UTokensBad;
    afs_SetPrimary(tu, flag);
    tu->tokenTime = osi_Time();
    afs_ResetUserConns(tu);
    afs_PutUser(tu, WRITE_LOCK);

    return 0;

  nocell:
    {
	int t1;
	t1 = afs_initState;
	if (t1 < 101)
	    return EIO;
	else
	    return ESRCH;
    }
}

/*!
 * VIOCGETVOLSTAT (4) - Get volume status
 *
 * \ingroup pioctl
 *
 * \param[in] ain	not in use
 * \param[out] aout	status of the volume
 *
 * \retval EINVAL	Error if some of the standard args aren't set
 *
 * \post The status of a volume (based on the FID of the volume), or an offline message /motd
 */
DECL_PIOCTL(PGetVolumeStatus)
{
    char volName[32];
    char *offLineMsg = afs_osi_Alloc(256);
    char *motd = afs_osi_Alloc(256);
    register struct conn *tc;
    register afs_int32 code = 0;
    struct AFSFetchVolumeStatus volstat;
    register char *cp;
    char *Name, *OfflineMsg, *MOTD;
    XSTATS_DECLS;

    AFS_STATCNT(PGetVolumeStatus);
    if (!avc) {
	code = EINVAL;
	goto out;
    }
    Name = volName;
    OfflineMsg = offLineMsg;
    MOTD = motd;
    do {
	tc = afs_Conn(&avc->fid, areq, SHARED_LOCK);
	if (tc) {
	    XSTATS_START_TIME(AFS_STATS_FS_RPCIDX_GETVOLUMESTATUS);
	    RX_AFS_GUNLOCK();
	    code =
		RXAFS_GetVolumeStatus(tc->id, avc->fid.Fid.Volume, &volstat,
				      &Name, &OfflineMsg, &MOTD);
	    RX_AFS_GLOCK();
	    XSTATS_END_TIME;
	} else
	    code = -1;
    } while (afs_Analyze
	     (tc, code, &avc->fid, areq, AFS_STATS_FS_RPCIDX_GETVOLUMESTATUS,
	      SHARED_LOCK, NULL));

    if (code)
	goto out;
    /* Copy all this junk into msg->im_data, keeping track of the lengths. */
    cp = aout;
    memcpy(cp, (char *)&volstat, sizeof(VolumeStatus));
    cp += sizeof(VolumeStatus);
    strcpy(cp, volName);
    cp += strlen(volName) + 1;
    strcpy(cp, offLineMsg);
    cp += strlen(offLineMsg) + 1;
    strcpy(cp, motd);
    cp += strlen(motd) + 1;
    *aoutSize = (cp - aout);
  out:
    afs_osi_Free(offLineMsg, 256);
    afs_osi_Free(motd, 256);
    return code;
}

/*!
 * VIOCSETVOLSTAT (5) - Set volume status
 *
 * \ingroup pioctl
 *
 * \param[in] ain	values to set the status at, offline message, message of the day, volume name, minimum quota, maximum quota
 * \param[out] aout	status of a volume, offlines messages, minimum quota, maximumm quota
 *
 * \retval EINVAL	Error if some of the standard args aren't set
 * \retval EROFS	Error if the volume is read only, or a backup volume
 * \retval ENODEV	Error if the volume can't be accessed
 * \retval E2BIG	Error if the volume name, offline message, and motd are too big
 *
 * \post Set the status of a volume, including any offline messages, a minimum quota, and a maximum quota
 */
DECL_PIOCTL(PSetVolumeStatus)
{
    char volName[32];
    char *offLineMsg = afs_osi_Alloc(256);
    char *motd = afs_osi_Alloc(256);
    register struct conn *tc;
    register afs_int32 code = 0;
    struct AFSFetchVolumeStatus volstat;
    struct AFSStoreVolumeStatus storeStat;
    register struct volume *tvp;
    register char *cp;
    XSTATS_DECLS;

    AFS_STATCNT(PSetVolumeStatus);
    if (!avc) {
	code = EINVAL;
	goto out;
    }

    tvp = afs_GetVolume(&avc->fid, areq, READ_LOCK);
    if (tvp) {
	if (tvp->states & (VRO | VBackup)) {
	    afs_PutVolume(tvp, READ_LOCK);
	    code = EROFS;
	    goto out;
	}
	afs_PutVolume(tvp, READ_LOCK);
    } else {
	code = ENODEV;
	goto out;
    }
    /* Copy the junk out, using cp as a roving pointer. */
    cp = ain;
    memcpy((char *)&volstat, cp, sizeof(AFSFetchVolumeStatus));
    cp += sizeof(AFSFetchVolumeStatus);
    if (strlen(cp) >= sizeof(volName)) {
	code = E2BIG;
	goto out;
    }
    strcpy(volName, cp);
    cp += strlen(volName) + 1;
    if (strlen(cp) >= sizeof(offLineMsg)) {
	code = E2BIG;
	goto out;
    }
    strcpy(offLineMsg, cp);
    cp += strlen(offLineMsg) + 1;
    if (strlen(cp) >= sizeof(motd)) {
	code = E2BIG;
	goto out;
    }
    strcpy(motd, cp);
    storeStat.Mask = 0;
    if (volstat.MinQuota != -1) {
	storeStat.MinQuota = volstat.MinQuota;
	storeStat.Mask |= AFS_SETMINQUOTA;
    }
    if (volstat.MaxQuota != -1) {
	storeStat.MaxQuota = volstat.MaxQuota;
	storeStat.Mask |= AFS_SETMAXQUOTA;
    }
    do {
	tc = afs_Conn(&avc->fid, areq, SHARED_LOCK);
	if (tc) {
	    XSTATS_START_TIME(AFS_STATS_FS_RPCIDX_SETVOLUMESTATUS);
	    RX_AFS_GUNLOCK();
	    code =
		RXAFS_SetVolumeStatus(tc->id, avc->fid.Fid.Volume, &storeStat,
				      volName, offLineMsg, motd);
	    RX_AFS_GLOCK();
	    XSTATS_END_TIME;
	} else
	    code = -1;
    } while (afs_Analyze
	     (tc, code, &avc->fid, areq, AFS_STATS_FS_RPCIDX_SETVOLUMESTATUS,
	      SHARED_LOCK, NULL));

    if (code)
	goto out;
    /* we are sending parms back to make compat. with prev system.  should
     * change interface later to not ask for current status, just set new status */
    cp = aout;
    memcpy(cp, (char *)&volstat, sizeof(VolumeStatus));
    cp += sizeof(VolumeStatus);
    strcpy(cp, volName);
    cp += strlen(volName) + 1;
    strcpy(cp, offLineMsg);
    cp += strlen(offLineMsg) + 1;
    strcpy(cp, motd);
    cp += strlen(motd) + 1;
    *aoutSize = cp - aout;
  out:
    afs_osi_Free(offLineMsg, 256);
    afs_osi_Free(motd, 256);
    return code;
}

/*!
 * VIOCFLUSH (6) - Invalidate cache entry
 *
 * \ingroup pioctl
 *
 * \param[in] ain	not in use
 * \param[out] aout	not in use
 *
 * \retval EINVAL	Error if some of the standard args aren't set
 *
 * \post Flush any information the cache manager has on an entry
 */
DECL_PIOCTL(PFlush)
{
    AFS_STATCNT(PFlush);
    if (!avc)
	return EINVAL;
#ifdef AFS_BOZONLOCK_ENV
    afs_BozonLock(&avc->pvnLock, avc);	/* Since afs_TryToSmush will do a pvn_vptrunc */
#endif
    ObtainWriteLock(&avc->lock, 225);
    ObtainWriteLock(&afs_xcbhash, 456);
    afs_DequeueCallback(avc);
    avc->states &= ~(CStatd | CDirty);	/* next reference will re-stat cache entry */
    ReleaseWriteLock(&afs_xcbhash);
    /* now find the disk cache entries */
    afs_TryToSmush(avc, *acred, 1);
    osi_dnlc_purgedp(avc);
    if (avc->linkData && !(avc->states & CCore)) {
	afs_osi_Free(avc->linkData, strlen(avc->linkData) + 1);
	avc->linkData = NULL;
    }
    ReleaseWriteLock(&avc->lock);
#ifdef AFS_BOZONLOCK_ENV
    afs_BozonUnlock(&avc->pvnLock, avc);
#endif
    return 0;
}

/*!
 * VIOC_AFS_STAT_MT_PT (29) - Stat mount point
 *
 * \ingroup pioctl
 *
 * \param[in] ain	the last component in a path, related to mountpoint that we're looking for information about
 * \param[out] aout	volume, cell, link data 
 *
 * \retval EINVAL	Error if some of the standard args aren't set
 * \retval ENOTDIR	Error if the 'mount point' argument isn't a directory
 * \retval EIO		Error if the link data can't be accessed
 *
 * \post Get the volume, and cell, as well as the link data for a mount point
 */
DECL_PIOCTL(PNewStatMount)
{
    register afs_int32 code;
    register struct vcache *tvc;
    register struct dcache *tdc;
    struct VenusFid tfid;
    char *bufp;
    struct sysname_info sysState;
    afs_size_t offset, len;

    AFS_STATCNT(PNewStatMount);
    if (!avc)
	return EINVAL;
    code = afs_VerifyVCache(avc, areq);
    if (code)
	return code;
    if (vType(avc) != VDIR) {
	return ENOTDIR;
    }
    tdc = afs_GetDCache(avc, (afs_size_t) 0, areq, &offset, &len, 1);
    if (!tdc)
	return ENOENT;
    Check_AtSys(avc, ain, &sysState, areq);
    ObtainReadLock(&tdc->lock);
    do {
	code = afs_dir_Lookup(tdc, sysState.name, &tfid.Fid);
    } while (code == ENOENT && Next_AtSys(avc, areq, &sysState));
    ReleaseReadLock(&tdc->lock);
    afs_PutDCache(tdc);		/* we're done with the data */
    bufp = sysState.name;
    if (code) {
	goto out;
    }
    tfid.Cell = avc->fid.Cell;
    tfid.Fid.Volume = avc->fid.Fid.Volume;
    if (!tfid.Fid.Unique && (avc->states & CForeign)) {
	tvc = afs_LookupVCache(&tfid, areq, NULL, avc, bufp);
    } else {
	tvc = afs_GetVCache(&tfid, areq, NULL, NULL);
    }
    if (!tvc) {
	code = ENOENT;
	goto out;
    }
    if (tvc->mvstat != 1) {
	afs_PutVCache(tvc);
	code = EINVAL;
	goto out;
    }
    ObtainWriteLock(&tvc->lock, 226);
    code = afs_HandleLink(tvc, areq);
    if (code == 0) {
	if (tvc->linkData) {
	    if ((tvc->linkData[0] != '#') && (tvc->linkData[0] != '%'))
		code = EINVAL;
	    else {
		/* we have the data */
		strcpy(aout, tvc->linkData);
		*aoutSize = strlen(tvc->linkData) + 1;
	    }
	} else
	    code = EIO;
    }
    ReleaseWriteLock(&tvc->lock);
    afs_PutVCache(tvc);
  out:
    if (sysState.allocked)
	osi_FreeLargeSpace(bufp);
    return code;
}

/*!
 * VIOCGETTOK (8) - Get authentication tokens
 *  
 * \ingroup pioctl
 *      
 * \param[in] ain       userid
 * \param[out] aout     token
 * 
 * \retval EIO		Error if the afs daemon hasn't started yet
 * \retval EDOM		Error if the input parameter is out of the bounds of the available tokens
 * \retval ENOTCONN	Error if there aren't tokens for this cell
 *  
 * \post If the input paramater exists, get the token that corresponds to the parameter value, if there is no token at this value, get the token for the first cell
 *
 * \notes "it's a weird interface (from comments in the code)"
 */

DECL_PIOCTL(PGetTokens)
{
    register struct cell *tcell;
    register afs_int32 i;
    register struct unixuser *tu;
    register char *cp;
    afs_int32 iterator;
    int newStyle;

    AFS_STATCNT(PGetTokens);
    if (!afs_resourceinit_flag)	/* afs daemons haven't started yet */
	return EIO;		/* Inappropriate ioctl for device */

    /* weird interface.  If input parameter is present, it is an integer and
     * we're supposed to return the parm'th tokens for this unix uid.
     * If not present, we just return tokens for cell 1.
     * If counter out of bounds, return EDOM.
     * If no tokens for the particular cell, return ENOTCONN.
     * Also, if this mysterious parm is present, we return, along with the
     * tokens, the primary cell indicator (an afs_int32 0) and the cell name
     * at the end, in that order.
     */
    if ((newStyle = (ainSize > 0))) {
	memcpy((char *)&iterator, ain, sizeof(afs_int32));
    }
    i = UHash(areq->uid);
    ObtainReadLock(&afs_xuser);
    for (tu = afs_users[i]; tu; tu = tu->next) {
	if (newStyle) {
	    if (tu->uid == areq->uid && (tu->states & UHasTokens)) {
		if (iterator-- == 0)
		    break;	/* are we done yet? */
	    }
	} else {
	    if (tu->uid == areq->uid && afs_IsPrimaryCellNum(tu->cell))
		break;
	}
    }
    if (tu) {
	/*
	 * No need to hold a read lock on each user entry
	 */
	tu->refCount++;
    }
    ReleaseReadLock(&afs_xuser);

    if (!tu) {
	return EDOM;
    }
    if (((tu->states & UHasTokens) == 0)
	|| (tu->ct.EndTimestamp < osi_Time())) {
	tu->states |= (UTokensBad | UNeedsReset);
	afs_PutUser(tu, READ_LOCK);
	return ENOTCONN;
    }
    /* use iterator for temp */
    cp = aout;
    iterator = tu->stLen;	/* for compat, we try to return 56 byte tix if they fit */
    if (iterator < 56)
	iterator = 56;		/* # of bytes we're returning */
    memcpy(cp, (char *)&iterator, sizeof(afs_int32));
    cp += sizeof(afs_int32);
    memcpy(cp, tu->stp, tu->stLen);	/* copy out st */
    cp += iterator;
    iterator = sizeof(struct ClearToken);
    memcpy(cp, (char *)&iterator, sizeof(afs_int32));
    cp += sizeof(afs_int32);
    memcpy(cp, (char *)&tu->ct, sizeof(struct ClearToken));
    cp += sizeof(struct ClearToken);
    if (newStyle) {
	/* put out primary id and cell name, too */
	iterator = (tu->states & UPrimary ? 1 : 0);
	memcpy(cp, (char *)&iterator, sizeof(afs_int32));
	cp += sizeof(afs_int32);
	tcell = afs_GetCell(tu->cell, READ_LOCK);
	if (tcell) {
	    strcpy(cp, tcell->cellName);
	    cp += strlen(tcell->cellName) + 1;
	    afs_PutCell(tcell, READ_LOCK);
	} else
	    *cp++ = 0;
    }
    *aoutSize = cp - aout;
    afs_PutUser(tu, READ_LOCK);
    return 0;
}

/*!
 * VIOCUNLOG (9) - Invalidate tokens
 *
 * \ingroup pioctl
 *
 * \param[in] ain	not in use
 * \param[out] aout	not in use
 *
 * \retval EIO	Error if the afs daemon hasn't been started yet
 *
 * \post remove tokens from a user, specified by the user id
 *
 * \notes sets the token's time to 0, which then causes it to be removed
 * \notes Unlog is the same as un-pag in OpenAFS
 */
DECL_PIOCTL(PUnlog)
{
    register afs_int32 i;
    register struct unixuser *tu;

    AFS_STATCNT(PUnlog);
    if (!afs_resourceinit_flag)	/* afs daemons haven't started yet */
	return EIO;		/* Inappropriate ioctl for device */

    i = UHash(areq->uid);
    ObtainWriteLock(&afs_xuser, 227);
    for (tu = afs_users[i]; tu; tu = tu->next) {
	if (tu->uid == areq->uid) {
	    tu->vid = UNDEFVID;
	    tu->states &= ~UHasTokens;
	    /* security is not having to say you're sorry */
	    memset((char *)&tu->ct, 0, sizeof(struct ClearToken));
	    tu->refCount++;
	    ReleaseWriteLock(&afs_xuser);
	    /* We have to drop the lock over the call to afs_ResetUserConns, since
	     * it obtains the afs_xvcache lock.  We could also keep the lock, and
	     * modify ResetUserConns to take parm saying we obtained the lock
	     * already, but that is overkill.  By keeping the "tu" pointer
	     * held over the released lock, we guarantee that we won't lose our
	     * place, and that we'll pass over every user conn that existed when
	     * we began this call.
	     */
	    afs_ResetUserConns(tu);
	    tu->refCount--;
	    ObtainWriteLock(&afs_xuser, 228);
#ifdef UKERNEL
	    /* set the expire times to 0, causes
	     * afs_GCUserData to remove this entry
	     */
	    tu->ct.EndTimestamp = 0;
	    tu->tokenTime = 0;
#endif /* UKERNEL */
	}
    }
    ReleaseWriteLock(&afs_xuser);
    return 0;
}

/*!
 * VIOC_AFS_MARINER_HOST (32) - Get/set mariner (cache manager monitor) host
 *
 * \ingroup pioctl
 *
 * \param[in] ain	host address to be set
 * \param[out] aout	old host address
 *
 * \post depending on whether or not a variable is set, either get the host for the cache manager monitor, or set the old address and give it a new address
 *
 * \notes Errors turn off mariner
 */
DECL_PIOCTL(PMariner)
{
    afs_int32 newHostAddr;
    afs_int32 oldHostAddr;

    AFS_STATCNT(PMariner);
    if (afs_mariner)
	memcpy((char *)&oldHostAddr, (char *)&afs_marinerHost,
	       sizeof(afs_int32));
    else
	oldHostAddr = 0xffffffff;	/* disabled */

    memcpy((char *)&newHostAddr, ain, sizeof(afs_int32));
    if (newHostAddr == 0xffffffff) {
	/* disable mariner operations */
	afs_mariner = 0;
    } else if (newHostAddr) {
	afs_mariner = 1;
	afs_marinerHost = newHostAddr;
    }
    memcpy(aout, (char *)&oldHostAddr, sizeof(afs_int32));
    *aoutSize = sizeof(afs_int32);
    return 0;
}

/*!
 * VIOCCKSERV (10) - Check that servers are up
 *
 * \ingroup pioctl
 *
 * \param[in] ain	name of the cell
 * \param[out] aout	current down server list
 *
 * \retval EIO		Error if the afs daemon hasn't started yet
 * \retval EACCES	Error if the user doesn't have super-user credentials
 * \retval ENOENT	Error if we are unable to obtain the cell
 *
 * \post Either a fast check (where it doesn't contact servers) or a local check (checks local cell only)
 */
DECL_PIOCTL(PCheckServers)
{
    register char *cp = 0;
    register int i;
    register struct server *ts;
    afs_int32 temp, *lp = (afs_int32 *) ain, havecell = 0;
    struct cell *cellp;
    struct chservinfo *pcheck;

    AFS_STATCNT(PCheckServers);

    if (!afs_resourceinit_flag)	/* afs daemons haven't started yet */
	return EIO;		/* Inappropriate ioctl for device */

    if (*lp == 0x12345678) {	/* For afs3.3 version */
	pcheck = (struct chservinfo *)ain;
	if (pcheck->tinterval >= 0) {
	    cp = aout;
	    memcpy(cp, (char *)&afs_probe_interval, sizeof(afs_int32));
	    *aoutSize = sizeof(afs_int32);
	    if (pcheck->tinterval > 0) {
		if (!afs_osi_suser(*acred))
		    return EACCES;
		afs_probe_interval = pcheck->tinterval;
	    }
	    return 0;
	}
	if (pcheck->tsize)
	    havecell = 1;
	temp = pcheck->tflags;
	cp = pcheck->tbuffer;
    } else {			/* For pre afs3.3 versions */
	memcpy((char *)&temp, ain, sizeof(afs_int32));
	cp = ain + sizeof(afs_int32);
	if (ainSize > sizeof(afs_int32))
	    havecell = 1;
    }

    /*
     * 1: fast check, don't contact servers.
     * 2: local cell only.
     */
    if (havecell) {
	/* have cell name, too */
	cellp = afs_GetCellByName(cp, READ_LOCK);
	if (!cellp)
	    return ENOENT;
    } else
	cellp = NULL;
    if (!cellp && (temp & 2)) {
	/* use local cell */
	cellp = afs_GetPrimaryCell(READ_LOCK);
    }
    if (!(temp & 1)) {		/* if not fast, call server checker routine */
	afs_CheckServers(1, cellp);	/* check down servers */
	afs_CheckServers(0, cellp);	/* check up servers */
    }
    /* now return the current down server list */
    cp = aout;
    ObtainReadLock(&afs_xserver);
    for (i = 0; i < NSERVERS; i++) {
	for (ts = afs_servers[i]; ts; ts = ts->next) {
	    if (cellp && ts->cell != cellp)
		continue;	/* cell spec'd and wrong */
	    if ((ts->flags & SRVR_ISDOWN)
		&& ts->addr->sa_portal != ts->cell->vlport) {
		memcpy(cp, (char *)&ts->addr->sa_ip, sizeof(afs_int32));
		cp += sizeof(afs_int32);
	    }
	}
    }
    ReleaseReadLock(&afs_xserver);
    if (cellp)
	afs_PutCell(cellp, READ_LOCK);
    *aoutSize = cp - aout;
    return 0;
}

/*!
 * VIOCCKBACK (11) - Check backup volume mappings
 *
 * \ingroup pioctl
 *
 * \param[in] ain	not in use
 * \param[out] aout	not in use
 *
 * \retval EIO		Error if the afs daemon hasn't started yet
 *
 * \post Check the root volume, and then check the names if the volume check variable is set to force, has expired, is busy, or if the mount points variable is set
 */
DECL_PIOCTL(PCheckVolNames)
{
    AFS_STATCNT(PCheckVolNames);
    if (!afs_resourceinit_flag)	/* afs daemons haven't started yet */
	return EIO;		/* Inappropriate ioctl for device */

    afs_CheckRootVolume();
    afs_CheckVolumeNames(AFS_VOLCHECK_FORCE | AFS_VOLCHECK_EXPIRED |
			 AFS_VOLCHECK_BUSY | AFS_VOLCHECK_MTPTS);
    return 0;
}

/*!
 * VIOCCKCONN (12) - Check connections for a user
 *
 * \ingroup pioctl
 *
 * \param[in] ain	not in use
 * \param[out] aout	not in use
 *
 * \retval EACCESS Error if no user is specififed, the user has no tokens set, or if the user's tokens are bad
 *
 * \post check to see if a user has the correct authentication.  If so, allow access.
 *
 * \notes Check the connections to all the servers specified
 */
DECL_PIOCTL(PCheckAuth)
{
    int i;
    struct srvAddr *sa;
    struct conn *tc;
    struct unixuser *tu;
    afs_int32 retValue;

    AFS_STATCNT(PCheckAuth);
    if (!afs_resourceinit_flag)	/* afs daemons haven't started yet */
	return EIO;		/* Inappropriate ioctl for device */

    retValue = 0;
    tu = afs_GetUser(areq->uid, 1, READ_LOCK);	/* check local cell authentication */
    if (!tu)
	retValue = EACCES;
    else {
	/* we have a user */
	ObtainReadLock(&afs_xsrvAddr);
	ObtainReadLock(&afs_xconn);

	/* any tokens set? */
	if ((tu->states & UHasTokens) == 0)
	    retValue = EACCES;
	/* all connections in cell 1 working? */
	for (i = 0; i < NSERVERS; i++) {
	    for (sa = afs_srvAddrs[i]; sa; sa = sa->next_bkt) {
		for (tc = sa->conns; tc; tc = tc->next) {
		    if (tc->user == tu && (tu->states & UTokensBad))
			retValue = EACCES;
		}
	    }
	}
	ReleaseReadLock(&afs_xsrvAddr);
	ReleaseReadLock(&afs_xconn);
	afs_PutUser(tu, READ_LOCK);
    }
    memcpy(aout, (char *)&retValue, sizeof(afs_int32));
    *aoutSize = sizeof(afs_int32);
    return 0;
}

static int
Prefetch(char *apath, struct afs_ioctl *adata, int afollow,
	 struct AFS_UCRED *acred)
{
    register char *tp;
    register afs_int32 code;
#if defined(AFS_SGI61_ENV) || defined(AFS_SUN57_ENV) || defined(AFS_DARWIN_ENV) || defined(AFS_XBSD_ENV)
    size_t bufferSize;
#else
    u_int bufferSize;
#endif

    AFS_STATCNT(Prefetch);
    if (!apath)
	return EINVAL;
    tp = osi_AllocLargeSpace(1024);
    AFS_COPYINSTR(apath, tp, 1024, &bufferSize, code);
    if (code) {
	osi_FreeLargeSpace(tp);
	return code;
    }
    if (afs_BBusy()) {		/* do this as late as possible */
	osi_FreeLargeSpace(tp);
	return EWOULDBLOCK;	/* pretty close */
    }
    afs_BQueue(BOP_PATH, (struct vcache *)0, 0, 0, acred, (afs_size_t) 0,
	       (afs_size_t) 0, tp);
    return 0;
}

/*!
 * VIOCWHEREIS (14) - Find out where a volume is located
 *
 * \ingroup pioctl
 *
 * \param[in] ain	not in use
 * \param[out] aout	volume location
 *
 * \retval EINVAL	Error if some of the default arguments don't exist
 * \retval ENODEV	Error if there is no such volume
 *
 * \post fine a volume, based on a volume file id
 *
 * \notes check each of the servers specified
 */
DECL_PIOCTL(PFindVolume)
{
    register struct volume *tvp;
    register struct server *ts;
    register afs_int32 i;
    register char *cp;

    AFS_STATCNT(PFindVolume);
    if (!avc)
	return EINVAL;
    tvp = afs_GetVolume(&avc->fid, areq, READ_LOCK);
    if (tvp) {
	cp = aout;
	for (i = 0; i < MAXHOSTS; i++) {
	    ts = tvp->serverHost[i];
	    if (!ts)
		break;
	    memcpy(cp, (char *)&ts->addr->sa_ip, sizeof(afs_int32));
	    cp += sizeof(afs_int32);
	}
	if (i < MAXHOSTS) {
	    /* still room for terminating NULL, add it on */
	    ainSize = 0;	/* reuse vbl */
	    memcpy(cp, (char *)&ainSize, sizeof(afs_int32));
	    cp += sizeof(afs_int32);
	}
	*aoutSize = cp - aout;
	afs_PutVolume(tvp, READ_LOCK);
	return 0;
    }
    return ENODEV;
}

/*!
 * VIOCACCESS (20) - Access using PRS_FS bits
 *
 * \ingroup pioctl
 *
 * \param[in] ain	PRS_FS bits
 * \param[out] aout	not in use
 *
 * \retval EINVAL	Error if some of the initial arguments aren't set
 * \retval EACCES	Error if access is denied
 *
 * \post check to make sure access is allowed
 */
DECL_PIOCTL(PViceAccess)
{
    register afs_int32 code;
    afs_int32 temp;

    AFS_STATCNT(PViceAccess);
    if (!avc)
	return EINVAL;
    code = afs_VerifyVCache(avc, areq);
    if (code)
	return code;
    memcpy((char *)&temp, ain, sizeof(afs_int32));
    code = afs_AccessOK(avc, temp, areq, CHECK_MODE_BITS);
    if (code)
	return 0;
    else
	return EACCES;
}

DECL_PIOCTL(PPrecache)
{
    afs_int32 newValue;

    /*AFS_STATCNT(PPrecache);*/
    if (!afs_osi_suser(*acred))
	return EACCES;
    memcpy((char *)&newValue, ain, sizeof(afs_int32));
    afs_preCache = newValue*1024;
    return 0;
}

/*!
 * VIOCSETCACHESIZE (24) - Set venus cache size in 1000 units
 *
 * \ingroup pioctl
 *
 * \param[in] ain	the size the venus cache should be set to
 * \param[out] aout	not in use
 *
 * \retval EACCES	Error if the user doesn't have super-user credentials
 * \retval EROFS	Error if the cache is set to be in memory
 *
 * \post Set the cache size based on user input.  If no size is given, set it to the default OpenAFS cache size.
 *
 * \notes recompute the general cache parameters for every single block allocated
 */
DECL_PIOCTL(PSetCacheSize)
{
    afs_int32 newValue;
    int waitcnt = 0;

    AFS_STATCNT(PSetCacheSize);
    if (!afs_osi_suser(*acred))
	return EACCES;
    /* too many things are setup initially in mem cache version */
    if (cacheDiskType == AFS_FCACHE_TYPE_MEM)
	return EROFS;
    memcpy((char *)&newValue, ain, sizeof(afs_int32));
    if (newValue == 0)
	afs_cacheBlocks = afs_stats_cmperf.cacheBlocksOrig;
    else {
	if (newValue < afs_min_cache)
	    afs_cacheBlocks = afs_min_cache;
	else
	    afs_cacheBlocks = newValue;
    }
    afs_stats_cmperf.cacheBlocksTotal = afs_cacheBlocks;
    afs_ComputeCacheParms();	/* recompute basic cache parameters */
    afs_MaybeWakeupTruncateDaemon();
    while (waitcnt++ < 100 && afs_cacheBlocks < afs_blocksUsed) {
	afs_osi_Wait(1000, 0, 0);
	afs_MaybeWakeupTruncateDaemon();
    }
    return 0;
}

#define MAXGCSTATS	16
/*!
 * VIOCGETCACHEPARMS (40) - Get cache stats
 *
 * \ingroup pioctl
 *
 * \param[in] ain	afs index flags
 * \param[out] aout	cache blocks, blocks used, blocks files (in an array)
 *
 * \post Get the cache blocks, and how many of the cache blocks there are
 */
DECL_PIOCTL(PGetCacheSize)
{
    afs_int32 results[MAXGCSTATS];
    afs_int32 flags;
    register struct dcache * tdc;
    int i, size;
    
    AFS_STATCNT(PGetCacheSize);

    if (sizeof(afs_int32) == ainSize){
	memcpy((char *)&flags, ain, sizeof(afs_int32));
    } else if (0 == ainSize){ 
	flags = 0;
    } else {
	return EINVAL;
    }
    
    memset((char *)results, 0, sizeof(results));
    results[0] = afs_cacheBlocks;
    results[1] = afs_blocksUsed;
    results[2] = afs_cacheFiles;
    
    if (1 == flags){
        for (i = 0; i < afs_cacheFiles; i++) {
	    if (afs_indexFlags[i] & IFFree) results[3]++;
	}
    } else if (2 == flags){
        for (i = 0; i < afs_cacheFiles; i++) {
	    if (afs_indexFlags[i] & IFFree) results[3]++;
	    if (afs_indexFlags[i] & IFEverUsed) results[4]++;
	    if (afs_indexFlags[i] & IFDataMod) results[5]++;
	    if (afs_indexFlags[i] & IFDirtyPages) results[6]++;
	    if (afs_indexFlags[i] & IFAnyPages) results[7]++;
	    if (afs_indexFlags[i] & IFDiscarded) results[8]++;

	    tdc = afs_indexTable[i];
	    if (tdc){
	        results[9]++;
	        size = tdc->validPos;
	        if ( 0 < size && size < (1<<12) ) results[10]++;
    	        else if (size < (1<<14) ) results[11]++;
	        else if (size < (1<<16) ) results[12]++;
	        else if (size < (1<<18) ) results[13]++;
	        else if (size < (1<<20) ) results[14]++;
	        else if (size >= (1<<20) ) results[15]++;
	    }
        }
    }
    memcpy(aout, (char *)results, sizeof(results));
    *aoutSize = sizeof(results);
    return 0;
}

/*!
 * VIOCFLUSHCB (25) - Flush callback only
 *
 * \ingroup pioctl
 *
 * \param[in] ain	not in use
 * \param[out] aout	not in use
 *
 * \retval EINVAL	Error if some of the standard args aren't set
 * \retval 0		0 returned if the volume is set to read-only
 *
 * \post Flushes callbacks, by setting the length of callbacks to one, setting the next callback to be sent to the CB_DROPPED value, and then dequeues everything else.
 */
DECL_PIOCTL(PRemoveCallBack)
{
    register struct conn *tc;
    register afs_int32 code = 0;
    struct AFSCallBack CallBacks_Array[1];
    struct AFSCBFids theFids;
    struct AFSCBs theCBs;
    XSTATS_DECLS;

    AFS_STATCNT(PRemoveCallBack);
    if (!avc)
	return EINVAL;
    if (avc->states & CRO)
	return 0;		/* read-only-ness can't change */
    ObtainWriteLock(&avc->lock, 229);
    theFids.AFSCBFids_len = 1;
    theCBs.AFSCBs_len = 1;
    theFids.AFSCBFids_val = (struct AFSFid *)&avc->fid.Fid;
    theCBs.AFSCBs_val = CallBacks_Array;
    CallBacks_Array[0].CallBackType = CB_DROPPED;
    if (avc->callback) {
	do {
	    tc = afs_Conn(&avc->fid, areq, SHARED_LOCK);
	    if (tc) {
		XSTATS_START_TIME(AFS_STATS_FS_RPCIDX_GIVEUPCALLBACKS);
		RX_AFS_GUNLOCK();
		code = RXAFS_GiveUpCallBacks(tc->id, &theFids, &theCBs);
		RX_AFS_GLOCK();
		XSTATS_END_TIME;
	    }
	    /* don't set code on failure since we wouldn't use it */
	} while (afs_Analyze
		 (tc, code, &avc->fid, areq,
		  AFS_STATS_FS_RPCIDX_GIVEUPCALLBACKS, SHARED_LOCK, NULL));

	ObtainWriteLock(&afs_xcbhash, 457);
	afs_DequeueCallback(avc);
	avc->callback = 0;
	avc->states &= ~(CStatd | CUnique);
	ReleaseWriteLock(&afs_xcbhash);
	if (avc->fid.Fid.Vnode & 1 || (vType(avc) == VDIR))
	    osi_dnlc_purgedp(avc);
    }
    ReleaseWriteLock(&avc->lock);
    return 0;
}

/*!
 * VIOCNEWCELL (26) - Configure new cell
 *
 * \ingroup pioctl
 *
 * \param[in] ain	the name of the cell, the hosts that will be a part of the cell, whether or not it's linked with another cell, the other cell it's linked with, the file server port, and the volume server port
 * \param[out] aout	not in use
 *
 * \retval EIO		Error if the afs daemon hasn't started yet
 * \retval EACCES	Error if the user doesn't have super-user cedentials
 * \retval EINVAL	Error if some 'magic' var doesn't have a certain bit set
 *
 * \post creates a new cell
 */
DECL_PIOCTL(PNewCell)
{
    /* create a new cell */
    afs_int32 cellHosts[MAXCELLHOSTS], *lp, magic = 0;
    char *newcell = 0, *linkedcell = 0, *tp = ain;
    register afs_int32 code, linkedstate = 0, ls;
    u_short fsport = 0, vlport = 0;
    afs_int32 scount;

    AFS_STATCNT(PNewCell);
    if (!afs_resourceinit_flag)	/* afs daemons haven't started yet */
	return EIO;		/* Inappropriate ioctl for device */

    if (!afs_osi_suser(*acred))
	return EACCES;

    memcpy((char *)&magic, tp, sizeof(afs_int32));
    tp += sizeof(afs_int32);
    if (magic != 0x12345678)
	return EINVAL;

    /* A 3.4 fs newcell command will pass an array of MAXCELLHOSTS
     * server addresses while the 3.5 fs newcell command passes
     * MAXHOSTS. To figure out which is which, check if the cellname
     * is good.
     */
    newcell = tp + (MAXCELLHOSTS + 3) * sizeof(afs_int32);
    scount = ((newcell[0] != '\0') ? MAXCELLHOSTS : MAXHOSTS);

    /* MAXCELLHOSTS (=8) is less than MAXHOSTS (=13) */
    memcpy((char *)cellHosts, tp, MAXCELLHOSTS * sizeof(afs_int32));
    tp += (scount * sizeof(afs_int32));

    lp = (afs_int32 *) tp;
    fsport = *lp++;
    vlport = *lp++;
    if (fsport < 1024)
	fsport = 0;		/* Privileged ports not allowed */
    if (vlport < 1024)
	vlport = 0;		/* Privileged ports not allowed */
    tp += (3 * sizeof(afs_int32));
    newcell = tp;
    if ((ls = *lp) & 1) {
	linkedcell = tp + strlen(newcell) + 1;
	linkedstate |= CLinkedCell;
    }

    linkedstate |= CNoSUID;	/* setuid is disabled by default for fs newcell */
    code =
	afs_NewCell(newcell, cellHosts, linkedstate, linkedcell, fsport,
		    vlport, (int)0);
    return code;
}

DECL_PIOCTL(PNewAlias)
{
    /* create a new cell alias */
    char *tp = ain;
    register afs_int32 code;
    char *realName, *aliasName;

    if (!afs_resourceinit_flag)	/* afs daemons haven't started yet */
	return EIO;		/* Inappropriate ioctl for device */

    if (!afs_osi_suser(*acred))
	return EACCES;

    aliasName = tp;
    tp += strlen(aliasName) + 1;
    realName = tp;

    code = afs_NewCellAlias(aliasName, realName);
    *aoutSize = 0;
    return code;
}

/*!
 * VIOCGETCELL (27) - Get cell info
 *
 * \ingroup pioctl
 *
 * \param[in] ain	The cell index of a specific cell
 * \param[out] aout	list of servers in the cell
 *
 * \retval EIO		Error if the afs daemon hasn't started yet
 * \retval EDOM		Error if there is no cell asked about
 *
 * \post Lists the cell's server names and and addresses
 */
DECL_PIOCTL(PListCells)
{
    afs_int32 whichCell;
    register struct cell *tcell = 0;
    register afs_int32 i;
    register char *cp, *tp = ain;

    AFS_STATCNT(PListCells);
    if (!afs_resourceinit_flag)	/* afs daemons haven't started yet */
	return EIO;		/* Inappropriate ioctl for device */

    memcpy((char *)&whichCell, tp, sizeof(afs_int32));
    tp += sizeof(afs_int32);
    tcell = afs_GetCellByIndex(whichCell, READ_LOCK);
    if (tcell) {
	cp = aout;
	memset(cp, 0, MAXCELLHOSTS * sizeof(afs_int32));
	for (i = 0; i < MAXCELLHOSTS; i++) {
	    if (tcell->cellHosts[i] == 0)
		break;
	    memcpy(cp, (char *)&tcell->cellHosts[i]->addr->sa_ip,
		   sizeof(afs_int32));
	    cp += sizeof(afs_int32);
	}
	cp = aout + MAXCELLHOSTS * sizeof(afs_int32);
	strcpy(cp, tcell->cellName);
	cp += strlen(tcell->cellName) + 1;
	*aoutSize = cp - aout;
	afs_PutCell(tcell, READ_LOCK);
    }
    if (tcell)
	return 0;
    else
	return EDOM;
}

DECL_PIOCTL(PListAliases)
{
    afs_int32 whichAlias;
    register struct cell_alias *tcalias = 0;
    register char *cp, *tp = ain;

    if (!afs_resourceinit_flag)	/* afs daemons haven't started yet */
	return EIO;		/* Inappropriate ioctl for device */
    if (ainSize < sizeof(afs_int32))
	return EINVAL;

    memcpy((char *)&whichAlias, tp, sizeof(afs_int32));
    tp += sizeof(afs_int32);

    tcalias = afs_GetCellAlias(whichAlias);
    if (tcalias) {
	cp = aout;
	strcpy(cp, tcalias->alias);
	cp += strlen(tcalias->alias) + 1;
	strcpy(cp, tcalias->cell);
	cp += strlen(tcalias->cell) + 1;
	*aoutSize = cp - aout;
	afs_PutCellAlias(tcalias);
    }
    if (tcalias)
	return 0;
    else
	return EDOM;
}

/*!
 * VIOC_AFS_DELETE_MT_PT (28) - Delete mount point
 *
 * \ingroup pioctl
 *
 * \param[in] ain 	the name of the file in this dir to remove
 * \param[out] aout	not in use
 *
 * \retval EINVAL	Error if some of the standard args aren't set
 * \retval ENOTDIR	Error if the argument to remove is not a directory
 * \retval ENOENT	Error if there is no cache to remove the mount point from or if a vcache doesn't exist
 *
 * \post Ensure that everything is OK before deleting the mountpoint.  If not, don't delete.  Delete a mount point based on a file id.
 */
DECL_PIOCTL(PRemoveMount)
{
    register afs_int32 code;
    char *bufp;
    struct sysname_info sysState;
    afs_size_t offset, len;
    register struct conn *tc;
    register struct dcache *tdc;
    register struct vcache *tvc;
    struct AFSFetchStatus OutDirStatus;
    struct VenusFid tfid;
    struct AFSVolSync tsync;
    XSTATS_DECLS;


    /* "ain" is the name of the file in this dir to remove */

    AFS_STATCNT(PRemoveMount);
    if (!avc)
	return EINVAL;
    code = afs_VerifyVCache(avc, areq);
    if (code)
	return code;
    if (vType(avc) != VDIR)
	return ENOTDIR;

    tdc = afs_GetDCache(avc, (afs_size_t) 0, areq, &offset, &len, 1);	/* test for error below */
    if (!tdc)
	return ENOENT;
    Check_AtSys(avc, ain, &sysState, areq);
    ObtainReadLock(&tdc->lock);
    do {
	code = afs_dir_Lookup(tdc, sysState.name, &tfid.Fid);
    } while (code == ENOENT && Next_AtSys(avc, areq, &sysState));
    ReleaseReadLock(&tdc->lock);
    bufp = sysState.name;
    if (code) {
	afs_PutDCache(tdc);
	goto out;
    }
    tfid.Cell = avc->fid.Cell;
    tfid.Fid.Volume = avc->fid.Fid.Volume;
    if (!tfid.Fid.Unique && (avc->states & CForeign)) {
	tvc = afs_LookupVCache(&tfid, areq, NULL, avc, bufp);
    } else {
	tvc = afs_GetVCache(&tfid, areq, NULL, NULL);
    }
    if (!tvc) {
	code = ENOENT;
	afs_PutDCache(tdc);
	goto out;
    }
    if (tvc->mvstat != 1) {
	afs_PutDCache(tdc);
	afs_PutVCache(tvc);
	code = EINVAL;
	goto out;
    }
    ObtainWriteLock(&tvc->lock, 230);
    code = afs_HandleLink(tvc, areq);
    if (!code) {
	if (tvc->linkData) {
	    if ((tvc->linkData[0] != '#') && (tvc->linkData[0] != '%'))
		code = EINVAL;
	} else
	    code = EIO;
    }
    ReleaseWriteLock(&tvc->lock);
    osi_dnlc_purgedp(tvc);
    afs_PutVCache(tvc);
    if (code) {
	afs_PutDCache(tdc);
	goto out;
    }
    ObtainWriteLock(&avc->lock, 231);
    osi_dnlc_remove(avc, bufp, tvc);
    do {
	tc = afs_Conn(&avc->fid, areq, SHARED_LOCK);
	if (tc) {
	    XSTATS_START_TIME(AFS_STATS_FS_RPCIDX_REMOVEFILE);
	    RX_AFS_GUNLOCK();
	    code =
		RXAFS_RemoveFile(tc->id, (struct AFSFid *)&avc->fid.Fid, bufp,
				 &OutDirStatus, &tsync);
	    RX_AFS_GLOCK();
	    XSTATS_END_TIME;
	} else
	    code = -1;
    } while (afs_Analyze
	     (tc, code, &avc->fid, areq, AFS_STATS_FS_RPCIDX_REMOVEFILE,
	      SHARED_LOCK, NULL));

    if (code) {
	if (tdc)
	    afs_PutDCache(tdc);
	ReleaseWriteLock(&avc->lock);
	goto out;
    }
    if (tdc) {
	/* we have the thing in the cache */
	ObtainWriteLock(&tdc->lock, 661);
	if (afs_LocalHero(avc, tdc, &OutDirStatus, 1)) {
	    /* we can do it locally */
	    code = afs_dir_Delete(tdc, bufp);
	    if (code) {
		ZapDCE(tdc);	/* surprise error -- invalid value */
		DZap(tdc);
	    }
	}
	ReleaseWriteLock(&tdc->lock);
	afs_PutDCache(tdc);	/* drop ref count */
    }
    avc->states &= ~CUnique;	/* For the dfs xlator */
    ReleaseWriteLock(&avc->lock);
    code = 0;
  out:
    if (sysState.allocked)
	osi_FreeLargeSpace(bufp);
    return code;
}

/*!
 * VIOC_VENUSLOG (34) - Enable/Disable venus logging
 *
 * \ingroup pioctl
 *
 * \retval EINVAL	Error if some of the standard args aren't set
 *
 * \notes Obsoleted, perhaps should be PBogus
 */
DECL_PIOCTL(PVenusLogging)
{
    return EINVAL;		/* OBSOLETE */
}

/*!
 * VIOC_GETCELLSTATUS (35) - Get cell status info
 *
 * \ingroup pioctl
 *
 * \param[in] ain	The cell you want status information on
 * \param[out] aout	cell state (as a struct)
 *
 * \retval EIO		Error if the afs daemon hasn't started yet
 * \retval ENOENT	Error if the cell doesn't exist
 *
 * \post Returns the state of the cell as defined in a struct cell
 */
DECL_PIOCTL(PGetCellStatus)
{
    register struct cell *tcell;
    afs_int32 temp;

    AFS_STATCNT(PGetCellStatus);
    if (!afs_resourceinit_flag)	/* afs daemons haven't started yet */
	return EIO;		/* Inappropriate ioctl for device */

    tcell = afs_GetCellByName(ain, READ_LOCK);
    if (!tcell)
	return ENOENT;
    temp = tcell->states;
    afs_PutCell(tcell, READ_LOCK);
    memcpy(aout, (char *)&temp, sizeof(afs_int32));
    *aoutSize = sizeof(afs_int32);
    return 0;
}

/*!
 * VIOC_SETCELLSTATUS (36) - Set corresponding info
 *
 * \ingroup pioctl
 *
 * \param[in] ain	The cell you want to set information about, and the values you want to set
 * \param[out] aout	not in use
 *
 * \retval EIO		Error if the afs daemon hasn't started yet
 * \retval EACCES	Error if the user doesn't have super-user credentials
 *
 * \post Set the state of the cell in a defined struct cell, based on whether or not SetUID is allowed
 */
DECL_PIOCTL(PSetCellStatus)
{
    register struct cell *tcell;
    afs_int32 temp;

    if (!afs_osi_suser(*acred))
	return EACCES;
    if (!afs_resourceinit_flag)	/* afs daemons haven't started yet */
	return EIO;		/* Inappropriate ioctl for device */

    tcell = afs_GetCellByName(ain + 2 * sizeof(afs_int32), WRITE_LOCK);
    if (!tcell)
	return ENOENT;
    memcpy((char *)&temp, ain, sizeof(afs_int32));
    if (temp & CNoSUID)
	tcell->states |= CNoSUID;
    else
	tcell->states &= ~CNoSUID;
    afs_PutCell(tcell, WRITE_LOCK);
    return 0;
}

/*!
 * VIOC_FLUSHVOLUME (37) - Flush whole volume's data
 *
 * \ingroup pioctl
 *
 * \param[in] ain	not in use (args in avc)
 * \param[out] aout	not in use
 *
 * \retval EINVAL	Error if some of the standard args aren't set
 * \retval EIO		Error if the afs daemon hasn't started yet
 *
 * \post Wipe everything on the volume.  This is done dependent on which platform this is for.
 *
 * \notes Does not flush a file that a user has open and is using, because it will be re-created on next write.  Also purges the dnlc, because things are screwed up.
 */
DECL_PIOCTL(PFlushVolumeData)
{
    register afs_int32 i;
    register struct dcache *tdc;
    register struct vcache *tvc;
    register struct volume *tv;
    afs_int32 cell, volume;
    struct afs_q *tq, *uq;
#ifdef AFS_DARWIN80_ENV
    vnode_t vp;
#endif

    AFS_STATCNT(PFlushVolumeData);
    if (!avc)
	return EINVAL;
    if (!afs_resourceinit_flag)	/* afs daemons haven't started yet */
	return EIO;		/* Inappropriate ioctl for device */

    volume = avc->fid.Fid.Volume;	/* who to zap */
    cell = avc->fid.Cell;

    /*
     * Clear stat'd flag from all vnodes from this volume; this will invalidate all
     * the vcaches associated with the volume.
     */
 loop:
    ObtainReadLock(&afs_xvcache);
    i = VCHashV(&avc->fid);
    for (tq = afs_vhashTV[i].prev; tq != &afs_vhashTV[i]; tq = uq) {
	    uq = QPrev(tq);
	    tvc = QTOVH(tq);
	    if (tvc->fid.Fid.Volume == volume && tvc->fid.Cell == cell) {
		if (tvc->states & CVInit) {
		    ReleaseReadLock(&afs_xvcache);
		    afs_osi_Sleep(&tvc->states);
		    goto loop;
		}
#ifdef AFS_DARWIN80_ENV
		if (tvc->states & CDeadVnode) {
		    ReleaseReadLock(&afs_xvcache);
		    afs_osi_Sleep(&tvc->states);
		    goto loop;
		}
#endif
#if	defined(AFS_SGI_ENV) || defined(AFS_OSF_ENV)  || defined(AFS_SUN5_ENV)  || defined(AFS_HPUX_ENV) || defined(AFS_LINUX20_ENV)
		VN_HOLD(AFSTOV(tvc));
#else
#ifdef AFS_DARWIN80_ENV
		vp = AFSTOV(tvc);
		if (vnode_get(vp))
		    continue;
		if (vnode_ref(vp)) {
		    AFS_GUNLOCK();
		    vnode_put(vp);
		    AFS_GLOCK();
		    continue;
		}
#else
#if defined(AFS_DARWIN_ENV) || defined(AFS_XBSD_ENV)
		osi_vnhold(tvc, 0);
#else
		VREFCOUNT_INC(tvc); /* AIX, apparently */
#endif
#endif
#endif
		ReleaseReadLock(&afs_xvcache);
#ifdef AFS_BOZONLOCK_ENV
		afs_BozonLock(&tvc->pvnLock, tvc);	/* Since afs_TryToSmush will do a pvn_vptrunc */
#endif
		ObtainWriteLock(&tvc->lock, 232);

		ObtainWriteLock(&afs_xcbhash, 458);
		afs_DequeueCallback(tvc);
		tvc->states &= ~(CStatd | CDirty);
		ReleaseWriteLock(&afs_xcbhash);
		if (tvc->fid.Fid.Vnode & 1 || (vType(tvc) == VDIR))
		    osi_dnlc_purgedp(tvc);
		afs_TryToSmush(tvc, *acred, 1);
		ReleaseWriteLock(&tvc->lock);
#ifdef AFS_BOZONLOCK_ENV
		afs_BozonUnlock(&tvc->pvnLock, tvc);
#endif
#ifdef AFS_DARWIN80_ENV
		vnode_put(AFSTOV(tvc));
#endif
		ObtainReadLock(&afs_xvcache);
		uq = QPrev(tq);
		/* our tvc ptr is still good until now */
		AFS_FAST_RELE(tvc);
	    }
	}
    ReleaseReadLock(&afs_xvcache);


    MObtainWriteLock(&afs_xdcache, 328);	/* needed if you're going to flush any stuff */
    for (i = 0; i < afs_cacheFiles; i++) {
	if (!(afs_indexFlags[i] & IFEverUsed))
	    continue;		/* never had any data */
	tdc = afs_GetDSlot(i, NULL);
	if (tdc->refCount <= 1) {	/* too high, in use by running sys call */
	    ReleaseReadLock(&tdc->tlock);
	    if (tdc->f.fid.Fid.Volume == volume && tdc->f.fid.Cell == cell) {
		if (!(afs_indexFlags[i] & IFDataMod)) {
		    /* if the file is modified, but has a ref cnt of only 1, then
		     * someone probably has the file open and is writing into it.
		     * Better to skip flushing such a file, it will be brought back
		     * immediately on the next write anyway.
		     * 
		     * If we *must* flush, then this code has to be rearranged to call
		     * afs_storeAllSegments() first */
		    afs_FlushDCache(tdc);
		}
	    }
	} else {
	    ReleaseReadLock(&tdc->tlock);
	}
	afs_PutDCache(tdc);	/* bumped by getdslot */
    }
    MReleaseWriteLock(&afs_xdcache);

    ObtainReadLock(&afs_xvolume);
    for (i = 0; i < NVOLS; i++) {
	for (tv = afs_volumes[i]; tv; tv = tv->next) {
	    if (tv->volume == volume) {
		afs_ResetVolumeInfo(tv);
		break;
	    }
	}
    }
    ReleaseReadLock(&afs_xvolume);

    /* probably, a user is doing this, probably, because things are screwed up.
     * maybe it's the dnlc's fault? */
    osi_dnlc_purge();
    return 0;
}


/*!
 * VIOCGETVCXSTATUS (41) - gets vnode x status
 *
 * \ingroup pioctl
 *
 * \param[in] ain 	not in use (avc used)
 * \param[out] aout	vcxstat: the file id, the data version, any lock, the parent vnode, the parent unique id, the trunc position, the callback, cbExpires, what access is being made, what files are open, any users executing/writing, the flock ount, the states, the move stat
 *
 * \retval EINVAL	Error if some of the initial default arguments aren't set
 * \retval EACCES	Error if access to check the mode bits is denied
 *
 * \post gets stats for the vnode, a struct listed in vcxstat
 */
DECL_PIOCTL(PGetVnodeXStatus)
{
    register afs_int32 code;
    struct vcxstat stat;
    afs_int32 mode, i;

/*  AFS_STATCNT(PGetVnodeXStatus); */
    if (!avc)
	return EINVAL;
    code = afs_VerifyVCache(avc, areq);
    if (code)
	return code;
    if (vType(avc) == VDIR)
	mode = PRSFS_LOOKUP;
    else
	mode = PRSFS_READ;
    if (!afs_AccessOK(avc, mode, areq, CHECK_MODE_BITS))
	return EACCES;

    memset(&stat, 0, sizeof(struct vcxstat));
    stat.fid = avc->fid;
    hset32(stat.DataVersion, hgetlo(avc->m.DataVersion));
    stat.lock = avc->lock;
    stat.parentVnode = avc->parentVnode;
    stat.parentUnique = avc->parentUnique;
    hset(stat.flushDV, avc->flushDV);
    hset(stat.mapDV, avc->mapDV);
    stat.truncPos = avc->truncPos;
    {				/* just grab the first two - won't break anything... */
	struct axscache *ac;

	for (i = 0, ac = avc->Access; ac && i < CPSIZE; i++, ac = ac->next) {
	    stat.randomUid[i] = ac->uid;
	    stat.randomAccess[i] = ac->axess;
	}
    }
    stat.callback = afs_data_pointer_to_int32(avc->callback);
    stat.cbExpires = avc->cbExpires;
    stat.anyAccess = avc->anyAccess;
    stat.opens = avc->opens;
    stat.execsOrWriters = avc->execsOrWriters;
    stat.flockCount = avc->flockCount;
    stat.mvstat = avc->mvstat;
    stat.states = avc->states;
    memcpy(aout, (char *)&stat, sizeof(struct vcxstat));
    *aoutSize = sizeof(struct vcxstat);
    return 0;
}


DECL_PIOCTL(PGetVnodeXStatus2)
{
    register afs_int32 code;
    struct vcxstat2 stat;
    afs_int32 mode;

    if (!avc)
        return EINVAL;
    code = afs_VerifyVCache(avc, areq);
    if (code)
        return code;
    if (vType(avc) == VDIR)
        mode = PRSFS_LOOKUP;
    else
        mode = PRSFS_READ;
    if (!afs_AccessOK(avc, mode, areq, CHECK_MODE_BITS))
        return EACCES;

    memset(&stat, 0, sizeof(struct vcxstat2));

    stat.cbExpires = avc->cbExpires;
    stat.anyAccess = avc->anyAccess;
    stat.mvstat = avc->mvstat;
    stat.callerAccess = afs_GetAccessBits(avc, ~0, areq);

    memcpy(aout, (char *)&stat, sizeof(struct vcxstat2));
    *aoutSize = sizeof(struct vcxstat2);
    return 0;
}


/*!
 * VIOC_AFS_SYSNAME (38) - Change @sys value
 *
 * \ingroup pioctl
 *
 * \param[in] ain	new value for @sys
 * \param[out] aout	count, entry, list (debug values?)
 *
 * \retval EINVAL	Error if afsd isn't running, the new sysname is too large, the new sysname causes issues (starts with a .0 or ..0), there is no PAG set in the credentials, the user of a PAG can't be found, (!(exporter = au->exporter)) "NOT SURE ON THIS"
 * \retval ENODEV 	Error if there isn't already a system named that ("I THINK")
 * \retval EACCES	Error if the user doesn't have super-user credentials
 *
 * \post Set the value of @sys if these things work: if the input isn't too long or if input doesn't start with .0 or ..0
 *
 * \notes We require root for local sysname changes, but not for remote (since we don't really believe remote uids anyway) outname[] shouldn't really be needed- this is left as an exercise for the reader.
 */
DECL_PIOCTL(PSetSysName)
{
    char *cp, *cp2 = NULL, inname[MAXSYSNAME], outname[MAXSYSNAME];
    afs_int32 setsysname;
    int foundname = 0;
    register struct afs_exporter *exporter;
    register struct unixuser *au;
    register afs_int32 pag, error;
    int t, count, num = 0, allpags = 0;
    char **sysnamelist;

    AFS_STATCNT(PSetSysName);
    if (!afs_globalVFS) {
	/* Afsd is NOT running; disable it */
#if defined(KERNEL_HAVE_UERROR)
	return (setuerror(EINVAL), EINVAL);
#else
	return (EINVAL);
#endif
    }
    memset(inname, 0, MAXSYSNAME);
    memcpy(&setsysname, ain, sizeof(afs_int32));
    ain += sizeof(afs_int32);
    if (setsysname & 0x8000) {
	allpags = 1;
	setsysname &= ~0x8000;
    }
    if (setsysname) {

	/* Check my args */
	if (setsysname < 0 || setsysname > MAXNUMSYSNAMES)
	    return EINVAL;
	cp2 = ain;
	for (cp = ain, count = 0; count < setsysname; count++) {
	    /* won't go past end of ain since maxsysname*num < ain length */
	    t = strlen(cp);
	    if (t >= MAXSYSNAME || t <= 0)
		return EINVAL;
	    /* check for names that can shoot us in the foot */
	    if (*cp == '.' && (cp[1] == 0 || (cp[1] == '.' && cp[2] == 0)))
		return EINVAL;
	    cp += t + 1;
	}
	/* args ok */

	/* inname gets first entry in case we're being a translator */
	t = strlen(ain);
	memcpy(inname, ain, t + 1);	/* include terminating null */
	ain += t + 1;
	num = count;
    }
    if ((*acred)->cr_gid == RMTUSER_REQ ||
	(*acred)->cr_gid == RMTUSER_REQ_PRIV) {	/* Handles all exporters */
	if (allpags && (*acred)->cr_gid != RMTUSER_REQ_PRIV) {
	    return EPERM;
	}
	pag = PagInCred(*acred);
	if (pag == NOPAG) {
	    return EINVAL;	/* Better than panicing */
	}
	if (!(au = afs_FindUser(pag, -1, READ_LOCK))) {
	    return EINVAL;	/* Better than panicing */
	}
	if (!(exporter = au->exporter)) {
	    afs_PutUser(au, READ_LOCK);
	    return EINVAL;	/* Better than panicing */
	}
	error = EXP_SYSNAME(exporter, (setsysname ? cp2 : NULL), &sysnamelist,
			    &num, allpags);
	if (error) {
	    if (error == ENODEV)
		foundname = 0;	/* sysname not set yet! */
	    else {
		afs_PutUser(au, READ_LOCK);
		return error;
	    }
	} else {
	    foundname = num;
	    strcpy(outname, sysnamelist[0]);
	}
	afs_PutUser(au, READ_LOCK);
	if (setsysname)
	    afs_sysnamegen++;
    } else {
	/* Not xlating, so local case */
	if (!afs_sysname)
	    osi_Panic("PSetSysName: !afs_sysname\n");
	if (!setsysname) {	/* user just wants the info */
	    strcpy(outname, afs_sysname);
	    foundname = afs_sysnamecount;
	    sysnamelist = afs_sysnamelist;
	} else {		/* Local guy; only root can change sysname */
	    if (!afs_osi_suser(*acred))
		return EACCES;

	    /* allpags makes no sense for local use */
	    if (allpags)
		return EINVAL;

	    /* clear @sys entries from the dnlc, once afs_lookup can
	     * do lookups of @sys entries and thinks it can trust them */
	    /* privs ok, store the entry, ... */
	    strcpy(afs_sysname, inname);
	    if (setsysname > 1) {	/* ... or list */
		cp = ain;
		for (count = 1; count < setsysname; ++count) {
		    if (!afs_sysnamelist[count])
			osi_Panic
			    ("PSetSysName: no afs_sysnamelist entry to write\n");
		    t = strlen(cp);
		    memcpy(afs_sysnamelist[count], cp, t + 1);	/* include null */
		    cp += t + 1;
		}
	    }
	    afs_sysnamecount = setsysname;
	    afs_sysnamegen++;
	}
    }
    if (!setsysname) {
	cp = aout;		/* not changing so report back the count and ... */
	memcpy(cp, (char *)&foundname, sizeof(afs_int32));
	cp += sizeof(afs_int32);
	if (foundname) {
	    strcpy(cp, outname);	/* ... the entry, ... */
	    cp += strlen(outname) + 1;
	    for (count = 1; count < foundname; ++count) {	/* ... or list. */
		if (!sysnamelist[count])
		    osi_Panic
			("PSetSysName: no afs_sysnamelist entry to read\n");
		t = strlen(sysnamelist[count]);
		if (t >= MAXSYSNAME)
		    osi_Panic("PSetSysName: sysname entry garbled\n");
		strcpy(cp, sysnamelist[count]);
		cp += t + 1;
	    }
	}
	*aoutSize = cp - aout;
    }
    return 0;
}

/* sequential search through the list of touched cells is not a good
 * long-term solution here. For small n, though, it should be just
 * fine.  Should consider special-casing the local cell for large n.
 * Likewise for PSetSPrefs.
 *
 * s - number of ids in array l[] -- NOT index of last id
 * l - array of cell ids which have volumes that need to be sorted
 * vlonly - sort vl servers or file servers?
 */
static void *
ReSortCells_cb(struct cell *cell, void *arg)
{
    afs_int32 *p = (afs_int32 *) arg;
    afs_int32 *l = p + 1;
    int i, s = p[0];

    for (i = 0; i < s; i++) {
	if (l[i] == cell->cellNum) {
	    ObtainWriteLock(&cell->lock, 690);
	    afs_SortServers(cell->cellHosts, MAXCELLHOSTS);
	    ReleaseWriteLock(&cell->lock);
	}
    }

    return NULL;
}

static void
ReSortCells(int s, afs_int32 * l, int vlonly)
{
    int i;
    struct volume *j;
    register int k;

    if (vlonly) {
	afs_int32 *p;
	p = (afs_int32 *) afs_osi_Alloc(sizeof(afs_int32) * (s + 1));
	p[0] = s;
	memcpy(p + 1, l, s * sizeof(afs_int32));
	afs_TraverseCells(&ReSortCells_cb, p);
	afs_osi_Free(p, sizeof(afs_int32) * (s + 1));
	return;
    }

    ObtainReadLock(&afs_xvolume);
    for (i = 0; i < NVOLS; i++) {
	for (j = afs_volumes[i]; j; j = j->next) {
	    for (k = 0; k < s; k++)
		if (j->cell == l[k]) {
		    ObtainWriteLock(&j->lock, 233);
		    afs_SortServers(j->serverHost, MAXHOSTS);
		    ReleaseWriteLock(&j->lock);
		    break;
		}
	}
    }
    ReleaseReadLock(&afs_xvolume);
}


static int debugsetsp = 0;
static int
afs_setsprefs(struct spref *sp, unsigned int num, unsigned int vlonly)
{
    struct srvAddr *sa;
    int i, j, k, matches, touchedSize;
    struct server *srvr = NULL;
    afs_int32 touched[34];
    int isfs;

    touchedSize = 0;
    for (k = 0; k < num; sp++, k++) {
	if (debugsetsp) {
	    printf("sp host=%x, rank=%d\n", sp->host.s_addr, sp->rank);
	}
	matches = 0;
	ObtainReadLock(&afs_xserver);

	i = SHash(sp->host.s_addr);
	for (sa = afs_srvAddrs[i]; sa; sa = sa->next_bkt) {
	    if (sa->sa_ip == sp->host.s_addr) {
		srvr = sa->server;
		isfs = (srvr->cell && (sa->sa_portal == srvr->cell->fsport))
		    || (sa->sa_portal == AFS_FSPORT);
		if ((!vlonly && isfs) || (vlonly && !isfs)) {
		    matches++;
		    break;
		}
	    }
	}

	if (sa && matches) {	/* found one! */
	    if (debugsetsp) {
		printf("sa ip=%x, ip_rank=%d\n", sa->sa_ip, sa->sa_iprank);
	    }
	    sa->sa_iprank = sp->rank + afs_randomMod15();
	    afs_SortOneServer(sa->server);

	    if (srvr->cell) {
		/* if we don't know yet what cell it's in, this is moot */
		for (j = touchedSize - 1;
		     j >= 0 && touched[j] != srvr->cell->cellNum; j--)
		    /* is it in our list of touched cells ?  */ ;
		if (j < 0) {	/* no, it's not */
		    touched[touchedSize++] = srvr->cell->cellNum;
		    if (touchedSize >= 32) {	/* watch for ovrflow */
			ReleaseReadLock(&afs_xserver);
			ReSortCells(touchedSize, touched, vlonly);
			touchedSize = 0;
			ObtainReadLock(&afs_xserver);
		    }
		}
	    }
	}

	ReleaseReadLock(&afs_xserver);
	/* if we didn't find one, start to create one. */
	/* Note that it doesn't have a cell yet...     */
	if (!matches) {
	    afs_uint32 temp = sp->host.s_addr;
	    srvr =
		afs_GetServer(&temp, 1, 0, (vlonly ? AFS_VLPORT : AFS_FSPORT),
			      WRITE_LOCK, (afsUUID *) 0, 0);
	    srvr->addr->sa_iprank = sp->rank + afs_randomMod15();
	    afs_PutServer(srvr, WRITE_LOCK);
	}
    }				/* for all cited preferences */

    ReSortCells(touchedSize, touched, vlonly);
    return 0;
}

/*!
 * VIOC_SETPREFS (46) - Set server ranks
 *
 * \param[in] ain	the sprefs value you want the sprefs to be set to
 * \param[out] aout	not in use
 *
 * \retval EIO		Error if the afs daemon hasn't started yet
 * \retval EACCES	Error if the user doesn't have super-user credentials
 * \retval EINVAL	Error if the struct setsprefs is too large or if it multiplied by the number of servers is too large
 *
 * \post set the sprefs using the afs_setsprefs() function
 */
DECL_PIOCTL(PSetSPrefs)
{
    struct setspref *ssp;
    AFS_STATCNT(PSetSPrefs);

    if (!afs_resourceinit_flag)	/* afs daemons haven't started yet */
	return EIO;		/* Inappropriate ioctl for device */

    if (!afs_osi_suser(*acred))
	return EACCES;

    if (ainSize < sizeof(struct setspref))
	return EINVAL;

    ssp = (struct setspref *)ain;
    if (ainSize < sizeof(struct spref) * ssp->num_servers)
	return EINVAL;

    afs_setsprefs(&(ssp->servers[0]), ssp->num_servers,
		  (ssp->flags & DBservers));
    return 0;
}

/* 
 * VIOC_SETPREFS33 (42) - Set server ranks (deprecated)
 *
 * \param[in] ain	the server preferences to be set
 * \param[out] aout	not in use
 *
 * \retval EIO		Error if the afs daemon hasn't started yet
 * \retval EACCES	Error if the user doesn't have super-user credentials
 *
 * \post set the server preferences, calling a function
 *
 * \notes this may only be performed by the local root user.
 */
DECL_PIOCTL(PSetSPrefs33)
{
    struct spref *sp;
    AFS_STATCNT(PSetSPrefs);
    if (!afs_resourceinit_flag)	/* afs daemons haven't started yet */
	return EIO;		/* Inappropriate ioctl for device */


    if (!afs_osi_suser(*acred))
	return EACCES;

    sp = (struct spref *)ain;
    afs_setsprefs(sp, ainSize / (sizeof(struct spref)), 0 /*!vlonly */ );
    return 0;
}

/* 
 * VIOC_GETSPREFS (43) - Get server ranks
 *
 * \ingroup pioctl
 *
 * \param[in] ain	the server preferences to get
 * \param[out] aout	the server preferences information
 *
 * \retval EIO		Error if the afs daemon hasn't started yet
 * \retval ENOENT	Error if the sprefrequest is too large
 *
 * \post Get the sprefs
 *
 * \notes in the hash table of server structs, all servers with the same IP address; will be on the same overflow chain; This could be sped slightly in some circumstances by having it cache the immediately previous slot in the hash table and some supporting information; Only reports file servers now.
 */
DECL_PIOCTL(PGetSPrefs)
{
    struct sprefrequest *spin;	/* input */
    struct sprefinfo *spout;	/* output */
    struct spref *srvout;	/* one output component */
    int i, j;			/* counters for hash table traversal */
    struct server *srvr;	/* one of CM's server structs */
    struct srvAddr *sa;
    int vlonly;			/* just return vlservers ? */
    int isfs;

    AFS_STATCNT(PGetSPrefs);
    if (!afs_resourceinit_flag)	/* afs daemons haven't started yet */
	return EIO;		/* Inappropriate ioctl for device */


    if (ainSize < sizeof(struct sprefrequest_33)) {
	return ENOENT;
    } else {
	spin = ((struct sprefrequest *)ain);
    }

    if (ainSize > sizeof(struct sprefrequest_33)) {
	vlonly = (spin->flags & DBservers);
    } else
	vlonly = 0;

    /* struct sprefinfo includes 1 server struct...  that size gets added
     * in during the loop that follows.
     */
    *aoutSize = sizeof(struct sprefinfo) - sizeof(struct spref);
    spout = (struct sprefinfo *)aout;
    spout->next_offset = spin->offset;
    spout->num_servers = 0;
    srvout = spout->servers;

    ObtainReadLock(&afs_xserver);
    for (i = 0, j = 0; j < NSERVERS; j++) {	/* sift through hash table */
	for (sa = afs_srvAddrs[j]; sa; sa = sa->next_bkt, i++) {
	    if (spin->offset > (unsigned short)i) {
		continue;	/* catch up to where we left off */
	    }
	    spout->next_offset++;

	    srvr = sa->server;
	    isfs = (srvr->cell && (sa->sa_portal == srvr->cell->fsport))
		|| (sa->sa_portal == AFS_FSPORT);

	    if ((vlonly && isfs) || (!vlonly && !isfs)) {
		/* only report ranks for vl servers */
		continue;
	    }

	    srvout->host.s_addr = sa->sa_ip;
	    srvout->rank = sa->sa_iprank;
	    *aoutSize += sizeof(struct spref);
	    spout->num_servers++;
	    srvout++;

	    if (*aoutSize > (PIGGYSIZE - sizeof(struct spref))) {
		ReleaseReadLock(&afs_xserver);	/* no more room! */
		return 0;
	    }
	}
    }
    ReleaseReadLock(&afs_xserver);

    spout->next_offset = 0;	/* start over from the beginning next time */
    return 0;
}

/* Enable/Disable the specified exporter. Must be root to disable an exporter */
int afs_NFSRootOnly = 1;
/*!
 * VIOC_EXPORTAFS (39) - Export afs to nfs clients
 *
 * \ingroup pioctl
 *
 * \param[in] ain	a struct Vic * EIOctl containing export values needed to change between nfs and afs
 * \param[out] aout	a struct of the exporter states (exporter->exp_states)
 *
 * \retval ENODEV	Error if the exporter doesn't exist
 * \retval EACCES	Error if the user doesn't have super-user credentials
 *
 * \post Changes the state of various values to reflect the change of the export values between nfs and afs.
 *
 * \notes Legacy code obtained from IBM.
 */
DECL_PIOCTL(PExportAfs)
{
    afs_int32 export, newint =
	0, type, changestate, handleValue, convmode, pwsync, smounts;
    afs_int32 rempags = 0, pagcb = 0;
    register struct afs_exporter *exporter;

    AFS_STATCNT(PExportAfs);
    memcpy((char *)&handleValue, ain, sizeof(afs_int32));
    type = handleValue >> 24;
    if (type == 0x71) {
	newint = 1;
	type = 1;		/* nfs */
    }
    exporter = exporter_find(type);
    if (newint) {
	export = handleValue & 3;
	changestate = handleValue & 0xfff;
	smounts = (handleValue >> 2) & 3;
	pwsync = (handleValue >> 4) & 3;
	convmode = (handleValue >> 6) & 3;
	rempags = (handleValue >> 8) & 3;
	pagcb = (handleValue >> 10) & 3;
    } else {
	changestate = (handleValue >> 16) & 0x1;
	convmode = (handleValue >> 16) & 0x2;
	pwsync = (handleValue >> 16) & 0x4;
	smounts = (handleValue >> 16) & 0x8;
	export = handleValue & 0xff;
    }
    if (!exporter) {
	/*  Failed finding desired exporter; */
	return ENODEV;
    }
    if (!changestate) {
	handleValue = exporter->exp_states;
	memcpy(aout, (char *)&handleValue, sizeof(afs_int32));
	*aoutSize = sizeof(afs_int32);
    } else {
	if (!afs_osi_suser(*acred))
	    return EACCES;	/* Only superuser can do this */
	if (newint) {
	    if (export & 2) {
		if (export & 1)
		    exporter->exp_states |= EXP_EXPORTED;
		else
		    exporter->exp_states &= ~EXP_EXPORTED;
	    }
	    if (convmode & 2) {
		if (convmode & 1)
		    exporter->exp_states |= EXP_UNIXMODE;
		else
		    exporter->exp_states &= ~EXP_UNIXMODE;
	    }
	    if (pwsync & 2) {
		if (pwsync & 1)
		    exporter->exp_states |= EXP_PWSYNC;
		else
		    exporter->exp_states &= ~EXP_PWSYNC;
	    }
	    if (smounts & 2) {
		if (smounts & 1) {
		    afs_NFSRootOnly = 0;
		    exporter->exp_states |= EXP_SUBMOUNTS;
		} else {
		    afs_NFSRootOnly = 1;
		    exporter->exp_states &= ~EXP_SUBMOUNTS;
		}
	    }
	    if (rempags & 2) {
		if (rempags & 1)
		    exporter->exp_states |= EXP_CLIPAGS;
		else
		    exporter->exp_states &= ~EXP_CLIPAGS;
	    }
	    if (pagcb & 2) {
		if (pagcb & 1)
		    exporter->exp_states |= EXP_CALLBACK;
		else
		    exporter->exp_states &= ~EXP_CALLBACK;
	    }
	    handleValue = exporter->exp_states;
	    memcpy(aout, (char *)&handleValue, sizeof(afs_int32));
	    *aoutSize = sizeof(afs_int32);
	} else {
	    if (export)
		exporter->exp_states |= EXP_EXPORTED;
	    else
		exporter->exp_states &= ~EXP_EXPORTED;
	    if (convmode)
		exporter->exp_states |= EXP_UNIXMODE;
	    else
		exporter->exp_states &= ~EXP_UNIXMODE;
	    if (pwsync)
		exporter->exp_states |= EXP_PWSYNC;
	    else
		exporter->exp_states &= ~EXP_PWSYNC;
	    if (smounts) {
		afs_NFSRootOnly = 0;
		exporter->exp_states |= EXP_SUBMOUNTS;
	    } else {
		afs_NFSRootOnly = 1;
		exporter->exp_states &= ~EXP_SUBMOUNTS;
	    }
	}
    }

    return 0;
}

/*!
 * VIOC_GAG (44) - Silence Cache Manager
 *
 * \ingroup pioctl
 *
 * \param[in] ain	the flags to either gag or de-gag the cache manager
 * \param[out] aout	not in use
 *
 * \retval EACCES	Error if the user doesn't have super-user credentials
 *
 * \post set the gag flags, then show these flags
 */
DECL_PIOCTL(PGag)
{
    struct gaginfo *gagflags;

    if (!afs_osi_suser(*acred))
	return EACCES;

    gagflags = (struct gaginfo *)ain;
    afs_showflags = gagflags->showflags;

    return 0;
}

/*!
 * VIOC_TWIDDLE (45) - Adjust RX knobs
 *
 * \ingroup pioctl
 *
 * \param[in] ain	the previous settings of the 'knobs'
 * \param[out] aout	not in use
 *
 * \retval EACCES	Error if the user doesn't have super-user credentials
 *
 * \post build out the struct rxp, from a struct rx
 */
DECL_PIOCTL(PTwiddleRx)
{
    struct rxparams *rxp;

    if (!afs_osi_suser(*acred))
	return EACCES;

    rxp = (struct rxparams *)ain;

    if (rxp->rx_initReceiveWindow)
	rx_initReceiveWindow = rxp->rx_initReceiveWindow;
    if (rxp->rx_maxReceiveWindow)
	rx_maxReceiveWindow = rxp->rx_maxReceiveWindow;
    if (rxp->rx_initSendWindow)
	rx_initSendWindow = rxp->rx_initSendWindow;
    if (rxp->rx_maxSendWindow)
	rx_maxSendWindow = rxp->rx_maxSendWindow;
    if (rxp->rxi_nSendFrags)
	rxi_nSendFrags = rxp->rxi_nSendFrags;
    if (rxp->rxi_nRecvFrags)
	rxi_nRecvFrags = rxp->rxi_nRecvFrags;
    if (rxp->rxi_OrphanFragSize)
	rxi_OrphanFragSize = rxp->rxi_OrphanFragSize;
    if (rxp->rx_maxReceiveSize) {
	rx_maxReceiveSize = rxp->rx_maxReceiveSize;
	rx_maxReceiveSizeUser = rxp->rx_maxReceiveSize;
    }
    if (rxp->rx_MyMaxSendSize)
	rx_MyMaxSendSize = rxp->rx_MyMaxSendSize;

    return 0;
}

/*!
 * VIOC_GETINITPARAMS (49) - Get initial cache manager parameters
 *
 * \ingroup pioctl
 *
 * \param[in] ain	not in use
 * \param[out] aout	initial cache manager params
 *
 * \retval E2BIG	Error if the initial parameters are bigger than some PIGGYSIZE
 *
 * \post return the initial cache manager parameters
 */
DECL_PIOCTL(PGetInitParams)
{
    if (sizeof(struct cm_initparams) > PIGGYSIZE)
	return E2BIG;

    memcpy(aout, (char *)&cm_initParams, sizeof(struct cm_initparams));
    *aoutSize = sizeof(struct cm_initparams);
    return 0;
}

#ifdef AFS_SGI65_ENV
/* They took crget() from us, so fake it. */
static cred_t *
crget(void)
{
    cred_t *cr;
    cr = crdup(get_current_cred());
    memset((char *)cr, 0, sizeof(cred_t));
#if CELL || CELL_PREPARE
    cr->cr_id = -1;
#endif
    return cr;
}
#endif

/*!
 * VIOC_GETRXKCRYPT (55) - Get rxkad encryption flag
 *
 * \ingroup pioctl
 *
 * \param[in] ain	not in use
 * \param[out] aout	value of cryptall
 *
 * \post get the value of cryptall (presumably whether or not things should be encrypted)
 */
DECL_PIOCTL(PGetRxkcrypt)
{
    memcpy(aout, (char *)&cryptall, sizeof(afs_int32));
    *aoutSize = sizeof(afs_int32);
    return 0;
}

/*!
 * VIOC_SETRXKCRYPT (56) - Set rxkad encryption flag
 *
 * \ingroup pioctl
 *
 * \param[in] ain	the argument whether or not things should be encrypted
 * \param[out] aout	not in use
 *
 * \retval EPERM	Error if the user doesn't have super-user credentials
 * \retval EINVAL	Error if the input is too big, or if the input is outside the bounds of what it can be set to
 *
 * \post set whether or not things should be encrypted
 *
 * \notes may need to be modified at a later date to take into account other values for cryptall (beyond true or false)
 */
DECL_PIOCTL(PSetRxkcrypt)
{
    afs_int32 tmpval;

    if (!afs_osi_suser(*acred))
	return EPERM;
    if (ainSize != sizeof(afs_int32) || ain == NULL)
	return EINVAL;
    memcpy((char *)&tmpval, ain, sizeof(afs_int32));
    /* if new mappings added later this will need to be changed */
    if (tmpval != 0 && tmpval != 1)
	return EINVAL;
    cryptall = tmpval;
    return 0;
}

#ifdef AFS_NEED_CLIENTCONTEXT
/*
 * Create new credentials to correspond to a remote user with given
 * <hostaddr, uid, g0, g1>.  This allows a server running as root to
 * provide pioctl (and other) services to foreign clients (i.e. nfs
 * clients) by using this call to `become' the client.
 */
#define	PSETPAG		110
#define	PIOCTL_HEADER	6
static int
HandleClientContext(struct afs_ioctl *ablob, int *com,
		    struct AFS_UCRED **acred, struct AFS_UCRED *credp)
{
    char *ain, *inData;
    afs_uint32 hostaddr;
    afs_int32 uid, g0, g1, i, code, pag, exporter_type, isroot = 0;
    struct afs_exporter *exporter, *outexporter;
    struct AFS_UCRED *newcred;
    struct unixuser *au;
    afs_uint32 comp = *com & 0xff00;
    afs_uint32 h, l;

#if defined(AFS_SGIMP_ENV)
    osi_Assert(ISAFS_GLOCK());
#endif
    AFS_STATCNT(HandleClientContext);
    if (ablob->in_size < PIOCTL_HEADER * sizeof(afs_int32)) {
	/* Must at least include the PIOCTL_HEADER header words required by the protocol */
	return EINVAL;		/* Too small to be good  */
    }
    ain = inData = osi_AllocLargeSpace(AFS_LRALLOCSIZ);
    AFS_COPYIN(ablob->in, ain, PIOCTL_HEADER * sizeof(afs_int32), code);
    if (code) {
	osi_FreeLargeSpace(inData);
	return code;
    }

    /* Extract information for remote user */
    hostaddr = *((afs_uint32 *) ain);
    ain += sizeof(hostaddr);
    uid = *((afs_uint32 *) ain);
    ain += sizeof(uid);
    g0 = *((afs_uint32 *) ain);
    ain += sizeof(g0);
    g1 = *((afs_uint32 *) ain);
    ain += sizeof(g1);
    *com = *((afs_uint32 *) ain);
    ain += sizeof(afs_int32);
    exporter_type = *((afs_uint32 *) ain);	/* In case we support more than NFS */

    /*
     * Of course, one must be root for most of these functions, but
     * we'll allow (for knfs) you to set things if the pag is 0 and
     * you're setting tokens or unlogging.
     */
    i = (*com) & 0xff;
    if (!afs_osi_suser(credp)) {
#if defined(AFS_SGI_ENV) && !defined(AFS_SGI64_ENV)
	/* Since SGI's suser() returns explicit failure after the call.. */
	u.u_error = 0;
#endif
	/* check for acceptable opcodes for normal folks, which are, so far,
	 * get/set tokens, sysname, and unlog.
	 */
	if (i != 9 && i != 3 && i != 38 && i != 8) {
	    osi_FreeLargeSpace(inData);
	    return EACCES;
	}
    }

    ablob->in_size -= PIOCTL_HEADER * sizeof(afs_int32);
    ablob->in += PIOCTL_HEADER * sizeof(afs_int32);
    osi_FreeLargeSpace(inData);
    if (uid == 0) {
	/*
	 * We map uid 0 to nobody to match the mapping that the nfs
	 * server does and to ensure that the suser() calls in the afs
	 * code fails for remote client roots.
	 */
	uid = afs_nobody;	/* NFS_NOBODY == -2 */
	isroot = 1;
    }
    newcred = crget();
#ifdef	AFS_AIX41_ENV
    setuerror(0);
#endif
    newcred->cr_gid = isroot ? RMTUSER_REQ_PRIV : RMTUSER_REQ;
#ifdef AFS_AIX51_ENV
    newcred->cr_groupset.gs_union.un_groups[0] = g0;
    newcred->cr_groupset.gs_union.un_groups[1] = g1;
#elif defined(AFS_LINUX26_ENV)
#ifdef AFS_LINUX26_ONEGROUP_ENV
    newcred->cr_group_info = groups_alloc(1); /* not that anything sets this */
    l = (((g0-0x3f00) & 0x3fff) << 14) | ((g1-0x3f00) & 0x3fff);
    h = ((g0-0x3f00) >> 14);
    h = ((g1-0x3f00) >> 14) + h + h + h;
    GROUP_AT(newcred->cr_group_info, 0) = ((h << 28) | l);
#else
    newcred->cr_group_info = groups_alloc(2);
    GROUP_AT(newcred->cr_group_info, 0) = g0;
    GROUP_AT(newcred->cr_group_info, 1) = g1;
#endif
#else
    newcred->cr_groups[0] = g0;
    newcred->cr_groups[1] = g1;
#endif
#ifdef AFS_AIX_ENV
    newcred->cr_ngrps = 2;
#elif !defined(AFS_LINUX26_ENV)
#if defined(AFS_SGI_ENV) || defined(AFS_SUN5_ENV) || defined(AFS_LINUX22_ENV)
    newcred->cr_ngroups = 2;
#else
    for (i = 2; i < NGROUPS; i++)
	newcred->cr_groups[i] = NOGROUP;
#endif
#endif
#if	!defined(AFS_OSF_ENV) 
    afs_nfsclient_init();	/* before looking for exporter, ensure one exists */
#endif
    if (!(exporter = exporter_find(exporter_type))) {
	/* Exporter wasn't initialized or an invalid exporter type */
	crfree(newcred);
	return EINVAL;
    }
    if (exporter->exp_states & EXP_PWSYNC) {
	if (uid != credp->cr_uid) {
	    crfree(newcred);
	    return ENOEXEC;	/* XXX Find a better errno XXX */
	}
    }
    newcred->cr_uid = uid;	/* Only temporary  */
    code = EXP_REQHANDLER(exporter, &newcred, hostaddr, &pag, &outexporter);
    /* The client's pag is the only unique identifier for it */
    newcred->cr_uid = pag;
    *acred = newcred;
    if (!code && *com == PSETPAG) {
	/* Special case for 'setpag' */
	afs_uint32 pagvalue = genpag();

	au = afs_GetUser(pagvalue, -1, WRITE_LOCK);	/* a new unixuser struct */
	/*
	 * Note that we leave the 'outexporter' struct held so it won't
	 * dissappear on us
	 */
	au->exporter = outexporter;
	if (ablob->out_size >= 4) {
	    AFS_COPYOUT((char *)&pagvalue, ablob->out, sizeof(afs_int32),
			code);
	}
	afs_PutUser(au, WRITE_LOCK);
	if (code)
	    return code;
	return PSETPAG;		/*  Special return for setpag  */
    } else if (!code) {
	EXP_RELE(outexporter);
    }
    if (!code) 
	*com = (*com) | comp;
    return code;
}
#endif /* AFS_NEED_CLIENTCONTEXT */


/*! 
 * VIOC_GETCPREFS (50) - Get client interface
 *
 * \ingroup pioctl
 *
 * \param[in] ain	sprefrequest input
 * \param[out] aout	spref information
 *
 * \retval EIO		Error if the afs daemon hasn't started yet
 * \retval EINVAL	Error if some of the standard args aren't set
 *
 * \post get all interface addresses and other information of the client interface
 */
DECL_PIOCTL(PGetCPrefs)
{
    struct sprefrequest *spin;	/* input */
    struct sprefinfo *spout;	/* output */
    struct spref *srvout;	/* one output component */
    int maxNumber;
    int i, j;

    AFS_STATCNT(PGetCPrefs);
    if (!afs_resourceinit_flag)	/* afs daemons haven't started yet */
	return EIO;		/* Inappropriate ioctl for device */

    if (ainSize < sizeof(struct sprefrequest))
	return EINVAL;

    spin = (struct sprefrequest *)ain;
    spout = (struct sprefinfo *)aout;

    maxNumber = spin->num_servers;	/* max addrs this time */
    srvout = spout->servers;

    ObtainReadLock(&afs_xinterface);

    /* copy out the client interface information from the
     ** kernel data structure "interface" to the output buffer
     */
    for (i = spin->offset, j = 0; (i < afs_cb_interface.numberOfInterfaces)
	 && (j < maxNumber); i++, j++, srvout++)
	srvout->host.s_addr = afs_cb_interface.addr_in[i];

    spout->num_servers = j;
    *aoutSize = sizeof(struct sprefinfo) + (j - 1) * sizeof(struct spref);

    if (i >= afs_cb_interface.numberOfInterfaces)
	spout->next_offset = 0;	/* start from beginning again */
    else
	spout->next_offset = spin->offset + j;

    ReleaseReadLock(&afs_xinterface);
    return 0;
}

/*!
 * VIOC_SETCPREFS (51) - Set client interface
 *
 * \ingroup pioctl
 *
 * \param[in] ain	the interfaces you want set
 * \param[out] aout	not in use
 *
 * \retval EIO		Error if the afs daemon hasn't started yet
 * \retval EINVAL	Error if the input is too large for the struct
 * \retval ENOMEM	Error if there are too many servers
 *
 * \post set the callbak interfaces addresses to those of the hosts
 */
DECL_PIOCTL(PSetCPrefs)
{
    struct setspref *sin;
    int i;

    AFS_STATCNT(PSetCPrefs);
    if (!afs_resourceinit_flag)	/* afs daemons haven't started yet */
	return EIO;		/* Inappropriate ioctl for device */

    sin = (struct setspref *)ain;

    if (ainSize < sizeof(struct setspref))
	return EINVAL;
#if 0				/* num_servers is unsigned */
    if (sin->num_servers < 0)
	return EINVAL;
#endif
    if (sin->num_servers > AFS_MAX_INTERFACE_ADDR)
	return ENOMEM;

    ObtainWriteLock(&afs_xinterface, 412);
    afs_cb_interface.numberOfInterfaces = sin->num_servers;
    for (i = 0; (unsigned short)i < sin->num_servers; i++)
	afs_cb_interface.addr_in[i] = sin->servers[i].host.s_addr;

    ReleaseWriteLock(&afs_xinterface);
    return 0;
}

/*!
 * VIOC_AFS_FLUSHMOUNT (52) - Flush mount symlink data
 *
 * \ingroup pioctl
 *
 * \param[in] ain	the last part of a path to a mount point, which tells us what to flush
 * \param[out] aout	not in use
 *
 * \retval EINVAL	Error if some of the initial arguments aren't set
 * \retval ENOTDIR	Error if the initial argument for the mount point isn't a directory
 * \retval ENOENT	Error if the dcache entry isn't set
 *
 * \post remove all of the mount data from the dcache regarding a certain mount point
 */
DECL_PIOCTL(PFlushMount)
{
    register afs_int32 code;
    register struct vcache *tvc;
    register struct dcache *tdc;
    struct VenusFid tfid;
    char *bufp;
    struct sysname_info sysState;
    afs_size_t offset, len;

    AFS_STATCNT(PFlushMount);
    if (!avc)
	return EINVAL;
    code = afs_VerifyVCache(avc, areq);
    if (code)
	return code;
    if (vType(avc) != VDIR) {
	return ENOTDIR;
    }
    tdc = afs_GetDCache(avc, (afs_size_t) 0, areq, &offset, &len, 1);
    if (!tdc)
	return ENOENT;
    Check_AtSys(avc, ain, &sysState, areq);
    ObtainReadLock(&tdc->lock);
    do {
	code = afs_dir_Lookup(tdc, sysState.name, &tfid.Fid);
    } while (code == ENOENT && Next_AtSys(avc, areq, &sysState));
    ReleaseReadLock(&tdc->lock);
    afs_PutDCache(tdc);		/* we're done with the data */
    bufp = sysState.name;
    if (code) {
	goto out;
    }
    tfid.Cell = avc->fid.Cell;
    tfid.Fid.Volume = avc->fid.Fid.Volume;
    if (!tfid.Fid.Unique && (avc->states & CForeign)) {
	tvc = afs_LookupVCache(&tfid, areq, NULL, avc, bufp);
    } else {
	tvc = afs_GetVCache(&tfid, areq, NULL, NULL);
    }
    if (!tvc) {
	code = ENOENT;
	goto out;
    }
    if (tvc->mvstat != 1) {
	afs_PutVCache(tvc);
	code = EINVAL;
	goto out;
    }
#ifdef AFS_BOZONLOCK_ENV
    afs_BozonLock(&tvc->pvnLock, tvc);	/* Since afs_TryToSmush will do a pvn_vptrunc */
#endif
    ObtainWriteLock(&tvc->lock, 649);
    ObtainWriteLock(&afs_xcbhash, 650);
    afs_DequeueCallback(tvc);
    tvc->states &= ~(CStatd | CDirty);	/* next reference will re-stat cache entry */
    ReleaseWriteLock(&afs_xcbhash);
    /* now find the disk cache entries */
    afs_TryToSmush(tvc, *acred, 1);
    osi_dnlc_purgedp(tvc);
    if (tvc->linkData && !(tvc->states & CCore)) {
	afs_osi_Free(tvc->linkData, strlen(tvc->linkData) + 1);
	tvc->linkData = NULL;
    }
    ReleaseWriteLock(&tvc->lock);
#ifdef AFS_BOZONLOCK_ENV
    afs_BozonUnlock(&tvc->pvnLock, tvc);
#endif
    afs_PutVCache(tvc);
  out:
    if (sysState.allocked)
	osi_FreeLargeSpace(bufp);
    return code;
}

/*!
 * VIOC_RXSTAT_PROC (53) - Control process RX statistics
 *
 * \ingroup pioctl
 *
 * \param[in] ain	the flags that control which stats to use
 * \param[out] aout	not in use
 *
 * \retval EACCES	Error if the user doesn't have super-user credentials
 * \retval EINVAL	Error if the flag input is too long
 *
 * \post either enable process RPCStats, disable process RPCStats, or clear the process RPCStats
 */
DECL_PIOCTL(PRxStatProc)
{
    int code = 0;
    afs_int32 flags;

    if (!afs_osi_suser(*acred)) {
	code = EACCES;
	goto out;
    }
    if (ainSize != sizeof(afs_int32)) {
	code = EINVAL;
	goto out;
    }
    memcpy((char *)&flags, ain, sizeof(afs_int32));
    if (!(flags & AFSCALL_RXSTATS_MASK) || (flags & ~AFSCALL_RXSTATS_MASK)) {
	code = EINVAL;
	goto out;
    }
    if (flags & AFSCALL_RXSTATS_ENABLE) {
	rx_enableProcessRPCStats();
    }
    if (flags & AFSCALL_RXSTATS_DISABLE) {
	rx_disableProcessRPCStats();
    }
    if (flags & AFSCALL_RXSTATS_CLEAR) {
	rx_clearProcessRPCStats(AFS_RX_STATS_CLEAR_ALL);
    }
  out:
    *aoutSize = 0;
    return code;
}


/*!
 * VIOC_RXSTAT_PEER (54) - Control peer RX statistics
 *
 * \ingroup pioctl
 *
 * \param[in] ain 	the flags that control which statistics to use
 * \param[out] aout	not in use
 *
 * \retval EACCES	Error if the user doesn't have super-user credentials
 * \retval EINVAL	Error if the flag input is too long
 *
 * \post either enable peer RPCStatws, disable peer RPCStats, or clear the peer RPCStats
 */
DECL_PIOCTL(PRxStatPeer)
{
    int code = 0;
    afs_int32 flags;

    if (!afs_osi_suser(*acred)) {
	code = EACCES;
	goto out;
    }
    if (ainSize != sizeof(afs_int32)) {
	code = EINVAL;
	goto out;
    }
    memcpy((char *)&flags, ain, sizeof(afs_int32));
    if (!(flags & AFSCALL_RXSTATS_MASK) || (flags & ~AFSCALL_RXSTATS_MASK)) {
	code = EINVAL;
	goto out;
    }
    if (flags & AFSCALL_RXSTATS_ENABLE) {
	rx_enablePeerRPCStats();
    }
    if (flags & AFSCALL_RXSTATS_DISABLE) {
	rx_disablePeerRPCStats();
    }
    if (flags & AFSCALL_RXSTATS_CLEAR) {
	rx_clearPeerRPCStats(AFS_RX_STATS_CLEAR_ALL);
    }
  out:
    *aoutSize = 0;
    return code;
}

DECL_PIOCTL(PPrefetchFromTape)
{
    register afs_int32 code, code1;
    afs_int32 bytes;
    struct conn *tc;
    struct rx_call *tcall;
    struct AFSVolSync tsync;
    struct AFSFetchStatus OutStatus;
    struct AFSCallBack CallBack;
    struct VenusFid tfid;
    struct AFSFid *Fid;
    struct vcache *tvc;

    AFS_STATCNT(PSetAcl);
    if (!avc)
	return EINVAL;

    if (ain && (ainSize == 3 * sizeof(afs_int32)))
	Fid = (struct AFSFid *)ain;
    else
	Fid = &avc->fid.Fid;
    tfid.Cell = avc->fid.Cell;
    tfid.Fid.Volume = Fid->Volume;
    tfid.Fid.Vnode = Fid->Vnode;
    tfid.Fid.Unique = Fid->Unique;

    tvc = afs_GetVCache(&tfid, areq, NULL, NULL);
    if (!tvc) {
	afs_Trace3(afs_iclSetp, CM_TRACE_PREFETCHCMD, ICL_TYPE_POINTER, tvc,
		   ICL_TYPE_FID, &tfid, ICL_TYPE_FID, &avc->fid);
	return ENOENT;
    }
    afs_Trace3(afs_iclSetp, CM_TRACE_PREFETCHCMD, ICL_TYPE_POINTER, tvc,
	       ICL_TYPE_FID, &tfid, ICL_TYPE_FID, &tvc->fid);

    do {
	tc = afs_Conn(&tvc->fid, areq, SHARED_LOCK);
	if (tc) {

	    RX_AFS_GUNLOCK();
	    tcall = rx_NewCall(tc->id);
	    code =
		StartRXAFS_FetchData(tcall, (struct AFSFid *)&tvc->fid.Fid, 0,
				     0);
	    if (!code) {
		bytes = rx_Read(tcall, (char *)aout, sizeof(afs_int32));
		code =
		    EndRXAFS_FetchData(tcall, &OutStatus, &CallBack, &tsync);
	    }
	    code1 = rx_EndCall(tcall, code);
	    RX_AFS_GLOCK();
	} else
	    code = -1;
    } while (afs_Analyze
	     (tc, code, &tvc->fid, areq, AFS_STATS_FS_RPCIDX_RESIDENCYRPCS,
	      SHARED_LOCK, NULL));
    /* This call is done only to have the callback things handled correctly */
    afs_FetchStatus(tvc, &tfid, areq, &OutStatus);
    afs_PutVCache(tvc);

    if (!code) {
	*aoutSize = sizeof(afs_int32);
    }
    return code;
}

DECL_PIOCTL(PResidencyCmd)
{
    register afs_int32 code;
    struct conn *tc;
    struct vcache *tvc;
    struct ResidencyCmdInputs *Inputs;
    struct ResidencyCmdOutputs *Outputs;
    struct VenusFid tfid;
    struct AFSFid *Fid;

    Inputs = (struct ResidencyCmdInputs *)ain;
    Outputs = (struct ResidencyCmdOutputs *)aout;
    if (!avc)
	return EINVAL;
    if (!ain || ainSize != sizeof(struct ResidencyCmdInputs))
	return EINVAL;

    Fid = &Inputs->fid;
    if (!Fid->Volume)
	Fid = &avc->fid.Fid;

    tfid.Cell = avc->fid.Cell;
    tfid.Fid.Volume = Fid->Volume;
    tfid.Fid.Vnode = Fid->Vnode;
    tfid.Fid.Unique = Fid->Unique;

    tvc = afs_GetVCache(&tfid, areq, NULL, NULL);
    afs_Trace3(afs_iclSetp, CM_TRACE_RESIDCMD, ICL_TYPE_POINTER, tvc,
	       ICL_TYPE_INT32, Inputs->command, ICL_TYPE_FID, &tfid);
    if (!tvc)
	return ENOENT;

    if (Inputs->command) {
	do {
	    tc = afs_Conn(&tvc->fid, areq, SHARED_LOCK);
	    if (tc) {
		RX_AFS_GUNLOCK();
		code =
		    RXAFS_ResidencyCmd(tc->id, Fid, Inputs,
				       (struct ResidencyCmdOutputs *)aout);
		RX_AFS_GLOCK();
	    } else
		code = -1;
	} while (afs_Analyze
		 (tc, code, &tvc->fid, areq,
		  AFS_STATS_FS_RPCIDX_RESIDENCYRPCS, SHARED_LOCK, NULL));
	/* This call is done to have the callback things handled correctly */
	afs_FetchStatus(tvc, &tfid, areq, &Outputs->status);
    } else {			/* just a status request, return also link data */
	code = 0;
	Outputs->code = afs_FetchStatus(tvc, &tfid, areq, &Outputs->status);
	Outputs->chars[0] = 0;
	if (vType(tvc) == VLNK) {
	    ObtainWriteLock(&tvc->lock, 555);
	    if (afs_HandleLink(tvc, areq) == 0)
		strncpy((char *)&Outputs->chars, tvc->linkData, MAXCMDCHARS);
	    ReleaseWriteLock(&tvc->lock);
	}
    }

    afs_PutVCache(tvc);

    if (!code) {
	*aoutSize = sizeof(struct ResidencyCmdOutputs);
    }
    return code;
}

DECL_PIOCTL(PNewUuid)
{
    /*AFS_STATCNT(PNewUuid); */
    if (!afs_resourceinit_flag)	/* afs deamons havn't started yet */
	return EIO;		/* Inappropriate ioctl for device */

    if (!afs_osi_suser(acred))
	return EACCES;

    ObtainWriteLock(&afs_xinterface, 555);
    afs_uuid_create(&afs_cb_interface.uuid);
    ReleaseWriteLock(&afs_xinterface);
    ForceAllNewConnections();
    return 0;
}

#if defined(AFS_CACHE_BYPASS)

DECL_PIOCTL(PSetCachingThreshold)
{
    afs_int32 getting;
    afs_int32 setting;

    setting = getting = 1;

    if (ain == NULL || ainSize < sizeof(afs_int32))
	setting = 0;

    if (aout == NULL)
	getting = 0;

    if (setting == 0 && getting == 0)
	return EINVAL;
	
    /* 
     * If setting, set first, and return the value now in effect
     */
    if (setting) {
	afs_int32 threshold;

	if (!afs_osi_suser(*acred))
	    return EPERM;
	memcpy((char *)&threshold, ain, sizeof(afs_int32));
	cache_bypass_threshold = threshold;
        afs_warn("Cache Bypass Threshold set to: %d\n", threshold);		
	/* TODO:  move to separate pioctl, or enhance pioctl */
	cache_bypass_strategy = LARGE_FILES_BYPASS_CACHE;
    }
	
    if (getting) {
	/* Return the current size threshold */
	afs_int32 oldThreshold = cache_bypass_threshold;
	memcpy(aout, (char *)&oldThreshold, sizeof(afs_int32));
	*aoutSize = sizeof(afs_int32);
    }

    return(0);
}

#endif /* defined(AFS_CACHE_BYPASS) */

DECL_PIOCTL(PCallBackAddr)
{
#ifndef UKERNEL
    afs_uint32 addr, code;
    int srvAddrCount;
    struct server *ts;
    struct srvAddr *sa;
    struct conn *tc;
    afs_int32 i, j;
    struct unixuser *tu;
    struct srvAddr **addrs;

    /*AFS_STATCNT(PCallBackAddr); */
    if (!afs_resourceinit_flag)	/* afs deamons havn't started yet */
	return EIO;		/* Inappropriate ioctl for device */

    if (!afs_osi_suser(acred))
	return EACCES;

    if (ainSize < sizeof(afs_int32))
	return EINVAL;

    memcpy(&addr, ain, sizeof(afs_int32));

    ObtainReadLock(&afs_xinterface);
    for (i = 0; (unsigned short)i < afs_cb_interface.numberOfInterfaces; i++) {
	if (afs_cb_interface.addr_in[i] == addr)
	    break;
    }

    ReleaseWriteLock(&afs_xinterface);

    if (afs_cb_interface.addr_in[i] != addr)
	return EINVAL;

    ObtainReadLock(&afs_xserver);	/* Necessary? */
    ObtainReadLock(&afs_xsrvAddr);

    srvAddrCount = 0;
    for (i = 0; i < NSERVERS; i++) {
	for (sa = afs_srvAddrs[i]; sa; sa = sa->next_bkt) {
	    srvAddrCount++;
	}
    }

    addrs = afs_osi_Alloc(srvAddrCount * sizeof(*addrs));
    j = 0;
    for (i = 0; i < NSERVERS; i++) {
	for (sa = afs_srvAddrs[i]; sa; sa = sa->next_bkt) {
	    if (j >= srvAddrCount)
		break;
	    addrs[j++] = sa;
	}
    }

    ReleaseReadLock(&afs_xsrvAddr);
    ReleaseReadLock(&afs_xserver);

    for (i = 0; i < j; i++) {
	sa = addrs[i];
	ts = sa->server;
	if (!ts)
	    continue;

	/* vlserver has no callback conn */
	if (sa->sa_portal == AFS_VLPORT) {
	    continue;
	}

	if (!ts->cell)		/* not really an active server, anyway, it must */
	    continue;		/* have just been added by setsprefs */

	/* get a connection, even if host is down; bumps conn ref count */
	tu = afs_GetUser(areq->uid, ts->cell->cellNum, SHARED_LOCK);
	tc = afs_ConnBySA(sa, ts->cell->fsport, ts->cell->cellNum, tu,
			  1 /*force */ , 1 /*create */ , SHARED_LOCK);
	afs_PutUser(tu, SHARED_LOCK);
	if (!tc)
	    continue;

	if ((sa->sa_flags & SRVADDR_ISDOWN) || afs_HaveCallBacksFrom(ts)) {
	    if (sa->sa_flags & SRVADDR_ISDOWN) {
		rx_SetConnDeadTime(tc->id, 3);
	    }
#ifdef RX_ENABLE_LOCKS
	    AFS_GUNLOCK();
#endif /* RX_ENABLE_LOCKS */
	    code = RXAFS_CallBackRxConnAddr(tc->id, &addr);
#ifdef RX_ENABLE_LOCKS
	    AFS_GLOCK();
#endif /* RX_ENABLE_LOCKS */
	}
	afs_PutConn(tc, SHARED_LOCK);	/* done with it now */
    }				/* Outer loop over addrs */
#endif /* UKERNEL */
    return 0;
}

DECL_PIOCTL(PDiscon)
{
#ifdef AFS_DISCON_ENV
    static afs_int32 mode = 1; /* Start up in 'off' */
    afs_int32 force = 0;
    int code = 0;

    if (ainSize) {

	if (!afs_osi_suser(*acred))
	    return EPERM;

	if (ain[0])
	    mode = ain[0] - 1;
	if (ain[1])
	    afs_ConflictPolicy = ain[1] - 1;
	if (ain[2])
	    force = 1;

	/*
	 * All of these numbers are hard coded in fs.c. If they
	 * change here, they should change there and vice versa
	 */
	switch (mode) {
	case 0: /* Disconnect ("offline" mode), breaking all callbacks */
	    if (!AFS_IS_DISCONNECTED) {
		ObtainWriteLock(&afs_discon_lock, 999);
		afs_DisconGiveUpCallbacks();
		afs_RemoveAllConns();
		afs_is_disconnected = 1;
		afs_is_discon_rw = 1;
		ReleaseWriteLock(&afs_discon_lock);
	    }
	    break;
	case 1: /* Fully connected, ("online" mode). */
	    ObtainWriteLock(&afs_discon_lock, 998);

	    afs_in_sync = 1;
	    code = afs_ResyncDisconFiles(areq, *acred);
	    afs_in_sync = 0;

	    if (code && !force) {
	    	printf("Files not synchronized properly, still in discon state. \
						Please retry or use \"force\".\n");
	    } else {
		afs_is_disconnected = 0;
		afs_is_discon_rw = 0;
		printf("\nSync succeeded. You are back online.\n");
	    }

	    ReleaseWriteLock(&afs_discon_lock);
	    break;
	default:
	    return EINVAL;
	}
    } else {
	return EINVAL;
    }

    memcpy(aout, &mode, sizeof(afs_int32));
    *aoutSize = sizeof(afs_int32);
    return 0;
#else
    return EINVAL;
#endif
}

DECL_PIOCTL(PNFSNukeCreds)
{
    afs_uint32 addr, code;
    register afs_int32 i;
    register struct unixuser *tu;

    AFS_STATCNT(PUnlog);
    if (!afs_resourceinit_flag)	/* afs daemons haven't started yet */
	return EIO;		/* Inappropriate ioctl for device */

    if (ainSize < sizeof(afs_int32))
	return EINVAL;
    memcpy(&addr, ain, sizeof(afs_int32));

    if ((*acred)->cr_gid == RMTUSER_REQ_PRIV && !addr) {
	tu = afs_GetUser(areq->uid, -1, SHARED_LOCK);
	if (!tu->exporter || !(addr = EXP_GETHOST(tu->exporter))) {
	    afs_PutUser(tu, SHARED_LOCK);
	    return EACCES;
	}
	afs_PutUser(tu, SHARED_LOCK);
    } else if (!afs_osi_suser(acred)) {
	return EACCES;
    }

    ObtainWriteLock(&afs_xuser, 227);
    for (i = 0; i < NUSERS; i++) {
	for (tu = afs_users[i]; tu; tu = tu->next) {
	    if (tu->exporter && EXP_CHECKHOST(tu->exporter, addr)) {
		tu->vid = UNDEFVID;
		tu->states &= ~UHasTokens;
		/* security is not having to say you're sorry */
		memset((char *)&tu->ct, 0, sizeof(struct ClearToken));
		tu->refCount++;
		ReleaseWriteLock(&afs_xuser);
		afs_ResetUserConns(tu);
		tu->refCount--;
		ObtainWriteLock(&afs_xuser, 228);
#ifdef UKERNEL
		/* set the expire times to 0, causes
		 * afs_GCUserData to remove this entry
		 */
		tu->ct.EndTimestamp = 0;
		tu->tokenTime = 0;
#endif /* UKERNEL */
	    }
	}
    }
    ReleaseWriteLock(&afs_xuser);
    return 0;
}
