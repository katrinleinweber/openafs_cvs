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
	Module:		vnode.c
	Institution:	The Information Technology Center, Carnegie-Mellon University

 */
#include <afsconfig.h>
#include <afs/param.h>

RCSID("$Header: /cvs/openafs/src/vol/vnode.c,v 1.14 2003/06/02 14:37:49 shadow Exp $");

#include <errno.h>
#include <stdio.h>
#ifdef HAVE_STRING_H
#include <string.h>
#else
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#endif
#ifdef AFS_PTHREAD_ENV
#include <assert.h>
#else /* AFS_PTHREAD_ENV */
#include <afs/assert.h>
#endif /* AFS_PTHREAD_ENV */

#include <rx/xdr.h>
#include <afs/afsint.h>
#include "nfs.h"
#include <afs/errors.h>
#include "lock.h"
#include "lwp.h"
#include <afs/afssyscalls.h>
#include "ihandle.h"
#include "vnode.h"
#include "partition.h"
#include "volume.h"
#if defined(AFS_SGI_ENV)
#include "sys/types.h"
#include "fcntl.h"
#undef min
#undef max
#include "stdlib.h"
#endif
#ifdef AFS_NT40_ENV
#include <fcntl.h>
#include "ntops.h"
#else
#include <sys/file.h>
#ifdef	AFS_SUN5_ENV
#include <sys/fcntl.h>
#endif
#include <unistd.h>
#endif /* AFS_NT40_ENV */
#include <sys/stat.h>

extern void Abort(const char *format, ...);


struct VnodeClassInfo VnodeClassInfo[nVNODECLASSES];

private int moveHash();
void StickOnLruChain_r();
void VPutVnode_r();

#define BAD_IGET	-1000

/* There are two separate vnode queue types defined here:
 * Each hash conflict chain -- is singly linked, with a single head
 * pointer. New entries are added at the beginning. Old
 * entries are removed by linear search, which generally
 * only occurs after a disk read).
 * LRU chain -- is doubly linked, single head pointer.
 * Entries are added at the head, reclaimed from the tail,
 * or removed from anywhere in the queue.
 */


/* Vnode hash table.  Find hash chain by taking lower bits of
 * (volume_hash_offset + vnode).
 * This distributes the root inodes of the volumes over the
 * hash table entries and also distributes the vnodes of
 * volumes reasonably fairly.  The volume_hash_offset field
 * for each volume is established as the volume comes on line
 * by using the VOLUME_HASH_OFFSET macro.  This distributes the
 * volumes fairly among the cache entries, both when servicing
 * a small number of volumes and when servicing a large number.
 */

/* logging stuff for finding bugs */
#define	THELOGSIZE	5120
static afs_int32 theLog[THELOGSIZE];
static afs_int32 vnLogPtr=0;
VNLog(aop, anparms, av1, av2, av3, av4)
afs_int32 aop, anparms;
afs_int32 av1, av2, av3,av4; {
    register afs_int32 temp;
    afs_int32 data[4];

    /* copy data to array */
    data[0] = av1;
    data[1] = av2;
    data[2] = av3;
    data[3] = av4;
    if (anparms>4) anparms = 4;	/* do bounds checking */

    temp = (aop<<16) | anparms;
    theLog[vnLogPtr++] = temp;
    if (vnLogPtr >= THELOGSIZE) vnLogPtr = 0;
    for(temp=0;temp<anparms;temp++) {
	theLog[vnLogPtr++] = data[temp];
	if (vnLogPtr >= THELOGSIZE) vnLogPtr = 0;
    }
}

/* VolumeHashOffset -- returns a new value to be stored in the
 * volumeHashOffset of a Volume structure.  Called when a
 * volume is initialized.  Sets the volumeHashOffset so that
 * vnode cache entries are distributed reasonably between
 * volumes (the root vnodes of the volumes will hash to
 * different values, and spacing is maintained between volumes
 * when there are not many volumes represented), and spread
 * equally amongst vnodes within a single volume.
 */
int VolumeHashOffset_r() {
    static int nextVolumeHashOffset = 0;
    /* hashindex Must be power of two in size */
#   define hashShift 3
#   define hashMask ((1<<hashShift)-1)
    static byte hashindex[1<<hashShift] = {0,128,64,192,32,160,96,224};
    int offset;
    offset = hashindex[nextVolumeHashOffset&hashMask]
           + (nextVolumeHashOffset>>hashShift);
    nextVolumeHashOffset++;
    return offset;
}

/* Change hashindex (above) if you change this constant */
#define VNODE_HASH_TABLE_SIZE 256
private Vnode *VnodeHashTable[VNODE_HASH_TABLE_SIZE];
#define VNODE_HASH(volumeptr,vnodenumber)\
    ((volumeptr->vnodeHashOffset + vnodenumber)&(VNODE_HASH_TABLE_SIZE-1))

/* Code to invalidate a vnode entry.  Called when we've damaged a vnode, and want
    to prevent future VGetVnode's from applying to it.  Leaves it in the same hash bucket
    but that shouldn't be important.  */
void VInvalidateVnode_r(register struct Vnode *avnode)
{
    avnode->changed_newTime = 0;    /* don't let it get flushed out again */
    avnode->changed_oldTime = 0;
    avnode->delete = 0;	    /* it isn't deleted, erally */
    avnode->cacheCheck = 0; /* invalid: prevents future vnode searches from working */
}

/* Not normally called by general client; called by volume.c */
int VInitVnodes(VnodeClass class, int nVnodes)
{
    byte *va;
    register struct VnodeClassInfo *vcp = &VnodeClassInfo[class];

    vcp->allocs = vcp->gets = vcp->reads = vcp->writes = 0;
    vcp->cacheSize = nVnodes;
    switch(class) {
      case vSmall:
      	assert(CHECKSIZE_SMALLVNODE);
	vcp->lruHead = NULL;
        vcp->residentSize = SIZEOF_SMALLVNODE;
	vcp->diskSize = SIZEOF_SMALLDISKVNODE;
	vcp->magic = SMALLVNODEMAGIC;
	break;
      case vLarge:
	vcp->lruHead = NULL;
	vcp->residentSize = SIZEOF_LARGEVNODE;
	vcp->diskSize = SIZEOF_LARGEDISKVNODE;
	vcp->magic = LARGEVNODEMAGIC;
        break;
    }
    {	int s = vcp->diskSize-1;
	int n = 0;
	while (s)
	    s >>= 1, n++;
	vcp->logSize = n;
    }

    if (nVnodes == 0)
	    return 0;

    va = (byte *) calloc(nVnodes,vcp->residentSize);
    assert (va != NULL);
    while (nVnodes--) {
	Vnode *vnp = (Vnode *) va;
	vnp->nUsers = 0;    /* no context switches */
	Lock_Init(&vnp->lock);
	vnp->changed_oldTime = 0;
	vnp->changed_newTime = 0;
	vnp->volumePtr = NULL;
	vnp->cacheCheck = 0;
	vnp->delete = vnp->vnodeNumber = 0;
#ifdef AFS_PTHREAD_ENV
	vnp->writer = (pthread_t) 0;
#else /* AFS_PTHREAD_ENV */
	vnp->writer = (PROCESS) 0;
#endif /* AFS_PTHREAD_ENV */
    	vnp->hashIndex = 0;
	vnp->handle = NULL;
	if (vcp->lruHead == NULL)
	    vcp->lruHead = vnp->lruNext = vnp->lruPrev = vnp;
	else {
	    vnp->lruNext = vcp->lruHead;
	    vnp->lruPrev = vcp->lruHead->lruPrev;
	    vcp->lruHead->lruPrev = vnp;
	    vnp->lruPrev->lruNext = vnp;
	    vcp->lruHead = vnp;
	}
	va += vcp->residentSize;
    }
    return 0;
}


/* allocate an *unused* vnode from the LRU chain, going backwards of course.  It shouldn't
    be necessary to specify that nUsers == 0 since if it is in the list, nUsers
    should be 0.  Things shouldn't be in lruq unless no one is using them.  */
Vnode *VGetFreeVnode_r(vcp)
struct VnodeClassInfo *vcp; {
    register Vnode *vnp;

    vnp = vcp->lruHead->lruPrev;
    if (vnp->nUsers != 0 || CheckLock(&vnp->lock))
	Abort("locked vnode in lruq");
    VNLog(1, 2, vnp->vnodeNumber, (afs_int32) vnp);
    IH_RELEASE(vnp->handle);
    return vnp;
}

static mlkReason=0;
static mlkLastAlloc = 0;
static mlkLastOver = 0;
static mlkLastDelete = 0;

Vnode *VAllocVnode(ec,vp,type)
    Error *ec;
    Volume *vp;
    VnodeType type;
{
    Vnode *retVal;
    VOL_LOCK
    retVal = VAllocVnode_r(ec, vp, type);
    VOL_UNLOCK
    return retVal;
}

Vnode *VAllocVnode_r(ec,vp,type)
    Error *ec;
    Volume *vp;
    VnodeType type;
{
    register Vnode *vnp;
    VnodeId vnodeNumber;
    int newHash, bitNumber;
    register struct VnodeClassInfo *vcp;
    VnodeClass class;
    Unique unique;

    *ec = 0;
    if (programType == fileServer && !V_inUse(vp)) {
	if (vp->specialStatus) {
	    *ec = vp->specialStatus;
	} else {
	    *ec = VOFFLINE;
	}
	return NULL;
    }
    class = vnodeTypeToClass(type);
    vcp = &VnodeClassInfo[class];

    if (!VolumeWriteable(vp)) {
	*ec = (bit32) VREADONLY;
	return NULL;
    }

    unique = vp->nextVnodeUnique++;
    if (!unique)
       unique = vp->nextVnodeUnique++;

    if (vp->nextVnodeUnique > V_uniquifier(vp)) {
	VUpdateVolume_r(ec,vp);
	if (*ec)
	    return NULL;
    }

    if (programType == fileServer) {
	VAddToVolumeUpdateList_r(ec, vp);
	if (*ec)
	    return NULL;
    }

    /* Find a slot in the bit map */
    bitNumber = VAllocBitmapEntry_r(ec,vp,&vp->vnodeIndex[class]);
    if (*ec)
    	return NULL;
    vnodeNumber = bitNumberToVnodeNumber(bitNumber,class);

    VNLog(2, 1, vnodeNumber);
    /* Prepare to move it to the new hash chain */
    newHash = VNODE_HASH(vp, vnodeNumber);
    for (vnp = VnodeHashTable[newHash];
         vnp && (vnp->vnodeNumber!=vnodeNumber || vnp->volumePtr!=vp
	         || vnp->volumePtr->cacheCheck!=vnp->cacheCheck);
         vnp = vnp->hashNext
	);
    if (vnp) {
	/* slot already exists.  May even not be in lruq (consider store file locking a file being deleted)
	    so we may have to wait for it below */
	VNLog(3, 2, vnodeNumber, (afs_int32) vnp);

	/* If first user, remove it from the LRU chain.  We can assume that
	   there is at least one item in the queue */
	if (++vnp->nUsers == 1) {
	    if (vnp == vcp->lruHead)
		vcp->lruHead = vcp->lruHead->lruNext;
	    vnp->lruPrev->lruNext = vnp->lruNext;
	    vnp->lruNext->lruPrev = vnp->lruPrev;
	    if (vnp == vcp->lruHead || vcp->lruHead == NULL)
		Abort("VGetVnode: lru chain addled!\n");
	    /* This won't block */
	    ObtainWriteLock(&vnp->lock);
	} else {
	    /* follow locking hierarchy */
	    VOL_UNLOCK
	    ObtainWriteLock(&vnp->lock);
	    VOL_LOCK
	}
#ifdef AFS_PTHREAD_ENV
	vnp->writer = pthread_self();
#else /* AFS_PTHREAD_ENV */
	LWP_CurrentProcess(&vnp->writer);
#endif /* AFS_PTHREAD_ENV */
    }
    else {
	vnp = VGetFreeVnode_r(vcp);
	/* Remove vnode from LRU chain and grab a write lock */
	if (vnp == vcp->lruHead)
	    vcp->lruHead = vcp->lruHead->lruNext;
	vnp->lruPrev->lruNext = vnp->lruNext;
	vnp->lruNext->lruPrev = vnp->lruPrev;
	if (vnp == vcp->lruHead || vcp->lruHead == NULL)
	    Abort("VGetVnode: lru chain addled!\n");
	/* Initialize the header fields so noone allocates another
	   vnode with the same number */
	vnp->vnodeNumber = vnodeNumber;
	vnp->volumePtr = vp;
	vnp->cacheCheck = vp->cacheCheck;
	vnp->nUsers = 1;
	moveHash(vnp, newHash);
	/* This will never block */
	ObtainWriteLock(&vnp->lock);
#ifdef AFS_PTHREAD_ENV
	vnp->writer = pthread_self();
#else /* AFS_PTHREAD_ENV */
	LWP_CurrentProcess(&vnp->writer);
#endif /* AFS_PTHREAD_ENV */
	/* Sanity check:  is this vnode really not in use? */
        { 
	  int size;
	  IHandle_t *ihP = vp->vnodeIndex[class].handle;
	  FdHandle_t *fdP;
	  off_t off = vnodeIndexOffset(vcp, vnodeNumber);

	  VOL_UNLOCK
	  fdP = IH_OPEN(ihP);
	  if (fdP == NULL)
	      Abort("VAllocVnode: can't open index file!\n");
	  if ((size = FDH_SIZE(fdP)) < 0)
	      Abort("VAllocVnode: can't stat index file!\n");
	  if (FDH_SEEK(fdP, off, SEEK_SET) < 0)
	      Abort("VAllocVnode: can't seek on index file!\n");
	  if (off < size) {
	      if (FDH_READ(fdP, &vnp->disk, vcp->diskSize) == vcp->diskSize) {
		  if (vnp->disk.type != vNull)
		      Abort("VAllocVnode:  addled bitmap or index!\n");
	      }
	  } else {
	      /* growing file - grow in a reasonable increment */
	      char *buf = (char *)malloc(16*1024);
	      if (!buf) Abort("VAllocVnode: malloc failed\n");
	      memset(buf, 0, 16*1024);
	      (void) FDH_WRITE(fdP, buf, 16*1024);
	      free(buf);
	  }
	  FDH_CLOSE(fdP);
	  VOL_LOCK
      }
	VNLog(4, 2, vnodeNumber, (afs_int32) vnp);
    }

    VNLog(5, 1, (afs_int32) vnp);
#ifdef AFS_PTHREAD_ENV
    vnp->writer = pthread_self();
#else /* AFS_PTHREAD_ENV */
    LWP_CurrentProcess(&vnp->writer);
#endif /* AFS_PTHREAD_ENV */
    memset(&vnp->disk, 0, sizeof(vnp->disk));
    vnp->changed_newTime = 0; /* set this bit when vnode is updated */
    vnp->changed_oldTime = 0; /* set this on CopyOnWrite. */
    vnp->delete = 0;
    vnp->disk.vnodeMagic = vcp->magic;
    vnp->disk.type = type;
    vnp->disk.uniquifier = unique;
    vnp->handle = NULL;
    vcp->allocs++;
    return vnp;
}
    
Vnode *VGetVnode(ec,vp,vnodeNumber,locktype)
    Error *ec;
    Volume *vp;
    VnodeId vnodeNumber;
    int locktype;	/* READ_LOCK or WRITE_LOCK, as defined in lock.h */
{
    Vnode *retVal;
    VOL_LOCK
    retVal = VGetVnode_r(ec, vp, vnodeNumber, locktype);
    VOL_UNLOCK
    return retVal;
}

Vnode *VGetVnode_r(ec,vp,vnodeNumber,locktype)
    Error *ec;
    Volume *vp;
    VnodeId vnodeNumber;
    int locktype;	/* READ_LOCK or WRITE_LOCK, as defined in lock.h */
{
    register Vnode *vnp;
    int newHash;
    VnodeClass class;
    struct VnodeClassInfo *vcp;

    *ec = 0;
    mlkReason =	0;  /* last call didn't fail */

    if (vnodeNumber == 0) {
	*ec = VNOVNODE;
	mlkReason = 1;
	return NULL;
    }

    VNLog(100, 1, vnodeNumber);
    if (programType == fileServer && !V_inUse(vp)) {
        *ec = (vp->specialStatus ? vp->specialStatus : VOFFLINE);

	/* If the volume is VBUSY (being cloned or dumped) and this is
	 * a READ operation, then don't fail.
	 */
	if ((*ec != VBUSY) || (locktype != READ_LOCK)) {
	   mlkReason = 2;
	   return NULL;
	}
	*ec = 0;
    }
    class = vnodeIdToClass(vnodeNumber);
    vcp = &VnodeClassInfo[class];
    if (locktype == WRITE_LOCK && !VolumeWriteable(vp)) {
	*ec = (bit32) VREADONLY;
	mlkReason = 3;
	return NULL;
    }

    if (locktype == WRITE_LOCK && programType == fileServer) {
	VAddToVolumeUpdateList_r(ec, vp);
	if (*ec) {
	    mlkReason = 1000 + *ec;
	    return NULL;
	}
    }

    /* See whether the vnode is in the cache. */
    newHash = VNODE_HASH(vp, vnodeNumber);
    for (vnp = VnodeHashTable[newHash];
         vnp && (vnp->vnodeNumber!=vnodeNumber || vnp->volumePtr!=vp
	         || vnp->volumePtr->cacheCheck!=vnp->cacheCheck);
         vnp = vnp->hashNext
	);
    vcp->gets++;
    if (vnp == NULL) {
	int     n;
	IHandle_t *ihP = vp->vnodeIndex[class].handle;
	FdHandle_t *fdP;
        /* Not in cache; tentatively grab most distantly used one from the LRU
           chain */
	vcp->reads++;
	vnp = VGetFreeVnode_r(vcp);
        /* Remove it from the old hash chain */
	moveHash(vnp, newHash);
	/* Remove it from the LRU chain */
	if (vnp == vcp->lruHead)
	    vcp->lruHead = vcp->lruHead->lruNext;
	if (vnp == vcp->lruHead || vcp->lruHead == NULL)
	    Abort("VGetVnode: lru chain addled!\n");
	vnp->lruPrev->lruNext = vnp->lruNext;
	vnp->lruNext->lruPrev = vnp->lruPrev;
	/* Initialize */
	vnp->changed_newTime = vnp->changed_oldTime = 0;
	vnp->delete = 0;
	vnp->vnodeNumber = vnodeNumber;
	vnp->volumePtr = vp;
	vnp->cacheCheck = vp->cacheCheck;
	vnp->nUsers = 1;

	/* This will never block */
	ObtainWriteLock(&vnp->lock);
#ifdef AFS_PTHREAD_ENV
	vnp->writer = pthread_self();
#else /* AFS_PTHREAD_ENV */
	  LWP_CurrentProcess(&vnp->writer);
#endif /* AFS_PTHREAD_ENV */

        /* Read vnode from volume index */
	VOL_UNLOCK
	fdP = IH_OPEN(ihP);
	if (fdP == NULL) {
	    Log("VGetVnode: can't open index dev=%d, i=%s\n",
		vp->device, PrintInode(NULL,
				       vp->vnodeIndex[class].handle->ih_ino));
	    *ec = VIO;
	    mlkReason=9;
	}
	else if (FDH_SEEK(fdP, vnodeIndexOffset(vcp, vnodeNumber),
			 SEEK_SET) < 0) {
	    Log ("VGetVnode: can't seek on index file vn=%d\n",vnodeNumber);
	    *ec = VIO;
	    mlkReason=10;
	    FDH_REALLYCLOSE(fdP);
	}
	else if ((n = FDH_READ(fdP, (char*)&vnp->disk, vcp->diskSize))
		 != vcp->diskSize) {
	    /* Don't take volume off line if the inumber is out of range
	       or the inode table is full. */
	    FDH_REALLYCLOSE(fdP);
	    VOL_LOCK
	    if(n == BAD_IGET) {
		Log("VGetVnode: bad inumber %s\n",
		    PrintInode(NULL, vp->vnodeIndex[class].handle->ih_ino));
		*ec = VIO;
		mlkReason = 4;
	    }
	    /* Check for disk errors.  Anything else just means that the vnode
	       is not allocated */
	    if (n == -1 && errno == EIO) {
		Log("VGetVnode: Couldn't read vnode %d, volume %u (%s); volume needs salvage\n",  
		    vnodeNumber, V_id(vp), V_name(vp));
		VForceOffline_r(vp);
		*ec = VSALVAGE;
		mlkReason = 4;
	    } else {
		mlkReason = 5;
		*ec = VIO;
	    }
	    VInvalidateVnode_r(vnp);
	    if (vnp->nUsers-- == 1)
		StickOnLruChain_r(vnp,vcp);
	    ReleaseWriteLock(&vnp->lock);
	    return NULL;
	}
	FDH_CLOSE(fdP);
	VOL_LOCK
        /* Quick check to see that the data is reasonable */
	if (vnp->disk.vnodeMagic != vcp->magic || vnp->disk.type == vNull) {
	    if (vnp->disk.type == vNull) {
		*ec = VNOVNODE;
		mlkReason = 6;
		VInvalidateVnode_r(vnp);
		if (vnp->nUsers-- == 1)
		    StickOnLruChain_r(vnp,vcp);
		ReleaseWriteLock(&vnp->lock);
		return NULL;	/* The vnode is not allocated */
	    }
	    else {
		struct vnodeIndex *index = &vp->vnodeIndex[class];
		unsigned int bitNumber = vnodeIdToBitNumber(vnodeNumber);
		unsigned int offset = bitNumber >> 3;

		/* Test to see if vnode number is valid. */
		if ((offset >= index->bitmapSize) ||
		    ((*(index->bitmap+offset) & (1<<(bitNumber&0x7))) == 0)) {
		    Log("VGetVnode: Request for unallocated vnode %u, volume %u (%s) denied.\n",
			vnodeNumber, V_id(vp), V_name(vp));
		    mlkReason = 11;
		    *ec = VNOVNODE;
		}
		else {
		    Log("VGetVnode: Bad magic number, vnode %d, volume %u (%s); volume needs salvage\n",  
		vnodeNumber, V_id(vp), V_name(vp));
		    vp->goingOffline = 1;	/* used to call VOffline, but that would mess
						   up the volume ref count if called here */
		    *ec = VSALVAGE;
		    mlkReason = 7;
		}
		VInvalidateVnode_r(vnp);
		if (vnp->nUsers-- == 1)
		    StickOnLruChain_r(vnp,vcp);
		ReleaseWriteLock(&vnp->lock);
		return NULL;
	    }
	}
	IH_INIT(vnp->handle, V_device(vp), V_parentId(vp), VN_GET_INO(vnp));
	ReleaseWriteLock(&vnp->lock);
    } else {
	VNLog(101, 2, vnodeNumber, (afs_int32) vnp);
	if (++vnp->nUsers == 1) {
	/* First user.  Remove it from the LRU chain.  We can assume that
	   there is at least one item in the queue */
	    if (vnp == vcp->lruHead)
		vcp->lruHead = vcp->lruHead->lruNext;
	    if (vnp == vcp->lruHead || vcp->lruHead == NULL)
		Abort("VGetVnode: lru chain addled!\n");
	    vnp->lruPrev->lruNext = vnp->lruNext;
	    vnp->lruNext->lruPrev = vnp->lruPrev;
	}
    }
    VOL_UNLOCK
    if (locktype == READ_LOCK)
	ObtainReadLock(&vnp->lock);
    else {
	ObtainWriteLock(&vnp->lock);
#ifdef AFS_PTHREAD_ENV
	vnp->writer = pthread_self();
#else /* AFS_PTHREAD_ENV */
        LWP_CurrentProcess(&vnp->writer);
#endif /* AFS_PTHREAD_ENV */
    }
    VOL_LOCK
    /* Check that the vnode hasn't been removed while we were obtaining
       the lock */
    VNLog(102, 2, vnodeNumber, (afs_int32) vnp);
    if ((vnp->disk.type == vNull) || (vnp->cacheCheck == 0)){
	if (vnp->nUsers-- == 1)
	    StickOnLruChain_r(vnp,vcp);
	if (locktype == READ_LOCK)
	    ReleaseReadLock(&vnp->lock);
	else
	    ReleaseWriteLock(&vnp->lock);
	*ec = VNOVNODE;
	mlkReason = 8;
	/* vnode is labelled correctly by now, so we don't have to invalidate it */
	return NULL;
    }
    if (programType == fileServer)
        VBumpVolumeUsage_r(vnp->volumePtr);/* Hack; don't know where it should be
					      called from.  Maybe VGetVolume */
    return vnp;
}


int  TrustVnodeCacheEntry = 1;
/* This variable is bogus--when it's set to 0, the hash chains fill
   up with multiple versions of the same vnode.  Should fix this!! */
void
VPutVnode(ec,vnp)
    Error *ec;
    register Vnode *vnp;
{
    VOL_LOCK
    VPutVnode_r(ec,vnp);
    VOL_UNLOCK
}

void
VPutVnode_r(ec,vnp)
    Error *ec;
    register Vnode *vnp;
{
    int writeLocked, offset;
    VnodeClass class;
    struct VnodeClassInfo *vcp;
    int code;

    *ec = 0;
    assert (vnp->nUsers != 0);
    class = vnodeIdToClass(vnp->vnodeNumber);
    vcp = &VnodeClassInfo[class];
    assert(vnp->disk.vnodeMagic == vcp->magic);
    VNLog(200, 2, vnp->vnodeNumber, (afs_int32) vnp);

    writeLocked = WriteLocked(&vnp->lock);
    if (writeLocked) {
#ifdef AFS_PTHREAD_ENV
	pthread_t thisProcess = pthread_self();
#else /* AFS_PTHREAD_ENV */
	PROCESS thisProcess;
	LWP_CurrentProcess(&thisProcess);
#endif /* AFS_PTHREAD_ENV */
	VNLog(201, 2, (afs_int32) vnp,
	      ((vnp->changed_newTime) << 1) | ((vnp->changed_oldTime) << 1) | vnp->delete);
	if (thisProcess != vnp->writer)
	    Abort("VPutVnode: Vnode at 0x%x locked by another process!\n",vnp);
	if (vnp->changed_oldTime || vnp->changed_newTime || vnp->delete) {
	    Volume *vp = vnp->volumePtr;
	    afs_uint32 now = FT_ApproxTime();
	    assert(vnp->cacheCheck == vp->cacheCheck);

	    if (vnp->delete) {
	        /* No longer any directory entries for this vnode. Free the Vnode */
		memset(&vnp->disk, 0, sizeof (vnp->disk));
		mlkLastDelete = vnp->vnodeNumber;
		/* delete flag turned off further down */
		VNLog(202, 2, vnp->vnodeNumber, (afs_int32) vnp);
	    } else if (vnp->changed_newTime) {
	        vnp->disk.serverModifyTime = now;
	    }
	    if (vnp->changed_newTime)
		V_updateDate(vp) = vp->updateTime = now;

	    /* The vnode has been changed. Write it out to disk */
   	    if (!V_inUse(vp)) {
		assert(V_needsSalvaged(vp));
		*ec = VSALVAGE;
	    } else {
		IHandle_t *ihP = vp->vnodeIndex[class].handle;
		FdHandle_t *fdP;
		VOL_UNLOCK
		fdP = IH_OPEN(ihP);
		if (fdP == NULL)
		    Abort("VPutVnode: can't open index file!\n");
		offset = vnodeIndexOffset(vcp, vnp->vnodeNumber);
		if (FDH_SEEK(fdP, offset, SEEK_SET) < 0) {
		   Abort("VPutVnode: can't seek on index file! fdp=0x%x offset=%d, errno=%d\n",
			 fdP, offset, errno);
		}
		code = FDH_WRITE(fdP, &vnp->disk, vcp->diskSize);
		if (code != vcp->diskSize) {
		    /* Don't force volume offline if the inumber is out of
		     * range or the inode table is full.
		     */
		    VOL_LOCK
		    if (code == BAD_IGET) {
			Log("VPutVnode: bad inumber %s\n",
			    PrintInode(NULL, vp->vnodeIndex[class].handle->ih_ino));
			*ec = VIO;
		    } else {
			Log("VPutVnode: Couldn't write vnode %d, volume %u (%s) (error %d)\n",
			    vnp->vnodeNumber, V_id(vnp->volumePtr),
			    V_name(vnp->volumePtr), code);
			VForceOffline_r(vp);
			*ec = VSALVAGE;
		    }
		    VOL_UNLOCK
		    FDH_REALLYCLOSE(fdP);
		} else {
		    FDH_CLOSE(fdP);
		}
	        VOL_LOCK

		/* If the vnode is to be deleted, and we wrote the vnode out,
		 * free its bitmap entry. Do after the vnode is written so we
		 * don't allocate from bitmap before the vnode is written
		 * (doing so could cause a "addled bitmap" message).
		 */
		if (vnp->delete && !*ec) {
		    VFreeBitMapEntry_r(ec, &vp->vnodeIndex[class],
				       vnodeIdToBitNumber(vnp->vnodeNumber));
		}
	    }
	    vcp->writes++;
	    vnp->changed_newTime = vnp->changed_oldTime = 0;
	}
    } else { /* Not write locked */
	if (vnp->changed_newTime || vnp->changed_oldTime || vnp->delete)
	    Abort("VPutVnode: Change or delete flag for vnode 0x%x is set but vnode is not write locked!\n", vnp);
    }

    /* Do not look at disk portion of vnode after this point; it may
       have been deleted above */
    if (vnp->nUsers-- == 1)
	StickOnLruChain_r(vnp,vcp);
    vnp->delete = 0;

    if (writeLocked)
        ReleaseWriteLock(&vnp->lock);
    else
        ReleaseReadLock(&vnp->lock);
}

/*
 * Make an attempt to convert a vnode lock from write to read.
 * Do nothing if the vnode isn't write locked or the vnode has
 * been deleted.
 */
int VVnodeWriteToRead(ec,vnp)
    Error *ec;
    register Vnode *vnp;
{
    int retVal;
    VOL_LOCK
    retVal = VVnodeWriteToRead_r(ec, vnp);
    VOL_UNLOCK
    return retVal;
}

int VVnodeWriteToRead_r(ec,vnp)
    Error *ec;
    register Vnode *vnp;
{
    int writeLocked;
    VnodeClass class;
    struct VnodeClassInfo *vcp;
    int code;
#ifdef AFS_PTHREAD_ENV
    pthread_t thisProcess;
#else /* AFS_PTHREAD_ENV */
    PROCESS thisProcess;
#endif /* AFS_PTHREAD_ENV */

    *ec = 0;
    assert (vnp->nUsers != 0);
    class = vnodeIdToClass(vnp->vnodeNumber);
    vcp = &VnodeClassInfo[class];
    assert(vnp->disk.vnodeMagic == vcp->magic);
    writeLocked = WriteLocked(&vnp->lock);
    VNLog(300, 2, vnp->vnodeNumber, (afs_int32) vnp);

    if (!writeLocked) {
	return 0;
    }

#ifdef AFS_PTHREAD_ENV
    thisProcess = pthread_self();
#else /* AFS_PTHREAD_ENV */
    LWP_CurrentProcess(&thisProcess);
#endif /* AFS_PTHREAD_ENV */

    VNLog(301, 2, (afs_int32) vnp,
	  ((vnp->changed_newTime) << 1) | ((vnp->changed_oldTime) << 1) |
	  vnp->delete);
    if (thisProcess != vnp->writer)
	Abort("VPutVnode: Vnode at 0x%x locked by another process!\n",vnp);
    if (vnp->delete) {
	return 0;
    }
    if (vnp->changed_oldTime || vnp->changed_newTime) {
	Volume *vp = vnp->volumePtr;
	afs_uint32 now = FT_ApproxTime();
	assert(vnp->cacheCheck == vp->cacheCheck);
	if (vnp->changed_newTime)
	    vnp->disk.serverModifyTime = now;
	if (vnp->changed_newTime)
	    V_updateDate(vp) = vp->updateTime = now;

	/* The inode has been changed.  Write it out to disk */
	if (!V_inUse(vp)) {
	    assert(V_needsSalvaged(vp));
	    *ec = VSALVAGE;
	} else {
	    IHandle_t *ihP = vp->vnodeIndex[class].handle;
	    FdHandle_t *fdP;
	    off_t off = vnodeIndexOffset(vcp, vnp->vnodeNumber);
	    VOL_UNLOCK
	    fdP = IH_OPEN(ihP);
	    if (fdP == NULL)
		Abort("VPutVnode: can't open index file!\n");
	    code = FDH_SEEK(fdP, off, SEEK_SET);
	    if (code < 0)
		Abort("VPutVnode: can't seek on index file!\n");
	    code = FDH_WRITE(fdP, &vnp->disk, vcp->diskSize);
	    if (code != vcp->diskSize) {
		/*
		 * Don't force volume offline if the inumber is out of
		 * range or the inode table is full.
		 */
		VOL_LOCK
		if(code == BAD_IGET)
		{
			    Log("VPutVnode: bad inumber %d\n",
				    vp->vnodeIndex[class].handle->ih_ino);
			    *ec = VIO;
		} else {
		    Log("VPutVnode: Couldn't write vnode %d, volume %u (%s)\n",
			vnp->vnodeNumber, V_id(vnp->volumePtr),
			V_name(vnp->volumePtr));
			VForceOffline_r(vp);
		    *ec = VSALVAGE;
		}
		VOL_UNLOCK
	    }
	    FDH_CLOSE(fdP);
	    VOL_LOCK
	}
	vcp->writes++;
	vnp->changed_newTime = vnp->changed_oldTime = 0;
    }

    ConvertWriteToReadLock(&vnp->lock);
    return 0;
}

/* Move the vnode, vnp, to the new hash table given by the
   hash table index, newHash */
static int moveHash(vnp, newHash)
    register Vnode *vnp;
    bit32 newHash;
{
    Vnode *tvnp;
 /* Remove it from the old hash chain */
    tvnp = VnodeHashTable[vnp->hashIndex];
    if (tvnp == vnp)
	VnodeHashTable[vnp->hashIndex] = vnp->hashNext;
    else {
	while (tvnp && tvnp->hashNext != vnp)
	    tvnp = tvnp->hashNext;
	if (tvnp)
	    tvnp->hashNext = vnp->hashNext;
    }
 /* Add it to the new hash chain */
    vnp->hashNext = VnodeHashTable[newHash];
    VnodeHashTable[newHash] = vnp;
    vnp->hashIndex = newHash;
    return 0;
}

void
StickOnLruChain_r(vnp,vcp)
    register Vnode *vnp;
    register struct VnodeClassInfo *vcp;
{
 /* Add it to the circular LRU list */
    if (vcp->lruHead == NULL)
	Abort("VPutVnode: vcp->lruHead==NULL");
    else {
	vnp->lruNext = vcp->lruHead;
	vnp->lruPrev = vcp->lruHead->lruPrev;
	vcp->lruHead->lruPrev = vnp;
	vnp->lruPrev->lruNext = vnp;
	vcp->lruHead = vnp;
    }
 /* If the vnode was just deleted, put it at the end of the chain so it
    will be reused immediately */
    if (vnp->delete)
	vcp->lruHead = vnp->lruNext;
 /* If caching is turned off, set volumeptr to NULL to invalidate the
    entry */
    if (!TrustVnodeCacheEntry)
	vnp->volumePtr = NULL;
}

/* VCloseVnodeFiles - called when a volume is going off line. All open
 * files for vnodes in that volume are closed. This might be excessive,
 * since we may only be taking one volume of a volume group offline.
 */
void VCloseVnodeFiles_r(Volume *vp)
{
    int i;
    Vnode *vnp;

    for (i=0; i<VNODE_HASH_TABLE_SIZE; i++) {
	for (vnp = VnodeHashTable[i]; vnp; vnp = vnp->hashNext) {
	    if (vnp->volumePtr == vp) {
		IH_REALLYCLOSE(vnp->handle);
	    }
	}
    }
}

/* VReleaseVnodeFiles - called when a volume is going detached. All open
 * files for vnodes in that volume are closed and all inode handles
 * for vnodes in that volume are released.
 */
void VReleaseVnodeFiles_r(Volume *vp)
{
    int i;
    Vnode *vnp;

    for (i=0; i<VNODE_HASH_TABLE_SIZE; i++) {
	for (vnp = VnodeHashTable[i]; vnp; vnp = vnp->hashNext) {
	    if (vnp->volumePtr == vp) {
		IH_RELEASE(vnp->handle);
	    }
	}
    }
}
