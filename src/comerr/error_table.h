/*
 * Copyright 1988 by the Student Information Processing Board of the
 * Massachusetts Institute of Technology.
 *
 * For copyright info, see mit-sipb-cr.h.
 */

#include <afs/param.h>

#include <errno.h>

#ifndef _ET_H

struct error_table {
    char const * const * msgs;
    afs_int32 base;
    int n_msgs;
};
struct et_list {
    struct et_list *next;
    const struct error_table *table;
};


#define	ERRCODE_RANGE	8	/* # of bits to shift table number */
#define	BITS_PER_CHAR	6	/* # bits to shift per character in name */

extern const char *error_table_name();
extern void add_to_error_table(struct et_list *new_table);
#define _ET_H
#endif
