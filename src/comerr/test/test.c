#include <afsconfig.h>
#include <afs/param.h>

RCSID
    ("$Header: /cvs/openafs/src/comerr/test/test.c,v 1.6 2003/07/15 23:14:54 shadow Exp $");

#include <stdio.h>
#include <errno.h>
#include <afs/com_err.h>
#ifdef AFS_NT40_ENV
#include <afs/afsutil.h>
#endif
#include "test1.h"
#include "test2.h"

extern int sys_nerr, errno;

main()
{
    printf("Before initiating error table:\n\n");
    printf("Table name '%s'\n", error_table_name(KRB_MK_AP_TGTEXP));
    printf("UNIX  name '%s'\n", error_table_name(EPERM));
    printf("Msg TGT-expired is '%s'\n", error_message(KRB_MK_AP_TGTEXP));
    printf("Msg EPERM is '%s'\n", error_message(EPERM));
    printf("Msg FOO_ERR is '%s'\n", error_message(FOO_ERR));
    printf("Msg {sys_nerr-1} is '%s'\n", error_message(sys_nerr - 1));
    printf("Msg {sys_nerr} is '%s'\n", error_message(sys_nerr));

    printf("With 0: tgt-expired -> %s\n", error_message(KRB_MK_AP_TGTEXP));

    initialize_KRB_error_table();
    initialize_KRB_error_table();
    printf("KRB error table initialized:  base %d (%s), name %s\n",
	   ERROR_TABLE_BASE_KRB, error_message(ERROR_TABLE_BASE_KRB),
	   error_table_name(ERROR_TABLE_BASE_KRB));
    printf("With krb: tgt-expired -> %s\n", error_message(KRB_MK_AP_TGTEXP));

    initialize_QUUX_error_table();
    printf("QUUX error table initialized: base %d (%s), name %s\n",
	   ERROR_TABLE_BASE_QUUX, error_message(ERROR_TABLE_BASE_QUUX),
	   error_table_name(ERROR_TABLE_BASE_QUUX));

    printf("Msg for TGT-expired is '%s'\n", error_message(KRB_MK_AP_TGTEXP));
    printf("Msg {sys_nerr-1} is '%s'\n", error_message(sys_nerr - 1));
    printf("Msg FOO_ERR is '%s'\n", error_message(FOO_ERR));
    printf("Msg KRB_SKDC_CANT is '%s'\n", error_message(KRB_SKDC_CANT));
    printf("Msg 1e6 (8B 64) is '%s'\n", error_message(1000000));
    printf("\n\nCOM_ERR tests:\n");
    com_err("whoami", FOO_ERR, (char *)NULL);
    com_err("whoami", FOO_ERR, " -- message goes %s", "here");
    com_err("whoami", 0, NULL);
    com_err("whoami", 0, "error number %d\n", 0);

    return 0;
}
