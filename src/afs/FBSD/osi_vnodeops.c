#include <afsconfig.h>
#include <afs/param.h>

RCSID("$Header: /cvs/openafs/src/afs/FBSD/osi_vnodeops.c,v 1.8 2002/10/16 03:58:19 shadow Exp $");

#include <afs/sysincludes.h>            /* Standard vendor system headers */
#include <afsincludes.h>            /* Afs-based standard headers */
#include <afs/afs_stats.h>              /* statistics */
#include <sys/malloc.h>
#include <sys/namei.h>
#include <vm/vm_zone.h>
#include <vm/vm_page.h>
#include <vm/vm_object.h>
#include <vm/vm_pager.h>
#include <vm/vnode_pager.h>
extern int afs_pbuf_freecnt;

int afs_vop_lookup(struct vop_lookup_args *);
int afs_vop_create(struct vop_create_args *);
int afs_vop_mknod(struct vop_mknod_args *);
int afs_vop_open(struct vop_open_args *);
int afs_vop_close(struct vop_close_args *);
int afs_vop_access(struct vop_access_args *);
int afs_vop_getattr(struct vop_getattr_args *);
int afs_vop_setattr(struct vop_setattr_args *);
int afs_vop_read(struct vop_read_args *);
int afs_vop_write(struct vop_write_args *);
int afs_vop_getpages(struct vop_getpages_args *);
int afs_vop_putpages(struct vop_putpages_args *);
int afs_vop_ioctl(struct vop_ioctl_args *);
int afs_vop_poll(struct vop_poll_args *);
int afs_vop_mmap(struct vop_mmap_args *);
int afs_vop_fsync(struct vop_fsync_args *);
int afs_vop_remove(struct vop_remove_args *);
int afs_vop_link(struct vop_link_args *);
int afs_vop_rename(struct vop_rename_args *);
int afs_vop_mkdir(struct vop_mkdir_args *);
int afs_vop_rmdir(struct vop_rmdir_args *);
int afs_vop_symlink(struct vop_symlink_args *);
int afs_vop_readdir(struct vop_readdir_args *);
int afs_vop_readlink(struct vop_readlink_args *);
int afs_vop_inactive(struct vop_inactive_args *);
int afs_vop_reclaim(struct vop_reclaim_args *);
int afs_vop_lock(struct vop_lock_args *);
int afs_vop_unlock(struct vop_unlock_args *);
int afs_vop_bmap(struct vop_bmap_args *);
int afs_vop_strategy(struct vop_strategy_args *);
int afs_vop_print(struct vop_print_args *);
int afs_vop_islocked(struct vop_islocked_args *);
int afs_vop_advlock(struct vop_advlock_args *);



/* Global vfs data structures for AFS. */
vop_t **afs_vnodeop_p;
struct vnodeopv_entry_desc afs_vnodeop_entries[] = {
	{ &vop_default_desc, (vop_t *) vop_eopnotsupp },
	{ &vop_access_desc, (vop_t *) afs_vop_access },          /* access */
	{ &vop_advlock_desc, (vop_t *) afs_vop_advlock },        /* advlock */
	{ &vop_bmap_desc, (vop_t *) afs_vop_bmap },              /* bmap */
	{ &vop_bwrite_desc, (vop_t *) vop_stdbwrite },
	{ &vop_close_desc, (vop_t *) afs_vop_close },            /* close */
        { &vop_createvobject_desc,  (vop_t *) vop_stdcreatevobject },
        { &vop_destroyvobject_desc, (vop_t *) vop_stddestroyvobject },
	{ &vop_create_desc, (vop_t *) afs_vop_create },          /* create */
	{ &vop_fsync_desc, (vop_t *) afs_vop_fsync },            /* fsync */
	{ &vop_getattr_desc, (vop_t *) afs_vop_getattr },        /* getattr */
	{ &vop_getpages_desc, (vop_t *) afs_vop_getpages },              /* read */
        { &vop_getvobject_desc, (vop_t *) vop_stdgetvobject },
	{ &vop_putpages_desc, (vop_t *) afs_vop_putpages },            /* write */
	{ &vop_inactive_desc, (vop_t *) afs_vop_inactive },      /* inactive */
	{ &vop_islocked_desc, (vop_t *) afs_vop_islocked },      /* islocked */
        { &vop_lease_desc, (vop_t *) vop_null },
	{ &vop_link_desc, (vop_t *) afs_vop_link },              /* link */
	{ &vop_lock_desc, (vop_t *) afs_vop_lock },              /* lock */
	{ &vop_lookup_desc, (vop_t *) afs_vop_lookup },          /* lookup */
	{ &vop_mkdir_desc, (vop_t *) afs_vop_mkdir },            /* mkdir */
	{ &vop_mknod_desc, (vop_t *) afs_vop_mknod },            /* mknod */
	{ &vop_mmap_desc, (vop_t *) afs_vop_mmap },              /* mmap */
	{ &vop_open_desc, (vop_t *) afs_vop_open },              /* open */
	{ &vop_poll_desc, (vop_t *) afs_vop_poll },          /* select */
	{ &vop_print_desc, (vop_t *) afs_vop_print },            /* print */
	{ &vop_read_desc, (vop_t *) afs_vop_read },              /* read */
	{ &vop_readdir_desc, (vop_t *) afs_vop_readdir },        /* readdir */
	{ &vop_readlink_desc, (vop_t *) afs_vop_readlink },      /* readlink */
	{ &vop_reclaim_desc, (vop_t *) afs_vop_reclaim },        /* reclaim */
	{ &vop_remove_desc, (vop_t *) afs_vop_remove },          /* remove */
	{ &vop_rename_desc, (vop_t *) afs_vop_rename },          /* rename */
	{ &vop_rmdir_desc, (vop_t *) afs_vop_rmdir },            /* rmdir */
	{ &vop_setattr_desc, (vop_t *) afs_vop_setattr },        /* setattr */
	{ &vop_strategy_desc, (vop_t *) afs_vop_strategy },      /* strategy */
	{ &vop_symlink_desc, (vop_t *) afs_vop_symlink },        /* symlink */
	{ &vop_unlock_desc, (vop_t *) afs_vop_unlock },          /* unlock */
	{ &vop_write_desc, (vop_t *) afs_vop_write },            /* write */
	{ &vop_ioctl_desc, (vop_t *) afs_vop_ioctl }, /* XXX ioctl */
	/*{ &vop_seek_desc, afs_vop_seek },*/              /* seek */
	{ NULL, NULL }
};
struct vnodeopv_desc afs_vnodeop_opv_desc =
	{ &afs_vnodeop_p, afs_vnodeop_entries };

#define GETNAME()       \
    struct componentname *cnp = ap->a_cnp; \
    char *name; \
    MALLOC(name, char *, cnp->cn_namelen+1, M_TEMP, M_WAITOK); \
    memcpy(name, cnp->cn_nameptr, cnp->cn_namelen); \
    name[cnp->cn_namelen] = '\0'

#define DROPNAME() FREE(name, M_TEMP)
	


int
afs_vop_lookup(ap)
struct vop_lookup_args /* {
	                  struct vnodeop_desc * a_desc;
	                  struct vnode *a_dvp;
	                  struct vnode **a_vpp;
	                  struct componentname *a_cnp;
	                  } */ *ap;
{
    int error;
    struct vcache *vcp;
    struct vnode *vp, *dvp;
    register int flags = ap->a_cnp->cn_flags;
    int lockparent;                     /* 1 => lockparent flag is set */
    int wantparent;                     /* 1 => wantparent or lockparent flag */
    struct proc *p;
    GETNAME();
    p=cnp->cn_proc;
    lockparent = flags & LOCKPARENT;
    wantparent = flags & (LOCKPARENT|WANTPARENT);

    if (ap->a_dvp->v_type != VDIR) {
	*ap->a_vpp = 0;
	DROPNAME();
	return ENOTDIR;
    }
    dvp = ap->a_dvp;
    if (flags & ISDOTDOT) 
       VOP_UNLOCK(dvp, 0, p);
    AFS_GLOCK();
    error = afs_lookup(VTOAFS(dvp), name, &vcp, cnp->cn_cred);
    AFS_GUNLOCK();
    if (error) {
        if (flags & ISDOTDOT) 
           VOP_LOCK(dvp, LK_EXCLUSIVE | LK_RETRY, p);
	if ((cnp->cn_nameiop == CREATE || cnp->cn_nameiop == RENAME) &&
	    (flags & ISLASTCN) && error == ENOENT)
	    error = EJUSTRETURN;
	if (cnp->cn_nameiop != LOOKUP && (flags & ISLASTCN))
	    cnp->cn_flags |= SAVENAME;
	DROPNAME();
	*ap->a_vpp = 0;
	return (error);
    }
    vp = AFSTOV(vcp);  /* always get a node if no error */

    /* The parent directory comes in locked.  We unlock it on return
       unless the caller wants it left locked.
       we also always return the vnode locked. */

    if (flags & ISDOTDOT) {
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, p);
        /* always return the child locked */
        if (lockparent && (flags & ISLASTCN) &&
           (error = vn_lock(dvp, LK_EXCLUSIVE, p))) {
            vput(vp);
            DROPNAME();
            return (error);
        }
    } else if (vp == dvp) {
	/* they're the same; afs_lookup() already ref'ed the leaf.
	   It came in locked, so we don't need to ref OR lock it */
    } else {
	if (!lockparent || !(flags & ISLASTCN))
	    VOP_UNLOCK(dvp, 0, p);         /* done with parent. */
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, p);
        /* always return the child locked */
    }
    *ap->a_vpp = vp;

    if ((cnp->cn_nameiop == RENAME && wantparent && (flags & ISLASTCN)) ||
	 (cnp->cn_nameiop != LOOKUP && (flags & ISLASTCN)))
	cnp->cn_flags |= SAVENAME;

    DROPNAME();
    return error;
}

int
afs_vop_create(ap)
	struct vop_create_args /* {
	        struct vnode *a_dvp;
	        struct vnode **a_vpp;
	        struct componentname *a_cnp;
	        struct vattr *a_vap;
	} */ *ap;
{
    int error = 0;
    struct vcache *vcp;
    register struct vnode *dvp = ap->a_dvp;
    struct proc *p;
    GETNAME();
    p=cnp->cn_proc;

    AFS_GLOCK();
    error = afs_create(VTOAFS(dvp), name, ap->a_vap, ap->a_vap->va_vaflags & VA_EXCLUSIVE? EXCL : NONEXCL,
	               ap->a_vap->va_mode, &vcp,
	               cnp->cn_cred);
    AFS_GUNLOCK();
    if (error) {
	DROPNAME();
	return(error);
    }

    if (vcp) {
	*ap->a_vpp = AFSTOV(vcp);
	vn_lock(AFSTOV(vcp), LK_EXCLUSIVE| LK_RETRY, p);
    }
    else *ap->a_vpp = 0;

    DROPNAME();
    return error;
}

int
afs_vop_mknod(ap)
	struct vop_mknod_args /* {
	        struct vnode *a_dvp;
	        struct vnode **a_vpp;
	        struct componentname *a_cnp;
	        struct vattr *a_vap;
	} */ *ap;
{
    return(ENODEV);
}

#if 0
static int validate_vops(struct vnode *vp, int after) 
{
   int ret=0;
   struct vnodeopv_entry_desc *this;
   for (this=afs_vnodeop_entries; this->opve_op; this++) {
	if (vp->v_op[this->opve_op->vdesc_offset] != this->opve_impl) {
            if (!ret) {
                printf("v_op %d ", after);
                vprint("check", vp);
            }
            ret=1;
            printf("For oper %d (%s), func is %p, not %p",
                    this->opve_op->vdesc_offset, this->opve_op->vdesc_name,
                    vp->v_op[this->opve_op->vdesc_offset],  this->opve_impl);
        }
   }
   return ret;
} 
#endif
int
afs_vop_open(ap)
	struct vop_open_args /* {
	        struct vnode *a_vp;
	        int  a_mode;
	        struct ucred *a_cred;
	        struct proc *a_p;
	} */ *ap;
{
    int error;
    int bad;
    struct vcache *vc = VTOAFS(ap->a_vp);
    bad=0;
    AFS_GLOCK();
    error = afs_open(&vc, ap->a_mode, ap->a_cred);
#ifdef DIAGNOSTIC
    if (AFSTOV(vc) != ap->a_vp)
	panic("AFS open changed vnode!");
#endif
    afs_BozonLock(&vc->pvnLock, vc);
    osi_FlushPages(vc, ap->a_cred);
    afs_BozonUnlock(&vc->pvnLock, vc);
    AFS_GUNLOCK();
    return error;
}

int
afs_vop_close(ap)
	struct vop_close_args /* {
	        struct vnode *a_vp;
	        int  a_fflag;
	        struct ucred *a_cred;
	        struct proc *a_p;
	} */ *ap;
{
    int code;
    struct vcache *avc=ap->a_vp;
    AFS_GLOCK();
    if (ap->a_cred) 
        code=afs_close(avc, ap->a_fflag, ap->a_cred, ap->a_p);
    else
        code=afs_close(avc, ap->a_fflag, &afs_osi_cred, ap->a_p);
    afs_BozonLock(&avc->pvnLock, avc);
    osi_FlushPages(avc, ap->a_cred);        /* hold bozon lock, but not basic vnode lock */
    afs_BozonUnlock(&avc->pvnLock, avc);
    AFS_GUNLOCK();
    return code;
}

int
afs_vop_access(ap)
	struct vop_access_args /* {
	        struct vnode *a_vp;
	        int  a_mode;
	        struct ucred *a_cred;
	        struct proc *a_p;
	} */ *ap;
{
    int code;
    AFS_GLOCK();
    code=afs_access(VTOAFS(ap->a_vp), ap->a_mode, ap->a_cred);
    AFS_GUNLOCK();
    return code;
}
int
afs_vop_getattr(ap)
	struct vop_getattr_args /* {
	        struct vnode *a_vp;
	        struct vattr *a_vap;
	        struct ucred *a_cred;
	        struct proc *a_p;
	} */ *ap;
{
    int code;
    AFS_GLOCK();
    code=afs_getattr(VTOAFS(ap->a_vp), ap->a_vap, ap->a_cred);
    AFS_GUNLOCK();
    return code;
}
int
afs_vop_setattr(ap)
	struct vop_setattr_args /* {
	        struct vnode *a_vp;
	        struct vattr *a_vap;
	        struct ucred *a_cred;
	        struct proc *a_p;
	} */ *ap;
{
    int code;
    AFS_GLOCK();
    code=afs_setattr(VTOAFS(ap->a_vp), ap->a_vap, ap->a_cred);
    AFS_GUNLOCK();
    return code;
}int
afs_vop_read(ap)
	struct vop_read_args /* {
	        struct vnode *a_vp;
	        struct uio *a_uio;
	        int a_ioflag;
	        struct ucred *a_cred;
	
} */ *ap;
{
    int code;
    struct vcache *avc=VTOAFS(ap->a_vp);
    AFS_GLOCK();
    afs_BozonLock(&avc->pvnLock, avc);
    osi_FlushPages(avc, ap->a_cred);        /* hold bozon lock, but not basic vnode lock */
    code=afs_read(avc, ap->a_uio, ap->a_cred, 0, 0, 0);
    afs_BozonUnlock(&avc->pvnLock, avc);
    AFS_GUNLOCK();
    return code;
}
int
afs_vop_getpages(ap)
	struct vop_getpages_args /* {
	        struct vnode *a_vp;
                vm_page_t *a_m;
                int a_count;
                int a_reqpage;
                vm_oofset_t a_offset;
        } */ *ap;
{
    int code;
    int i, nextoff, size, toff, npages;
    struct uio uio;
    struct iovec iov;
    struct buf *bp;
    vm_offset_t kva;
    struct vcache *avc=VTOAFS(ap->a_vp);

    if (avc->v.v_object == NULL) {
        printf("afs_getpages: called with non-merged cache vnode??\n");
        return VM_PAGER_ERROR;
    }
    npages=btoc(ap->a_count);
    /*
     * If the requested page is partially valid, just return it and
     * allow the pager to zero-out the blanks.  Partially valid pages
     * can only occur at the file EOF.
     */

    {
       vm_page_t m = ap->a_m[ap->a_reqpage];

       if (m->valid != 0) {
               /* handled by vm_fault now        */
               /* vm_page_zero_invalid(m, TRUE); */
               for (i = 0; i < npages; ++i) {
                       if (i != ap->a_reqpage)
                               vnode_pager_freepage(ap->a_m[i]);
               }
               return(0);
       }
    }
    bp = getpbuf(&afs_pbuf_freecnt);
    kva = (vm_offset_t) bp->b_data;
    pmap_qenter(kva, ap->a_m, npages);
    iov.iov_base=(caddr_t)kva;
    iov.iov_len=ap->a_count;
    uio.uio_iov=&iov;
    uio.uio_iovcnt=1;
    uio.uio_offset=IDX_TO_OFF(ap->a_m[0]->pindex);
    uio.uio_resid=ap->a_count;
    uio.uio_segflg=UIO_SYSSPACE;
    uio.uio_rw=UIO_READ;
    uio.uio_procp=curproc;
    AFS_GLOCK();
    afs_BozonLock(&avc->pvnLock, avc);
    osi_FlushPages(avc, curproc->p_cred->pc_ucred);  /* hold bozon lock, but not basic vnode lock */
    code=afs_read(avc, &uio, curproc->p_cred->pc_ucred, 0, 0, 0);
    afs_BozonUnlock(&avc->pvnLock, avc);
    AFS_GUNLOCK();
    pmap_qremove(kva, npages);

    relpbuf(bp, &afs_pbuf_freecnt);
    if (code && (uio.uio_resid == ap->a_count)) {
           for (i = 0; i < npages; ++i) {
               if (i != ap->a_reqpage)
                   vnode_pager_freepage(ap->a_m[i]);
           }
           return VM_PAGER_ERROR;
    }
    size = ap->a_count - uio.uio_resid;
    for (i = 0, toff = 0; i < npages; i++, toff = nextoff) {
        vm_page_t m;
        nextoff = toff + PAGE_SIZE;
        m = ap->a_m[i];

        m->flags &= ~PG_ZERO;

        if (nextoff <= size) {
                /*
                 * Read operation filled an entire page
                 */
                m->valid = VM_PAGE_BITS_ALL;
                vm_page_undirty(m);
        } else if (size > toff) {
                /*
                 * Read operation filled a partial page.
                 */
                m->valid = 0;
                vm_page_set_validclean(m, 0, size - toff);
                /* handled by vm_fault now        */
                /* vm_page_zero_invalid(m, TRUE); */
        }
        
        if (i != ap->a_reqpage) {
                /*
                 * Whether or not to leave the page activated is up in
                 * the air, but we should put the page on a page queue
                 * somewhere (it already is in the object).  Result:
                 * It appears that emperical results show that
                 * deactivating pages is best.
                 */

                /*
                 * Just in case someone was asking for this page we
                 * now tell them that it is ok to use.
                 */
                if (!code) {
                        if (m->flags & PG_WANTED)
                                vm_page_activate(m);
                        else
                                vm_page_deactivate(m);
                        vm_page_wakeup(m);
                } else {
                        vnode_pager_freepage(m);
                }
        }
    }
    return 0;
}
	
int
afs_vop_write(ap)
	struct vop_write_args /* {
	        struct vnode *a_vp;
	        struct uio *a_uio;
	        int a_ioflag;
	        struct ucred *a_cred;
	} */ *ap;
{
    int code;
    struct vcache *avc=VTOAFS(ap->a_vp);
    AFS_GLOCK();
    afs_BozonLock(&avc->pvnLock, avc);
    osi_FlushPages(avc, ap->a_cred);        /* hold bozon lock, but not basic vnode lock */
    code=afs_write(VTOAFS(ap->a_vp), ap->a_uio, ap->a_ioflag, ap->a_cred, 0);
    afs_BozonUnlock(&avc->pvnLock, avc);
    AFS_GUNLOCK();
    return code;
}

int
afs_vop_putpages(ap)
	struct vop_putpages_args /* {
	        struct vnode *a_vp;
                vm_page_t *a_m;
                int a_count;
                int a_sync;
                int *a_rtvals;
                vm_oofset_t a_offset;
        } */ *ap;
{
    int code;
    int i, size, npages, sync;
    struct uio uio;
    struct iovec iov;
    struct buf *bp;
    vm_offset_t kva;
    struct vcache *avc=VTOAFS(ap->a_vp);

    if (avc->v.v_object == NULL) {
        printf("afs_putpages: called with non-merged cache vnode??\n");
        return VM_PAGER_ERROR;
    }
    if (vType(avc) != VREG) {
        printf("afs_putpages: not VREG");
        return VM_PAGER_ERROR;
    }
    npages=btoc(ap->a_count);
    for (i=0; i < npages; i++ ) ap->a_rtvals[i]=VM_PAGER_AGAIN;
    bp = getpbuf(&afs_pbuf_freecnt);
    kva = (vm_offset_t) bp->b_data;
    pmap_qenter(kva, ap->a_m, npages);
    iov.iov_base=(caddr_t)kva;
    iov.iov_len=ap->a_count;
    uio.uio_iov=&iov;
    uio.uio_iovcnt=1;
    uio.uio_offset=IDX_TO_OFF(ap->a_m[0]->pindex);
    uio.uio_resid=ap->a_count;
    uio.uio_segflg=UIO_SYSSPACE;
    uio.uio_rw=UIO_WRITE;
    uio.uio_procp=curproc;
    sync=IO_VMIO;
    if (ap->a_sync & VM_PAGER_PUT_SYNC)
       sync|=IO_SYNC;
    /*if (ap->a_sync & VM_PAGER_PUT_INVAL)
       sync|=IO_INVAL;*/

    AFS_GLOCK();
    afs_BozonLock(&avc->pvnLock, avc);
    code=afs_write(avc, &uio, sync, curproc->p_cred->pc_ucred,  0);
    afs_BozonUnlock(&avc->pvnLock, avc);
    AFS_GUNLOCK();
    pmap_qremove(kva, npages);

    relpbuf(bp, &afs_pbuf_freecnt);
    if (!code) {
           size = ap->a_count - uio.uio_resid;
           for (i = 0; i < round_page(size) / PAGE_SIZE; i++) {
               ap->a_rtvals[i]=VM_PAGER_OK;
               ap->a_m[i]->dirty=0;
           }
           return VM_PAGER_ERROR;
    }
    return ap->a_rtvals[0];
}
int
afs_vop_ioctl(ap)
	struct vop_ioctl_args /* {
	        struct vnode *a_vp;
	        int  a_command;
	        caddr_t  a_data;
	        int  a_fflag;
	        struct ucred *a_cred;
	        struct proc *a_p;
	} */ *ap;
{
    struct vcache *tvc = VTOAFS(ap->a_vp);
    struct afs_ioctl data;
    int error = 0;
  
    /* in case we ever get in here... */

    AFS_STATCNT(afs_ioctl);
    if (((ap->a_command >> 8) & 0xff) == 'V') {
	/* This is a VICEIOCTL call */
    AFS_GLOCK();
	error = HandleIoctl(tvc, (struct file *)0/*Not used*/,
	                    ap->a_command, ap->a_data);
    AFS_GUNLOCK();
	return(error);
    } else {
	/* No-op call; just return. */
	return(ENOTTY);
    }
}

/* ARGSUSED */
int
afs_vop_poll(ap)
	struct vop_poll_args /* {
	        struct vnode *a_vp;
	        int  a_events;
	        struct ucred *a_cred;
	        struct proc *a_p;
	} */ *ap;
{
	/*
	 * We should really check to see if I/O is possible.
	 */
	return (1);
}
/*
 * Mmap a file
 *
 * NB Currently unsupported.
 */
/* ARGSUSED */
int
afs_vop_mmap(ap)
	struct vop_mmap_args /* {
	        struct vnode *a_vp;
	        int  a_fflags;
	        struct ucred *a_cred;
	        struct proc *a_p;
	} */ *ap;
{
	return (EINVAL);
}

int
afs_vop_fsync(ap)
	struct vop_fsync_args /* {
	        struct vnode *a_vp;
	        struct ucred *a_cred;
	        int a_waitfor;
	        struct proc *a_p;
	} */ *ap;
{
    int wait = ap->a_waitfor == MNT_WAIT;
    int error;
    register struct vnode *vp = ap->a_vp;

    AFS_GLOCK();
    /*vflushbuf(vp, wait);*/
    if (ap->a_cred)
      error=afs_fsync(VTOAFS(vp), ap->a_cred);
    else
      error=afs_fsync(VTOAFS(vp), &afs_osi_cred);
    AFS_GUNLOCK();
    return error;
}

int
afs_vop_remove(ap)
	struct vop_remove_args /* {
	        struct vnode *a_dvp;
	        struct vnode *a_vp;
	        struct componentname *a_cnp;
	} */ *ap;
{
    int error = 0;
    register struct vnode *vp = ap->a_vp;
    register struct vnode *dvp = ap->a_dvp;

    GETNAME();
    AFS_GLOCK();
    error =  afs_remove(VTOAFS(dvp), name, cnp->cn_cred);
    AFS_GUNLOCK();
    cache_purge(vp);
    DROPNAME();
    return error;
}

int
afs_vop_link(ap)
	struct vop_link_args /* {
	        struct vnode *a_vp;
	        struct vnode *a_tdvp;
	        struct componentname *a_cnp;
	} */ *ap;
{
    int error = 0;
    register struct vnode *dvp = ap->a_tdvp;
    register struct vnode *vp = ap->a_vp;
    struct proc *p;

    GETNAME();
    p=cnp->cn_proc;
    if (dvp->v_mount != vp->v_mount) {
	error = EXDEV;
	goto out;
    }
    if (vp->v_type == VDIR) {
	error = EISDIR;
	goto out;
    }
    if (error = vn_lock(vp, LK_EXCLUSIVE, p)) {
	goto out;
    }
    AFS_GLOCK();
    error = afs_link(VTOAFS(vp), VTOAFS(dvp), name, cnp->cn_cred);
    AFS_GUNLOCK();
    if (dvp != vp)
	VOP_UNLOCK(vp,0, p);
out:
    DROPNAME();
    return error;
}

int
afs_vop_rename(ap)
	struct vop_rename_args  /* {
	        struct vnode *a_fdvp;
	        struct vnode *a_fvp;
	        struct componentname *a_fcnp;
	        struct vnode *a_tdvp;
	        struct vnode *a_tvp;
	        struct componentname *a_tcnp;
	} */ *ap;
{
    int error = 0;
    struct componentname *fcnp = ap->a_fcnp;
    char *fname;
    struct componentname *tcnp = ap->a_tcnp;
    char *tname;
    struct vnode *tvp = ap->a_tvp;
    register struct vnode *tdvp = ap->a_tdvp;
    struct vnode *fvp = ap->a_fvp;
    register struct vnode *fdvp = ap->a_fdvp;
    struct proc *p=fcnp->cn_proc;

    /*
     * Check for cross-device rename.
     */
    if ((fvp->v_mount != tdvp->v_mount) ||
	(tvp && (fvp->v_mount != tvp->v_mount))) {
	error = EXDEV;
abortit:
	if (tdvp == tvp)
	    vrele(tdvp);
	else
	    vput(tdvp);
	if (tvp)
	    vput(tvp);
	vrele(fdvp);
	vrele(fvp);
	return (error);
    }
    /*
     * if fvp == tvp, we're just removing one name of a pair of
     * directory entries for the same element.  convert call into rename.
     ( (pinched from FreeBSD 4.4's ufs_rename())
       
     */
    if (fvp == tvp) {
	if (fvp->v_type == VDIR) {
	    error = EINVAL;
	    goto abortit;
	}

	/* Release destination completely. */
	vput(tdvp);
	vput(tvp);

	/* Delete source. */
	vrele(fdvp);
	vrele(fvp);
	fcnp->cn_flags &= ~MODMASK;
	fcnp->cn_flags |= LOCKPARENT | LOCKLEAF;
	if ((fcnp->cn_flags & SAVESTART) == 0)
	    panic("afs_rename: lost from startdir");
	fcnp->cn_nameiop = DELETE;
        VREF(fdvp);
	error=relookup(fdvp, &fvp, fcnp);
        if (error == 0)
                vrele(fdvp);
        if (fvp == NULL) {
                return (ENOENT);
        }
        
	error=VOP_REMOVE(fdvp, fvp, fcnp);
        if (fdvp == fvp)
            vrele(fdvp);
        else
            vput(fdvp);
        vput(fvp);
        return (error);
    }
    if (error = vn_lock(fvp, LK_EXCLUSIVE, p))
	goto abortit;

    MALLOC(fname, char *, fcnp->cn_namelen+1, M_TEMP, M_WAITOK);
    memcpy(fname, fcnp->cn_nameptr, fcnp->cn_namelen);
    fname[fcnp->cn_namelen] = '\0';
    MALLOC(tname, char *, tcnp->cn_namelen+1, M_TEMP, M_WAITOK);
    memcpy(tname, tcnp->cn_nameptr, tcnp->cn_namelen);
    tname[tcnp->cn_namelen] = '\0';


    AFS_GLOCK();
    /* XXX use "from" or "to" creds? NFS uses "to" creds */
    error = afs_rename(VTOAFS(fdvp), fname, VTOAFS(tdvp), tname, tcnp->cn_cred);
    AFS_GUNLOCK();

    FREE(fname, M_TEMP);
    FREE(tname, M_TEMP);
    if (tdvp == tvp)
	vrele(tdvp);
    else
	vput(tdvp);
    if (tvp)
	vput(tvp);
    vrele(fdvp);
    vput(fvp);
    return error;
}

int
afs_vop_mkdir(ap)
	struct vop_mkdir_args /* {
	        struct vnode *a_dvp;
	        struct vnode **a_vpp;
	        struct componentname *a_cnp;
	        struct vattr *a_vap;
	} */ *ap;
{
    register struct vnode *dvp = ap->a_dvp;
    register struct vattr *vap = ap->a_vap;
    int error = 0;
    struct vcache *vcp;
    struct proc *p;

    GETNAME();
    p=cnp->cn_proc;
#ifdef DIAGNOSTIC
    if ((cnp->cn_flags & HASBUF) == 0)
	panic("afs_vop_mkdir: no name");
#endif
    AFS_GLOCK();
    error = afs_mkdir(VTOAFS(dvp), name, vap, &vcp, cnp->cn_cred);
    AFS_GUNLOCK();
    if (error) {
	vput(dvp);
	DROPNAME();
	return(error);
    }
    if (vcp) {
	*ap->a_vpp = AFSTOV(vcp);
	vn_lock(AFSTOV(vcp), LK_EXCLUSIVE|LK_RETRY, p);
    } else
	*ap->a_vpp = 0;
    DROPNAME();
    return error;
}

int
afs_vop_rmdir(ap)
	struct vop_rmdir_args /* {
	        struct vnode *a_dvp;
	        struct vnode *a_vp;
	        struct componentname *a_cnp;
	} */ *ap;
{
    int error = 0;
    register struct vnode *vp = ap->a_vp;
    register struct vnode *dvp = ap->a_dvp;

    GETNAME();
    AFS_GLOCK();
    error = afs_rmdir(VTOAFS(dvp), name, cnp->cn_cred);
    AFS_GUNLOCK();
    DROPNAME();
    return error;
}

int
afs_vop_symlink(ap)
	struct vop_symlink_args /* {
	        struct vnode *a_dvp;
	        struct vnode **a_vpp;
	        struct componentname *a_cnp;
	        struct vattr *a_vap;
	        char *a_target;
	} */ *ap;
{
    register struct vnode *dvp = ap->a_dvp;
    int error = 0;
    /* NFS ignores a_vpp; so do we. */

    GETNAME();
    AFS_GLOCK();
    error = afs_symlink(VTOAFS(dvp), name, ap->a_vap, ap->a_target,
	                cnp->cn_cred);
    AFS_GUNLOCK();
    DROPNAME();
    return error;
}

int
afs_vop_readdir(ap)
	struct vop_readdir_args /* {
	        struct vnode *a_vp;
	        struct uio *a_uio;
	        struct ucred *a_cred;
	        int *a_eofflag;
	        u_long *a_cookies;
	        int ncookies;
	} */ *ap;
{
    int error;
    off_t off;
/*    printf("readdir %x cookies %x ncookies %d\n", ap->a_vp, ap->a_cookies,
	   ap->a_ncookies); */
    off=ap->a_uio->uio_offset;
    AFS_GLOCK();
    error= afs_readdir(VTOAFS(ap->a_vp), ap->a_uio, ap->a_cred,
	               ap->a_eofflag);
    AFS_GUNLOCK();
    if (!error && ap->a_ncookies != NULL) {
	struct uio *uio = ap->a_uio;
	const struct dirent *dp, *dp_start, *dp_end;
	int ncookies;
	u_long *cookies, *cookiep;

	if (uio->uio_segflg != UIO_SYSSPACE || uio->uio_iovcnt != 1)
	    panic("afs_readdir: burned cookies");
	dp = (const struct dirent *)
	    ((const char *)uio->uio_iov->iov_base - (uio->uio_offset - off));

	dp_end = (const struct dirent *) uio->uio_iov->iov_base;
	for (dp_start = dp, ncookies = 0;
	     dp < dp_end;
	     dp = (const struct dirent *)((const char *) dp + dp->d_reclen))
	    ncookies++;

	MALLOC(cookies, u_long *, ncookies * sizeof(u_long),
	       M_TEMP, M_WAITOK);
	for (dp = dp_start, cookiep = cookies;
	     dp < dp_end;
	     dp = (const struct dirent *)((const char *) dp + dp->d_reclen)) {
	    off += dp->d_reclen;
	    *cookiep++ = off;
	}
	*ap->a_cookies = cookies;
	*ap->a_ncookies = ncookies;
    }

    return error;
}

int
afs_vop_readlink(ap)
	struct vop_readlink_args /* {
	        struct vnode *a_vp;
	        struct uio *a_uio;
	        struct ucred *a_cred;
	} */ *ap;
{
    int error;
/*    printf("readlink %x\n", ap->a_vp);*/
    AFS_GLOCK();
    error= afs_readlink(VTOAFS(ap->a_vp), ap->a_uio, ap->a_cred);
    AFS_GUNLOCK();
    return error;
}

extern int prtactive;

int
afs_vop_inactive(ap)
	struct vop_inactive_args /* {
	        struct vnode *a_vp;
                struct proc *a_p;
	} */ *ap;
{
    register struct vnode *vp = ap->a_vp;

    if (prtactive && vp->v_usecount != 0)
	vprint("afs_vop_inactive(): pushing active", vp);

    AFS_GLOCK();
    afs_InactiveVCache(VTOAFS(vp), 0);   /* decrs ref counts */
    AFS_GUNLOCK();
    VOP_UNLOCK(vp, 0, ap->a_p);
    return 0;
}

int
afs_vop_reclaim(ap)
	struct vop_reclaim_args /* {
	        struct vnode *a_vp;
	} */ *ap;
{
    int error;
    int sl;
    register struct vnode *vp = ap->a_vp;

    cache_purge(vp);                    /* just in case... */

#if 0 
    AFS_GLOCK();
    error = afs_FlushVCache(VTOAFS(vp), &sl); /* tosses our stuff from vnode */
    AFS_GUNLOCK();
    ubc_unlink(vp);
    if (!error && vp->v_data)
	panic("afs_reclaim: vnode not cleaned");
    return error;
#else
   if (vp->v_usecount == 2) {
        vprint("reclaim count==2", vp);
   } else if (vp->v_usecount == 1) {
        vprint("reclaim count==1", vp);
   } else 
        vprint("reclaim bad count", vp);

   return 0;
#endif
}

int
afs_vop_lock(ap)
	struct vop_lock_args /* {
	        struct vnode *a_vp;
	} */ *ap;
{
	register struct vnode *vp = ap->a_vp;
	register struct vcache *avc = VTOAFS(vp);

	if (vp->v_tag == VT_NON)
	        return (ENOENT);
	return (lockmgr(&avc->rwlock, ap->a_flags, &vp->v_interlock,
                ap->a_p));
}

int
afs_vop_unlock(ap)
	struct vop_unlock_args /* {
	        struct vnode *a_vp;
	} */ *ap;
{
    struct vnode *vp = ap->a_vp;
    struct vcache *avc = VTOAFS(vp);
    return (lockmgr(&avc->rwlock, ap->a_flags | LK_RELEASE,
            &vp->v_interlock, ap->a_p));

}

int
afs_vop_bmap(ap)
	struct vop_bmap_args /* {
	        struct vnode *a_vp;
	        daddr_t  a_bn;
	        struct vnode **a_vpp;
	        daddr_t *a_bnp;
	        int *a_runp;
	        int *a_runb;
	} */ *ap;
{
    struct vcache *vcp;
    int error;
    if (ap->a_bnp) {
	*ap->a_bnp = ap->a_bn * (PAGE_SIZE / DEV_BSIZE);
    }
    if (ap->a_vpp) {
	*ap->a_vpp = ap->a_vp;
    }
	if (ap->a_runp != NULL)
	        *ap->a_runp = 0;
	if (ap->a_runb != NULL)
	        *ap->a_runb = 0;
 
    return 0;
}
int
afs_vop_strategy(ap)
	struct vop_strategy_args /* {
	        struct buf *a_bp;
	} */ *ap;
{
    int error;
    AFS_GLOCK();
    error= afs_ustrategy(ap->a_bp);
    AFS_GUNLOCK();
    return error;
}
int
afs_vop_print(ap)
	struct vop_print_args /* {
	        struct vnode *a_vp;
	} */ *ap;
{
    register struct vnode *vp = ap->a_vp;
    register struct vcache *vc = VTOAFS(ap->a_vp);
    int s = vc->states;
    printf("tag %d, fid: %ld.%x.%x.%x, opens %d, writers %d", vp->v_tag, vc->fid.Cell,
	   vc->fid.Fid.Volume, vc->fid.Fid.Vnode, vc->fid.Fid.Unique, vc->opens,
	   vc->execsOrWriters);
    printf("\n  states%s%s%s%s%s", (s&CStatd) ? " statd" : "", (s&CRO) ? " readonly" : "",(s&CDirty) ? " dirty" : "",(s&CMAPPED) ? " mapped" : "", (s&CVFlushed) ? " flush in progress" : "");
    printf("\n");
    return 0;
}

int
afs_vop_islocked(ap)
	struct vop_islocked_args /* {
	        struct vnode *a_vp;
	} */ *ap;
{
    struct vcache *vc = VTOAFS(ap->a_vp);
    return lockstatus(&vc->rwlock, ap->a_p);
}

/*
 * Advisory record locking support (fcntl() POSIX style)
 */
int
afs_vop_advlock(ap)
	struct vop_advlock_args /* {
	        struct vnode *a_vp;
	        caddr_t  a_id;
	        int  a_op;
	        struct flock *a_fl;
	        int  a_flags;
	} */ *ap;
{
    int error;
    struct proc *p=curproc;
    struct ucred cr;
    cr=*p->p_cred->pc_ucred;
    AFS_GLOCK();
    error= afs_lockctl(VTOAFS(ap->a_vp), ap->a_fl, ap->a_op, &cr,
	               (int) ap->a_id);
    AFS_GUNLOCK();
    return error;
}

