/*
 * Copyright 1985, 1988 by the Massachusetts Institute of Technology.
 *
 * For copying and distribution information, please
 * see the file <mit-cpyright.h>.
 *
 */

#include <afsconfig.h>
#include <afs/param.h>

RCSID("$Header: /cvs/openafs/src/des/make_p_table.c,v 1.6 2002/01/19 09:21:12 shadow Exp $");

#include <mit-cpyright.h>
#include <stdio.h>
#include "des_internal.h"

#define WANT_P_TABLE
#include "tables.h"

extern afs_uint32 swap_byte_bits();
extern afs_uint32 rev_swap_bit_pos_0();
static unsigned char P_temp[32];
static afs_uint32 P_prime[4][256];

void gen(stream)
    FILE *stream;
{
    register int i,j,k,m;
    /* P permutes 32 bit input R1 into 32 bit output R2 */

#ifdef BIG
    /* flip p into p_temp */
    for (i = 0; i<32; i++)
	P_temp[(int) P[rev_swap_bit_pos_0(i)]] = rev_swap_bit_pos_0(i);

    /*
     * now for each byte of input, figure out all possible combinations
     */
    for (i = 0; i <4 ; i ++) {	/* each input byte */
	for (j = 0; j<256; j++) { /* each possible byte value */
	    /* flip bit order */
	    k = j;
	    /* swap_byte_bits(j); */
	    for (m = 0; m < 8; m++) { /* each bit */
		if (k & (1 << m)) {
		    /* set output values */
		    P_prime[i][j] |= 1 << P_temp[(i*8)+m];
		}
	    }
	}
    }

    fprintf(stream,
	    "\n\tstatic afs_uint32 const P_prime[4][256] = {\n\t");
    for (i = 0; i < 4; i++) {
	fprintf(stream,"\n{ ");
	for (j = 0; j < 64; j++) {
	    fprintf(stream,"\n");
	    for (k = 0; k < 4; k++) {
		fprintf(stream,"0x%08lX",(unsigned long)P_prime[i][j*4+k]);
		if ((j == 63) && (k == 3))
		    fprintf(stream, "}");
		if ((i == 3) && (j == 63) && (k == 3))
		    fprintf(stream,"\n};");
		else
		    fprintf(stream,", ");
	    }
	}
    }

#endif
    fprintf(stream,"\n");
}
