/*
 * Copyright 1988 by the Massachusetts Institute of Technology.
 *
 * For copying and distribution information, please see the file
 * <mit-cpyright.h>.
 *
 * Miscellaneous debug printing utilities
 */

#include <afsconfig.h>
#include <afs/param.h>

RCSID
    ("$Header: /cvs/openafs/src/des/util.c,v 1.8 2005/06/18 23:52:48 shadow Exp $");

#include "mit-cpyright.h"
#ifndef KERNEL
#include <stdio.h>
#endif
#include <sys/types.h>
#include <des.h>
#include "des_prototypes.h"
#include "stats.h"

#ifndef AFS_PTHREAD_ENV 
struct rxkad_stats rxkad_stats = { { 0 } }; 
#endif

int
des_cblock_print_file(des_cblock * x, FILE * fp)
{
    unsigned char *y = (unsigned char *)x;
    register int i = 0;
    fprintf(fp, " 0x { ");

    while (i++ < 8) {
	fprintf(fp, "%x", *y++);
	if (i < 8)
	    fprintf(fp, ", ");
    }
    fprintf(fp, " }");

    return (0);
}

#ifdef DEBUG
int
des_debug_print(char *area, int x, char *arg1, char *arg2)
{
    ;
}
#endif
