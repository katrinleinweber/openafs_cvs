/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

/*
  *	    Information Technology Center
  *	    October 1987
  *
  * Description:
  * 	The Andrew File System startup process.  It is responsible for
  * reading and parsing the various configuration files, starting up
  * the kernel processes required by AFS and feeding the configuration
  * information to the kernel.
  *
  * Recognized flags are:
  *	-blocks	    The number of blocks available in the workstation cache.
  *	-files	    The target number of files in the workstation cache (Default:
  *		    1000).
  *	-rootvol	    The name of the root volume to use.
  *	-stat	    The number of stat cache entries.
  *	-hosts	    List of servers to check for volume location info FOR THE
  *		    HOME CELL.
  *     -memcache   Use an in-memory cache rather than disk.
  *	-cachedir    The base directory for the workstation cache.
  *	-mountdir   The directory on which the AFS is to be mounted.
  *	-confdir    The configuration directory .
  *	-nosettime  Don't keep checking the time to avoid drift.
  *	-verbose     Be chatty.
  *	-debug	   Print out additional debugging info.
  *	-kerndev    [OBSOLETE] The kernel device for AFS.
  *	-dontfork   [OBSOLETE] Don't fork off as a new process.
  *	-daemons   The number of background daemons to start (Default: 2).
  *	-rmtsys	   Also fires up an afs remote sys call (e.g. pioctl, setpag)
  *                support daemon 
  *     -chunksize [n]   2^n is the chunksize to be used.  0 is default.
  *     -dcache    The number of data cache entries.
  *     -biods     Number of bkg I/O daemons (AIX3.1 only)
  *	-prealloc  Number of preallocated "small" memory blocks
  *     -pininodes Number of inodes which can be spared from inode[] for 
  *                pointing at Vfiles.  If this is set too high, you may have
  *                system problems, which can only be ameliorated by changing
  *                NINODE (or equivalent) and rebuilding the kernel.
  *		   This option is now disabled.
  *	-logfile   Place where to put the logfile (default in <cache>/etc/AFSLog.
  *	-waitclose make close calls always synchronous (slows em down, tho)
  *	-shutdown  Shutdown afs daemons
  *---------------------------------------------------------------------------*/

#define VFS 1

#include <afs/param.h>
#include <afs/cmd.h>

#include <assert.h>
#include <itc.h>
#include <afs/afsutil.h>
#undef in
#undef out
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <errno.h>
#include <sys/time.h>
#ifdef	AFS_DEC_ENV
#include <sys/param.h>
#include <sys/fs_types.h>
#endif
#if	defined(AFS_SUN_ENV)
#include <sys/vfs.h>
#endif
#ifndef	AFS_AIX41_ENV
#include <sys/mount.h>
#endif
#include <dirent.h>
#ifdef	  AFS_SUN5_ENV
#include <sys/fcntl.h>
#include <sys/mnttab.h>
#include <sys/mntent.h>
#else
#if	defined(AFS_SUN_ENV) || defined(AFS_SGI_ENV) || defined(AFS_HPUX_ENV) || defined(AFS_LINUX20_ENV)
#include <mntent.h>
#endif
#endif

#if defined(AFS_OSF_ENV) || defined(AFS_DEC_ENV)
#include <sys/mount.h>
#else
#include <sys/vfs.h>
#endif

#include <netinet/in.h>
#include <afs/afs_args.h>
#include <afs/cellconfig.h>
#include <afs/auth.h>
#include <ctype.h>
#include <afs/afssyscalls.h>
#include <afs/afsutil.h>

#ifdef AFS_SGI61_ENV
#include <unistd.h>
#include <libelf.h>
#include <dwarf.h>
#include <libdwarf.h>
void set_staticaddrs(void);
#endif /* AFS_SGI61_ENV */
#if defined(AFS_SGI62_ENV) && !defined(AFS_SGI65_ENV)
#include <sym.h>
#include <symconst.h>
#endif
#ifdef AFS_SGI65_ENV
#include <sched.h>
#endif
#ifdef AFS_LINUX20_ENV
#include <sys/resource.h>
#endif

#ifndef MOUNT_AFS
#define	MOUNT_AFS AFS_MOUNT_AFS
#endif /* MOUNT_AFS */

#if AFS_HAVE_STATVFS
#include <sys/statvfs.h>
#else
#if !defined(AFS_OSF_ENV)
#include <sys/statfs.h>
#endif
#endif

#undef	VIRTUE
#undef	VICE

#define CACHEINFOFILE   "cacheinfo"
#define	AFSLOGFILE	"AFSLog"
#define	DCACHEFILE	"CacheItems"
#define	VOLINFOFILE	"VolumeItems"

#define MAXIPADDRS 1024

char LclCellName[64];

#define MAX_CACHE_LOOPS 4

/*
 * Internet address (old style... should be updated).  This was pulled out of the old 4.2
 * version of <netinet/in.h>, since it's still useful.
 */
struct in_addr_42 {
	union {
		struct { u_char s_b1,s_b2,s_b3,s_b4; } S_un_b;
		struct { u_short s_w1,s_w2; } S_un_w;
		afs_uint32 S_addr;
	} S_un;
#define	s_host	S_un.S_un_b.s_b2	/* host on imp */
#define	s_net	S_un.S_un_b.s_b1	/* network */
#define	s_imp	S_un.S_un_w.s_w2	/* imp */
#define	s_impno	S_un.S_un_b.s_b4	/* imp # */
#define	s_lh	S_un.S_un_b.s_b3	/* logical host */
};

#define	mPrintIPAddr(ipaddr)  printf("[%d.%d.%d.%d] ",		\
      ((struct in_addr_42 *) &(ipaddr))->S_un.S_un_b.s_b1,	\
      ((struct in_addr_42 *) &(ipaddr))->S_un.S_un_b.s_b2,	\
      ((struct in_addr_42 *) &(ipaddr))->S_un.S_un_b.s_b3,	\
      ((struct in_addr_42 *) &(ipaddr))->S_un.S_un_b.s_b4)

/*
 * Global configuration variables.
 */
afs_int32 afs_shutdown = 0;
afs_int32 cacheBlocks;			/*Num blocks in the cache*/
afs_int32 cacheFiles	= 1000;			/*Optimal # of files in workstation cache*/
afs_int32 cacheStatEntries =	300;		/*Number of stat cache entries*/
char cacheBaseDir[1024];		/*Where the workstation AFS cache lives*/
char confDir[1024];			/*Where the workstation AFS configuration lives*/
char fullpn_DCacheFile[1024];		/*Full pathname of DCACHEFILE*/
char fullpn_VolInfoFile[1024];		/*Full pathname of VOLINFOFILE*/
char fullpn_AFSLogFile[1024];		/*Full pathname of AFSLOGFILE*/
char fullpn_CacheInfo[1024];		/*Full pathname of CACHEINFO*/
char fullpn_VFile[1024];		/*Full pathname of data cache files*/
char *vFileNumber;			/*Ptr to the number part of above pathname*/
int sawCacheMountDir = 0;		/* from cmd line */
int sawCacheBaseDir = 0;
int sawCacheBlocks = 0;
int sawDCacheSize = 0;
int sawBiod = 0;
char cacheMountDir[1024];		/*Mount directory for AFS*/
char rootVolume[64] = "root.afs";	/*AFS root volume name*/
afs_int32 cacheSetTime = 1;			/*Keep checking time to avoid drift?*/
afs_int32 isHomeCell;			/*Is current cell info for the home cell?*/
afs_int32 lookingForHomeCell;		/*Are we still looking for the home cell?*/
int createAndTrunc = O_CREAT | O_TRUNC; /*Create & truncate on open*/
int ownerRWmode	= 0600;			/*Read/write OK by owner*/
static int filesSet = 0;		/*True if number of files explicitly set*/
static int nDaemons = 2;		/* Number of background daemons */
static int chunkSize = 0;               /* 2^chunkSize bytes per chunk */
static int dCacheSize = 300;            /* # of dcache entries */
static int vCacheSize = 50;             /* # of volume cache entries */
static int rootVolSet =	0;		/*True if root volume name explicitly set*/
int addrNum;				/*Cell server address index being printed*/
static int cacheFlags = 0;              /*Flags to cache manager*/
static int nBiods = 5;			/* AIX3.1 only */
static int preallocs = 400;		/* Def # of allocated memory blocks */
static int enable_peer_stats = 0;	/* enable rx stats */
static int enable_process_stats = 0;	/* enable rx stats */
#ifdef notdef
static int inodes = 60;		        /* VERY conservative, but has to be */
#endif
int afsd_verbose = 0;			/*Are we being chatty?*/
int afsd_debug = 0;			/*Are we printing debugging info?*/
int afsd_CloseSynch = 0;		/*Are closes synchronous or not? */

#ifdef AFS_SGI62_ENV
#define AFSD_INO_T ino64_t
#else
#define AFSD_INO_T afs_uint32
#endif
AFSD_INO_T *inode_for_V;		/* Array of inodes for desired
					 * cache files */
int missing_DCacheFile	= 1;		/*Is the DCACHEFILE missing?*/
int missing_VolInfoFile	= 1;		/*Is the VOLINFOFILE missing?*/
int afsd_rmtsys = 0;				/* Default: don't support rmtsys */
struct afs_cacheParams cparams;          /* params passed to cache manager */

static int HandleMTab();

/* ParseArgs is now obsolete, being handled by cmd */

/*------------------------------------------------------------------------------
  * ParseCacheInfoFile
  *
  * Description:
  *	Open the file containing the description of the workstation's AFS cache
  *	and pull out its contents.  The format of this file is as follows:
  *
  *	    cacheMountDir:cacheBaseDir:cacheBlocks
  *
  * Arguments:
  *	None.
  *
  * Returns:
  *	0 if everything went well,
  *	1 otherwise.
  *
  * Environment:
  *	Nothing interesting.
  *
  *  Side Effects:
  *	Sets globals.
  *---------------------------------------------------------------------------*/

int ParseCacheInfoFile()
{
    static char rn[]="ParseCacheInfoFile";	/*This routine's name*/
    FILE *cachefd;				/*Descriptor for cache info file*/
    int	parseResult;				/*Result of our fscanf()*/
    afs_int32 tCacheBlocks;
    char tCacheBaseDir[1024], *tbd, tCacheMountDir[1024], *tmd;

    if (afsd_debug)
	printf("%s: Opening cache info file '%s'...\n",
	    rn, fullpn_CacheInfo);

    cachefd = fopen(fullpn_CacheInfo, "r");
    if (!cachefd) {
	printf("%s: Can't read cache info file '%s'\n",
	       rn, fullpn_CacheInfo);
        return(1);
    }

    /*
     * Parse the contents of the cache info file.  All chars up to the first
     * colon are the AFS mount directory, all chars to the next colon are the
     * full path name of the workstation cache directory and all remaining chars
     * represent the number of blocks in the cache.
     */
    tCacheMountDir[0] = tCacheBaseDir[0] = '\0';
    parseResult = fscanf(cachefd,
			  "%1024[^:]:%1024[^:]:%d",
			  tCacheMountDir, tCacheBaseDir, &tCacheBlocks);

    /*
     * Regardless of how the parse went, we close the cache info file.
     */
    fclose(cachefd);

    if (parseResult == EOF || parseResult < 3) {
	printf("%s: Format error in cache info file!\n",
	       rn);
	if (parseResult == EOF)
	    printf("\tEOF encountered before any field parsed.\n");
	else
	    printf("\t%d out of 3 fields successfully parsed.\n",
		   parseResult);

	printf("\tcacheMountDir: '%s'\n\tcacheBaseDir: '%s'\n\tcacheBlocks: %d\n",
	       cacheMountDir, cacheBaseDir, cacheBlocks);
	return(1);
    }

    for (tmd = tCacheMountDir; *tmd == '\n' || *tmd == ' ' || *tmd == '\t'; tmd++) ;
    for (tbd = tCacheBaseDir; *tbd == '\n' || *tbd == ' ' || *tbd == '\t'; tbd++) ;
    /* now copy in the fields not explicitly overridden by cmd args */
    if (!sawCacheMountDir) 
	strcpy(cacheMountDir, tmd);
    if (!sawCacheBaseDir)
	strcpy(cacheBaseDir, tbd);
    if  (!sawCacheBlocks)
	cacheBlocks = tCacheBlocks;

    if (afsd_debug) {
	printf("%s: Cache info file successfully parsed:\n",
	       rn);
	printf("\tcacheMountDir: '%s'\n\tcacheBaseDir: '%s'\n\tcacheBlocks: %d\n",
	       tmd, tbd, tCacheBlocks);
    }
    if (! (cacheFlags & AFSCALL_INIT_MEMCACHE))
	PartSizeOverflow(tbd, cacheBlocks);

    return(0);
}

/*
 * All failures to open the partition are ignored. Also if the cache dir 
 * isn't a mounted partition it's also ignored since we can't guarantee 
 * what will be stored afterwards. Too many if's. This is now purely
 * advisory. ODS with over 2G partition also gives warning message.
 */
PartSizeOverflow(path, cs)
char *path;
int cs;
{
    int bsize=-1, totalblks, mint;
#if AFS_HAVE_STATVFS
    struct statvfs statbuf;

    if (statvfs(path, &statbuf) != 0) {  
	if (afsd_debug) printf("statvfs failed on %s; skip checking for adequate partition space\n", path);
	return 0;
    }
    totalblks = statbuf.f_blocks;
    bsize = statbuf.f_frsize;
#else
    struct statfs statbuf;

    if (statfs(path, &statbuf) < 0) {
	if (afsd_debug) printf("statfs failed on %s; skip checking for adequate partition space\n", path);
	return 0;
    }
    totalblks = statbuf.f_blocks;
    bsize = statbuf.f_bsize;
#endif
    if (bsize == -1) return 0;	/* sucess */

    /* now free and totalblks are in fragment units, but we want them in 1K units */
    if (bsize >= 1024) {
	totalblks *= (bsize / 1024);
    } else {
	totalblks /= (1024/bsize);
    }

    mint = totalblks - ((totalblks * 5) / 100);
    if (cs > mint) {
	printf("Cache size (%d) must be less than 95%% of partition size (which is %d). Lower cache size\n", 
	       cs, mint);
    } 
}

/*-----------------------------------------------------------------------------
  * GetVFileNumber
  *
  * Description:
  *	Given the final component of a filename expected to be a data cache file,
  *	return the integer corresponding to the file.  Note: we reject names that
  *	are not a ``V'' followed by an integer.  We also reject those names having
  *	the right format but lying outside the range [0..cacheFiles-1].
  *
  * Arguments:
  *	fname : Char ptr to the filename to parse.
  *
  * Returns:
  *	>= 0 iff the file is really a data cache file numbered from 0 to cacheFiles-1, or
  *	-1      otherwise.
  *
  * Environment:
  *	Nothing interesting.
  *
  * Side Effects:
  *	None.
  *---------------------------------------------------------------------------*/

int GetVFileNumber(fname)
    char *fname;
{
    int	computedVNumber;    /*The computed file number we return*/
    int	filenameLen;	    /*Number of chars in filename*/
    int	currDigit;	    /*Current digit being processed*/

    /*
     * The filename must have at least two characters, the first of which must be a ``V''
     * and the second of which cannot be a zero unless the file is exactly two chars long.
     */
    filenameLen = strlen(fname);
    if (filenameLen < 2)
	return(-1);
    if (fname[0] != 'V')
	return(-1);
    if ((filenameLen > 2) && (fname[1] == '0'))
	return(-1);

    /*
     * Scan through the characters in the given filename, failing immediately if a non-digit
     * is found.
     */
    for (currDigit = 1; currDigit < filenameLen; currDigit++)
	if (isdigit(fname[currDigit]) == 0)
	    return(-1);

    /*
     * All relevant characters are digits.  Pull out the decimal number they represent.
     * Reject it if it's out of range, otherwise return it.
     */
    computedVNumber = atoi(++fname);
    if (computedVNumber < cacheFiles)
	return(computedVNumber);
    else
	return(-1);
}

/*-----------------------------------------------------------------------------
  * CreateCacheFile
  *
  * Description:
  *	Given a full pathname for a file we need to create for the workstation AFS
  *	cache, go ahead and create the file.
  *
  * Arguments:
  *	fname : Full pathname of file to create.
  *
  * Returns:
  *	0   iff the file was created,
  *	-1  otherwise.
  *
  * Environment:
  *	The given cache file has been found to be missing.
  *
  * Side Effects:
  *	As described.
  *---------------------------------------------------------------------------*/

int CreateCacheFile(fname)
    char *fname;
{
    static char	rn[] = "CreateCacheFile";   /*Routine name*/
    int	cfd;				    /*File descriptor to AFS cache file*/
    int	closeResult;			    /*Result of close()*/

    if (afsd_verbose)
	printf("%s: Creating cache file '%s'\n",
	       rn, fname);
    cfd = open(fname, createAndTrunc, ownerRWmode);
    if	(cfd <=	0) {
	printf("%s: Can't create '%s', error return is %d (%d)\n",
	       rn, fname, cfd, errno);
	return(-1);
    }
    closeResult = close(cfd);
    if	(closeResult) {
	printf("%s: Can't close newly-created AFS cache file '%s' (code %d)\n",
	       rn, fname, errno);
	return(-1);
    }

    return(0);
}

/*-----------------------------------------------------------------------------
  * SweepAFSCache
  *
  * Description:
  *	Sweep through the AFS cache directory, recording the inode number for
  *	each valid data cache file there.  Also, delete any file that doesn't beint32
  *	in the cache directory during this sweep, and remember which of the other
  *	residents of this directory were seen.  After the sweep, we create any data
  *	cache files that were missing.
  *
  * Arguments:
  *	vFilesFound : Set to the number of data cache files found.
  *
  * Returns:
  *	0   if everything went well,
  *	-1 otherwise.
  *
  * Environment:
  *	This routine may be called several times.  If the number of data cache files
  *	found is less than the global cacheFiles, then the caller will need to call it
  *	again to record the inodes of the missing zero-length data cache files created
  *	in the previous call.
  *
  * Side Effects:
  *	Fills up the global inode_for_V array, may create and/or delete files as
  *	explained above.
  *---------------------------------------------------------------------------*/

int SweepAFSCache(vFilesFound)
    int *vFilesFound;
{
    static char	rn[] = "SweepAFSCache";	/*Routine name*/
    char fullpn_FileToDelete[1024];	/*File to be deleted from cache*/
    char *fileToDelete;			/*Ptr to last component of above*/
    DIR	*cdirp;				/*Ptr to cache directory structure*/
#ifdef AFS_SGI62_ENV
    struct dirent64 *currp;		/*Current directory entry*/
#else
    struct dirent *currp;		/*Current directory entry*/
#endif
    int	vFileNum;			/*Data cache file's associated number*/

    if (cacheFlags & AFSCALL_INIT_MEMCACHE) {
	if (afsd_debug)
	    printf("%s: Memory Cache, no cache sweep done\n", rn);
	*vFilesFound = 0;
	return 0;
    }

    if (afsd_debug)
	printf("%s: Opening cache directory '%s'\n",
	       rn, cacheBaseDir);

    if (chmod(cacheBaseDir, 0700)) {		/* force it to be 700 */
	printf("%s: Can't 'chmod 0700' the cache dir, '%s'.\n",
	       rn, cacheBaseDir);
	return (-1);
    }
    cdirp = opendir(cacheBaseDir);
    if (cdirp == (DIR *)0) {
	printf("%s: Can't open AFS cache directory, '%s'.\n",
	       rn, cacheBaseDir);
	return(-1);
    }

    /*
     * Scan the directory entries, remembering data cache file inodes and the existance
     * of other important residents.  Delete all files that don't belong here.
     */
    *vFilesFound = 0;
    sprintf(fullpn_FileToDelete, "%s/", cacheBaseDir);
    fileToDelete = fullpn_FileToDelete + strlen(fullpn_FileToDelete);

#ifdef AFS_SGI62_ENV
    for (currp = readdir64(cdirp); currp; currp = readdir64(cdirp))
#else
    for (currp = readdir(cdirp); currp; currp = readdir(cdirp))
#endif
	{
	if (afsd_debug) {
	    printf("%s: Current directory entry:\n",
		   rn);
#ifdef AFS_SGI62_ENV
	    printf("\tinode=%lld, reclen=%d, name='%s'\n",
		   currp->d_ino, currp->d_reclen, currp->d_name);
#else
	    printf("\tinode=%d, reclen=%d, name='%s'\n",
		   currp->d_ino, currp->d_reclen, currp->d_name);
#endif
	}

	/*
	 * Guess current entry is for a data cache file.
	 */
	vFileNum = GetVFileNumber(currp->d_name);
	if (vFileNum >= 0) {
	    /*
	     * Found a valid data cache filename.  Remember this file's inode and bump
	     * the number of files found.
	     */
	    inode_for_V[vFileNum] = currp->d_ino;
	    (*vFilesFound)++;
	}
	else if (strcmp(currp->d_name, DCACHEFILE) == 0) {
	    /*
	     * Found the file holding the dcache entries.
	     */
	    missing_DCacheFile = 0;
	}
	else if (strcmp(currp->d_name, VOLINFOFILE) == 0) {
	    /*
	     * Found the file holding the volume info.
	     */
	    missing_VolInfoFile = 0;
	}
	else  if ((strcmp(currp->d_name,          ".") == 0) ||
	          (strcmp(currp->d_name,         "..") == 0) ||
		  (strcmp(currp->d_name, "lost+found") == 0)) {
	    /*
	     * Don't do anything - this file is legit, and is to be left alone.
	     */
	}
	else {
	    /*
	     * This file doesn't belong in the cache.  Nuke it.
	     */
	    sprintf(fileToDelete, "%s", currp->d_name);
	    if (afsd_verbose)
		printf("%s: Deleting '%s'\n",
		       rn, fullpn_FileToDelete);
	    if (unlink(fullpn_FileToDelete)) {
		printf("%s: Can't unlink '%s', errno is %d\n",
		       rn, fullpn_FileToDelete, errno);
	    }
	}
    }

    /*
     * Create all the cache files that are missing.
     */
    if (missing_DCacheFile) {
	if (afsd_verbose)
	    printf("%s: Creating '%s'\n",
		   rn, fullpn_DCacheFile);
	if (CreateCacheFile(fullpn_DCacheFile))
	    printf("%s: Can't create '%s'\n",
		   rn, fullpn_DCacheFile);
    }
    if (missing_VolInfoFile) {
	if (afsd_verbose)
	    printf("%s: Creating '%s'\n",
		   rn, fullpn_VolInfoFile);
	if (CreateCacheFile(fullpn_VolInfoFile))
	    printf("%s: Can't create '%s'\n",
		   rn, fullpn_VolInfoFile);
    }

    if (*vFilesFound < cacheFiles) {
	/*
	 * We came up short on the number of data cache files found.  Scan through the inode
	 * list and create all missing files.
	 */
	for (vFileNum = 0; vFileNum < cacheFiles; vFileNum++)
	    if (inode_for_V[vFileNum] == (AFSD_INO_T)0) {
		sprintf(vFileNumber, "%d", vFileNum);
		if (afsd_verbose)
		    printf("%s: Creating '%s'\n",
			   rn, fullpn_VFile);
		if (CreateCacheFile(fullpn_VFile))
		    printf("%s: Can't create '%s'\n",
			   rn, fullpn_VFile);
	    }
    }
    
    /*
     * Close the directory, return success.
     */
    if (afsd_debug)
	printf("%s: Closing cache directory.\n",
	       rn);
    closedir(cdirp);
    return(0);
}

static ConfigCell(aci, arock, adir)
register struct afsconf_cell *aci;
char *arock;
struct afsconf_dir *adir; {
    register int isHomeCell;
    register int i;
    afs_int32 cellFlags;
    afs_int32 hosts[MAXHOSTSPERCELL];
    
    /* figure out if this is the home cell */
    isHomeCell = (strcmp(aci->name, LclCellName) == 0);
    if (isHomeCell) {
	lookingForHomeCell = 0;
	cellFlags = 1;	    /* home cell, suid is ok */
    }
    else {
	cellFlags = 2;	    /* not home, suid is forbidden */
    }
    
    /* build address list */
    for(i=0;i<MAXHOSTSPERCELL;i++)
	memcpy(&hosts[i], &aci->hostAddr[i].sin_addr, sizeof(afs_int32));

    if (aci->linkedCell) cellFlags |= 4; /* Flag that linkedCell arg exists,
					    for upwards compatibility */

    /* configure one cell */
    call_syscall(AFSOP_ADDCELL2,
	     hosts,			/* server addresses */
	     aci->name,			/* cell name */
	     cellFlags,			/* is this the home cell? */
	     aci->linkedCell);		/* Linked cell, if any */
    return 0;
}

#ifdef mac2
#include <sys/ioctl.h>
#endif /* mac2 */

#ifdef AFS_SGI65_ENV
#define SET_RTPRI(P) {  \
    struct sched_param sp; \
    sp.sched_priority = P; \
    if (sched_setscheduler(0, SCHED_RR, &sp)<0) { \
	perror("sched_setscheduler"); \
    } \
}
#define SET_AFSD_RTPRI() SET_RTPRI(68)
#define SET_RX_RTPRI()   SET_RTPRI(199)
#else
#ifdef AFS_LINUX20_ENV
#define SET_AFSD_RTPRI()
#define SET_RX_RTPRI()	do { if (setpriority(PRIO_PROCESS, 0, -10)<0) \
			   perror("setting rx priority"); \
			 } while (0)
#else
#define SET_AFSD_RTPRI() 
#define SET_RX_RTPRI()   
#endif
#endif

mainproc(as, arock)
  register struct cmd_syndesc *as;
  char *arock;
{
    static char rn[] = "afsd";	    /*Name of this routine*/
    register afs_int32 code;		    /*Result of fork()*/
    register int i;
    int	currVFile;		    /*Current AFS cache file number passed in*/
    int	mountFlags;		    /*Flags passed to mount()*/
    int	lookupResult;		    /*Result of GetLocalCellName()*/
    int	cacheIteration;		    /*How many times through cache verification*/
    int	vFilesFound;		    /*How many data cache files were found in sweep*/
    struct afsconf_dir *cdir;	    /* config dir */
    FILE *logfd;
#ifdef	AFS_SUN5_ENV
    struct stat st;
#endif
    afs_int32 vfs1_type = -1;
#ifdef AFS_SGI65_ENV
    struct sched_param sp;
#endif

#ifdef AFS_SGI_VNODE_GLUE
    if (afs_init_kernel_config(-1) <0) {
	printf("Can't determine NUMA configuration, not starting AFS.\n");
	exit(1);
    }
#endif

    /* call atoi on the appropriate parsed results */
    if (as->parms[0].items) {
	/* -blocks */
	cacheBlocks = atoi(as->parms[0].items->data);
	sawCacheBlocks = 1;
    }
    if (as->parms[1].items) {
	/* -files */
	cacheFiles = atoi(as->parms[1].items->data);
	filesSet = 1;	/* set when spec'd on cmd line */
    }
    if (as->parms[2].items) {
	/* -rootvol */
	strcpy(rootVolume, as->parms[2].items->data);
	rootVolSet = 1;
    }
    if (as->parms[3].items) {
	/* -stat */
	cacheStatEntries = atoi(as->parms[3].items->data);
    }
    if (as->parms[4].items) {
	/* -memcache */
	cacheBaseDir[0] = '\0';
	sawCacheBaseDir = 1;
	cacheFlags |= AFSCALL_INIT_MEMCACHE;
	if (chunkSize == 0)
	    chunkSize = 13;
    }
    if (as->parms[5].items) {
	/* -cachedir */
	strcpy(cacheBaseDir, as->parms[5].items->data);
	sawCacheBaseDir = 1;
    }
    if (as->parms[6].items) {
	/* -mountdir */
	strcpy(cacheMountDir, as->parms[6].items->data);
	sawCacheMountDir = 1;
    }
    if (as->parms[7].items) {
	/* -daemons */
	nDaemons = atoi(as->parms[7].items->data);
    }
    if (as->parms[8].items) {
	/* -nosettime */
	cacheSetTime = FALSE;
    }
    if (as->parms[9].items) {
	/* -verbose */
	afsd_verbose = 1;
    }
    if (as->parms[10].items) {
	/* -rmtsys */
	afsd_rmtsys = 1;
    }
    if (as->parms[11].items) {
	/* -debug */
	afsd_debug = 1;
	afsd_verbose = 1;
    }
    if (as->parms[12].items) {
	/* -chunksize */
	chunkSize = atoi(as->parms[12].items->data);
	if (chunkSize < 0 || chunkSize > 30) {
	    printf("afsd:invalid chunk size spec'd, using default\n");
	    chunkSize = 0;
	}
    }
    if (as->parms[13].items) {
	/* -dcache */
	dCacheSize = atoi(as->parms[13].items->data);
	sawDCacheSize = 1;
    }
    if (as->parms[14].items) {
	/* -volumes */
	vCacheSize = atoi(as->parms[14].items->data);
    }
    if (as->parms[15].items) {
	/* -biods */
#ifndef	AFS_AIX32_ENV
	printf("afsd: [-biods] currently only enabled for aix3.x VM supported systems\n");
#else
	nBiods = atoi(as->parms[15].items->data);
	sawBiod = 1;
#endif
    }
    if (as->parms[16].items) {
	/* -prealloc */
	preallocs = atoi(as->parms[16].items->data);
    }
#ifdef notdef 
    if (as->parms[17].items) {
	/* -pininodes */
	inodes = atoi(as->parms[17].items->data);
    }
#endif
    strcpy(confDir, AFSDIR_CLIENT_ETC_DIRPATH);
    if (as->parms[17].items) {
	/* -confdir */
	strcpy(confDir, as->parms[17].items->data);
    }
    sprintf(fullpn_CacheInfo,  "%s/%s", confDir, CACHEINFOFILE);
    sprintf(fullpn_AFSLogFile,  "%s/%s", confDir, AFSLOGFILE);
    if (as->parms[18].items) {
	/* -logfile */
	strcpy(fullpn_AFSLogFile, as->parms[18].items->data);
    }
    if (as->parms[19].items) {
      /* -waitclose */
      afsd_CloseSynch = 1;
    }
    if (as->parms[20].items) {
	/* -shutdown */
	afs_shutdown = 1;
	/* 
	 * Cold shutdown is the default
	 */
	printf("afsd: Shutting down all afs processes and afs state\n");
	call_syscall(AFSOP_SHUTDOWN, 1);
	exit(0);
    }
    if (as->parms[21].items) {
	/* -enable_peer_stats */
	enable_peer_stats = 1;
    }
    if (as->parms[22].items) {
	/* -enable_process_stats */
	enable_process_stats = 1;
    }
    if (as->parms[23].items) {
	/* -mem_alloc_sleep */
	cacheFlags |= AFSCALL_INIT_MEMCACHE_SLEEP;
    }

    /*
     * Pull out all the configuration info for the workstation's AFS cache and
     * the cellular community we're willing to let our users see.
     */
    cdir = afsconf_Open(confDir);
    if (!cdir) {
	printf("afsd: some file missing or bad in %s\n", confDir);
	exit(1);
    }

    lookupResult = afsconf_GetLocalCell(cdir, LclCellName, sizeof(LclCellName));
    if (lookupResult) {
	printf("%s: Can't get my home cell name!  [Error is %d]\n",
	       rn, lookupResult);
    }
    else {
	if (afsd_verbose)
	    printf("%s: My home cell is '%s'\n",
		   rn, LclCellName);
    }

    /* parse cacheinfo file if this is a diskcache */
    if (ParseCacheInfoFile()) {
	exit(1);
    }

    if ((logfd = fopen(fullpn_AFSLogFile,"r+")) == 0) {
	if (afsd_verbose)  printf("%s: Creating '%s'\n",  rn, fullpn_AFSLogFile);
	if (CreateCacheFile(fullpn_AFSLogFile)) {
	    printf("%s: Can't create '%s' (You may want to use the -logfile option)\n",  rn, fullpn_AFSLogFile);
	    exit(1);
	}
    } else
	fclose(logfd);

    /* do some random computations in memcache case to get things to work
     * reasonably no matter which parameters you set.
     */
    if (cacheFlags & AFSCALL_INIT_MEMCACHE) {
	/* memory cache: size described either as blocks or dcache entries, but
	 * not both.
	 */
	if (sawDCacheSize) {
	    if (sawCacheBlocks) {
		printf("%s: can't set cache blocks and dcache size simultaneously when diskless.\n", rn);
		exit(1);
	    }
	    /* compute the cache size based on # of chunks times the chunk size */
	    i = (chunkSize == 0? 13 : chunkSize);
	    i = (1<<i);	/* bytes per chunk */
	    cacheBlocks = i * dCacheSize;
	    sawCacheBlocks = 1;	/* so that ParseCacheInfoFile doesn't overwrite */
	}
	else {
	    /* compute the dcache size from overall cache size and chunk size */
	    i = (chunkSize == 0? 13 : chunkSize);
	    /* dCacheSize = (cacheBlocks << 10) / (1<<i); */
	    if (i > 10) {
		dCacheSize = (cacheBlocks >> (i-10));
	    } else if (i < 10) {
		dCacheSize = (cacheBlocks << (10-i));
	    } else {
		dCacheSize = cacheBlocks;
	    }
	    /* don't have to set sawDCacheSize here since it isn't overwritten
	     * by ParseCacheInfoFile.
	     */
	}
	/* kernel computes # of dcache entries as min of cacheFiles and dCacheSize,
	 * so we now make them equal.
	 */
	cacheFiles = dCacheSize;
    }
    else {
	/* Disk cache:
	 * Compute the number of cache files based on cache size,
	 * but only if -files isn't given on the command line.
	 * Don't let # files be so small as to prevent full utilization 
	 * of the cache unless user has explicitly asked for it.
	 * average V-file is ~10K, according to tentative empirical studies.
	 */
	if (!filesSet) {
	    cacheFiles = cacheBlocks / 10;       
	    if (cacheFiles <  100) cacheFiles =  100;
	    /* Always allow more files than chunks.  Presume average V-file 
	     * is ~67% of a chunk...  (another guess, perhaps Honeyman will
	     * have a grad student write a paper).  i is KILOBYTES.
	     */
	    i = 1 << (chunkSize == 0? 6 : (chunkSize<10 ? 0 : chunkSize -10));
	    cacheFiles = max(cacheFiles, 1.5 * (cacheBlocks / i));
	    /* never permit more files than blocks, while leaving space for
	     * VolumeInfo and CacheItems files.  VolumeInfo is usually 20K,
	     * CacheItems is 50 Bytes / file (== 1K/20)
	     */
#define VOLINFOSZ 20
#define CACHEITMSZ (cacheFiles / 20)
#ifdef AFS_AIX_ENV
	    cacheFiles= min(cacheFiles,(cacheBlocks -VOLINFOSZ -CACHEITMSZ)/4);
#else
	    cacheFiles = min(cacheFiles, cacheBlocks - VOLINFOSZ - CACHEITMSZ);
#endif
	    if (cacheFiles <  100) 
	      fprintf (stderr, "%s: WARNING: cache probably too small!\n", rn);
	}
	if (!sawDCacheSize) {
	    if ((cacheFiles / 2) > dCacheSize)
		dCacheSize = cacheFiles / 2;
	    if (dCacheSize > 2000)
		dCacheSize = 2000;
	}
    }

    /*
     * Create and zero the inode table for the desired cache files.
     */
    inode_for_V = (AFSD_INO_T *) malloc(cacheFiles * sizeof(AFSD_INO_T));
    if (inode_for_V == (AFSD_INO_T *)0) {
	printf("%s: malloc() failed for cache file inode table with %d entries.\n",
	       rn, cacheFiles);
	exit(1);
    }
    memset(inode_for_V, '\0', (cacheFiles * sizeof(AFSD_INO_T)));
    if (afsd_debug)
	printf("%s: %d inode_for_V entries at 0x%x, %d bytes\n",
	       rn, cacheFiles, inode_for_V,
	       (cacheFiles * sizeof(AFSD_INO_T)));

    /*
     * Set up all the pathnames we'll need for later.
     */
    sprintf(fullpn_DCacheFile,  "%s/%s", cacheBaseDir, DCACHEFILE);
    sprintf(fullpn_VolInfoFile, "%s/%s", cacheBaseDir, VOLINFOFILE);
    sprintf(fullpn_VFile,       "%s/V",  cacheBaseDir);
    vFileNumber = fullpn_VFile + strlen(fullpn_VFile);

#if 0
    fputs(AFS_GOVERNMENT_MESSAGE, stdout); 
    fflush(stdout);
#endif

    /*
     * Set up all the kernel processes needed for AFS.
     */
#ifdef mac2
    setpgrp(getpid(), 0);
#endif /* mac2 */

    /* Initialize RX daemons and services */

    /* initialize the rx random number generator from user space */
    {
      /* parse multihomed address files */
      afs_int32 addrbuf[MAXIPADDRS],maskbuf[MAXIPADDRS],mtubuf[MAXIPADDRS];
      char reason[1024];
      code=parseNetFiles(addrbuf,maskbuf,mtubuf,MAXIPADDRS,reason,
		    AFSDIR_CLIENT_NETINFO_FILEPATH,
		    AFSDIR_CLIENT_NETRESTRICT_FILEPATH);
      if(code>0) 
	call_syscall(AFSOP_ADVISEADDR, code, addrbuf, maskbuf, mtubuf);
      else 
	printf("ADVISEADDR: Error in specifying interface addresses:%s\n",reason);
    }

    /* Set realtime priority for most threads to same as for biod's. */
    SET_AFSD_RTPRI();

#ifdef	AFS_SGI53_ENV
#ifdef AFS_SGI61_ENV
    set_staticaddrs();
#else /* AFS_SGI61_ENV */
    code = get_nfsstaticaddr();
    if (code)
	call_syscall(AFSOP_NFSSTATICADDR, code);
#endif /* AFS_SGI61_ENV */
#endif /* AFS_SGI_53_ENV */

    /* Start listener, then callback listener. Lastly, start rx event daemon.
     * Change in ordering is so that Linux port has socket fd in listener
     * process.
     * preallocs are passed for both listener and callback server. Only
     * the one which actually does the initialization uses them though.
     */
    if (preallocs < cacheStatEntries+50)
	preallocs = cacheStatEntries+50;
#ifdef RXK_LISTENER_ENV
    if (afsd_verbose)
	printf("%s: Forking rx listener daemon.\n", rn);
    code = fork();
    if (code == 0) {
	/* Child */
	SET_RX_RTPRI(); /* max advised for non-interrupts */
	call_syscall(AFSOP_RXLISTENER_DAEMON,
		     preallocs,
		     enable_peer_stats,
		     enable_process_stats);
	exit(1);
    }
#endif
    if (afsd_verbose)
	printf("%s: Forking rx callback listener.\n", rn);
    code = fork();
    if (code == 0) {
	/* Child */
	call_syscall(AFSOP_START_RXCALLBACK, preallocs);
	exit(1);
    }
#if defined(AFS_SUN5_ENV) || defined(RXK_LISTENER_ENV)
    if (afsd_verbose)
	printf("%s: Forking rxevent daemon.\n", rn);
    code = fork();
    if (code == 0) {
	/* Child */
	SET_RX_RTPRI(); /* max advised for non-interrupts */
	call_syscall(AFSOP_RXEVENT_DAEMON);
	exit(1);
    }
#endif

    /* Initialize AFS daemon threads. */
    if (afsd_verbose)
	printf("%s: Forking AFS daemon.\n", rn);
    code = fork();
    if (code == 0) {
	/* Child */
	call_syscall(AFSOP_START_AFS);
	exit(1);
    }

    if (afsd_verbose)
	printf("%s: Forking Check Server Daemon.\n", rn);
    code = fork();
    if (code == 0) {
	/* Child */
	code = call_syscall(AFSOP_START_CS);
	if (code)
	    printf("%s: No check server daemon in client.\n", rn);
	exit(1);
    }

    if (afsd_verbose)
	printf("%s: Forking %d background daemons.\n", rn, nDaemons);
#if defined(AFS_SGI_ENV) && defined(AFS_SGI_SHORTSTACK)
    /* Add one because for sgi we always "steal" the first daemon for a
     * different task if we only have a 4K stack.
     */
    nDaemons++;
#endif
    for (i=0;i<nDaemons;i++) {
	code = fork();
	if (code == 0) {
	    /* Child */
#ifdef	AFS_AIX32_ENV
	    call_syscall(AFSOP_START_BKG, 0);
#else
	    call_syscall(AFSOP_START_BKG);
#endif
	    exit(1);
	}
    }
#ifdef	AFS_AIX32_ENV
    if (!sawBiod) 
	nBiods = nDaemons * 2;
    if (nBiods < 5)
	nBiods = 5;
    for (i=0; i< nBiods;i++) {
	code = fork();
	if (code == 0) {	/* Child */
	    call_syscall(AFSOP_START_BKG, nBiods);
	    exit(1);
	}
    }
#endif

    /*
     * Tell the kernel about each cell in the configuration.
     */
    lookingForHomeCell = 1;

    afsconf_CellApply(cdir, ConfigCell, (char *) 0);

    /*
     * If we're still looking for the home cell after the whole cell configuration database
     * has been parsed, there's something wrong.
     */
    if (lookingForHomeCell) {
	printf("%s: Can't find information for home cell '%s' in cell database!\n",
	       rn, LclCellName);
    }

    /*
         * If the root volume has been explicitly set, tell the kernel.
         */
    if (rootVolSet) {
	if (afsd_verbose)
	    printf("%s: Calling AFSOP_ROOTVOLUME with '%s'\n",
	      rn, rootVolume);
	call_syscall(AFSOP_ROOTVOLUME, rootVolume);
    }

    /*
     * Tell the kernel some basic information about the workstation's cache.
     */
    if (afsd_verbose)
	printf("%s: Calling AFSOP_CACHEINIT: %d stat cache entries, %d optimum cache files, %d blocks in the cache, flags = 0x%x, dcache entries %d\n",
	       rn, cacheStatEntries, cacheFiles, cacheBlocks, cacheFlags,
	       dCacheSize);
    memset(&cparams, '\0', sizeof(cparams));
    cparams.cacheScaches = cacheStatEntries;
    cparams.cacheFiles = cacheFiles;
    cparams.cacheBlocks = cacheBlocks;
    cparams.cacheDcaches = dCacheSize;
    cparams.cacheVolumes = vCacheSize;
    cparams.chunkSize = chunkSize;
    cparams.setTimeFlag = cacheSetTime;
    cparams.memCacheFlag = cacheFlags;
#ifdef notdef
    cparams.inodes       = inodes;
#endif
    call_syscall(AFSOP_CACHEINIT, &cparams);
    if (afsd_CloseSynch) 
      call_syscall(AFSOP_CLOSEWAIT);

    /*
     * Sweep the workstation AFS cache directory, remembering the inodes of
     * valid files and deleting extraneous files.  Keep sweeping until we
     * have the right number of data cache files or we've swept too many
     * times.
     */
    if (afsd_verbose)
	printf("%s: Sweeping workstation's AFS cache directory.\n",
	       rn);
    cacheIteration = 0;
    /* Memory-cache based system doesn't need any of this */
    if(!(cacheFlags & AFSCALL_INIT_MEMCACHE)) {
	do {
	    cacheIteration++;
	    if (SweepAFSCache(&vFilesFound)) {
		printf("%s: Error on sweep %d of workstation AFS cache \
                       directory.\n", rn, cacheIteration);
		exit(1);
	    }
	    if (afsd_verbose)
		printf("%s: %d out of %d data cache files found in sweep %d.\n",
		       rn, vFilesFound, cacheFiles, cacheIteration);
	} while ((vFilesFound < cacheFiles) &&
		 (cacheIteration < MAX_CACHE_LOOPS));
    } else if(afsd_verbose)
	printf("%s: Using memory cache, not swept\n", rn);

    /*
     * Pass the kernel the name of the workstation cache file holding the 
     * dcache entries.
     */
    if (afsd_debug)
	printf("%s: Calling AFSOP_CACHEINFO: dcache file is '%s'\n",
	       rn, fullpn_DCacheFile);
    /* once again, meaningless for a memory-based cache. */
    if(!(cacheFlags & AFSCALL_INIT_MEMCACHE))
	call_syscall(AFSOP_CACHEINFO, fullpn_DCacheFile);

    /*
     * Pass the kernel the name of the workstation cache file holding the
     * volume information.
     */
    if (afsd_debug)
	printf("%s: Calling AFSOP_VOLUMEINFO: volume info file is '%s'\n",
	       rn, fullpn_VolInfoFile);
    call_syscall(AFSOP_VOLUMEINFO,fullpn_VolInfoFile);

    /*
     * Pass the kernel the name of the afs logging file holding the volume
     * information.
     */
    if (afsd_debug)
	printf("%s: Calling AFSOP_AFSLOG: volume info file is '%s'\n",
	       rn, fullpn_AFSLogFile);
#if defined(AFS_SGI_ENV)
    /* permit explicit -logfile argument to enable logging on memcache systems */
    if (!(cacheFlags & AFSCALL_INIT_MEMCACHE) || as->parms[18].items)
#else
    if (!(cacheFlags & AFSCALL_INIT_MEMCACHE)) /* ... nor this ... */
#endif
	call_syscall(AFSOP_AFSLOG,fullpn_AFSLogFile);

    /*
     * Give the kernel the names of the AFS files cached on the workstation's
     * disk.
     */
    if (afsd_debug)
	printf("%s: Calling AFSOP_CACHEINODE for each of the %d files in '%s'\n",
	       rn, cacheFiles, cacheBaseDir);
    if (!(cacheFlags & AFSCALL_INIT_MEMCACHE)) /* ... and again ... */
	for (currVFile = 0; currVFile < cacheFiles; currVFile++) {
#ifdef AFS_SGI62_ENV
	    call_syscall(AFSOP_CACHEINODE,
			 (afs_uint32)(inode_for_V[currVFile]>>32),
			 (afs_uint32)(inode_for_V[currVFile] & 0xffffffff));
#else
	    call_syscall(AFSOP_CACHEINODE, inode_for_V[currVFile]);
#endif
	} /*end for*/


    /*
     * All the necessary info has been passed into the kernel to run an AFS
     * system.  Give the kernel our go-ahead.
     */
    if (afsd_debug)
	 printf("%s: Calling AFSOP_GO with cacheSetTime = %d\n",
		rn, cacheSetTime);
     call_syscall(AFSOP_GO, cacheSetTime);

    /*
     * At this point, we have finished passing the kernel all the info 
     * it needs to set up the AFS.  Mount the AFS root.
     */
    printf("%s: All AFS daemons started.\n", rn);

    if (afsd_verbose)
	printf("%s: Forking trunc-cache daemon.\n", rn);
    code = fork();
    if (code == 0) {
	/* Child */
	call_syscall(AFSOP_START_TRUNCDAEMON);
	exit(1);
    }

    mountFlags = 0;	/* Read/write file system, can do setuid() */
#if	defined(AFS_SUN_ENV) || defined(AFS_SUN5_ENV)
#ifdef	AFS_SUN5_ENV
    mountFlags |= MS_DATA;
#else
    mountFlags |= M_NEWTYPE; /* This searches by name in vfs_conf.c so don't need to recompile vfs.c because MOUNT_MAXTYPE has changed; it seems that Sun fixed this at last... */
#endif
#endif

#if defined(AFS_HPUX100_ENV)
	mountFlags |= MS_DATA;
#endif

    if (afsd_verbose)
	printf("%s: Mounting the AFS root on '%s', flags: %d.\n",
	       rn, cacheMountDir, mountFlags);
#ifdef AFS_DEC_ENV
    if ((mount("AFS",cacheMountDir,mountFlags,GT_AFS,(caddr_t) 0)) < 0) {
#else
#ifdef	AFS_AUX_ENV
    if ((fsmount(MOUNT_AFS,cacheMountDir,mountFlags,(caddr_t) 0)) < 0)	{
#else
#ifdef	AFS_AIX_ENV
    if (aix_vmount()) {
#else
#if defined(AFS_HPUX100_ENV)
    if ((mount("",cacheMountDir,mountFlags,"afs", (char *)0, 0)) < 0) {
#else
#ifdef	AFS_HPUX_ENV
#if	defined(AFS_HPUX90_ENV)
    { 
	char buffer[80];
	int code;

	strcpy(buffer, "afs");
	code = vfsmount(-1,cacheMountDir,mountFlags,(caddr_t) buffer);
	sscanf(buffer, "%d", &vfs1_type);
	if (code < 0) {
	    printf("Can't find 'afs' type in the registered filesystem table!\n");
	    exit(1);
	}
	sscanf(buffer, "%d", &vfs1_type);
	if (afsd_verbose)
	    printf("AFS vfs slot number is %d\n", vfs1_type);
    }
    if ((vfsmount(vfs1_type,cacheMountDir,mountFlags,(caddr_t) 0)) < 0) {
#else
    if (call_syscall(AFSOP_AFS_VFSMOUNT, MOUNT_AFS, cacheMountDir,
		     mountFlags, (caddr_t)NULL) < 0) {
#endif
#else
#ifdef	AFS_SUN5_ENV
    if ((mount("AFS",cacheMountDir,mountFlags,"afs", (char *)0, 0)) < 0) {
#else
#if defined(AFS_SGI_ENV)
    mountFlags = MS_FSS;

    if ((mount(MOUNT_AFS,cacheMountDir,mountFlags,(caddr_t) MOUNT_AFS)) < 0) {
#else
#ifdef AFS_LINUX20_ENV
    if ((mount("AFS", cacheMountDir, MOUNT_AFS, 0, NULL))<0) {
#else
/* This is the standard mount used by the suns and rts */
    if ((mount(MOUNT_AFS,cacheMountDir,mountFlags,(caddr_t) 0)) < 0) {
#endif /* AFS_LINUX20_ENV */
#endif /* AFS_SGI_ENV */
#endif /* AFS_SUN5_ENV */
#endif /* AFS_HPUX100_ENV */
#endif /* AFS_HPUX_ENV */
#endif /* AFS_AIX_ENV */
#endif /* AFS_AUX_ENV */
#endif /* AFS_DEC_ENV */
         printf("%s: Can't mount AFS on %s(%d)\n",
		   rn, cacheMountDir, errno);
         exit(1);
    }

    HandleMTab();

    if (afsd_rmtsys) {
	if (afsd_verbose)
	    printf("%s: Forking 'rmtsys' daemon.\n", rn);
	code = fork();
	if (code == 0) {
	    /* Child */
	    rmtsysd();
	    exit(1);
	}
    }
    /*
     * Exit successfully.
     */
    exit(0);
}

#include "AFS_component_version_number.c"



main(argc, argv)
int argc;
char **argv; {
    register struct cmd_syndesc *ts;

    ts = cmd_CreateSyntax((char *) 0, mainproc, (char *) 0, "start AFS");
    cmd_AddParm(ts, "-blocks", CMD_SINGLE, CMD_OPTIONAL, "1024 byte blocks in cache");
    cmd_AddParm(ts, "-files", CMD_SINGLE, CMD_OPTIONAL, "files in cache");
    cmd_AddParm(ts, "-rootvol", CMD_SINGLE, CMD_OPTIONAL, "name of AFS root volume");
    cmd_AddParm(ts, "-stat", CMD_SINGLE, CMD_OPTIONAL, "number of stat entries");
    cmd_AddParm(ts, "-memcache", CMD_FLAG, CMD_OPTIONAL, "run diskless");
    cmd_AddParm(ts, "-cachedir", CMD_SINGLE, CMD_OPTIONAL, "cache directory");
    cmd_AddParm(ts, "-mountdir", CMD_SINGLE, CMD_OPTIONAL, "mount location");
    cmd_AddParm(ts, "-daemons", CMD_SINGLE, CMD_OPTIONAL, "number of daemons to use");
    cmd_AddParm(ts, "-nosettime", CMD_FLAG, CMD_OPTIONAL, "don't set the time");
    cmd_AddParm(ts, "-verbose", CMD_FLAG, CMD_OPTIONAL, "display lots of information");
    cmd_AddParm(ts, "-rmtsys", CMD_FLAG, CMD_OPTIONAL, "start NFS rmtsysd program");
    cmd_AddParm(ts, "-debug", CMD_FLAG, CMD_OPTIONAL, "display debug info");
    cmd_AddParm(ts, "-chunksize", CMD_SINGLE, CMD_OPTIONAL, "log(2) of chunk size");
    cmd_AddParm(ts, "-dcache", CMD_SINGLE, CMD_OPTIONAL, "number of dcache entries");
    cmd_AddParm(ts, "-volumes", CMD_SINGLE, CMD_OPTIONAL, "number of volume entries");
    cmd_AddParm(ts, "-biods", CMD_SINGLE, CMD_OPTIONAL, "number of bkg I/O daemons (aix vm)");

    cmd_AddParm(ts, "-prealloc", CMD_SINGLE, CMD_OPTIONAL, "number of 'small' preallocated blocks");
#ifdef notdef
    cmd_AddParm(ts, "-pininodes", CMD_SINGLE, CMD_OPTIONAL, "number of inodes to hog"); 
#endif
    cmd_AddParm(ts, "-confdir", CMD_SINGLE, CMD_OPTIONAL, "configuration directory");
    cmd_AddParm(ts, "-logfile", CMD_SINGLE, CMD_OPTIONAL, "Place to keep the CM log");
    cmd_AddParm(ts, "-waitclose", CMD_FLAG, CMD_OPTIONAL, "make close calls synchronous");
    cmd_AddParm(ts, "-shutdown", CMD_FLAG, CMD_OPTIONAL, "Shutdown all afs state");
    cmd_AddParm(ts, "-enable_peer_stats", CMD_FLAG, CMD_OPTIONAL|CMD_HIDE, "Collect rpc statistics by peer");
    cmd_AddParm(ts, "-enable_process_stats", CMD_FLAG, CMD_OPTIONAL|CMD_HIDE, "Collect rpc statistics for this process");
    cmd_AddParm(ts, "-mem_alloc_sleep", CMD_FLAG, (CMD_OPTIONAL | CMD_HIDE), "Allow sleeps when allocating memory cache");
    return (cmd_Dispatch(argc, argv));
}


#ifdef	AFS_HPUX_ENV
#define	MOUNTED_TABLE	MNT_MNTTAB
#else
#ifdef	AFS_SUN5_ENV
#define	MOUNTED_TABLE	MNTTAB
#else
#define	MOUNTED_TABLE	MOUNTED
#endif
#endif

static int HandleMTab() {
#if (defined (AFS_SUN_ENV) || defined (AFS_HPUX_ENV) || defined(AFS_SUN5_ENV) || defined(AFS_SGI_ENV) || defined(AFS_LINUX20_ENV)) && !defined(AFS_SUN58_ENV)
    FILE *tfilep;
#ifdef	AFS_SUN5_ENV
    char tbuf[16];
    struct mnttab tmntent;

    memset(&tmntent, '\0', sizeof(struct mnttab));
    if (!(tfilep = fopen(MOUNTED_TABLE, "a+"))) {
	printf("Can't open %s\n", MOUNTED_TABLE);
	perror(MNTTAB);
	exit(-1);
    }
    tmntent.mnt_special = "AFS";
    tmntent.mnt_mountp = cacheMountDir;
    tmntent.mnt_fstype = "xx";
    tmntent.mnt_mntopts = "rw";
    sprintf(tbuf, "%ld", (long)time((time_t *)0));
    tmntent.mnt_time = tbuf;
    putmntent(tfilep, &tmntent);
    fclose(tfilep);
#else
#if defined(AFS_SGI_ENV) || defined(AFS_LINUX20_ENV)
    struct mntent tmntent;

    tfilep = setmntent("/etc/mtab", "a+");
    tmntent.mnt_fsname = "AFS";
    tmntent.mnt_dir = cacheMountDir;
    tmntent.mnt_type = "afs";
    tmntent.mnt_opts = "rw";
    tmntent.mnt_freq = 1;
    tmntent.mnt_passno = 3;
    addmntent(tfilep, &tmntent);
    endmntent(tfilep);
#else
    struct mntent tmntent;

    memset(&tmntent, '\0', sizeof(struct mntent));
    tfilep = setmntent(MOUNTED_TABLE, "a+");
    if (!tfilep) {
	printf("Can't open %s for write; Not adding afs entry to it\n",
	       MOUNTED_TABLE);
	return 1;
    }
    tmntent.mnt_fsname = "AFS";
    tmntent.mnt_dir = cacheMountDir;
    tmntent.mnt_type = "xx";
    tmntent.mnt_opts = "rw";
    tmntent.mnt_freq = 1;
    tmntent.mnt_passno = 3;
#ifdef	AFS_HPUX_ENV
    tmntent.mnt_time = time(0);
    tmntent.mnt_cnode = 0;
#endif
    addmntent(tfilep, &tmntent);
    endmntent(tfilep);
#endif	/* AFS_SGI_ENV */
#endif	/* AFS_SUN5_ENV */
#endif	/* unreasonable systems */
    return 0;
}

#if !defined(AFS_SGI_ENV) && !defined(AFS_AIX32_ENV)
call_syscall(param1, param2, param3, param4, param5, param6, param7)
long param1, param2, param3, param4, param5, param6, param7;
{
    int error;

    error = syscall(AFS_SYSCALL, AFSCALL_CALL, param1, param2, param3, param4, param5, param6, param7);
    if (afsd_verbose) printf("SScall(%d, %d)=%d ", AFS_SYSCALL, AFSCALL_CALL, error);
    return (error);
}
#else	/* AFS_AIX32_ENV */
#if defined(AFS_SGI_ENV)
call_syscall(call, parm0, parm1, parm2, parm3, parm4)
{

	int error;

	error = afs_syscall(call, parm0, parm1, parm2, parm3, parm4);
	if (afsd_verbose) printf("SScall(%d, %d)=%d ", call, parm0, error);

	return error;
}
#else
call_syscall(call, parm0, parm1, parm2, parm3, parm4, parm5, parm6) {

    return syscall(AFSCALL_CALL, call, parm0, parm1, parm2, parm3, parm4, parm5, parm6);
}
#endif /* AFS_SGI_ENV */
#endif /* AFS_AIX32_ENV	*/


#ifdef	AFS_AIX_ENV
/* Special handling for AIX's afs mount operation since they require much more miscl. information before making the vmount(2) syscall */
#include <sys/vfs.h>

#define	ROUNDUP(x)  (((x) + 3) & ~3)

aix_vmount() {
    struct vmount   *vmountp;
    int size, error;

    size = sizeof(struct vmount) + ROUNDUP(strlen(cacheMountDir)+1) + 5*4;
    /* Malloc the vmount structure */
    if ((vmountp = (struct vmount *)malloc(size)) == (struct vmount *)NULL) {
	 printf("Can't allocate space for the vmount structure (AIX)\n");
	 exit(1);
    }

    /* zero out the vmount structure */
    memset(vmountp, '\0', size);
 
    /* transfer info into the vmount structure */
    vmountp->vmt_revision = VMT_REVISION;
    vmountp->vmt_length = size;
    vmountp->vmt_fsid.fsid_dev = 0;
    vmountp->vmt_fsid.fsid_type = AFS_MOUNT_AFS;
    vmountp->vmt_vfsnumber = 0;
    vmountp->vmt_time = 0;/* We'll put the time soon! */
    vmountp->vmt_flags = VFS_DEVMOUNT;	/* read/write permission */
    vmountp->vmt_gfstype = AFS_MOUNT_AFS;
    vmountdata(vmountp, "AFS", cacheMountDir, "", "", "", "rw");
    
    /* Do the actual mount system call */
    error = vmount(vmountp, size);
    free(vmountp);
    return(error);
}

vmountdata(vmtp, obj, stub, host, hostsname, info, args)
struct vmount	*vmtp;
char	*obj, *stub, *host, *hostsname, *info, *args;
{
	register struct data {
				short	vmt_off;
				short	vmt_size;
			} *vdp, *vdprev;
	register int	size;

	vdp = (struct data *)vmtp->vmt_data;
	vdp->vmt_off = sizeof(struct vmount);
	size = ROUNDUP(strlen(obj) + 1);
	vdp->vmt_size = size;
	strcpy(vmt2dataptr(vmtp, VMT_OBJECT), obj);

	vdprev = vdp;
	vdp++;
	vdp->vmt_off =  vdprev->vmt_off + size;
	size = ROUNDUP(strlen(stub) + 1);
	vdp->vmt_size = size;
	strcpy(vmt2dataptr(vmtp, VMT_STUB), stub);

	vdprev = vdp;
	vdp++;
	vdp->vmt_off = vdprev->vmt_off + size;
	size = ROUNDUP(strlen(host) + 1);
	vdp->vmt_size = size;
	strcpy(vmt2dataptr(vmtp, VMT_HOST), host);

	vdprev = vdp;
	vdp++;
	vdp->vmt_off = vdprev->vmt_off + size;
	size = ROUNDUP(strlen(hostsname) + 1);
	vdp->vmt_size = size;
	strcpy(vmt2dataptr(vmtp, VMT_HOSTNAME), hostsname);


	vdprev = vdp;
	vdp++;
	vdp->vmt_off =  vdprev->vmt_off + size;
	size = ROUNDUP(strlen(info) + 1);
	vdp->vmt_size = size;
	strcpy(vmt2dataptr(vmtp, VMT_INFO), info);

	vdprev = vdp;
	vdp++;
	vdp->vmt_off =  vdprev->vmt_off + size;
	size = ROUNDUP(strlen(args) + 1);
	vdp->vmt_size = size;
	strcpy(vmt2dataptr(vmtp, VMT_ARGS), args);
}
#endif /* AFS_AIX_ENV */

#ifdef	AFS_SGI53_ENV
#ifdef AFS_SGI61_ENV
/* The dwarf structures are searched to find entry points of static functions
 * and the addresses of static variables. The file name as well as the
 * sybmol name is reaquired.
 */

/* Contains list of names to find in given file. */
typedef struct {
    char *name;		/* Name of variable or function. */
    afs_hyper_t addr;	/* Address of function, undefined if not found. */
    Dwarf_Half type; /* DW_AT_location for vars, DW_AT_lowpc for func's */
    char found;		/* set if found. */
} staticAddrList;

typedef struct  {
    char *file;			/* Name of file containing vars or funcs */
    staticAddrList *addrList;	/* List of vars and/or funcs. */
    int nAddrs;			/* # of addrList's */
    int found;			/* set if we've found this file already. */
} staticNameList;

/* routines used to find addresses in /unix */
#if defined(AFS_SGI62_ENV) && !defined(AFS_SGI65_ENV)
void findMDebugStaticAddresses(staticNameList *, int, int);
#endif
void findDwarfStaticAddresses(staticNameList *, int);
void findElfAddresses(Dwarf_Debug, Dwarf_Die, staticNameList *);
void getElfAddress(Dwarf_Debug, Dwarf_Die, staticAddrList *);

#if defined(AFS_SGI62_ENV) && !defined(AFS_SGI65_ENV)
#define AFS_N_FILELISTS 2
#define AFS_SYMS_NEEDED 3
#else /* AFS_SGI62_ENV */
#define AFS_N_FILELISTS 1
#endif /* AFS_SGI62_ENV */



void set_staticaddrs(void)
{
    staticNameList fileList[AFS_N_FILELISTS];

    fileList[0].addrList = (staticAddrList*)calloc(1, sizeof(staticAddrList));
    if (!fileList[0].addrList) {
	printf("set_staticaddrs: Can't calloc fileList[0].addrList\n");
	return;
    }
    fileList[0].file = "nfs_server.c";
    fileList[0].found = 0;
    fileList[0].nAddrs = 1;
    fileList[0].addrList[0].name = "rfsdisptab_v2";
    fileList[0].addrList[0].type = DW_AT_location;
    fileList[0].addrList[0].found = 0;

#if defined(AFS_SGI62_ENV) && !defined(AFS_SGI65_ENV)
    fileList[1].addrList = (staticAddrList*)calloc(2, sizeof(staticAddrList));
    if (!fileList[1].addrList) {
	printf("set_staticaddrs: Can't malloc fileList[1].addrList\n");
	return;
    }
    fileList[1].file = "uipc_socket.c";
    fileList[1].found = 0;
    fileList[1].nAddrs = 2;
    fileList[1].addrList[0].name = "sblock";
    fileList[1].addrList[0].type = DW_AT_low_pc;
    fileList[1].addrList[0].found = 0;
    fileList[1].addrList[1].name = "sbunlock";
    fileList[1].addrList[1].type = DW_AT_low_pc;
    fileList[1].addrList[1].found = 0;

    if (64 != sysconf(_SC_KERN_POINTERS))
	findMDebugStaticAddresses(fileList, AFS_N_FILELISTS, AFS_SYMS_NEEDED);
    else
#endif /* AFS_SGI62_ENV */
	findDwarfStaticAddresses(fileList, AFS_N_FILELISTS);

    if (fileList[0].addrList[0].found) {
	call_syscall(AFSOP_NFSSTATICADDR2, fileList[0].addrList[0].addr.high,
		     fileList[0].addrList[0].addr.low);
    }
    else {
	if (afsd_verbose)
	    printf("NFS V2 is not present in the kernel.\n");
    }
    free(fileList[0].addrList);
#if defined(AFS_SGI62_ENV) && !defined(AFS_SGI65_ENV)
    if (fileList[1].addrList[0].found && fileList[1].addrList[1].found) {
	call_syscall(AFSOP_SBLOCKSTATICADDR2,
		     fileList[1].addrList[0].addr.high,
		     fileList[1].addrList[0].addr.low,
		     fileList[1].addrList[1].addr.high,
		     fileList[1].addrList[1].addr.low);
    }
    else {
	if (!fileList[1].addrList[0].found)
	    printf("Can't find %s in kernel. Exiting.\n",
		   fileList[1].addrList[0].name);
	if (!fileList[1].addrList[0].found)
	    printf("Can't find %s in kernel. Exiting.\n",
		   fileList[1].addrList[1].name);
	exit(1);
    }
    free(fileList[1].addrList);
#endif /* AFS_SGI62_ENV */
}


/* Find addresses for static variables and functions. */
void
findDwarfStaticAddresses(staticNameList * nameList, int nLists)
{
    int fd;
    int i;
    int found = 0;
    int code;
    char *s;
    char *hname = (char*)0;
    Dwarf_Unsigned dwarf_access = O_RDONLY;
    Dwarf_Debug dwarf_debug;
    Dwarf_Error dwarf_error;
    Dwarf_Unsigned dwarf_cu_header_length;
    Dwarf_Unsigned dwarf_abbrev_offset;
    Dwarf_Half dwarf_address_size;
    Dwarf_Unsigned next_cu_header;
    Dwarf_Die dwarf_die;
    Dwarf_Die dwarf_next_die;
    Dwarf_Die dwarf_child_die;

    if (elf_version(EV_CURRENT) == EV_NONE) {
	printf("findDwarfStaticAddresses: Bad elf version.\n");
	return;
    }

    if ((fd=open("/unix", O_RDONLY,0))<0) {
	printf("findDwarfStaticAddresses: Failed to open /unix.\n");
	return;
    }
    code = dwarf_init(fd, dwarf_access, NULL, NULL, &dwarf_debug,
		      &dwarf_error);
    if (code != DW_DLV_OK) {
	/* Nope hope for the elves and dwarves, try intermediate code. */
	close(fd);
	return;
    }

    found = 0;
    while (1) {
	/* Run through the headers until we find ones for files we've
	 * specified in nameList.
	 */
	code = dwarf_next_cu_header(dwarf_debug,
				    &dwarf_cu_header_length, NULL,
				    &dwarf_abbrev_offset,
				    &dwarf_address_size,
				    &next_cu_header,
				    &dwarf_error);
	if (code == DW_DLV_NO_ENTRY) {
	    break;
	}
	else if (code == DW_DLV_ERROR) {
	    printf("findDwarfStaticAddresses: Error reading headers: %s\n",
		   dwarf_errmsg(dwarf_error));
	    break;
	}

	code = dwarf_siblingof(dwarf_debug, NULL, &dwarf_die, &dwarf_error);
	if (code != DW_DLV_OK) {
	    printf("findDwarfStaticAddresses: Can't get first die. %s\n",
		   (code == DW_DLV_ERROR) ? dwarf_errmsg(dwarf_error) : "");
	    break;
	}

	/* This is the header, test the name. */
	code = dwarf_diename(dwarf_die, &hname, &dwarf_error);
	if (code == DW_DLV_OK) {
	    s = strrchr(hname, '/');
	    for (i=0; i<nLists; i++) {
		if (s && !strcmp(s+1, nameList[i].file)) {
		    findElfAddresses(dwarf_debug, dwarf_die, &nameList[i]);
		    found ++;
		    break;
		}
	    }
	}
	else {
	    printf("findDwarfStaticAddresses: Can't get name of current header. %s\n",
		   (code == DW_DLV_ERROR) ? dwarf_errmsg(dwarf_error) : "");
	    break;
	}
	dwarf_dealloc(dwarf_debug, hname, DW_DLA_STRING);
	hname = (char*)0;
	if (found >= nLists) { /* we're done */
	    break;
	}
    }

     /* Frees up all allocated space. */
    (void) dwarf_finish(dwarf_debug, &dwarf_error);
    close(fd);
}

void
findElfAddresses(Dwarf_Debug dwarf_debug, Dwarf_Die dwarf_die,
		 staticNameList * nameList)
{
    int i;
    Dwarf_Error dwarf_error;
    Dwarf_Die dwarf_next_die;
    Dwarf_Die dwarf_child_die;
    Dwarf_Attribute dwarf_return_attr;
    char *vname = (char*)0;
    int found = 0;
    int code;
    
    /* Drop into this die to find names in addrList. */
    code = dwarf_child(dwarf_die, &dwarf_child_die, &dwarf_error);
    if (code != DW_DLV_OK) {
	printf("findElfAddresses: Can't get child die. %s\n",
	       (code == DW_DLV_ERROR) ? dwarf_errmsg(dwarf_error) : "");
	return;
    }

    /* Try to find names in each sibling. */
    dwarf_next_die = (Dwarf_Die)0;
    do {
	code = dwarf_diename(dwarf_child_die, &vname,
			     &dwarf_error);
	/* It's possible that some siblings don't have names. */
	if (code == DW_DLV_OK) {
	    for (i=0; i<nameList->nAddrs; i++) {
		if (!nameList->addrList[i].found) {
		    if (!strcmp(vname, nameList->addrList[i].name)) {
			getElfAddress(dwarf_debug, dwarf_child_die,
					  &(nameList->addrList[i]));
			found ++;
			break;
		    }
		}
	    }
	}
	if (dwarf_next_die)
	    dwarf_dealloc(dwarf_debug, dwarf_next_die, DW_DLA_DIE);

	if (found >= nameList->nAddrs) { /* we're done. */
	    break;
	}

	dwarf_next_die = dwarf_child_die;
	code = dwarf_siblingof(dwarf_debug, dwarf_next_die,
			       &dwarf_child_die,
			       &dwarf_error);
     
    } while(code == DW_DLV_OK);
}

/* Get address out of current die. */
void
getElfAddress(Dwarf_Debug dwarf_debug,
	      Dwarf_Die dwarf_child_die, staticAddrList *addrList)
{
    int i;
    Dwarf_Error dwarf_error;
    Dwarf_Attribute dwarf_return_attr;
    Dwarf_Bool dwarf_return_bool;
    Dwarf_Locdesc *llbuf = NULL;
    Dwarf_Signed listlen;
    off64_t addr = (off64_t)0;
    int code;

    code = dwarf_hasattr(dwarf_child_die, addrList->type,
			 &dwarf_return_bool, &dwarf_error);
    if ((code !=  DW_DLV_OK) || (!dwarf_return_bool)) {
	printf("getElfAddress: no address given for %s. %s\n",
	       addrList->name, (code == DW_DLV_ERROR) ? dwarf_errmsg(dwarf_error) : "");
	return;
    }
    code = dwarf_attr(dwarf_child_die, addrList->type,
		      &dwarf_return_attr,  &dwarf_error);
    if (code !=  DW_DLV_OK) {
	printf("getElfAddress: Can't get attribute. %s\n",
	       (code == DW_DLV_ERROR) ? dwarf_errmsg(dwarf_error) : "");
	return;
    }

    switch (addrList->type) {
    case DW_AT_location:
	code = dwarf_loclist(dwarf_return_attr, &llbuf,
			     &listlen, &dwarf_error);
	if (code !=   DW_DLV_OK) {
	    printf("getElfAddress: Can't get location for %s. %s\n",
		   addrList->name,
		   (code == DW_DLV_ERROR) ? dwarf_errmsg(dwarf_error) : "");
	    return;
	}
	if ((listlen != 1) || (llbuf[0].ld_cents != 1)) {
	    printf("getElfAddress: %s has more than one address.\n",
		   addrList->name);
	    return;
	}
	addr = llbuf[0].ld_s[0].lr_number;
	break;

    case DW_AT_low_pc:
	code = dwarf_lowpc(dwarf_child_die, (Dwarf_Addr*)&addr, &dwarf_error);
	if ( code !=  DW_DLV_OK) {
	    printf("getElfAddress: Can't get lowpc for %s. %s\n",
		   addrList->name,
		   (code == DW_DLV_ERROR) ? dwarf_errmsg(dwarf_error) : "");
	    return;
	}
	break;
	
    default:
	printf("getElfAddress: Bad case %d in switch.\n", addrList->type);
	return;
    }

    addrList->addr.high = (addr>>32) & 0xffffffff;
    addrList->addr.low  = addr & 0xffffffff;
    addrList->found = 1;
}

#if defined(AFS_SGI62_ENV) && !defined(AFS_SGI65_ENV)
/* Find symbols in the .mdebug section for 32 bit kernels. */
/*
 * do_mdebug()
 * On 32bit platforms, we're still using the ucode compilers to build
 * the kernel, so we need to get our static text/data from the .mdebug
 * section instead of the .dwarf sections.
 */
/* SearchNameList searches our bizarre structs for the given string.
 * If found, sets the found bit and the address and returns 1.
 * Not found returns 0.
 */
int SearchNameList(char *name, afs_uint32 addr, staticNameList *nameList,
		   int nLists)
{
    int i, j;			
    for (i=0; i<nLists; i++) {
	for (j=0; j<nameList[i].nAddrs; j++) {
	    if (nameList[i].addrList[j].found)
		continue;
	    if (!strcmp(name, nameList[i].addrList[j].name)) {
		nameList[i].addrList[j].addr.high = 0;
		nameList[i].addrList[j].addr.low = addr;
		nameList[i].addrList[j].found = 1;
		return 1;
	    }
	}
    }
    return 0;
}
					
static void
SearchMDebug(Elf_Scn *scnp, Elf32_Shdr *shdrp, staticNameList * nameList,
	     int nLists, int needed)
{
    long *buf = (long *)(elf_getdata(scnp, NULL)->d_buf); 
    u_long addr, mdoff = shdrp->sh_offset;
    HDRR *hdrp;
    SYMR *symbase, *symp, *symend;
    FDR *fdrbase, *fdrp;
    int i, j;
    char *strbase, *str;
    int ifd;
    int nFound = 0;
    
    /* get header */
    addr = (__psunsigned_t)buf;
    hdrp = (HDRR *)addr;
    
    /* setup base addresses */
    addr = (u_long)buf + (u_long)(hdrp->cbFdOffset - mdoff);
    fdrbase = (FDR *)addr;
    addr = (u_long)buf + (u_long)(hdrp->cbSymOffset - mdoff);
    symbase = (SYMR *)addr;
    addr = (u_long)buf + (u_long)(hdrp->cbSsOffset - mdoff);
    strbase = (char *)addr;
    
#define KEEPER(a,b)	((a == stStaticProc && b == scText) || \
			 (a == stStatic && (b == scData || b == scBss || \
					    b == scSBss || b == scSData)))
	
    for (fdrp = fdrbase; fdrp < &fdrbase[hdrp->ifdMax]; fdrp++) {
	str = strbase + fdrp->issBase + fdrp->rss;
	
	/* local symbols for each fd */
	for (symp = &symbase[fdrp->isymBase];
	     symp < &symbase[fdrp->isymBase+fdrp->csym];
	     symp++) {
	    if (KEEPER(symp->st, symp->sc)) {
		if (symp->value == 0)
		    continue;
		
		str = strbase + fdrp->issBase + symp->iss;
		/* Look for AFS symbols of interest */
		if (SearchNameList(str, symp->value,
				   nameList, nLists)) {
		    nFound ++;
		    if (nFound >= needed)
			return;
		}
	    }
	}
    }
}
    
/*
 * returns section with the name of scn_name, & puts its header in shdr64 or
 * shdr32 based on elf's file type
 *
 */
Elf_Scn *
findMDebugSection(Elf *elf, char *scn_name)
{
	Elf64_Ehdr *ehdr64;
	Elf32_Ehdr *ehdr32;
	Elf_Scn *scn = NULL;
	Elf64_Shdr *shdr64;
	Elf32_Shdr *shdr32;

	if ((ehdr32 = elf32_getehdr(elf)) == NULL)
		return(NULL);
	do {
		if ((scn = elf_nextscn(elf, scn)) == NULL)
			break;
		if ((shdr32 = elf32_getshdr(scn)) == NULL)
			return(NULL);
	} while (strcmp(scn_name, elf_strptr(elf, ehdr32->e_shstrndx,
						  shdr32->sh_name)));

	return(scn);	
}


void findMDebugStaticAddresses(staticNameList * nameList, int nLists,
			       int needed)
{
    int fd;
    Elf *elf;
    Elf_Scn *mdebug_scn;
    Elf32_Shdr *mdebug_shdr;
    char *names;

    if ((fd = open("/unix", O_RDONLY)) == -1) {
	printf("findMDebugStaticAddresses: Failed to open /unix.\n");
	return;
    }

    (void)elf_version(EV_CURRENT);
    if((elf = elf_begin(fd, ELF_C_READ, NULL)) == NULL) {
	printf("findMDebugStaticAddresses: /unix doesn't seem to be an elf file\n");
	close(fd);
	return;
    }
    mdebug_scn = findMDebugSection(elf, ".mdebug");
    if (!mdebug_scn) {
	printf("findMDebugStaticAddresses: Can't find .mdebug section.\n");
	goto find_end;
    }
    mdebug_shdr = elf32_getshdr(mdebug_scn);
    if (!mdebug_shdr) {
	printf("findMDebugStaticAddresses: Can't find .mdebug header.\n");
	goto find_end;
    }

    (void) SearchMDebug(mdebug_scn, mdebug_shdr, nameList, nLists, needed);

 find_end:
    elf_end(elf);
    close(fd);
}
#endif /* AFS_SGI62_ENV */

#else /* AFS_SGI61_ENV */
#include <nlist.h>
struct	nlist nlunix[] = {
   { "rfsdisptab_v2" },
   { 0 },
};

get_nfsstaticaddr() {
    int i, j, kmem, count;

    if ((kmem = open("/dev/kmem", O_RDONLY)) < 0) {
	printf("Warning: can't open /dev/kmem\n");
        return 0;
    }
    if ((j = nlist("/unix", nlunix)) < 0) {
	printf("Warning: can't nlist /unix\n");
	return 0;
    }
    i = nlunix[0].n_value;
    if (lseek(kmem, i, L_SET/*0*/) != i) {
	printf("Warning: can't lseek to %x\n", i);	
        return 0;
    }
    if ((j = read(kmem, &count, sizeof count)) != sizeof count) {
	printf("WARNING: kmem read at %x failed\n", i);
        return 0;
    }
    return i;
}
#endif /* AFS_SGI61_ENV */
#endif /* AFS_SGI53_ENV */
