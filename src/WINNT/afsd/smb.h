/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#ifndef __SMB_H_ENV__
#define __SMB_H_ENV__ 1

#ifdef DJGPP
#include "netbios95.h"
#endif /* DJGPP */

/* basic core protocol SMB structure */
typedef struct smb {
	unsigned char id[4];
        unsigned char com;
        unsigned char rcls;
        unsigned char reh;
        unsigned char errLow;
        unsigned char errHigh;
        unsigned char reb;
        unsigned short flg2;
        unsigned short res[6];
        unsigned short tid;
        unsigned short pid;
        unsigned short uid;
        unsigned short mid;
        unsigned char wct;
        unsigned char vdata[1];
} smb_t;

/* more defines */
#define SMB_NOPCODES		256	/* # of opcodes in the dispatch table */

/* threads per VC */
#define SMB_THREADSPERVC	4	/* threads per VC */

/* flags for functions */
#define SMB_FLAG_CREATE		1	/* create the structure if necessary */

/* max # of bytes we'll receive in an incoming SMB message */
#define SMB_PACKETSIZE	8400

/* a packet structure for receiving SMB messages; locked by smb_globalLock.
 * Most of the work involved is in handling chained requests and responses.
 *
 * When handling input, inWctp points to the current request's wct field (and
 * the other parameters and request data can be found from this field).  The
 * opcode, unfortunately, isn't available there, so is instead copied to the
 * packet's inCom field.  It is initially set to com, but each chained
 * operation sets it, also.
 * The function smb_AdvanceInput advances an input packet to the next request
 * in the chain.  The inCom field is set to 0xFF when there are no more
 * requests.  The inCount field is 0 if this is the first request, and
 * otherwise counts which request it is.
 *
 * When handling output, we also have to chain all of the responses together.
 * The function smb_GetResponsePacket will setup outWctp to point to the right
 * place.
 */
#define SMB_PACKETMAGIC	0x7436353	/* magic # for packets */
typedef struct smb_packet {
	char data[SMB_PACKETSIZE];
	struct smb_packet *nextp;	/* in free list, or whatever */
        long magic;
	cm_space_t *spacep;		/* use this for stripping last component */
	NCB *ncbp;			/* use this for sending */
	struct smb_vc *vcp;
	unsigned long resumeCode;
        unsigned short inCount;
        unsigned short fid;		/* for calls bundled with openAndX */
        unsigned char *wctp;
        unsigned char inCom;
	unsigned char oddByte;
	unsigned short ncb_length;
	unsigned char flags;
#ifdef DJGPP
        dos_ptr dos_pkt;
        unsigned int dos_pkt_sel;
#endif /* DJGPP */
} smb_packet_t;

/* smb_packet flags */
#define SMB_PACKETFLAG_PROFILE_UPDATE_OK	1
#define SMB_PACKETFLAG_NOSEND			2
#define SMB_PACKETFLAG_SUSPENDED		4

/* a structure for making Netbios calls; locked by smb_globalLock */
#define SMB_NCBMAGIC	0x2334344
typedef struct myncb {
	NCB ncb;			/* ncb to use */
        struct myncb *nextp;		/* when on free list */
        long magic;
#ifdef DJGPP
        dos_ptr dos_ncb;
        smb_packet_t *orig_pkt;
        unsigned int dos_ncb_sel;
#endif /* DJGPP */
} smb_ncb_t;

/* structures representing environments from kernel / SMB network.
 * Most have their own locks, but the tree connection fields and
 * reference counts are locked by the smb_rctLock.  Those fields will
 * be marked in comments.
 */

/* one per virtual circuit */
typedef struct smb_vc {
	struct smb_vc *nextp;		/* not used */
        int refCount;			/* the reference count */
        long flags;			/* the flags, if any; locked by mx */
        osi_mutex_t mx;			/* the mutex */
	long vcID;			/* VC id */
        unsigned short lsn;		/* the NCB LSN associated with this */
	unsigned short uidCounter;	/* session ID counter */
        unsigned short tidCounter;	/* tree ID counter */
        unsigned short fidCounter;	/* file handle ID counter */
        struct smb_tid *tidsp;		/* the first child in the tid list */
        struct smb_user *usersp;	/* the first child in the user session list */
        struct smb_fid *fidsp;		/* the first child in the open file list */
	struct smb_user *justLoggedOut;	/* ready for profile upload? */
	unsigned long logoffTime;	/* tick count when logged off */
	/*struct cm_user *logonDLLUser;	/* integrated logon user */
	unsigned char errorCount;
        char rname[17];
	int lana;
} smb_vc_t;

					/* have we negotiated ... */
#define SMB_VCFLAG_USEV3	1	/* ... version 3 of the protocol */
#define SMB_VCFLAG_USECORE	2	/* ... the core protocol */
#define SMB_VCFLAG_USENT	4	/* ... NT LM 0.12 or beyond */
#define SMB_VCFLAG_STATUS32	8	/* use 32-bit NT status codes */
#define SMB_VCFLAG_REMOTECONN	0x10	/* bad: remote conns not allowed */
#define SMB_VCFLAG_ALREADYDEAD	0x20	/* do not get tokens from this vc */

/* one per user session */
typedef struct smb_user {
	struct smb_user *nextp;		/* next sibling */
        long refCount;			/* ref count */
        long flags;			/* flags; locked by mx */
        osi_mutex_t mx;
        long userID;			/* the session identifier */
        struct smb_vc *vcp;		/* back ptr to virtual circuit */
  struct smb_username *unp;        /* user name struct */
} smb_user_t;

typedef struct smb_username {
	struct smb_username *nextp;		/* next sibling */
        long refCount;			/* ref count */
        long flags;			/* flags; locked by mx */
        osi_mutex_t mx;
	struct cm_user *userp;		/* CM user structure */
        char *name;			/* user name */
  char *machine;                  /* machine name */
} smb_username_t;

#define SMB_USERFLAG_DELETE	1	/* delete struct when ref count zero */

/* one per tree-connect */
typedef struct smb_tid {
	struct smb_tid *nextp;		/* next sibling */
        long refCount;
        long flags;
        osi_mutex_t mx;			/* for non-tree-related stuff */
        unsigned short tid;		/* the tid */
        struct smb_vc *vcp;		/* back ptr */
        struct cm_user *userp;		/* user logged in at the
					 * tree connect level (base) */
	char *pathname;			/* pathname derived from sharename */
} smb_tid_t;

#define SMB_TIDFLAG_DELETE	1	/* delete struct when ref count zero */

/* one per process ID */
typedef struct smb_pid {
	struct smb_pid *nextp;		/* next sibling */
        long refCount;
        long flags;
        osi_mutex_t mx;			/* for non-tree-related stuff */
        unsigned short pid;		/* the pid */
        struct smb_tid *tidp;		/* back ptr */
} smb_pid_t;

/* ioctl parameter, while being assembled and/or processed */
typedef struct smb_ioctl {
	/* input side */
	char *inDatap;			/* ioctl func's current position
					 * in input parameter block */
	char *inAllocp;			/* allocated input parameter block */
        long inCopied;			/* # of input bytes copied in so far
					 * by write calls */
	cm_space_t *prefix;		/* prefix for subst drives */
	char *tidPathp;			/* Pathname associated with Tree ID */

	/* output side */
        char *outDatap;			/* output results assembled so far */
        char *outAllocp;		/* output results assembled so far */
        long outCopied;			/* # of output bytes copied back so far
					 * by read calls */
	
        /* flags */
        long flags;

        /* fid pointer */
        struct smb_fid *fidp;

  /* uid pointer */
  smb_user_t *uidp;

} smb_ioctl_t;

/* flags for smb_ioctl_t */
#define SMB_IOCTLFLAG_DATAIN	1	/* reading data from client to server */
#define SMB_IOCTLFLAG_LOGON	2	/* got tokens from integrated logon */

/* one per file ID; these are really file descriptors */
typedef struct smb_fid {
	osi_queue_t q;
        long refCount;
        long flags;
        osi_mutex_t mx;			/* for non-tree-related stuff */
        unsigned short fid;		/* the file ID */
        struct smb_vc *vcp;		/* back ptr */
        struct cm_scache *scp;		/* scache of open file */
        long offset;			/* our file pointer */
        smb_ioctl_t *ioctlp;		/* ptr to ioctl structure */
					/* Under NT, we may need to know the
					 * parent directory and pathname used
					 * to open the file, either to delete
					 * the file on close, or to do a
					 * change notification */
	struct cm_scache *NTopen_dscp;	/* parent directory (NT) */
	char *NTopen_pathp;		/* path used in open (NT) */
	char *NTopen_wholepathp;	/* entire path, not just last name */
	int curr_chunk;			/* chunk being read */
	int prev_chunk;			/* previous chunk read */
	int raw_writers;		/* pending async raw writes */
	EVENT_HANDLE raw_write_event;	/* signal this when raw_writers zero */
} smb_fid_t;

#define SMB_FID_OPENREAD		1	/* open for reading */
#define SMB_FID_OPENWRITE		2	/* open for writing */
#define SMB_FID_DELETE			4	/* delete struct on ref count 0 */
#define SMB_FID_IOCTL			8	/* a file descriptor for the
						 * magic ioctl file */
#define SMB_FID_OPENDELETE		0x10	/* open for deletion (NT) */
#define SMB_FID_DELONCLOSE		0x20	/* marked for deletion */
/*
 * Now some special flags to work around a bug in NT Client
 */
#define SMB_FID_LENGTHSETDONE		0x40	/* have done 0-length write */
#define SMB_FID_MTIMESETDONE		0x80	/* have set modtime via Tr2 */
#define SMB_FID_LOOKSLIKECOPY	(SMB_FID_LENGTHSETDONE | SMB_FID_MTIMESETDONE)
#define SMB_FID_NTOPEN			0x100	/* have dscp and pathp */

/* for tracking in-progress directory searches */
typedef struct smb_dirSearch {
	osi_queue_t q;			/* queue of all outstanding cookies */
        osi_mutex_t mx;			/* just in case the caller screws up */
        int refCount;			/* reference count */
        long cookie;			/* value returned to the caller */
        struct cm_scache *scp;		/* vnode of the dir we're searching */
        long lastTime;			/* last time we used this */
        long flags;			/* flags (see below);
					 * locked by smb_globalLock */
        unsigned short attribute;	/* search attribute
					 * (used for extended protocol) */
        char mask[256];			/* search mask for V3 */
} smb_dirSearch_t;

#define SMB_DIRSEARCH_DELETE	1	/* delete struct when ref count zero */
#define SMB_DIRSEARCH_HITEOF	2	/* perhaps useful for advisory later */
#define SMB_DIRSEARCH_SMALLID	4	/* cookie can only be 8 bits, not 16 */
#define SMB_DIRSEARCH_BULKST	8	/* get bulk stat info */

/* type for patching directory listings */
typedef struct smb_dirListPatch {
	osi_queue_t q;
        char *dptr;		/* ptr to attr, time, data, sizel, sizeh */
	cm_fid_t fid;
  cm_dirEntry_t *dep;   /* temp */
} smb_dirListPatch_t;

/* waiting lock list elements */
typedef struct smb_waitingLock {
	osi_queue_t q;
	smb_vc_t *vcp;
	smb_packet_t *inp;
	smb_packet_t *outp;
	u_long timeRemaining;
	void *lockp;
} smb_waitingLock_t;

smb_waitingLock_t *smb_allWaitingLocks;

typedef long (smb_proc_t)(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *outp);

typedef struct smb_dispatch {
	smb_proc_t *procp;	/* proc to call */
        int flags;		/* flags describing function */
} smb_dispatch_t;

#define SMB_DISPATCHFLAG_CHAINED	1	/* this is an _AND_X function */
#define SMB_DISPATCHFLAG_NORESPONSE	2	/* don't send the response
						 * packet, typically because
						 * the response was already
						 * sent.
                                                 */
#define SMB_MAX_PATH                    256     /* max path length */

/* prototypes */

extern void smb_Init(osi_log_t *logp, char *smbNamep, int useV3, int LANadapt,
	int nThreads
#ifndef DJGPP
        , void *aMBfunc
#endif
  );

extern void smb_LargeSearchTimeFromUnixTime(FILETIME *largeTimep, long unixTime);

extern void smb_UnixTimeFromLargeSearchTime(long *unixTimep, FILETIME *largeTimep);

extern void smb_SearchTimeFromUnixTime(long *dosTimep, long unixTime);

extern void smb_UnixTimeFromSearchTime(long *unixTimep, long searchTime);

extern void smb_DosUTimeFromUnixTime(long *dosUTimep, long unixTime);

extern void smb_UnixTimeFromDosUTime(long *unixTimep, long dosUTime);

extern smb_vc_t *smb_FindVC(unsigned short lsn, int flags, int lana);

extern void smb_ReleaseVC(smb_vc_t *vcp);

extern smb_tid_t *smb_FindTID(smb_vc_t *vcp, unsigned short tid, int flags);

extern void smb_ReleaseTID(smb_tid_t *tidp);

extern smb_user_t *smb_FindUID(smb_vc_t *vcp, unsigned short uid, int flags);

extern void smb_ReleaseUID(smb_user_t *uidp);

extern cm_user_t *smb_GetUser(smb_vc_t *vcp, smb_packet_t *inp);

extern char *smb_GetTIDPath(smb_vc_t *vcp, unsigned short tid);

extern smb_fid_t *smb_FindFID(smb_vc_t *vcp, unsigned short fid, int flags);

extern void smb_ReleaseFID(smb_fid_t *fidp);

extern int smb_FindShare(smb_vc_t *vcp, smb_packet_t *inp, char *shareName, char **pathNamep);

extern smb_dirSearch_t *smb_FindDirSearchNL(long cookie);

extern void smb_DeleteDirSearch(smb_dirSearch_t *dsp);

extern void smb_ReleaseDirSearch(smb_dirSearch_t *dsp);

extern smb_dirSearch_t *smb_FindDirSearch(long cookie);

extern smb_dirSearch_t *smb_NewDirSearch(int isV3);

extern smb_packet_t *smb_CopyPacket(smb_packet_t *packetp);

extern void smb_FreePacket(smb_packet_t *packetp);

extern unsigned char *smb_GetSMBData(smb_packet_t *smbp, int *nbytesp);

extern void smb_SetSMBDataLength(smb_packet_t *smbp, unsigned int dsize);

extern unsigned int smb_GetSMBParm(smb_packet_t *smbp, int parm);

extern unsigned int smb_GetSMBOffsetParm(smb_packet_t *smbp, int parm, int offset);

extern void smb_SetSMBParm(smb_packet_t *smbp, int slot, unsigned int parmValue);

extern void smb_SetSMBParmLong(smb_packet_t *smbp, int slot, unsigned int parmValue);

extern void smb_SetSMBParmDouble(smb_packet_t *smbp, int slot, char *parmValuep);

extern void smb_SetSMBParmByte(smb_packet_t *smbp, int slot, unsigned int parmValue);

extern void smb_StripLastComponent(char *outPathp, char **lastComponentp,
	char *inPathp);

extern unsigned char *smb_ParseASCIIBlock(unsigned char *inp, char **chainpp);

extern unsigned char *smb_ParseVblBlock(unsigned char *inp, char **chainpp,
	int *lengthp);

extern smb_packet_t *smb_GetResponsePacket(smb_vc_t *vcp, smb_packet_t *inp);

extern void smb_SendPacket(smb_vc_t *vcp, smb_packet_t *inp);

extern void smb_MapCoreError(long code, smb_vc_t *vcp, unsigned short *scodep,
	unsigned char *classp);

extern void smb_MapNTError(long code, unsigned long *NTStatusp);

extern void smb_HoldVC(smb_vc_t *vcp);

/* some globals, too */
extern int loggedOut;
extern unsigned long loggedOutTime;
extern char *loggedOutName;
extern smb_user_t *loggedOutUserp;

extern osi_log_t *smb_logp;

extern osi_rwlock_t smb_globalLock;

extern osi_rwlock_t smb_rctLock;

extern int smb_LogoffTokenTransfer;
extern unsigned long smb_LogoffTransferTimeout;

extern void smb_FormatResponsePacket(smb_vc_t *vcp, smb_packet_t *inp,
	smb_packet_t *op);

extern unsigned int smb_Attributes(cm_scache_t *scp);

extern int smb_ChainFID(int fid, smb_packet_t *inp);

extern smb_fid_t *smb_FindFID(smb_vc_t *vcp, unsigned short fid, int flags);

extern void smb_ReleaseFID(smb_fid_t *fidp);

extern unsigned char *smb_ParseDataBlock(unsigned char *inp, char **chainpp, int *lengthp);

extern unsigned char *smb_ParseASCIIBlock(unsigned char *inp, char **chainpp);

extern unsigned char *smb_ParseVblBlock(unsigned char *inp, char **chainpp, int *lengthp);

extern int smb_SUser(cm_user_t *userp);

#ifndef DJGPP
extern long smb_ReadData(smb_fid_t *fidp, osi_hyper_t *offsetp, long count,
	char *op, cm_user_t *userp, long *readp);
#else /* DJGPP */
extern long smb_ReadData(smb_fid_t *fidp, osi_hyper_t *offsetp, long count,
	char *op, cm_user_t *userp, long *readp, int dosflag);
#endif /* !DJGPP */

extern BOOL smb_IsLegalFilename(char *filename);

/* include other include files */
#include "smb3.h"
#include "smb_ioctl.h"
#include "smb_iocons.h"

cm_user_t *smb_FindOrCreateUser(smb_vc_t *vcp, char *usern);

#endif /* whole file */
