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

RCSID("$Header: /cvs/openafs/src/budb/server.c,v 1.6 2001/08/08 00:03:40 shadow Exp $");

#ifdef AFS_NT40_ENV
#include <winsock2.h>
#include <fcntl.h>
#include <WINNT/afsevent.h>
#else
#include <netinet/in.h>
#include <sys/file.h>
#include <sys/time.h>
#include <netdb.h>
#endif
#include <afs/stds.h>
#include <sys/types.h>
#include <time.h>
#include <stdio.h>
#include <afs/cmd.h>
#include <lwp.h>
#include <ubik.h>
#include <rx/xdr.h>
#include <rx/rx.h>
#include <rx/rxkad.h>
#include <rx/rx_globals.h>
#include <afs/cellconfig.h>
#include <afs/auth.h>
#include <afs/bubasics.h>
#include <afs/afsutil.h>
#include <afs/com_err.h>
#include <errno.h>
#ifdef	AFS_AIX32_ENV
#include <signal.h>
#endif
#include "budb_errs.h"
#include "database.h"
#include "error_macros.h"
#include "globals.h"
#include "afs/audit.h"


extern afs_int32 ubik_lastYesTime;
extern afs_int32 ubik_nBuffers;

struct ubik_dbase	*BU_dbase;
struct afsconf_dir 	*BU_conf;	/* for getting cell info */

char lcell[MAXKTCREALMLEN];
afs_int32 myHost = 0;
int  helpOption;

/* server's global configuration information. This is exported to other
 * files/routines
 */

buServerConfT	globalConf;
buServerConfP	globalConfPtr = &globalConf;
char dbDir[AFSDIR_PATH_MAX], cellConfDir[AFSDIR_PATH_MAX];
/* debugging control */
int debugging = 0;

/* check whether caller is authorized to manage RX statistics */
int BU_rxstat_userok(call)
    struct rx_call *call;
{
    return afsconf_SuperUser(BU_conf, call, (char *)0);
}

int
convert_cell_to_ubik (cellinfo, myHost, serverList)
     struct afsconf_cell *cellinfo;
     afs_int32		      *myHost;
     afs_int32		      *serverList;
{   
    int  i;
    char hostname[64];
    struct hostent *th;

    /* get this host */
    gethostname(hostname,sizeof(hostname));
    th = gethostbyname(hostname);
    if (!th)
    {
	printf("prserver: couldn't get address of this host.\n");
	BUDB_EXIT(1);
    }
    memcpy(myHost, th->h_addr, sizeof(afs_int32));

    for (i=0; i<cellinfo->numServers; i++)
	/* omit my host from serverList */
	if (cellinfo->hostAddr[i].sin_addr.s_addr != *myHost) 
	    *serverList++ = cellinfo->hostAddr[i].sin_addr.s_addr;

    *serverList = 0;			/* terminate list */
    return 0;
}

/* MyBeforeProc
 *      The whole purpose of MyBeforeProc is to detect
 *      if the -help option was not within the command line.
 *      If it were, this routine would never have been called.
 */
static int MyBeforeProc(as)
   register struct cmd_syndesc *as;
{
   helpOption = 0;
   return 0;
}

/* initializeCommands
 *	initialize all the supported commands and their arguments
 */

initializeArgHandler()
{
    struct cmd_syndesc *cptr;

    int argHandler();

    cmd_SetBeforeProc(MyBeforeProc, (char *)0);

    cptr = cmd_CreateSyntax((char *) 0, argHandler, (char *) 0,
                            "Backup database server");

    cmd_AddParm(cptr, "-database", CMD_SINGLE, CMD_OPTIONAL,
                "database directory");

    cmd_AddParm(cptr, "-cellservdb", CMD_SINGLE, CMD_OPTIONAL,
                "cell configuration directory");

    cmd_AddParm(cptr, "-resetdb", CMD_FLAG, CMD_OPTIONAL,
                "truncate the database");

    cmd_AddParm(cptr, "-noauth", CMD_FLAG, CMD_OPTIONAL,
                "run without authentication");

    cmd_AddParm(cptr, "-smallht", CMD_FLAG, CMD_OPTIONAL,
                "use small hash tables");

    cmd_AddParm(cptr, "-servers", CMD_LIST, CMD_OPTIONAL,
                "list of ubik database servers");
}

int
argHandler(as, arock)
     struct cmd_syndesc *as;
     char *arock;
{

    /* globalConfPtr provides the handle for the configuration information */

    /* database directory */
    if ( as->parms[0].items != 0 )
    {
	globalConfPtr->databaseDirectory = 
	    		(char *) malloc(strlen(as->parms[0].items->data)+1);
	if ( globalConfPtr->databaseDirectory == 0 )
	    BUDB_EXIT(-1);
	strcpy(globalConfPtr->databaseDirectory, as->parms[0].items->data);
    }

    /* -cellservdb, cell configuration directory */
    if ( as->parms[1].items != 0 )
    {
	globalConfPtr->cellConfigdir = 
	    		(char *) malloc(strlen(as->parms[1].items->data)+1);
	if ( globalConfPtr->cellConfigdir == 0 )
	    BUDB_EXIT(-1);

	strcpy(globalConfPtr->cellConfigdir, as->parms[1].items->data);

	globalConfPtr->debugFlags |= DF_RECHECKNOAUTH;
    }

    /* truncate the database */
    if ( as->parms[2].items != 0 )
	truncateDatabase();

    /* run without authentication */
    if ( as->parms[3].items != 0 )
	globalConfPtr->debugFlags |= DF_NOAUTH;

    /* use small hash tables */
    if ( as->parms[4].items != 0 )
	globalConfPtr->debugFlags |= DF_SMALLHT;

    /* user provided list of ubik database servers */
    if ( as->parms[5].items != 0 )
	parseServerList(as->parms[5].items);

    return 0;
}

/* --- */

parseServerList(itemPtr)
     struct cmd_item *itemPtr;
{
    struct cmd_item *save;
    char **serverArgs;
    char **ptr;
    afs_int32 nservers = 0;
    afs_int32 code = 0;

    save = itemPtr;

    /* compute number of servers in the list */
    while ( itemPtr )
    {
	nservers++;
	itemPtr = itemPtr->next;
    }

    LogDebug(3, "%d servers\n", nservers);
    
    /* now can allocate the space for the server arguments */
    serverArgs = (char **) malloc( (nservers+2) * sizeof(char *) );
    if ( serverArgs == 0 )
	ERROR(-1);

    ptr = serverArgs;
    *ptr++ = "";
    *ptr++ = "-servers";

    /* now go through and construct the list of servers */
    itemPtr = save;
    while ( itemPtr )
    {
	*ptr++ = itemPtr->data;
	itemPtr = itemPtr->next;
    }

    code = ubik_ParseServerList(nservers+2, serverArgs,
				&globalConfPtr->myHost,
				globalConfPtr->serverList);
    if ( code )
	ERROR(code);
 
    /* free space for the server args */
    free( (char *) serverArgs);
    
error_exit:
    return(code);
}

/* truncateDatabase
 *	truncates just the database file.
 */

truncateDatabase()
{
    char *path;
    afs_int32 code = 0;
    int fd;

    path = (char *) malloc(strlen(globalConfPtr->databaseDirectory) +
			   strlen(globalConfPtr->databaseName) +
			   strlen(globalConfPtr->databaseExtension) + 1);
    if ( path == 0 )
	ERROR(-1);

    /* construct the database name */
    strcpy(path, globalConfPtr->databaseDirectory);
    strcat(path, globalConfPtr->databaseName);
    strcat(path, globalConfPtr->databaseExtension);

    fd = open(path, O_RDWR, 0755);
    if (!fd) {
	code = errno;
    } else {
	if (ftruncate(fd, 0) != 0 ) {
	    code = errno;
	} else
	    close(fd);
    }

error_exit:
    return(code);
}


/* --- */

#include "AFS_component_version_number.c"

main(argc, argv)
     int   argc;
     char *argv[];
{
    char *whoami = argv[0];
    char *dbNamePtr = 0;
    struct afsconf_cell  cellinfo;
    time_t  currentTime;
    afs_int32  code = 0;

    struct rx_service *tservice;
    struct rx_securityClass *sca[3];
    
    extern int afsconf_ServerAuth();
    extern int afsconf_CheckAuth();

    extern int rx_stackSize;
    extern struct rx_securityClass *rxnull_NewServerSecurityObject();
    extern int BUDB_ExecuteRequest();
    
#ifdef AFS_NT40_ENV
    /* initialize winsock */
    if (afs_winsockInit()<0) {
      ReportErrorEventAlt(AFSEVT_SVR_WINSOCK_INIT_FAILED, 0, argv[0],0);
      fprintf(stderr, "%s: Couldn't initialize winsock.\n", whoami);
      exit(1);
    }
#endif

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
    sigaction(SIGSEGV, &nsa, NULL);
    sigaction(SIGABRT, &nsa, NULL);
#endif
    osi_audit(BUDB_StartEvent, 0, AUD_END);

    initialize_budb_error_table();
    initializeArgHandler();

    /* Initialize dirpaths */
    if (!(initAFSDirPath() & AFSDIR_SERVER_PATHS_OK)) {
#ifdef AFS_NT40_ENV
	ReportErrorEventAlt(AFSEVT_SVR_NO_INSTALL_DIR, 0, argv[0], 0);
#endif
	com_err(whoami,errno,"; Unable to obtain AFS server directory.");
	exit(2);
    }

    memset(globalConfPtr, 0, sizeof(*globalConfPtr));

    /* set default configuration values */
    strcpy(dbDir, AFSDIR_SERVER_DB_DIRPATH);
    strcat(dbDir, "/");
    globalConfPtr->databaseDirectory = dbDir;
    globalConfPtr->databaseName = DEFAULT_DBPREFIX;
    strcpy(cellConfDir, AFSDIR_SERVER_ETC_DIRPATH);
    globalConfPtr->cellConfigdir = cellConfDir;

    /* open the log file */
/*
    globalConfPtr->log = fopen(DEFAULT_LOGNAME,"a");
    if ( globalConfPtr->log == NULL )
    {   
	printf("Can't open log file %s - aborting\n", DEFAULT_LOGNAME);
	BUDB_EXIT(-1);
    }
*/
    
    srandom (1);

    /* process the user supplied args */
    helpOption = 1;
    code = cmd_Dispatch(argc, argv);
    if ( code )
	ERROR(code);

    /* exit if there was a help option */
    if (helpOption)
        BUDB_EXIT(0);

    /* open the log file */
    globalConfPtr->log = fopen(AFSDIR_SERVER_BUDBLOG_FILEPATH, "a");
    if ( globalConfPtr->log == NULL )
    {
	printf("Can't open log file %s - aborting\n", 
	       AFSDIR_SERVER_BUDBLOG_FILEPATH);
	BUDB_EXIT(-1);
    }

	/* keep log closed so can remove it */

	fclose(globalConfPtr->log);

    /* open the cell's configuration directory */
    LogDebug(4, "opening %s\n", globalConfPtr->cellConfigdir);

    BU_conf = afsconf_Open(globalConfPtr->cellConfigdir);
    if ( BU_conf == 0 )
    {
	LogError(code, "Failed getting cell info\n");
	com_err(whoami, code, "Failed getting cell info");
	ERROR(BUDB_NOCELLS);
    }

    code = afsconf_GetLocalCell(BU_conf, lcell, sizeof(lcell));
    if ( code )
    {
	LogError(0, "** Can't determine local cell name!\n");
	ERROR(code);
    }

    if ( globalConfPtr->myHost == 0 )
    {
	/* if user hasn't supplied a list of servers, extract server
	 * list from the cell's database
	 */

	LogDebug(1, "Using server list from %s cell database.\n", lcell);

	code = afsconf_GetCellInfo (BU_conf, lcell, 0, &cellinfo);
	code = convert_cell_to_ubik (&cellinfo, 
				     &globalConfPtr->myHost,
				     globalConfPtr->serverList);
	if ( code )
	    ERROR(code);
    }

    /* initialize ubik */
    ubik_CRXSecurityProc = afsconf_ClientAuth;
    ubik_CRXSecurityRock = (char *)BU_conf;

    ubik_SRXSecurityProc = afsconf_ServerAuth;
    ubik_SRXSecurityRock = (char *)BU_conf;

    ubik_CheckRXSecurityProc = afsconf_CheckAuth;
    ubik_CheckRXSecurityRock = (char *)BU_conf;

    ubik_nBuffers = 400;

    dbNamePtr = (char *) malloc(strlen(globalConfPtr->databaseDirectory) +
                           strlen(globalConfPtr->databaseName) + 1);
    if ( dbNamePtr == 0 )
        ERROR(-1);

    /* construct the database name */
    strcpy(dbNamePtr, globalConfPtr->databaseDirectory);
    strcat(dbNamePtr, globalConfPtr->databaseName);	/* name prefix */

    rx_SetRxDeadTime(60);                     /* 60 seconds inactive before timeout */

    code = ubik_ServerInit(globalConfPtr->myHost,
			   htons(AFSCONF_BUDBPORT), 
			   globalConfPtr->serverList, 
			   dbNamePtr,			/* name prefix */
			   &BU_dbase);
    if (code)
    {
	LogError(code, "Ubik init failed\n");
	com_err(whoami, code, "Ubik init failed");
	ERROR(code);
    }

    sca[RX_SCINDEX_NULL] = rxnull_NewServerSecurityObject();
    sca[RX_SCINDEX_VAB] = 0;
    sca[RX_SCINDEX_KAD] = rxkad_NewServerSecurityObject(rxkad_clear,
							  BU_conf,
							  afsconf_GetKey,
							  (char *) 0);

    /* These two lines disallow jumbograms */
    rx_maxReceiveSize = OLD_MAX_PACKET_SIZE;
    rxi_nSendFrags = rxi_nRecvFrags = 1;

    tservice = rx_NewService(0, BUDB_SERVICE, "BackupDatabase",
			     sca, 3, BUDB_ExecuteRequest);
    if (tservice == (struct rx_service *)0) 
    {
	LogError(0, "Could not create backup database rx service\n");
	printf("Could not create backup database rx service\n");
	BUDB_EXIT(3);
    }
    rx_SetMinProcs(tservice, 1);
    rx_SetMaxProcs(tservice, 3);
    rx_SetStackSize(tservice, 10000);

    /* allow super users to manage RX statistics */
    rx_SetRxStatUserOk(BU_rxstat_userok);

    /* misc. initialization */

    /* database dump synchronization */
    memset(dumpSyncPtr, 0, sizeof(*dumpSyncPtr));
    Lock_Init(&dumpSyncPtr->ds_lock);

    rx_StartServer(0);			/* start handling requests */

    code = InitProcs();
    if ( code )
	ERROR(code);


    currentTime = time(0);
    LogError(0, "Ready to process requests at %s\n", ctime(&currentTime));

    rx_ServerProc();			/* donate this LWP */

error_exit:
    osi_audit(BUDB_FinishEvent, code, AUD_END);
    return(code);
}


consistencyCheckDb()
{
    /* do consistency checks on structure sizes */
    if ( (sizeof(struct htBlock) > BLOCKSIZE)
    ||	 (sizeof(struct vfBlock) > BLOCKSIZE)
    ||   (sizeof(struct viBlock) > BLOCKSIZE)
    ||   (sizeof(struct dBlock) > BLOCKSIZE)
    ||   (sizeof(struct tBlock) > BLOCKSIZE) 
       )
    {
	fprintf (stderr, "Block layout error!\n");
	BUDB_EXIT (99);
    }
}

/*VARARGS*/
LogDebug(level, a,b,c,d,e,f,g,h,i)
    int level;
    char *a, *b, *c, *d, *e, *f, *g, *h, *i;
{

    if ( debugging >= level)
    {
	/* log normally closed so can remove it */
    	globalConfPtr->log = fopen(AFSDIR_SERVER_BUDBLOG_FILEPATH, "a");
	if ( globalConfPtr->log != NULL )
	{
	    	fprintf(globalConfPtr->log, a, b, c, d, e, f, g, h, i);
		fflush(globalConfPtr->log);
		fclose(globalConfPtr->log);
	}
    }
}

static char *TimeStamp(time_t t)
{
  struct tm *lt;
  static char timestamp[20];

  lt = localtime(&t);
  strftime (timestamp, 20, "%m/%d/%Y %T", lt);
  return timestamp;
}

/*VARARGS*/
Log(a,b,c,d,e,f,g,h,i)
    char *a, *b, *c, *d, *e, *f, *g, *h, *i;
{
    time_t now;

    globalConfPtr->log = fopen(AFSDIR_SERVER_BUDBLOG_FILEPATH, "a");
    if ( globalConfPtr->log != NULL )
    {
        now = time(0);
	fprintf(globalConfPtr->log, "%s ", TimeStamp(now));

	fprintf(globalConfPtr->log, a, b, c, d, e, f, g, h, i);
	fflush(globalConfPtr->log);
	fclose(globalConfPtr->log);
    }
}

/*VARARGS*/
LogError(code, a,b,c,d,e,f,g,h,i)
long code;
char *a, *b, *c, *d, *e, *f, *g, *h, *i;
{
    time_t now;

    globalConfPtr->log = fopen(AFSDIR_SERVER_BUDBLOG_FILEPATH, "a");

    if ( globalConfPtr->log != NULL )
    {
        now = time(0);
	fprintf(globalConfPtr->log, "%s ", TimeStamp(now));

	if ( code )
	    fprintf(globalConfPtr->log, "%s: %s\n", error_table_name(code),
		    error_message(code));
	fprintf(globalConfPtr->log, a, b, c, d, e, f, g, h, i);
	fflush(globalConfPtr->log);
	fclose(globalConfPtr->log);
    }
}


/*  ----------------
 * debug
 * ----------------
 */


LogNetDump(dumpPtr)
     struct dump *dumpPtr;
{
    struct dump hostDump;
    extern buServerConfP globalConfPtr;
    
    dump_ntoh(dumpPtr, &hostDump);

    globalConfPtr->log = fopen(AFSDIR_SERVER_BUDBLOG_FILEPATH, "a");
    if ( globalConfPtr->log != NULL )
    {
	printDump(globalConfPtr->log, &hostDump);
	fclose(globalConfPtr->log);
    }
}
