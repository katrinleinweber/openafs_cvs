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

RCSID("$Header: /cvs/openafs/src/update/server.c,v 1.7 2001/07/12 19:59:20 shadow Exp $");

#include <afs/stds.h>
#ifdef	AFS_AIX32_ENV
#include <signal.h>
#endif
#include <sys/types.h>
#ifdef AFS_NT40_ENV
#include <winsock2.h>
#include <WINNT/afsevent.h>
#include <fcntl.h>
#include <io.h>
#include <afs/dirent.h>
#include <afs/procmgmt.h>
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/file.h>
#include <dirent.h>
#endif
#ifdef HAVE_STRINGS_H
#include <strings.h>
#else
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <rx/xdr.h>
#include <rx/rx.h>
#include <rx/rxkad.h>
#include <afs/cellconfig.h>
#include <afs/afsutil.h>
#include <afs/fileutil.h>
#include "update.h"
#include "global.h"

extern int UPDATE_ExecuteRequest();
static int AddObject(char **expPath, char *dir);
static int PathInDirectory(char *dir, char *path);

struct afsconf_dir *cdir;
int   nDirs;
char *dirName[MAXENTRIES];
int   dirLevel[MAXENTRIES];
char *whoami;

static int Quit();

/* check whether caller is authorized to manage RX statistics */
int update_rxstat_userok(call)
    struct rx_call *call;
{
    return afsconf_SuperUser(cdir, call, (char *)0);
}

/*
 * PathInDirectory() -- determine if path is in directory (or is directory)
 */
static int
PathInDirectory(char *dir, char *path)
{
    int inDir = 0;
    size_t dirLen;
    char dirNorm[AFSDIR_PATH_MAX], pathNorm[AFSDIR_PATH_MAX];

#ifdef AFS_NT40_ENV
    /* case-insensitive comparison of normalized, same-flavor (short) paths */
    DWORD status;

    status = GetShortPathName(dir, dirNorm, AFSDIR_PATH_MAX);
    if (status == 0 || status > AFSDIR_PATH_MAX) {
	/* can't convert path to short version; just use long version */
	strcpy(dirNorm, dir);
    }
    FilepathNormalize(dirNorm);

    status = GetShortPathName(path, pathNorm, AFSDIR_PATH_MAX);
    if (status == 0 || status > AFSDIR_PATH_MAX) {
	/* can't convert path to short version; just use long version */
	strcpy(pathNorm, path);
    }
    FilepathNormalize(pathNorm);

    dirLen = strlen(dirNorm);

    if (_strnicmp(dirNorm, pathNorm, dirLen) == 0) {
	/* substrings match; path must match dir or be subdirectory */
	if (pathNorm[dirLen] == '\0' || pathNorm[dirLen] == '/') {
	    inDir = 1;
	}
    }
#else
    /* case-sensitive comparison of normalized paths */
    strcpy(dirNorm, dir);
    FilepathNormalize(dirNorm);

    strcpy(pathNorm, path);
    FilepathNormalize(pathNorm);

    dirLen = strlen(dirNorm);

    if (strncmp(dirNorm, pathNorm, dirLen) == 0) {
	/* substrings match; path must match dir or be subdirectory */
	if (pathNorm[dirLen] == '\0' || pathNorm[dirLen] == '/') {
	    inDir = 1;
	}
    }
#endif /* AFS_NT40_ENV */
    return inDir;
}


int AuthOkay (call, name)
  struct rx_call *call;
  char *name;
{
    int i;
    rxkad_level level;
    afs_int32 code;
    int matches;

    /* Must be in 'UserList' to use */
    if (!afsconf_SuperUser (cdir, call, (char *)0)) return 0;

    if (rx_SecurityClassOf (rx_ConnectionOf(call)) == 2) {
	code = rxkad_GetServerInfo (call->conn, &level, 0,0,0,0,0);
	if (code) return 0;
    } else level = 0;

    matches = 0;
    for (i=0; i<nDirs; i++) {
	if (PathInDirectory(dirName[i], name)) {
	    if (dirLevel[i] > level) return 0;
	    matches++;
	    /* keep searching in case there's a more restrictive subtree
             * specified later. */
	}
    }
    if (nDirs && !matches) return 0;	/* if dirs spec., name must match */
    return 1;				/* okay or no dirs */
}

int osi_audit()
{
/* this sucks but it works for now.
*/
return 0;
}

#ifndef AFS_NT40_ENV
#include "AFS_component_version_number.c"
#endif

int main (argc, argv)
  int   argc;
  char *argv[];
{
    struct rx_securityClass *securityObjects[3];
    struct rx_service *service;
    extern struct rx_securityClass *rxnull_NewServerSecurityObject();

    int a = 0;
    rxkad_level level;
    rxkad_level newLevel;

#ifdef	AFS_AIX32_ENV
    /*
     * The following signal action for AIX is necessary so that in case of a 
     * crash (i.e. core is generated) we can include the user's data section 
     * in the core dump. Unfortunately, by default, only a partial core is
     * generated which, in many cases, isn't too useful.
     */
    struct sigaction nsa;
    
    sigemptyset(&nsa.sa_mask);
    nsa.sa_handler = SIG_DFL;
    nsa.sa_flags = SA_FULLDUMP;
    sigaction(SIGABRT, &nsa, NULL);
    sigaction(SIGSEGV, &nsa, NULL);
#endif

    whoami = argv[0];

#ifdef AFS_NT40_ENV
      /* dummy signal call to force afsprocmgmt.dll to load on NT */
      signal(SIGUSR1, SIG_DFL);
#endif

    /* Initialize dirpaths */
    if (!(initAFSDirPath() & AFSDIR_SERVER_PATHS_OK)) {
#ifdef AFS_NT40_ENV
	ReportErrorEventAlt(AFSEVT_SVR_NO_INSTALL_DIR, 0, argv[0],0);
#endif
	fprintf(stderr, "%s: Unable to obtain AFS server directory.\n", argv[0]);
	exit(2);
    }
    nDirs = 0;
    level = rxkad_clear;

    if (argc == 1) /* no arguments */
      goto usage;

    /* support help flag */
    if(strcmp("-help",argv[1])==0)
      goto usage;
    if(strcmp("help",argv[1])==0)
      goto usage;

    for (a=1; a<argc; a++) {
	if (argv[a][0] == '-') { /* parse options */
	    char arg[256];
	    lcstring (arg, argv[a], sizeof(arg));
	    newLevel = StringToLevel (&argv[a][1]);
	    if (newLevel != -1) {
		level = newLevel;	/* set new level */
		continue;
	    }
  usage:
	    Quit ("Usage: upserver [<directory>+] [-crypt <directory>+] [-clear <directory>+] [-auth <directory>+] [-help]\n");
	} else {
	    int dirlen;
	    if (nDirs >= sizeof(dirName)/sizeof(dirName[0]))
		Quit("Too many dirs");
	    dirlen = strlen (argv[a]);
	    if (AddObject(&dirName[nDirs], argv[a])) {
	      printf("%s: Unable to export dir %s. Skipping\n", whoami, argv[a]);
	      continue;
	    }
	    dirLevel[nDirs] = level; /* remember current level */
	    nDirs++;
	}
    }

    if (nDirs == 0) { /* Didn't find any directories to export */
      printf("%s: No directories to export. Quitting\n", whoami);
      exit(1);
    }

    cdir = afsconf_Open(AFSDIR_SERVER_ETC_DIRPATH);
    if (cdir == 0) {
	fprintf (stderr, "Can't get server configuration info (%s)\n",
		 AFSDIR_SERVER_ETC_DIRPATH);
	exit (1);
    }

    /* Initialize Rx, telling it port number this server will use for its
     * single service */
    if (rx_Init(htons(AFSCONF_UPDATEPORT)) < 0) Quit("rx_init");

    /* Create a single security object, in this case the null security object,
     * for unauthenticated connections, which will be used to control security
     * on connections made to this server. */

    /* WHAT A JOKE!  Let's use rxkad at least so we know who we're talking to,
     * then sometimes require full encryption. */

    /* rxnull and rxvab are no longer supported */
    securityObjects[0] = rxnull_NewServerSecurityObject();

    securityObjects[1] = (struct rx_securityClass *) 0;

    securityObjects[2] = rxkad_NewServerSecurityObject
	(rxkad_clear, cdir, afsconf_GetKey, 0);
    if (securityObjects[2] == (struct rx_securityClass *) 0)
	Quit("rxkad_NewServerSecurityObject");

    /* Instantiate a single UPDATE service.  The rxgen-generated procedure
     * which is called to decode requests is passed in here
     * (UPDATE_ExecuteRequest). */
    service = rx_NewService (0, UPDATE_SERVICEID, "UPDATE",
			     securityObjects, 3, UPDATE_ExecuteRequest);
    if (service == (struct rx_service *) 0) Quit("rx_NewService");
    rx_SetMaxProcs(service, 2);

    /* allow super users to manage RX statistics */
    rx_SetRxStatUserOk(update_rxstat_userok);

    rx_StartServer(1); /* Donate this process to the server process pool */
    Quit("StartServer returned?");
    return 0;
}

/* fetch the file name and send it to the remote requester specified by call */

int UPDATE_FetchFile(call, name)
    struct rx_call *call;
    char *name;
{
    int fd = -1;
    int error = 0;
    struct stat status;
    char *reqObject;

    /* construct a local path from a canonical (wire-format) path */
    if ((error = ConstructLocalPath(name, "/", &reqObject))) {
	com_err(whoami, error, "Unable to construct local path");
	return UPDATE_ERROR;
    }

    if (!AuthOkay (call, reqObject)) {
	error = UPDATE_ERROR;
    } else {
	fd = open(reqObject, O_RDONLY, 0);
	if (fd < 0 || fstat(fd, &status) < 0) {
	    printf("Failed to open %s\n", reqObject);
	    error = UPDATE_ERROR;
	}
	if (!error) error = update_SendFile(fd, call, &status);
	if (fd >= 0) close(fd);
    }
    free(reqObject);
    return error;
}

/* fetch dir info about directory name and send it to remote host associated
  with call. */
int UPDATE_FetchInfo(call, name)
   struct rx_call *call;
   char *name;
{
    int fd = -1;
    int error = 0;
    struct stat status;
    char *reqObject;

    /* construct a local path from a canonical (wire-format) path */
    if ((error = ConstructLocalPath(name, "/", &reqObject))) {
	com_err(whoami, error, "Unable to construct local path");
	return UPDATE_ERROR;
    }

    if (!AuthOkay (call, reqObject)) {
	error = UPDATE_ERROR;
    } else {
	/* we only need to stat the obj, not open it. */
	if (stat(reqObject, &status) < 0) {
	    printf("Failed to open %s\n", reqObject);
	    error = UPDATE_ERROR;
	}
	if((status.st_mode & S_IFMT) != S_IFDIR){
	    printf(" file %s is not a directory \n", reqObject);
	    if (fd >= 0) close(fd);
	    error = -1;
	}

	if (fd >= 0) close(fd);
	if (!error) error = update_SendDirInfo(reqObject, call, &status, name);
    }
    free(reqObject);
    return error;
} 

static int Quit(msg, a, b)
    char *msg;
{
    fprintf(stderr, msg, a, b);
    exit(1);
}

int update_SendFile(fd, call, status)
register int fd;
register struct rx_call *call;
register struct stat *status;
{
    char *buffer = (char*) 0;
    int blockSize;
    afs_int32 length, tlen;
#ifdef notdef
    XDR xdr;
#endif
#ifdef	AFS_AIX_ENV
#include <sys/statfs.h>
    struct statfs tstatfs;
#endif

    afs_int32 error = 0;
#ifdef	AFS_AIX_ENV
    /* Unfortunately in AIX valuable fields such as st_blksize are gone from the stat structure!! */
    fstatfs(fd, &tstatfs);
    blockSize = tstatfs.f_bsize;
#elif AFS_NT40_ENV
    blockSize = 4096;
#else
    blockSize = status->st_blksize;
#endif
    length = status->st_size;
    buffer = (char *)malloc(blockSize);
    if (!buffer) {
	printf("malloc failed\n");
	return UPDATE_ERROR;
    }
#ifdef	notdef
    xdrrx_create(&xdr, call, XDR_ENCODE);
    if (!xdr_afs_int32(&xdr, &length)) error = UPDATE_ERROR;
#else
    tlen = htonl(length);
    rx_Write(call, &tlen, sizeof(afs_int32));	/* send length on fetch */
#endif
    while (!error && length) {
	register int nbytes = (length>blockSize?blockSize:length);
	nbytes = read(fd, buffer, nbytes);
	if (nbytes <= 0) {
	    fprintf(stderr, "File system read failed\n");
	    break;
	}
	if (rx_Write(call, buffer, nbytes) != nbytes)
	    break;
	length -= nbytes;
    }
    if (buffer) free(buffer);
    if (length) error = UPDATE_ERROR;
    return error;
}

/* Enumerate dir (name) and write dir entry info into temp file. 
 */
int update_SendDirInfo(name, call, status, origDir)
char *name;                      /* Name of dir to enumerate */
register struct rx_call *call;   /* rx call */
register struct stat *status;    /* stat struct for dir */
char *origDir;                   /* orig name of dir before being localized */
{
    DIR *dirp;
    struct dirent *dp;
    FILE *stream;
    struct stat tstatus;
    char filename[MAXSIZE], dirInfoFile[MAXSIZE];
    int fd, tfd, errcode, error, err;

    error = 0;
    dirp = opendir(name);
    sprintf(dirInfoFile, "%s/upserver.tmp", gettmpdir());
    stream = fopen(dirInfoFile, "w");
    if (!stream) {
	error = EIO;
    } else {
	while((dp = readdir(dirp))){
	    strcpy(filename, name);
	    strcat(filename,"/");
	    strcat(filename,dp->d_name);
	
	    tfd = open(filename, O_RDONLY, 0);
	    if (tfd < 0 || fstat(tfd, &tstatus) < 0) {
		printf("Failed to open %s\n", name);
		error = UPDATE_ERROR;
		goto fail;
	    }
	    if((tstatus.st_mode & S_IFMT) != S_IFDIR){/* not a directory */
		char dirEntry[MAXSIZE];
		
		strcpy(dirEntry, origDir); strcat(dirEntry, "/");
		strcat(dirEntry, dp->d_name);
		err = fprintf(stream, "\"%s\" %u %u %u %u %u %u\n", dirEntry, 
			(unsigned int) tstatus.st_mtime,
			(unsigned int) tstatus.st_size,
			tstatus.st_mode,
			tstatus.st_uid,
			tstatus.st_gid, 
			(unsigned int) tstatus.st_atime);
		if (err < 0) error = EIO;
	    }	
	    err = close(tfd);
	    if(err) {
		printf("could not close file %s \n",filename);
		error = UPDATE_ERROR;
		goto fail;
	    }
	}
    }
fail:
    if (dirp) closedir(dirp);
    if (stream) {
	if (ferror(stream))
	    if (!error) error = UPDATE_ERROR;
	fclose(stream);
    }
    if (error == 0) {
	fd = open(dirInfoFile, O_RDONLY, 0);
	if (fd) {
	    fstat(fd, &tstatus);
	    errcode = update_SendFile(fd, call, &tstatus); 
	    if(errcode) if(!error) error = UPDATE_ERROR;
	    close(fd);
	}
    }
    unlink(dirInfoFile);
    return error;
}


/* AddObject() - Adds the object to the list of exported objects after
 *     converting to a local path.
 *
 * expPath : points to allocated storage in which the exportable path is 
 *           passed back.
 * dir     : dir name passed in for export 
 *
 */  
static int AddObject(char **expPath, char *dir)
{
  int error;
  struct stat statbuf;

  /* construct a local path from a canonical (wire-format) path */
  if ((error = ConstructLocalPath(dir, "/", expPath))) {
      com_err(whoami, error, "Unable to construct local path");
      return error;
  }

  /* stat the object */
  error = stat(*expPath, &statbuf);
  if (error) {
    com_err(whoami, error, ";Can't stat object.");
    return error;
  }
  /* now check if the object has an exportable (file/dir)  type */
  if ( ! (statbuf.st_mode & S_IFDIR) ) {
    fprintf(stderr, "%s: Unacceptable object type for %s\n", whoami, *expPath);
    return -1;
  }

  return 0;
}
