/* @(#)rpc_parse.c	1.1 87/11/04 3.9 RPCSRC */
/*
 * Sun RPC is a product of Sun Microsystems, Inc. and is provided for
 * unrestricted use provided that this legend is included on all tape
 * media and as a part of the software program in whole or part.  Users
 * may copy or modify Sun RPC without charge, but are not authorized
 * to license or distribute it to anyone else except as part of a product or
 * program developed by the user.
 * 
 * SUN RPC IS PROVIDED AS IS WITH NO WARRANTIES OF ANY KIND INCLUDING THE
 * WARRANTIES OF DESIGN, MERCHANTIBILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE, OR ARISING FROM A COURSE OF DEALING, USAGE OR TRADE PRACTICE.
 * 
 * Sun RPC is provided with no support and without any obligation on the
 * part of Sun Microsystems, Inc. to assist in its use, correction,
 * modification or enhancement.
 * 
 * SUN MICROSYSTEMS, INC. SHALL HAVE NO LIABILITY WITH RESPECT TO THE
 * INFRINGEMENT OF COPYRIGHTS, TRADE SECRETS OR ANY PATENTS BY SUN RPC
 * OR ANY PART THEREOF.
 * 
 * In no event will Sun Microsystems, Inc. be liable for any lost revenue
 * or profits or other special, indirect and consequential damages, even if
 * Sun has been advised of the possibility of such damages.
 * 
 * Sun Microsystems, Inc.
 * 2550 Garcia Avenue
 * Mountain View, California  94043
 */

/*
 * rpc_parse.c, Parser for the RPC protocol compiler 
 * Copyright (C) 1987 Sun Microsystems, Inc.
 */
#include <afsconfig.h>
#include <afs/param.h>

RCSID("$Header: /cvs/openafs/src/rxgen/rpc_parse.c,v 1.10 2001/08/08 00:04:05 shadow Exp $");

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#ifdef HAVE_STRING_H
#include <string.h>
#else
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#endif
#include "rpc_util.h"
#include "rpc_scan.h"
#include "rpc_parse.h"

list *proc_defined[MAX_PACKAGES], *special_defined, *typedef_defined, *uniondef_defined;
char *SplitStart = NULL;
char *SplitEnd = NULL;
char *MasterPrefix = NULL;
char *ServerPrefix = "";
char *PackagePrefix[MAX_PACKAGES];
char *PackageStatIndex[MAX_PACKAGES];
int no_of_stat_funcs = 0; /*
				  * current function number in client interface
				  * starts at 0
				  */
int no_of_stat_funcs_header[MAX_PACKAGES]; /*
					 * Total number of functions in client
					 * interface
					 */
int no_of_opcodes[MAX_PACKAGES], master_no_of_opcodes = 0;
int lowest_opcode[MAX_PACKAGES], master_lowest_opcode = 99999;
int highest_opcode[MAX_PACKAGES], master_highest_opcode = 0;
int master_opcodenumber = 99999;
int opcodesnotallowed[MAX_PACKAGES];
int combinepackages = 0;
int PackageIndex = -1;
int PerProcCounter = 0;
int Multi_Init = 0;

/*
 * Character arrays to keep list of function names as we process the file
 */

char function_list[MAX_PACKAGES]
		  [MAX_FUNCTIONS_PER_PACKAGE]
		  [MAX_FUNCTION_NAME_LEN];
int function_list_index;

extern int pushed, scan_print;

static isdefined();
static def_struct();
static def_program();
static def_enum();
static def_const();
static def_union();
static def_typedef();
static get_declaration();
static get_type();
static unsigned_dec();
static def_package();
static def_prefix();
static def_startingopcode();
static def_statindex();
static def_split();
static customize_struct();
static def_special();
static check_proc();
static int InvalidConstant();
static opcodenum_is_defined();
static analyze_ProcParams();
static generate_code();
static handle_split_proc();
static do_split();
static hdle_param_tok();
static get1_param_type();
static get_param_type();
static cs_Proc_CodeGeneration();
static cs_ProcName_setup();
static cs_ProcParams_setup();
static cs_ProcMarshallInParams_setup();
static cs_ProcSendPacket_setup();
static cs_ProcUnmarshallOutParams_setup();
static cs_ProcTail_setup();
static ss_Proc_CodeGeneration();
static ss_ProcName_setup();
static ss_ProcParams_setup();
static ss_ProcProto_setup();
static ss_ProcSpecial_setup();
static ss_ProcUnmarshallInParams_setup();
static ss_ProcCallRealProc_setup();
static ss_ProcMarshallOutParams_setup();
static ss_ProcTail_setup();
static er_ProcDeclExterns_setup();
static er_ProcProcsArray_setup();
static er_ProcMainBody_setup();
static er_HeadofOldStyleProc_setup();
static er_BodyofOldStyleProc_setup();
static proc_er_case();
static er_TailofOldStyleProc_setup();

/*
 * return the next definition you see
 */
definition *
get_definition()
{
	definition *defp;
	token tok;

	defp = ALLOC(definition);
	memset((char *)defp, 0, sizeof(definition));
	get_token(&tok);
	switch (tok.kind) {
	case TOK_STRUCT:
		def_struct(defp);
		break;
	case TOK_UNION:
		def_union(defp);
		break;
	case TOK_TYPEDEF:
		def_typedef(defp);
		break;
	case TOK_ENUM:
		def_enum(defp);
		break;
	case TOK_PROGRAM:
		def_program(defp);
		break;
	case TOK_CONST:
		def_const(defp);
		break;
	case TOK_EOF:
		return (NULL);
		break;
	case TOK_PACKAGE:
	    def_package(defp);
	    break;
	case TOK_PREFIX:
	    def_prefix(defp);
	    break;	    
	case TOK_STATINDEX:
	    def_statindex(defp);
	    break;
	case TOK_SPECIAL:
	    {
	    declaration dec;
	    def_special(&dec, defp);
	    break;
	    }			    
	case TOK_STARTINGOPCODE:
	    def_startingopcode(defp);
	    break;
	case TOK_CUSTOMIZED:
	    get_token(&tok);
	    def_struct(defp);
	    customize_struct(defp);
	    break;
	case TOK_SPLITPREFIX:
	    def_split(defp);
	    break;
	case TOK_PROC:
	    get_token(&tok);
	    if (tok.kind == TOK_LPAREN) {
		unget_token(&tok);
		check_proc(defp, &tok, 1);
	    }
	    else
		check_proc(defp, &tok, 0);
	    break;
	case TOK_IDENT:
	    check_proc(defp, &tok, 0);
	    break;
	case TOK_LPAREN:
	    unget_token(&tok);
	    check_proc(defp, &tok, 1);
	    break;
	default:
		error("definition keyword expected");
	}
	if (!IsRxgenToken(&tok))
	{
	    scan(TOK_SEMICOLON,&tok);
	    isdefined(defp);
	} else pushed = 0;
	return (defp);
}

static
isdefined(defp)
	definition *defp;
{
	STOREVAL(&defined, defp);
}


static
def_struct(defp)
	definition *defp;
{
	token tok;
	declaration dec;
	decl_list *decls;
	decl_list **tailp;

	defp->def_kind = DEF_STRUCT;

	scan(TOK_IDENT, &tok);
	defp->def_name = tok.str;
	scan(TOK_LBRACE, &tok);
	tailp = &defp->def.st.decls;
	do {
		get_declaration(&dec, DEF_STRUCT);
		decls = ALLOC(decl_list);
		decls->decl = dec;
		*tailp = decls;
		tailp = &decls->next;
		scan(TOK_SEMICOLON, &tok);
		peek(&tok);
	} while (tok.kind != TOK_RBRACE);
	get_token(&tok);
	*tailp = NULL;
}

static
def_program(defp)
	definition *defp;
{
	token tok;
	version_list *vlist;
	version_list **vtailp;
	proc_list *plist;
	proc_list **ptailp;

	defp->def_kind = DEF_PROGRAM;
	scan(TOK_IDENT, &tok);
	defp->def_name = tok.str;
	scan(TOK_LBRACE, &tok);
	vtailp = &defp->def.pr.versions;
	scan(TOK_VERSION, &tok);
	do {
		scan(TOK_IDENT, &tok);
		vlist = ALLOC(version_list);
		vlist->vers_name = tok.str;
		scan(TOK_LBRACE, &tok);
		ptailp = &vlist->procs;
		do {
			plist = ALLOC(proc_list);
			get_type(&plist->res_prefix, &plist->res_type, DEF_PROGRAM);
			if (streq(plist->res_type, "opaque")) {
				error("illegal result type");
			}
			scan(TOK_IDENT, &tok);
			plist->proc_name = tok.str;
			scan(TOK_LPAREN, &tok);
			get_type(&plist->arg_prefix, &plist->arg_type, DEF_PROGRAM);
			if (streq(plist->arg_type, "opaque")) {
				error("illegal argument type");
			}
			scan(TOK_RPAREN, &tok);
			scan(TOK_EQUAL, &tok);
			scan_num(&tok);
			scan(TOK_SEMICOLON, &tok);
			plist->proc_num = tok.str;
			*ptailp = plist;
			ptailp = &plist->next;
			peek(&tok);
		} while (tok.kind != TOK_RBRACE);
		*vtailp = vlist;
		vtailp = &vlist->next;
		scan(TOK_RBRACE, &tok);
		scan(TOK_EQUAL, &tok);
		scan_num(&tok);
		vlist->vers_num = tok.str;
		scan(TOK_SEMICOLON, &tok);
		scan2(TOK_VERSION, TOK_RBRACE, &tok);
	} while (tok.kind == TOK_VERSION);
	scan(TOK_EQUAL, &tok);
	scan_num(&tok);
	defp->def.pr.prog_num = tok.str;
	*vtailp = NULL;
}

static
def_enum(defp)
	definition *defp;
{
	token tok;
	enumval_list *elist;
	enumval_list **tailp;

	defp->def_kind = DEF_ENUM;
	scan(TOK_IDENT, &tok);
	defp->def_name = tok.str;
	scan(TOK_LBRACE, &tok);
	tailp = &defp->def.en.vals;
	do {
		scan(TOK_IDENT, &tok);
		elist = ALLOC(enumval_list);
		elist->name = tok.str;
		elist->assignment = NULL;
		scan3(TOK_COMMA, TOK_RBRACE, TOK_EQUAL, &tok);
		if (tok.kind == TOK_EQUAL) {
			scan_num(&tok);
			elist->assignment = tok.str;
			scan2(TOK_COMMA, TOK_RBRACE, &tok);
		}
		*tailp = elist;
		tailp = &elist->next;
	} while (tok.kind != TOK_RBRACE);
	*tailp = NULL;
}

static
def_const(defp)
	definition *defp;
{
	token tok;

	defp->def_kind = DEF_CONST;
	scan(TOK_IDENT, &tok);
	defp->def_name = tok.str;
	scan(TOK_EQUAL, &tok);
	scan2(TOK_IDENT, TOK_STRCONST, &tok);
	defp->def.co = tok.str;
}

static
def_union(defp)
	definition *defp;
{
	token tok;
	declaration dec;
	case_list *cases;
	case_list **tailp;

	defp->def_kind = DEF_UNION;
	scan(TOK_IDENT, &tok);
	defp->def_name = tok.str;
	scan(TOK_SWITCH, &tok);
	scan(TOK_LPAREN, &tok);
	get_declaration(&dec, DEF_UNION);
	defp->def.un.enum_decl = dec;
	tailp = &defp->def.un.cases;
	scan(TOK_RPAREN, &tok);
	scan(TOK_LBRACE, &tok);
	scan(TOK_CASE, &tok);
	while (tok.kind == TOK_CASE) {
		scan(TOK_IDENT, &tok);
		cases = ALLOC(case_list);
		cases->case_name = tok.str;
		scan(TOK_COLON, &tok);
		get_declaration(&dec, DEF_UNION);
		cases->case_decl = dec;
		*tailp = cases;
		tailp = &cases->next;
		scan(TOK_SEMICOLON, &tok);
		scan3(TOK_CASE, TOK_DEFAULT, TOK_RBRACE, &tok);
	}
	*tailp = NULL;
	if (tok.kind == TOK_DEFAULT) {
		scan(TOK_COLON, &tok);
		get_declaration(&dec, DEF_UNION);
		defp->def.un.default_decl = ALLOC(declaration);
		*defp->def.un.default_decl = dec;
		scan(TOK_SEMICOLON, &tok);
		scan(TOK_RBRACE, &tok);
	} else {
		defp->def.un.default_decl = NULL;
	}
}


static
def_typedef(defp)
	definition *defp;
{
	declaration dec;

	defp->def_kind = DEF_TYPEDEF;
	get_declaration(&dec, DEF_TYPEDEF);
	defp->def_name = dec.name;
	defp->def.ty.old_prefix = dec.prefix;
	defp->def.ty.old_type = dec.type;
	defp->def.ty.rel = dec.rel;
	defp->def.ty.array_max = dec.array_max;
}


static
get_declaration(dec, dkind)
	declaration *dec;
	defkind dkind;
{
	token tok;

	get_type(&dec->prefix, &dec->type, dkind);
	dec->rel = REL_ALIAS;
	if (streq(dec->type, "void")) {
		return;
	}
	scan2(TOK_STAR, TOK_IDENT, &tok);
	if (tok.kind == TOK_STAR) {
		dec->rel = REL_POINTER;
		scan(TOK_IDENT, &tok);
	}
	dec->name = tok.str;
	if (peekscan(TOK_LBRACKET, &tok)) {
		if (dec->rel == REL_POINTER) {
			error("no array-of-pointer declarations -- use typedef");
		}
		dec->rel = REL_VECTOR;
		scan_num(&tok);
		dec->array_max = tok.str;
		scan(TOK_RBRACKET, &tok);
	} else if (peekscan(TOK_LANGLE, &tok)) {
		if (dec->rel == REL_POINTER) {
			error("no array-of-pointer declarations -- use typedef");
		}
		dec->rel = REL_ARRAY;
		if (peekscan(TOK_RANGLE, &tok)) {
			dec->array_max = "~0";	/* unspecified size, use max */
		} else {
			scan_num(&tok);
			dec->array_max = tok.str;
			scan(TOK_RANGLE, &tok);
		}
	}
	if (streq(dec->type, "opaque")) {
		if (dec->rel != REL_ARRAY && dec->rel != REL_VECTOR) {
			error("array declaration expected");
		}
	} else if (streq(dec->type, "string")) {
		if (dec->rel != REL_ARRAY) {
			error(" variable-length array declaration expected");
		}
	}
}


static
get_type(prefixp, typep, dkind)
	char **prefixp;
	char **typep;
	defkind dkind;
{
	token tok;

	*prefixp = NULL;
	get_token(&tok);
	switch (tok.kind) {
	case TOK_IDENT:
		*typep = tok.str;
		break;
	case TOK_STRUCT:
	case TOK_ENUM:
	case TOK_UNION:
		*prefixp = tok.str;
		scan(TOK_IDENT, &tok);
		*typep = tok.str;
		break;
	case TOK_UNSIGNED:
		unsigned_dec(typep);
		break;
	case TOK_SHORT:
		*typep = "short";
		(void) peekscan(TOK_INT, &tok);
		break;
	case TOK_INT32:
		*typep = "afs_int32";
		(void) peekscan(TOK_INT, &tok);
		break;
	case TOK_VOID:
		if (dkind != DEF_UNION && dkind != DEF_PROGRAM) {
			error("voids allowed only inside union and program definitions");
		}
		*typep = tok.str;
		break;
	case TOK_STRING:
	case TOK_OPAQUE:
	case TOK_CHAR:
	case TOK_INT:
	case TOK_FLOAT:
	case TOK_DOUBLE:
	case TOK_BOOL:
	case TOK_AFSUUID:
		*typep = tok.str;
		break;
	default:
		error("expected type specifier");
	}
}


static
unsigned_dec(typep)
	char **typep;
{
	token tok;

	peek(&tok);
	switch (tok.kind) {
	case TOK_CHAR:
		get_token(&tok);
		*typep = "u_char";
		break;
	case TOK_SHORT:
		get_token(&tok);
		*typep = "u_short";
		(void) peekscan(TOK_INT, &tok);
		break;
	case TOK_INT32:
		get_token(&tok);
		*typep = "afs_uint32";
		(void) peekscan(TOK_INT, &tok);
		break;
	case TOK_INT:
		get_token(&tok);
		*typep = "u_int";
		break;
	default:
		*typep = "u_int";
		break;
	}
}


static
def_package(defp)
definition *defp;
{
    token tok;

    defp->def_kind = DEF_PACKAGE;
    scan(TOK_IDENT, &tok);
    defp->def_name = tok.str;
    no_of_stat_funcs = 0;
    if (PackageIndex++ >= MAX_PACKAGES)
	error("Exceeded upper limit of package statements\n");
    function_list_index = 0;
    PackagePrefix[PackageIndex] = tok.str;
    if (MasterPrefix == NULL)
	MasterPrefix = tok.str;
    no_of_opcodes[PackageIndex] = highest_opcode[PackageIndex] = opcodesnotallowed[PackageIndex] = 0;
    lowest_opcode[PackageIndex] = 99999;
    proc_defined[PackageIndex] = NULL;
    PackageStatIndex[PackageIndex] = NULL;
}

static
def_prefix(defp)
definition *defp;
{
    token tok;

    defp->def_kind = DEF_PREFIX;
    scan(TOK_IDENT, &tok);
    defp->def_name = tok.str;
    ServerPrefix = tok.str;
}

static
def_statindex(defp)
definition *defp;
{
    token tok;
    char *name;

    defp->def_kind = DEF_CONST;
    scan_num(&tok);
    if (PackageIndex < 0)
	error("'statindex' command must follow 'package' command!\n");
    if (PackageStatIndex[PackageIndex])
	error("Cannot have more then one 'statindex' per package!\n");
    if (InvalidConstant(tok.str))
	error("Index in 'statindex' command must be a constant!");
    name = alloc(strlen(PackagePrefix[PackageIndex])+strlen("STATINDEX")+1);
    strcpy(name, PackagePrefix[PackageIndex]);
    strcat(name, "STATINDEX");
    defp->def_name = name;
    defp->def.co = tok.str;
    PackageStatIndex[PackageIndex] = name;
}

static
def_startingopcode(defp)
definition *defp;
{
    token tok;

    defp->def_kind = DEF_STARTINGOPCODE;
    scan(TOK_IDENT, &tok);
    defp->def_name = tok.str;
    if (InvalidConstant(defp->def_name))
	error("Opcode in 'startingopcode' command must be a constant!");
    lowest_opcode[PackageIndex] = master_lowest_opcode = atoi(tok.str);
    if (lowest_opcode[PackageIndex] < 0 || lowest_opcode[PackageIndex] > 99999)
	error("startingopcode number is out of bounds (must be >= 0 < 100000)");
    master_opcodenumber = lowest_opcode[PackageIndex];
    opcodesnotallowed[PackageIndex]=1;
}

static
def_split(defp)
definition *defp;
{
    token tok;

    defp->def_kind = DEF_SPLITPREFIX;
    do {
	get_token(&tok);
	switch (tok.kind) {
		case TOK_IN:
		    scan(TOK_EQUAL, &tok);
		    scan(TOK_IDENT, &tok);
		    SplitStart = tok.str;
		    break;
		case TOK_OUT:
		    scan(TOK_EQUAL, &tok);
		    scan(TOK_IDENT, &tok);
		    SplitEnd = tok.str;
		    break;
		case TOK_SEMICOLON:
		    break;
		default:
		    error("syntax error in the 'splitprefix' line");
	}
    } while (tok.kind != TOK_SEMICOLON);
    if (!SplitStart && !SplitEnd)
	error("At least one param should be passed to 'splitprefix' cmd");
}


static
customize_struct(defp)
definition *defp;
{
    decl_list *listp;
    declaration *dec;
    definition *defp1 = ALLOC(definition);
    spec_list *specs, **tailp;

    defp->def_kind = DEF_CUSTOMIZED;
    defp1->def_kind = DEF_SPECIAL;
    tailp = &defp1->def.sd.specs;
    for (listp = defp->def.st.decls; listp; listp = listp->next) {
	dec = &listp->decl;
	if (streq(dec->type, "string") || (dec->rel == REL_POINTER)) {
	    specs = ALLOC(spec_list);
	    specs->sdef.string_name = dec->name;
	    specs->sdef.string_value = defp->def_name;
	    *tailp = specs;
	    tailp = &specs->next;
	}
    }
    tailp = NULL;
    STOREVAL(&special_defined, defp1);
}

static
char *
structname(name)
char *name;
{
    char namecontents[150], *pnt, *pnt1;

    strcpy(namecontents, name);
    pnt = namecontents;
    if (!strncmp(pnt, "struct", 6)) pnt += 6;
    while (isspace(*pnt)) pnt++;
    pnt1 = pnt;
    while (*pnt != ' ' && *pnt != '\0') pnt++;
    *pnt = '\0';
    return pnt1;
}


static
def_special(dec, defp)
declaration *dec;
definition *defp;
{
    char *typename;
    spec_list *specs, **tailp;
    token tok;

    defp->def_kind = DEF_SPECIAL;
    get_type(&dec->prefix, &dec->type, DEF_SPECIAL);
    dec->rel = REL_POINTER;
    scan(TOK_IDENT, &tok);
    tailp = &defp->def.sd.specs;
    do {
	specs = ALLOC(spec_list);
	specs->sdef.string_name = tok.str;
	get_param_type(defp, dec, &specs->sdef.string_value, &typename);
	*tailp = specs;
	tailp = &specs->next;
	scan2(TOK_COMMA, TOK_SEMICOLON, &tok);
	if (tok.kind == TOK_SEMICOLON)
	    break;
	get_token(&tok);
    } while (tok.kind == TOK_IDENT);
    tailp = NULL;
    STOREVAL(&special_defined, defp);
}

 
proc1_list *Proc_list, **Proc_listp;

static
check_proc(defp, tokp, noname)
definition *defp;
token *tokp;
int noname;
{
    token tok;
    int proc_split = 0;
    int proc_multi = 0;

    tokp->kind = TOK_PROC;
    defp->def_kind = DEF_PROC;
    if (noname)
	defp->pc.proc_name = "";
    else
	defp->pc.proc_name = tokp->str;
    PerProcCounter = 0;
    defp->pc.proc_prefix = alloc(strlen(PackagePrefix[PackageIndex])+1);
    strcpy(defp->pc.proc_prefix, PackagePrefix[PackageIndex]);
    scan2(TOK_LPAREN, TOK_IDENT, &tok);
    defp->pc.proc_serverstub = NULL;
    if (tok.kind == TOK_IDENT) {
	defp->pc.proc_serverstub = tok.str;
	scan(TOK_LPAREN, &tok);
    }
    analyze_ProcParams(defp, &tok);
    defp->pc.proc_opcodenum = -1;
    scan4(TOK_SPLIT, TOK_MULTI, TOK_EQUAL, TOK_SEMICOLON, &tok);
    if (tok.kind == TOK_MULTI) {
	proc_multi = 1;
	scan2(TOK_EQUAL, TOK_SEMICOLON, &tok);
    }
    if (tok.kind  == TOK_SPLIT) {
	proc_split = 1;
	scan2(TOK_EQUAL, TOK_SEMICOLON, &tok);
    }
    if (tok.kind == TOK_EQUAL) {
	if (opcodesnotallowed[PackageIndex])
	    error("Opcode assignment isn't allowed here!");
	scan_num(&tok);
	if (InvalidConstant(tok.str))
	    error("Illegal Opcode assignment (Must be a constant opcode!)");
	if (opcodenum_is_defined(atoi(tok.str)))
	    error("The opcode number is already used by a previous proc");
	defp->pc.proc_opcodename = tok.str;
	defp->pc.proc_opcodenum = atoi(tok.str);
	if (defp->pc.proc_opcodenum < lowest_opcode[PackageIndex])
	    lowest_opcode[PackageIndex] = defp->pc.proc_opcodenum;
	if (defp->pc.proc_opcodenum < master_lowest_opcode)
	    master_lowest_opcode = defp->pc.proc_opcodenum;
	if (defp->pc.proc_opcodenum > highest_opcode[PackageIndex])
	    highest_opcode[PackageIndex] = defp->pc.proc_opcodenum;
	if (defp->pc.proc_opcodenum > master_highest_opcode)
	    master_highest_opcode = defp->pc.proc_opcodenum;
	scan(TOK_SEMICOLON, &tok);
    } else {
	if (master_opcodenumber == 99999) master_opcodenumber = 0;
	defp->pc.proc_opcodenum = master_opcodenumber++;
	if (defp->pc.proc_opcodenum < lowest_opcode[PackageIndex])
	    lowest_opcode[PackageIndex] = defp->pc.proc_opcodenum;
	if (defp->pc.proc_opcodenum > highest_opcode[PackageIndex])
	    highest_opcode[PackageIndex] = defp->pc.proc_opcodenum;
	if (defp->pc.proc_opcodenum > master_highest_opcode)
	    master_highest_opcode = defp->pc.proc_opcodenum;
	opcodesnotallowed[PackageIndex] = 1;	/* force it */
    }
    no_of_opcodes[PackageIndex]++, master_no_of_opcodes++;
    if (proc_multi) {
	generate_code(defp, 0, 1);
	if (Cflag || cflag) {
	    generate_code(defp, 1, 1);
	}
	generate_multi_macros(defp);
    } else {
	generate_code(defp, proc_split, 0);
    }

    if (function_list_index >= MAX_FUNCTIONS_PER_INTERFACE) {
	error("too many functions in interface, "
	      "increase MAX_FUNCTIONS_PER_INTERFACE");
    }
    sprintf(function_list[PackageIndex][function_list_index],
	    "%s%s%s",
	    prefix,
	    PackagePrefix[PackageIndex],
	    defp->pc.proc_name);

    function_list_index++;
    no_of_stat_funcs_header[PackageIndex]++;
    no_of_stat_funcs++;
    *Proc_listp = NULL;
}


#define LEGALNUMS "0123456789"
static int
InvalidConstant(name)
char *name;
{
    char *map;
    int slen;

    map = LEGALNUMS;
    slen = strlen(name);
    return(slen != strspn(name, map));
}

static
opcodenum_is_defined(opcode_num)
int opcode_num;
{
    list *listp;
    definition *defp;
    
    for (listp = proc_defined[PackageIndex]; listp != NULL; listp = listp->next) {
	defp = (definition *)listp->val;
	if (opcode_num == defp->pc.proc_opcodenum)
	    return 1;
    }
    return 0;
}


static
analyze_ProcParams(defp, tokp)
definition *defp;
token *tokp;
{
    declaration dec;
    decl_list *decls, **tailp;

    Proc_listp = &defp->pc.plists;
    tailp = &defp->def.st.decls;
    do {
	get_token(tokp);
	Proc_list = ALLOC(proc1_list);
	memset((char *)Proc_list, 0, sizeof(proc1_list));
	Proc_list->pl.param_flag = 0;
	switch (tokp->kind) {
	    case TOK_IN:
		hdle_param_tok(defp, &dec, tokp, DEF_INPARAM);
		break;
	    case TOK_OUT:
		hdle_param_tok(defp, &dec, tokp, DEF_OUTPARAM);
		break;
	    case TOK_INOUT:
		hdle_param_tok(defp, &dec, tokp, DEF_INOUTPARAM);
		break;
	    case TOK_RPAREN:
		break;
	    default:
		unget_token(tokp);
		hdle_param_tok(defp, &dec, tokp, DEF_NULL);
		break;
	}
	*Proc_listp = Proc_list;
	Proc_listp = &Proc_list->next;
	decls = ALLOC(decl_list);
	memset((char *)decls, 0, sizeof(decl_list));
	decls->decl = dec;
	*tailp = decls;
	tailp = &decls->next;
    } while (tokp->kind != TOK_RPAREN);
    *tailp = NULL;
}


static
generate_code(defp, proc_split_flag, multi_flag)
definition *defp;
int proc_split_flag;
int multi_flag;
{
    if (proc_split_flag)
	handle_split_proc(defp, multi_flag);
    else {
	if (Cflag || cflag) {
	    cs_Proc_CodeGeneration(defp, 0, "");
	}
	if (Sflag || cflag)
	    ss_Proc_CodeGeneration(defp);
    }
    if (Sflag)
	STOREVAL(&proc_defined[PackageIndex], defp);
}


static
handle_split_proc(defp, multi_flag)
definition *defp;
int multi_flag;
{
    char *startname = SplitStart, *endname = SplitEnd;
    int numofparams;

    if (!startname) 
	startname = "Start";
    if (!endname) 
	endname = "End";
    if (Cflag || cflag) {
	if (!cflag) {
	    do_split(defp, OUT, &numofparams, DEF_OUTPARAM, 0);
	}
	cs_Proc_CodeGeneration(defp, 1, startname);
	if (!cflag) {
	    do_split(defp, OUT, &numofparams, DEF_OUTPARAM, 1);
	    do_split(defp, IN, &numofparams, DEF_INPARAM, 0);
	}
	cs_Proc_CodeGeneration(defp, (multi_flag ? 3 : 2), endname);
	if (!cflag) {
	    do_split(defp, IN, &numofparams, DEF_INPARAM, 1);
	}
    }
    if (Sflag || cflag)
	ss_Proc_CodeGeneration(defp);
}


static
do_split(defp, direction, numofparams, param_kind, restore_flag)
definition *defp;
int direction, *numofparams, restore_flag;
defkind param_kind;
{
    proc1_list *plist;

    if (restore_flag) {
	defp->pc.paramtypes[direction] = *numofparams;
	for (plist = defp->pc.plists; plist; plist = plist->next) {
	    if (plist->component_kind == DEF_NULL && plist->pl.param_kind == param_kind)
		plist->component_kind = DEF_PARAM;
	}
    } else {
	*numofparams = defp->pc.paramtypes[direction];
	defp->pc.paramtypes[direction] = 0;
	for (plist = defp->pc.plists; plist; plist = plist->next) {
	    if (plist->component_kind == DEF_PARAM && plist->pl.param_kind == param_kind)
		plist->component_kind = DEF_NULL;
	}
    }
}


static
hdle_param_tok(defp, dec, tokp, par_kind)
definition *defp;
declaration *dec;
token *tokp;
defkind par_kind;
{
    static defkind last_param_kind = DEF_NULL;
    
    if (par_kind == DEF_NULL)
	Proc_list->pl.param_kind = last_param_kind;
    else
	Proc_list->pl.param_kind = par_kind;
    last_param_kind = Proc_list->pl.param_kind;
    defp->pc.paramtypes[(int)last_param_kind]++;
    Proc_list->component_kind = DEF_PARAM;
    Proc_list->code = alloc(250);
    Proc_list->scode =  alloc(250);
    get_declaration(dec, DEF_PARAM);
    Proc_list->pl.param_name = dec->name;
    get1_param_type(defp, dec, &Proc_list->pl.param_type);
    print_param(dec);
    scan2(TOK_COMMA, TOK_RPAREN, tokp);
    if (tokp->kind == TOK_COMMA)
	peek(tokp);
}


static
get1_param_type(defp, dec, param_type)
definition *defp;
declaration *dec;
char **param_type;
{
    char typecontents[100];

    if (streq(dec->type,"string")) {	
	*param_type = "char *";
    } else {
	if (dec->prefix) {
	    strcpy(typecontents, dec->prefix);	
	    strcat(typecontents, " ");
	    strcat(typecontents, dec->type);
	    strcat(typecontents, " *");
	} else if (dec->rel == REL_POINTER) {
	    strcpy(typecontents, dec->type);
	    strcat(typecontents, " *");
	} else
	    strcpy(typecontents, dec->type);
	*param_type = alloc(100);
	strcpy(*param_type, typecontents);
    }
}


static
get_param_type(defp, dec, param_type, typename)
definition *defp;
declaration *dec;
char **param_type, **typename;
{
    char typecontents[100];

    if (streq(dec->type,"string")) {	
	*typename = "wrapstring";    
	*param_type = "char *";
    } else {
	*typename = dec->type;
	if (dec->prefix) {
	    strcpy(typecontents, dec->prefix);	
	    strcat(typecontents, " ");
	    strcat(typecontents, dec->type);
	    strcat(typecontents, " *");
	    dec->rel = REL_POINTER;
	} else if (dec->rel == REL_POINTER) {
	    strcpy(typecontents, dec->type);
	    strcat(typecontents, " *");
	} else
	    strcpy(typecontents, dec->type);
	*param_type = alloc(100);
	strcpy(*param_type, typecontents);
    }
}


static
hndle_param_tail(defp, dec, tokp, typename)
definition *defp;
declaration *dec;
token *tokp;
char *typename;
{
    char *amp;

    if (dec->rel == REL_POINTER)
	Proc_list->pl.param_flag |= INDIRECT_PARAM;
    else
	Proc_list->pl.param_flag &= ~INDIRECT_PARAM;
    amp = "";
    if (!(Proc_list->pl.param_flag & INDIRECT_PARAM))
	amp = "&";

    sprintf(Proc_list->code, "xdr_%s(&z_xdrs, %s%s)", typename, amp, Proc_list->pl.param_name);
    sprintf(Proc_list->scode, "xdr_%s(z_xdrs, &%s)", typename, Proc_list->pl.param_name);
    scan2(TOK_COMMA, TOK_RPAREN, tokp);
    if (tokp->kind == TOK_COMMA)
	peek(tokp);
}


static
cs_Proc_CodeGeneration(defp, split_flag, procheader)
definition *defp;
int split_flag;
char *procheader;
{
    defp->can_fail = 0;
    cs_ProcName_setup(defp, procheader, split_flag);
    if (!cflag) {
	cs_ProcParams_setup(defp, split_flag);
	cs_ProcMarshallInParams_setup(defp, split_flag);
	if (split_flag != 1) {
	    cs_ProcSendPacket_setup(defp, split_flag);
	    cs_ProcUnmarshallOutParams_setup(defp);
	}
	cs_ProcTail_setup(defp, split_flag);
    }
}


static
cs_ProcName_setup(defp, procheader, split_flag)
definition *defp;
char *procheader;
int split_flag;
{
    proc1_list *plist;

    if (!cflag) {
	if (split_flag) {
	    f_print(fout, "int %s%s%s%s(z_call", procheader, prefix, PackagePrefix[PackageIndex], defp->pc.proc_name);
	} else {
	    f_print(fout, "int %s%s%s%s(z_conn", procheader, prefix, PackagePrefix[PackageIndex], defp->pc.proc_name);
	}
    }
    if ((strlen(procheader) + strlen(prefix) + strlen(PackagePrefix[PackageIndex]) + strlen(defp->pc.proc_name)) >= MAX_FUNCTION_NAME_LEN) {
	error("function name is too long, increase MAX_FUNCTION_NAME_LEN");
    }
    if (!cflag) {
	for (plist = defp->pc.plists; plist; plist = plist->next) {
	    if (plist->component_kind == DEF_PARAM) {
		plist->pl.param_flag &= ~PROCESSED_PARAM;
		f_print(fout, ", %s", plist->pl.param_name);
	    }
	}
	f_print(fout, ")\n");  
    }
}


static
cs_ProcParams_setup(defp, split_flag)
definition *defp;
int split_flag;
{
    proc1_list *plist, *plist1;

    if (!split_flag)
	f_print(fout, "\tregister struct rx_connection *z_conn;\n");    
    if (split_flag) {
	f_print(fout, "\tregister struct rx_call *z_call;\n");
    }
    for (plist = defp->pc.plists; plist; plist = plist->next) {
	if (plist->component_kind == DEF_PARAM && !(plist->pl.param_flag & PROCESSED_PARAM)) {
	    if (plist->pl.param_flag & OUT_STRING) {
		f_print(fout, "\t%s *%s", plist->pl.param_type, plist->pl.param_name);
	    } else {
		f_print(fout, "\t%s %s", plist->pl.param_type, plist->pl.param_name);
	    }
	    plist->pl.param_flag |= PROCESSED_PARAM;
	    for (plist1 = defp->pc.plists; plist1; plist1 = plist1->next) {
		if ((plist1->component_kind == DEF_PARAM) && streq(plist->pl.param_type, plist1->pl.param_type) && !(plist1->pl.param_flag & PROCESSED_PARAM)) {
		    char *star="";
		    char *pntr = strchr(plist1->pl.param_type, '*');
		    if (pntr) star = "*";
		    if (plist1->pl.param_flag & OUT_STRING) {
			f_print(fout, ", *%s%s", star, plist1->pl.param_name);
		    } else {
			f_print(fout, ", %s%s", star, plist1->pl.param_name);
		    }
		    plist1->pl.param_flag |= PROCESSED_PARAM;
		}
	    }
	    f_print(fout, ";\n");
	}
    }
}


static
cs_ProcMarshallInParams_setup(defp, split_flag)
definition *defp;
int split_flag;
{
    int noofparams, i=0;
    proc1_list *plist;
    decl_list *dl;
    int noofallparams = defp->pc.paramtypes[IN] + defp->pc.paramtypes[INOUT] +
			defp->pc.paramtypes[OUT];

    f_print(fout, "{\n");
    if (!split_flag)
	f_print(fout, "\tstruct rx_call *z_call = rx_NewCall(z_conn);\n");
    if ((!split_flag) || (split_flag == 1)) {
	if (opcodesnotallowed[PackageIndex]) {
	    f_print(fout, "\tstatic int z_op = %d;\n", defp->pc.proc_opcodenum);
	} else {
	    f_print(fout, "\tstatic int z_op = %s;\n", defp->pc.proc_opcodename);
	}
    }
    f_print(fout, "\tint z_result;\n");
    if (!(split_flag > 1) || (noofallparams != 0)) {
	f_print(fout, "\tXDR z_xdrs;\n");
    }
    /*
     * Print out client side stat gathering call
     */
    if (xflag && split_flag != 1) {
	f_print(fout, "\tstruct clock __QUEUE, __EXEC;\n");
    }

    if ((!split_flag) || (split_flag == 1)) {
	f_print(fout, "\txdrrx_create(&z_xdrs, z_call, XDR_ENCODE);\n");
	f_print(fout, "\n\t/* Marshal the arguments */\n");
	f_print(fout, "\tif ((!xdr_int(&z_xdrs, &z_op))");
	noofparams = defp->pc.paramtypes[IN] + defp->pc.paramtypes[INOUT];
	for (plist = defp->pc.plists, dl=defp->def.st.decls; plist; plist = plist->next, dl = dl->next) {
	    if (plist->component_kind == DEF_PARAM && (plist->pl.param_kind == DEF_INPARAM || plist->pl.param_kind == DEF_INOUTPARAM)) {
		f_print(fout, "\n\t     || (!%s)", plist->code);
		if (++i == noofparams) {
		    f_print(fout, ") {\n\t\tz_result = RXGEN_CC_MARSHAL;\n\t\tgoto fail;\n\t}\n\n");
		    defp->can_fail = 1;
		}
	    }
	}
	if (!i) {
	    f_print(fout, ") {\n\t\tz_result = RXGEN_CC_MARSHAL;\n\t\tgoto fail;\n\t}\n\n");
	    defp->can_fail = 1;
	}
    }
}


static
cs_ProcSendPacket_setup(defp, split_flag)
definition *defp;
int split_flag;
{
    int noofoutparams = defp->pc.paramtypes[INOUT] + defp->pc.paramtypes[OUT];

    if (noofoutparams) {
	    f_print(fout, "\t/* Un-marshal the reply arguments */\n");
	    if (split_flag) {
		f_print(fout, "\txdrrx_create(&z_xdrs, z_call, XDR_DECODE);\n");
	    } else {
		f_print(fout, "\tz_xdrs.x_op = XDR_DECODE;\n");
	    }
    }
}


static
cs_ProcUnmarshallOutParams_setup(defp)
definition *defp;
{
    int noofparams, i;
    proc1_list *plist;
    decl_list *dl;

    noofparams = defp->pc.paramtypes[INOUT] + defp->pc.paramtypes[OUT];
    if (noofparams)
	for (plist = defp->pc.plists, dl=defp->def.st.decls,i = 0; plist; plist = plist->next, dl=dl->next) {
	    if (plist->component_kind == DEF_PARAM && (plist->pl.param_kind == DEF_OUTPARAM || plist->pl.param_kind == DEF_INOUTPARAM)) {
		    if (!i) {
			f_print(fout, "\tif ((!%s)", plist->code);
		    } else {
			f_print(fout, "\n\t     || (!%s)", plist->code);
		    }
		if (++i == noofparams) {
		    f_print(fout, ") {\n\t\tz_result = RXGEN_CC_UNMARSHAL;\n\t\tgoto fail;\n\t}\n\n");
		    defp->can_fail = 1;
		}
	    }	    
	}
}


static
cs_ProcTail_setup(defp, split_flag)
definition *defp;
int split_flag;
{
    f_print(fout, "\tz_result = RXGEN_SUCCESS;\n");
    if (defp->can_fail) {
	f_print(fout, "fail:\n");
    }
    if (!split_flag) {
	f_print(fout, "\tz_result = rx_EndCall(z_call, z_result);\n");
    }
    if (xflag && split_flag != 1) {
	f_print(fout, "\tif (rx_enable_stats) {\n");
	f_print(fout, "\t    clock_GetTime(&__EXEC);\n");
	f_print(fout, "\t    clock_Sub(&__EXEC, &z_call->startTime);\n");
	f_print(fout, "\t    __QUEUE = z_call->startTime;\n");
	f_print(fout, "\t    clock_Sub(&__QUEUE, &z_call->queueTime);\n");
	if (PackageStatIndex[PackageIndex]) {
	    if (!split_flag) {
		f_print(fout,
		    "\t    rx_IncrementTimeAndCount(z_conn->peer, %s,\n",
		    PackageStatIndex[PackageIndex]);
	    } else {
		f_print(fout,
		    "\t    rx_IncrementTimeAndCount(z_call->conn->peer, %s,\n",
		    PackageStatIndex[PackageIndex]);
	    }
	} else {
	    if (!split_flag) {
		f_print(fout,
		    "\t    rx_IncrementTimeAndCount(z_conn->peer,\n"
		    "\t\t(((afs_uint32)(ntohs(z_conn->serviceId) << 16)) \n"
		    "\t\t| ((afs_uint32)ntohs(z_conn->peer->port))),\n");
	    } else {
		f_print(fout,
		    "\t    rx_IncrementTimeAndCount(z_call->conn->peer,\n"
		    "\t\t(((afs_uint32)(ntohs(z_call->conn->serviceId) << 16)) |\n"
		    "\t\t((afs_uint32)ntohs(z_call->conn->peer->port))),\n");
	    }
	}
	if (xflag) {
	    f_print(fout, "\t\t%d, %sNO_OF_STAT_FUNCS, &__QUEUE, &__EXEC,\n",
			  no_of_stat_funcs,
			  PackagePrefix[PackageIndex]);
	    f_print(fout, "\t\t&z_call->bytesSent, &z_call->bytesRcvd, 1);\n");
	}
	f_print(fout, "\t}\n\n");
    }
    f_print(fout, "\treturn z_result;\n}\n\n");
}


static
ss_Proc_CodeGeneration(defp)
definition *defp;
{
    int somefrees=0;

    defp->can_fail = 0;
    ss_ProcName_setup(defp);
    if (!cflag) {
	ss_ProcParams_setup(defp, &somefrees);
	ss_ProcProto_setup(defp, &somefrees);
	ss_ProcSpecial_setup(defp, &somefrees);
	ss_ProcUnmarshallInParams_setup(defp);
	ss_ProcCallRealProc_setup(defp);
	ss_ProcMarshallOutParams_setup(defp);
	ss_ProcTail_setup(defp, somefrees);
    }
}


static
ss_ProcName_setup(defp)
definition *defp;
{
    proc1_list *plist;

    if ((strlen(prefix) + strlen(PackagePrefix[PackageIndex]) + strlen(defp->pc.proc_name)) >= MAX_FUNCTION_NAME_LEN) {
	error("function name is too long, increase MAX_FUNCTION_NAME_LEN");
    }

    if (!cflag) {
	f_print(fout, "afs_int32 _%s%s%s(z_call, z_xdrs)\n", prefix, PackagePrefix[PackageIndex], defp->pc.proc_name);
	f_print(fout, "\tstruct rx_call *z_call;\n\tXDR *z_xdrs;\n{\n");
	f_print(fout, "\t" "afs_int32 z_result;\n");
	if (xflag) {
	    f_print(fout, "\tstruct clock __QUEUE, __EXEC;\n");
	}

	for (plist = defp->pc.plists; plist; plist = plist->next) 
	    if (plist->component_kind == DEF_PARAM) {
		plist->pl.param_flag &= ~(PROCESSED_PARAM);
		plist->pl.string_name = (char *)0;
	    }
    }
}


static
ss_ProcParams_setup(defp, somefrees)
definition *defp;
int *somefrees;
{
    proc1_list *plist, *plist1;
    list *listp;
    definition *defp1;
    int preserve_flag = 0;

    for (plist = defp->pc.plists; plist; plist = plist->next) {
	if ((plist->component_kind == DEF_PARAM) && !(plist->pl.param_flag & PROCESSED_PARAM)) {
	    if (plist->pl.param_flag & INDIRECT_PARAM) {
		    char pres, *pntr = strchr(plist->pl.param_type, '*');
		    if (pntr){ --pntr; pres = *pntr; *pntr = (char)0; }
		    f_print(fout, "\t%s %s", plist->pl.param_type, plist->pl.param_name);
		    *pntr = pres;
	    } else if (strchr(plist->pl.param_type, '*') == 0) {
		f_print(fout, "\t%s %s", plist->pl.param_type, plist->pl.param_name);
	    } else {
		plist->pl.param_flag |= FREETHIS_PARAM;
		*somefrees = 1;
		f_print(fout, "\t%s %s=(%s)0", plist->pl.param_type, plist->pl.param_name, plist->pl.param_type);
	    }
	    plist->pl.param_flag |= PROCESSED_PARAM;
	    for (plist1 = defp->pc.plists; plist1; plist1 = plist1->next) {
		if ((plist1->component_kind == DEF_PARAM) && streq(plist->pl.param_type, plist1->pl.param_type) && !(plist1->pl.param_flag & PROCESSED_PARAM)) {
		    if (plist1->pl.param_flag & INDIRECT_PARAM) {
			    f_print(fout, ", %s", plist1->pl.param_name);
		    } else if (strchr(plist1->pl.param_type, '*') == 0) {
			f_print(fout, ", %s", plist1->pl.param_name);
		    } else {
			plist1->pl.param_flag |= FREETHIS_PARAM;
			*somefrees = 1;
			f_print(fout, ", *%s=(%s)0", plist1->pl.param_name, plist1->pl.param_type);
		    }
		    plist1->pl.param_flag |= PROCESSED_PARAM;
		}
	    }
	    f_print(fout, ";\n");
	}
    }
    for (listp = typedef_defined; listp != NULL; listp = listp->next) {
	defp1 = (definition *)listp->val;
	for (plist=defp->pc.plists; plist; plist=plist->next) {
	    if (plist->component_kind == DEF_PARAM && (plist->pl.param_kind == DEF_OUTPARAM || plist->pl.param_kind == DEF_INOUTPARAM) && !(plist->pl.param_flag & FREETHIS_PARAM)) {
		if (streq(defp1->def_name, structname(plist->pl.param_type))) {
		    switch (defp1->pc.rel) {
			case REL_ARRAY:
			case REL_POINTER:
			    break;
		    }
		}
	    }
	}
    }	
    fprintf(fout, "\n");
}


static
ss_ProcProto_setup(defp, somefrees)
definition *defp;
int *somefrees;
{
    proc1_list *plist, *plist1;
    list *listp;
    definition *defp1;
    int preserve_flag = 0;

	f_print(fout, "#ifndef KERNEL\n");
	f_print(fout, "\tafs_int32 %s%s%s%s();\n", prefix, ServerPrefix, 
		PackagePrefix[PackageIndex], defp->pc.proc_name);
	f_print(fout, "#endif\n");
}


static
ss_ProcSpecial_setup(defp, somefrees)
definition *defp;
int *somefrees;
{
    proc1_list *plist;
    definition *defp1;
    list *listp;

   for (listp = special_defined; listp != NULL; listp = listp->next) {
	defp1 = (definition *)listp->val;
	for (plist=defp->pc.plists; plist; plist=plist->next) {
	    if (plist->component_kind == DEF_PARAM && (plist->pl.param_kind == DEF_INPARAM || plist->pl.param_kind == DEF_INOUTPARAM)) {
		spec_list *spec = defp1->def.sd.specs;
		char string[40];
		strcpy(string, structname(spec->sdef.string_value));
		if (streq(string, structname(plist->pl.param_type))) {
		    plist->pl.string_name = spec->sdef.string_name;
		    plist->pl.param_flag |= FREETHIS_PARAM;
		    *somefrees = 1;
		    fprintf(fout, "\n\t%s.%s = 0;", plist->pl.param_name, spec->sdef.string_name);
		}
	    }
	}
    }
    if (!*somefrees)
	fprintf(fout, "\n");
    for (listp = typedef_defined; listp != NULL; listp = listp->next) {
	defp1 = (definition *)listp->val;
	for (plist=defp->pc.plists; plist; plist=plist->next) {
	    if (plist->component_kind == DEF_PARAM ) {
		if (streq(defp1->def_name, structname(plist->pl.param_type))) {
		    plist->pl.param_flag |= FREETHIS_PARAM;
		    *somefrees = 1;
		    switch (defp1->pc.rel) {
			case REL_ARRAY:
			    f_print(fout, "\n\t%s.%s_val = 0;", plist->pl.param_name, defp1->def_name);
			    f_print(fout, "\n\t%s.%s_len = 0;", plist->pl.param_name, defp1->def_name);
			    plist->pl.string_name = alloc(40);
			    s_print(plist->pl.string_name, "%s_val", defp1->def_name);
			    break;
			case REL_POINTER:
			    f_print(fout, "\n\t%s = 0;", plist->pl.param_name);
			    plist->pl.string_name = NULL;
			    break;
		    }
		}
	    }
	}
    }	
    f_print(fout, "\n");
}


static
ss_ProcUnmarshallInParams_setup(defp)
definition *defp;
{
    int noofparams, noofoutparams, i;
    proc1_list *plist;

    noofparams = defp->pc.paramtypes[IN] + defp->pc.paramtypes[INOUT];
    noofoutparams = defp->pc.paramtypes[INOUT] + defp->pc.paramtypes[OUT];
    for (plist = defp->pc.plists, i=0; plist; plist = plist->next) {
	if (plist->component_kind == DEF_PARAM && (plist->pl.param_kind == DEF_INPARAM || plist->pl.param_kind == DEF_INOUTPARAM)) {
		if (!i) {
		    f_print(fout, "\n\tif ((!%s)", (plist->scode ? plist->scode : plist->code));
		} else {
		    f_print(fout, "\n\t     || (!%s)", (plist->scode ? plist->scode : plist->code));
		}
	    if (++i == noofparams) {
		if (!noofoutparams) {
		    f_print(fout, ") {\n");
		} else {
		    f_print(fout, ") {\n");
		}
		f_print(fout, "\t\tz_result = RXGEN_SS_UNMARSHAL;\n\t\tgoto fail;\n\t}\n\n");
		defp->can_fail = 1;
	    }
	}
    }
}


static
ss_ProcCallRealProc_setup(defp)
definition *defp;
{
    extern char zflag;
    proc1_list *plist;

    f_print(fout, "\tz_result = %s%s%s%s(z_call", prefix, ServerPrefix, PackagePrefix[PackageIndex], defp->pc.proc_name);
    for (plist = defp->pc.plists; plist; plist = plist->next) {
	if (plist->component_kind == DEF_PARAM)	{
	    if (plist->pl.param_flag & INDIRECT_PARAM) {
		    f_print(fout, ", &%s", plist->pl.param_name);
	    } else {
		if (plist->pl.param_flag & OUT_STRING) {
		    f_print(fout, ", &%s", plist->pl.param_name);
		} else {
		    f_print(fout, ", %s", plist->pl.param_name);
		}
	    }
	}
    }
    f_print(fout, ");\n");
    if (zflag) {
	f_print(fout, "\tif (z_result)\n\t\treturn z_result;\n");
    }
}


static
ss_ProcMarshallOutParams_setup(defp)
definition *defp;
{
    proc1_list *plist;
    int noofparams, i;

    noofparams = defp->pc.paramtypes[INOUT] + defp->pc.paramtypes[OUT];
    if (noofparams)
	f_print(fout, "\tz_xdrs->x_op = XDR_ENCODE;\n");
    if (noofparams) {
	for (plist = defp->pc.plists, i=0; plist; plist = plist->next) {
	    if (plist->component_kind == DEF_PARAM && (plist->pl.param_kind == DEF_OUTPARAM || plist->pl.param_kind == DEF_INOUTPARAM)) {
		if (!i) {
		    f_print(fout, "\tif ((!%s)", (plist->scode ? plist->scode : plist->code));
		} else {
		    f_print(fout, "\n\t     || (!%s)", (plist->scode ? plist->scode : plist->code));
		}
		if (++i == noofparams) {
		    f_print(fout, ")\n\t\tz_result = RXGEN_SS_MARSHAL;\n");
		}
	    }
	}		
    }
}


static
ss_ProcTail_setup(defp, somefrees)
definition *defp;
int somefrees;
{
    proc1_list *plist;
    definition *defp1;
    list *listp;
    int firsttime = 0;

    if (defp->can_fail) {
	f_print(fout, "fail:\n");
    }
    for (plist = defp->pc.plists; plist; plist = plist->next) {    
	if (plist->component_kind == DEF_PARAM && (plist->pl.param_flag & FREETHIS_PARAM))
	    somefrees = 1;
    }
    if (somefrees)
	f_print(fout, "\tz_xdrs->x_op = XDR_FREE;\n");
    for (plist = defp->pc.plists; plist; plist = plist->next) {    
	if (plist->component_kind == DEF_PARAM && (plist->pl.param_flag & FREETHIS_PARAM)) {
	    char *dot = "", *extens = "";
	    if (plist->pl.string_name) {
		dot = ".";
		extens = plist->pl.string_name;
	    }
	    f_print(fout, "\tif (!%s) goto fail1;\n", plist->scode);
	}
    }
    for (listp = typedef_defined; listp != NULL; listp = listp->next) {
	defp1 = (definition *)listp->val;
	for (plist=defp->pc.plists; plist; plist=plist->next) {
	    if (plist->component_kind == DEF_PARAM && (plist->pl.param_kind == DEF_OUTPARAM || plist->pl.param_kind == DEF_INOUTPARAM) && !(plist->pl.param_flag & FREETHIS_PARAM)) {
		if (streq(defp1->def_name, structname(plist->pl.param_type))) {
		    switch (defp1->pc.rel) {
			case REL_ARRAY:
			case REL_POINTER:
			    if (!somefrees && !firsttime) {
				firsttime = 1;
				f_print(fout, "\tz_xdrs->x_op = XDR_FREE;\n");
			    }
			    somefrees = 1;
			    f_print(fout, "\tif (!%s) goto fail1;\n", plist->scode);
			    break;
		    }
		}
	    }
	}
    }	
    for (listp = uniondef_defined; listp != NULL; listp = listp->next) {
	defp1 = (definition *)listp->val;
	for (plist=defp->pc.plists; plist; plist=plist->next) {
	    if (plist->component_kind == DEF_PARAM && (plist->pl.param_kind == DEF_OUTPARAM || plist->pl.param_kind == DEF_INOUTPARAM) && !(plist->pl.param_flag & FREETHIS_PARAM)) {
		if (streq(defp1->def_name, structname(plist->pl.param_type))) {
		    if (plist->pl.param_flag & INDIRECT_PARAM) {
			if (!somefrees && !firsttime) {
			    firsttime = 1;
			    f_print(fout, "\tz_xdrs->x_op = XDR_FREE;\n");
			}
			somefrees = 1;
			f_print(fout, "\tif (!%s) goto fail1;\n", plist->scode);
		    }
		}
	    }
	}
    }	

    if (xflag) {
	f_print(fout, "\tif (rx_enable_stats) {\n");
	f_print(fout, "\t    clock_GetTime(&__EXEC);\n");
	f_print(fout, "\t    clock_Sub(&__EXEC, &z_call->startTime);\n");
	f_print(fout, "\t    __QUEUE = z_call->startTime;\n");
	f_print(fout, "\t    clock_Sub(&__QUEUE, &z_call->queueTime);\n");
	f_print(fout, "\t    rx_IncrementTimeAndCount(z_call->conn->peer,");
	if (PackageStatIndex[PackageIndex]) {
	    f_print(fout, " %s,\n", PackageStatIndex[PackageIndex]);
	} else {
	    f_print(fout, 
		"\n\t\t(((afs_uint32)(ntohs(z_call->conn->serviceId) << 16)) |\n"
		"\t\t((afs_uint32)ntohs(z_call->conn->service->servicePort))),\n");
	}
	f_print(fout, "\t\t%d, %sNO_OF_STAT_FUNCS, &__QUEUE, &__EXEC,\n",
		      no_of_stat_funcs,
		      PackagePrefix[PackageIndex]);
	f_print(fout, "\t\t&z_call->bytesSent, &z_call->bytesRcvd, 0);\n");
	f_print(fout, "\t}\n\n");
    }

    f_print(fout, "\treturn z_result;\n");
    if (somefrees) {
	f_print(fout, "fail1:\n");

	if (xflag) {
	    f_print(fout, "\tif (rx_enable_stats) {\n");
	    f_print(fout, "\t    clock_GetTime(&__EXEC);\n");
	    f_print(fout, "\t    clock_Sub(&__EXEC, &z_call->startTime);\n");
	    f_print(fout, "\t    __QUEUE = z_call->startTime;\n");
	    f_print(fout, "\t    clock_Sub(&__QUEUE, &z_call->queueTime);\n");
	    f_print(fout, "\t    rx_IncrementTimeAndCount(z_call->conn->peer,");
	    if (PackageStatIndex[PackageIndex]) {
		f_print(fout, " %s,\n", PackageStatIndex[PackageIndex]);
	    } else {
		f_print(fout,
		    "\n\t\t(((afs_uint32)(ntohs(z_call->conn->serviceId) << 16)) |\n"
		    "\t\t((afs_uint32)ntohs(z_call->conn->service->servicePort))),\n");
	    }
	    f_print(fout, "\t\t%d, %sNO_OF_STAT_FUNCS, &__QUEUE, &__EXEC,\n",
			  no_of_stat_funcs,
			  PackagePrefix[PackageIndex]);
	    f_print(fout, "\t\t&z_call->bytesSent, &z_call->bytesRcvd, 0);\n");
	    f_print(fout, "\t}\n\n");
	}

	f_print(fout, "\treturn RXGEN_SS_XDRFREE;\n}\n\n");
    } else {
	f_print(fout, "}\n\n");
    }
}


static
opcode_holes_exist()
{
    int i;
    
    for (i=lowest_opcode[PackageIndex]; i<highest_opcode[PackageIndex]; i++) {
	if (!opcodenum_is_defined(i))
	    return 1;
    }
    return 0;
}

	
int
er_Proc_CodeGeneration()
{
    int temp;

    temp = PackageIndex;
    if (!combinepackages) PackageIndex = 0;
    for (; PackageIndex <= temp; PackageIndex++) {
	if (proc_defined[PackageIndex] == NULL) continue;
	if (combinepackages || opcode_holes_exist()) {
	    er_HeadofOldStyleProc_setup();
	    er_BodyofOldStyleProc_setup();
	    er_TailofOldStyleProc_setup();
	} else {
	    er_ProcDeclExterns_setup();
	    er_ProcProcsArray_setup();
	    er_ProcMainBody_setup();
	}
    }
    PackageIndex = temp;
}


static
er_ProcDeclExterns_setup()
{
    list *listp;
    definition *defp;

    f_print(fout, "\n");
    for (listp = proc_defined[PackageIndex]; listp != NULL; listp = listp->next) {
	defp = (definition *)listp->val;
	if (defp->pc.proc_serverstub) {
	    f_print(fout, "afs_int32 %s();\n", defp->pc.proc_serverstub);
	} else {
	    f_print(fout, "afs_int32 _%s%s%s();\n", prefix, defp->pc.proc_prefix, defp->pc.proc_name);
	}
    }
}


static
er_ProcProcsArray_setup()
{
    list *listp;
    definition *defp;

    if (listp = proc_defined[PackageIndex]) {
	defp = (definition *)listp->val;
	if (defp->pc.proc_serverstub){
	    f_print(fout, "\nstatic afs_int32 (*StubProcsArray%d[])() = {%s", PackageIndex, defp->pc.proc_serverstub);
	} else {
	    f_print(fout, "\nstatic afs_int32 (*StubProcsArray%d[])() = {_%s%s%s", PackageIndex, prefix, defp->pc.proc_prefix, (defp = (definition *)listp->val)->pc.proc_name);
	}
	listp = listp->next;
    }
    for (; listp != NULL; listp = listp->next) {
	defp = (definition *)listp->val;
	if (defp->pc.proc_serverstub) {
	    f_print(fout, ",%s", defp->pc.proc_serverstub);
	} else {
	    f_print(fout, ", _%s%s%s", prefix, defp->pc.proc_prefix, defp->pc.proc_name);
	}
    }
    f_print(fout, "};\n\n");
}


static
er_ProcMainBody_setup()
{
    f_print(fout, "int %s%sExecuteRequest(z_call)\n", prefix,  PackagePrefix[PackageIndex]);
    f_print(fout, "\tregister struct rx_call *z_call;\n");
    f_print(fout, "{\n\tint op;\n");
    f_print(fout, "\tXDR z_xdrs;\n");
    f_print(fout, "\t" "afs_int32 z_result;\n\n");
    f_print(fout, "\txdrrx_create(&z_xdrs, z_call, XDR_DECODE);\n");
    f_print(fout, "\tif (!xdr_int(&z_xdrs, &op))\n\t\tz_result = RXGEN_DECODE;\n");
    f_print(fout, "\telse if (op < %sLOWEST_OPCODE || op > %sHIGHEST_OPCODE)\n\t\tz_result = RXGEN_OPCODE;\n", PackagePrefix[PackageIndex], PackagePrefix[PackageIndex]);
    f_print(fout, "\telse\n\t\tz_result = (*StubProcsArray%d[op - %sLOWEST_OPCODE])(z_call, &z_xdrs);\n", PackageIndex, PackagePrefix[PackageIndex]);
    f_print(fout, "\treturn hton_syserr_conv(z_result);\n}\n");
}


static
er_HeadofOldStyleProc_setup()
{
    f_print(fout, "\nint %s%sExecuteRequest (z_call)\n", prefix, (combinepackages ? MasterPrefix : PackagePrefix[PackageIndex]));
    f_print(fout, "\tregister struct rx_call *z_call;\n");
    f_print(fout, "{\n");
    f_print(fout, "\tint op;\n");
    f_print(fout, "\tXDR z_xdrs;\n");
    f_print(fout, "\t" "afs_int32 z_result;\n\n");
    f_print(fout, "\txdrrx_create(&z_xdrs, z_call, XDR_DECODE);\n");
    f_print(fout, "\tz_result = RXGEN_DECODE;\n");
    f_print(fout, "\tif (!xdr_int(&z_xdrs, &op)) goto fail;\n");
    f_print(fout, "\tswitch (op) {\n");
}

static
er_BodyofOldStyleProc_setup()
{
    list *listp;

    if (combinepackages) {
	int temp = PackageIndex;
	for (PackageIndex = 0; PackageIndex <= temp; PackageIndex++) {
	    for (listp = proc_defined[PackageIndex]; listp != NULL; listp = listp->next)
		proc_er_case((definition *)listp->val);	    
	}
	PackageIndex = temp;
    } else {
    for (listp = proc_defined[PackageIndex]; listp != NULL; listp = listp->next)
	proc_er_case((definition *)listp->val);
    }
}


static
proc_er_case(defp)
definition *defp;
{
    if (opcodesnotallowed[PackageIndex]) {
	f_print(fout, "\t\tcase %d: {\n", defp->pc.proc_opcodenum);
    } else {
	f_print(fout, "\t\tcase %s: {\n", defp->pc.proc_opcodename);
    }
    if (defp->pc.proc_serverstub) {
	f_print(fout, "\t\t\t" "afs_int32 %s();\n", defp->pc.proc_serverstub);
	f_print(fout, "\t\t\tz_result = %s(z_call, &z_xdrs);\n", defp->pc.proc_serverstub);
    } else {
	f_print(fout, "\t\t\t" "afs_int32 _%s%s%s();\n", prefix, defp->pc.proc_prefix, defp->pc.proc_name);
	f_print(fout, "\t\t\tz_result = _%s%s%s(z_call, &z_xdrs);\n", prefix, defp->pc.proc_prefix, defp->pc.proc_name);
    }
    f_print(fout, "\t\t\tbreak;\n\t\t}\n");	
}


static
er_TailofOldStyleProc_setup()
{
    f_print(fout, "\t\tdefault:\n");
    f_print(fout, "\t\t\tz_result = RXGEN_OPCODE;\n");
    f_print(fout, "\t\t\tbreak;\n\t}\n");
    f_print(fout, "fail:\n");
    f_print(fout, "\treturn z_result;\n}\n");
}


int h_opcode_stats()
{
    if (combinepackages) {
	f_print(fout, "\n/* Opcode-related useful stats for Master package: %s */\n", MasterPrefix);
	f_print(fout, "#define %sLOWEST_OPCODE   %d\n", MasterPrefix, master_lowest_opcode);
	f_print(fout, "#define %sHIGHEST_OPCODE	%d\n", MasterPrefix, master_highest_opcode);
	f_print(fout, "#define %sNUMBER_OPCODES	%d\n\n", MasterPrefix, master_no_of_opcodes);
	if (xflag) {
	    f_print(fout, "#define %sNO_OF_STAT_FUNCS\t%d\n\n", MasterPrefix, no_of_stat_funcs_header[0]);
	    f_print(fout, "AFS_RXGEN_EXPORT\n");
	    f_print(fout, "extern const char *%sfunction_names[];\n\n", MasterPrefix);
	}
    } else {
	int i;
	for (i=0; i <= PackageIndex; i++) {
	    f_print(fout, "\n/* Opcode-related useful stats for package: %s */\n", PackagePrefix[i]); 
	    f_print(fout, "#define %sLOWEST_OPCODE   %d\n", PackagePrefix[i], lowest_opcode[i]);
	    f_print(fout, "#define %sHIGHEST_OPCODE	%d\n", PackagePrefix[i], highest_opcode[i]);
	    f_print(fout, "#define %sNUMBER_OPCODES	%d\n\n", PackagePrefix[i], no_of_opcodes[i]);
	    if (xflag) {
		f_print(fout, "#define %sNO_OF_STAT_FUNCS\t%d\n\n", PackagePrefix[i], no_of_stat_funcs_header[i]);
		f_print(fout, "AFS_RXGEN_EXPORT\n");
		f_print(fout, "extern const char *%sfunction_names[];\n\n", PackagePrefix[i]);
	    }
	}
    }
}


int generate_multi_macros(defp)
definition *defp;
{
    char *startname = SplitStart, *endname = SplitEnd;
    proc1_list *plist;
    int numofparams;
    int first = 0;

    if (!hflag) return;
    if (!Multi_Init) {
	Multi_Init = 1;
	f_print(fout, "\n#include <rx/rx_multi.h>");
    }
    f_print(fout, "\n#define multi_%s%s(", PackagePrefix[PackageIndex],defp->pc.proc_name);
    for (plist = defp->pc.plists; plist; plist = plist->next) {
	if (plist->component_kind == DEF_PARAM) {
	    if (!first) {
		first = 1;
		f_print(fout, "%s", plist->pl.param_name);
	    } else {
		f_print(fout, ", %s", plist->pl.param_name);
	    }
	}
    }
    f_print(fout, ") \\\n");
    if (!startname) startname = "Start";
    if (!endname) endname = "End";
    f_print(fout, "\tmulti_Body(%s%s%s(multi_call", startname, PackagePrefix[PackageIndex], defp->pc.proc_name);
    do_split(defp, OUT, &numofparams, DEF_OUTPARAM, 0);
    for (plist = defp->pc.plists; plist; plist = plist->next) {
	if (plist->component_kind == DEF_PARAM)
	    f_print(fout, ", %s", plist->pl.param_name);
    }
    do_split(defp, OUT, &numofparams, DEF_OUTPARAM, 1);
    f_print(fout, "), %s%s%s(multi_call", endname, PackagePrefix[PackageIndex], defp->pc.proc_name);
    do_split(defp, IN, &numofparams, DEF_INPARAM, 0);
    for (plist = defp->pc.plists; plist; plist = plist->next) {
	if (plist->component_kind == DEF_PARAM) {
	    f_print(fout, ", %s", plist->pl.param_name);
	}
    }
    do_split(defp, IN, &numofparams, DEF_INPARAM, 1);
    f_print(fout, "))\n\n");
}


int
IsRxgenToken(tokp)
token *tokp;
{
    if (tokp->kind == TOK_PACKAGE || tokp->kind == TOK_PREFIX ||
	 tokp->kind == TOK_SPECIAL || tokp->kind == TOK_STARTINGOPCODE ||
	 tokp->kind == TOK_SPLITPREFIX || tokp->kind == TOK_PROC ||
	 tokp->kind == TOK_STATINDEX)
	return 1;
    return 0;
}

int
IsRxgenDefinition(def)
definition *def;
{
    if (def->def_kind == DEF_PACKAGE || def->def_kind == DEF_PREFIX || def->def_kind == DEF_SPECIAL || def->def_kind == DEF_STARTINGOPCODE || def->def_kind == DEF_SPLITPREFIX || def->def_kind == DEF_PROC)
	return 1;
    return 0;
}
