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
	Module:		partition.h
	Institution:	The Information Technology Center, Carnegie-Mellon University

 */

#include <afs/param.h>
#include "nfs.h"
#if	defined(AFS_HPUX_ENV)
#define	AFS_DSKDEV	"/dev/dsk"
#define	AFS_RDSKDEV	"/dev/rdsk/"
#define	AFS_LVOLDEV	"/dev/vg0"
#define	AFS_ACVOLDEV	"/dev/ac"
#define	AFS_RACVOLDEV	"/dev/ac/r"
#else
#define	AFS_DSKDEV	"/dev"
#define	AFS_RDSKDEV	"/dev/r"
#endif

/* All Vice partitions on a server will have the following name prefix */
#define VICE_PARTITION_PREFIX	"/vicep"
#define VICE_PREFIX_SIZE	(sizeof(VICE_PARTITION_PREFIX)-1)

/* If a file by this name exists in a /vicepX directory, it means that
 * this directory should be used as an AFS partition even if it's not
 * on a separate partition (for instance if it's part of a large /).
 * This feature only works with the NAMEI fileserver.
 */
#ifdef AFS_NAMEI_ENV
#define VICE_ALWAYSATTACH_FILE	"AlwaysAttach"
#endif

/* For NT, the roles of "name" and "devName" are reversed. That is, "name"
 * refers to the drive letter name and "devName" refers to the /vicep style
 * or name. The reason for this is that a lot of places assume that "name"
 * is the right thing to use to access the partition. Silly of them isn't it?
 * The NT version of VInitPartition does the intial setup. There is an NT
 * variant for VGetPartition as well. Also, the VolPartitionInfo RPC does
 * a swap before sending the data out on the wire.
 */
struct DiskPartition {
    struct DiskPartition *next;
    char	*name;		/* Mounted partition name */
    char	*devName;	/* Device mounted on */
    Device	device;		/* device number */
    int		lock_fd;	/* File descriptor of this partition if locked; otherwise -1;
    				   Not used by the file server */
    int		free;		/* Total number of blocks (1K) presumed
				   available on this partition (accounting
				   for the minfree parameter for the
				   partition).  This is adjusted
				   approximately by the sizes of files
				   and directories read/written, and
				   periodically the superblock is read and
				   this is recomputed.  This number can
				   be negative, if the partition starts
				   out too full */
    int		totalUsable;	/* Total number of blocks available on this
    				   partition, taking into account the minfree
				   parameter for the partition (see the
				   4.2bsd command tunefs, but note that the
				   bug mentioned there--that the superblock
				   is not reread--does not apply here.  The
				   superblock is re-read periodically by
				   VSetPartitionDiskUsage().) */
    int		minFree;	/* Number blocks to be kept free, as last read
    				   from the superblock */
    int		flags;
    int 	f_files;	/* total number of files in this partition */
};
#define	PART_DONTUPDATE	1
#define PART_DUPLICATE  2 /* NT - used if we find more than one partition 
			   * using the same drive. Will be dumped before
			   * all partitions attached.
			   */

#ifdef AFS_NT40_ENV
#include <WINNT/vptab.h>
extern int VValidVPTEntry(struct vptab *vptp);
#endif


extern struct DiskPartition *DiskPartitionList;
extern struct DiskPartition *VGetPartition();
extern int VAttachPartitions(void);
extern void VLockPartition(char *name);
extern void VLockPartition_r(char *name);
extern void VUnlockPartition(char *name);
extern void VUnlockPartition_r(char *name);
extern void VResetDiskUsage(void);
extern void VResetDiskUsage_r(void);
extern void VSetPartitionDiskUsage(register struct DiskPartition *dp);
extern void VSetPartitionDiskUsage_r(register struct DiskPartition *dp);
extern char *VPartitionPath(struct DiskPartition *p);
/*extern void VAdjustDiskUsage(Error *ec, Volume *vp, afs_int32 blocks,
			     afs_int32 checkBlocks); */
extern void VPrintDiskStats(void);

