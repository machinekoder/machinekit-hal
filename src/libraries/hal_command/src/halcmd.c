/** This file, 'halcmd.c', is a HAL component that provides a simple
    command line interface to the hal.  It is a user space component.
    For detailed instructions, see "man halcmd".
*/

/** Copyright (C) 2003 John Kasunich
                       <jmkasunich AT users DOT sourceforge DOT net>

    Other contributers:
                       Martin Kuhnle
                       <mkuhnle AT users DOT sourceforge DOT net>
                       Alex Joni
                       <alex_joni AT users DOT sourceforge DOT net>
                       Benn Lipkowitz
                       <fenn AT users DOT sourceforge DOT net>
                       Stephen Wille Padnos
                       <swpadnos AT users DOT sourceforge DOT net>
 */

/** This program is free software; you can redistribute it and/or
    modify it under the terms of version 2 of the GNU General
    Public License as published by the Free Software Foundation.
    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111 USA

    THE AUTHORS OF THIS LIBRARY ACCEPT ABSOLUTELY NO LIABILITY FOR
    ANY HARM OR LOSS RESULTING FROM ITS USE.  IT IS _EXTREMELY_ UNWISE
    TO RELY ON SOFTWARE ALONE FOR SAFETY.  Any machinery capable of
    harming persons must have provisions for completely removing power
    from all motors, etc, before persons enter any danger area.  All
    machinery must be designed to comply with local and national safety
    codes, and the authors of this software can not, and do not, take
    any responsibility for such compliance.

    This code is part of the Machinekit HAL project.  For more
    information, go to https://github.com/machinekit.
*/

#include "runtime/config.h"

#include "mkini/mk-inifile.h"		/* iniFind() */
FILE *halcmd_inifile = NULL;

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <fnmatch.h>
#include <search.h>

#include "runtime/rtapi.h"		/* RTAPI realtime OS API */
#include "hal/hal.h"		/* HAL public API decls */
#include "hal/hal_priv.h"	/* private HAL decls */
#include "hal_command/halcmd_commands.h"
#include "hal_command/halcmd_rtapiapp.h"



/***********************************************************************
*                  LOCAL FUNCTION DECLARATIONS                         *
************************************************************************/

/* These functions are used internally by this file.  The code is at
   the end of the file.  */

/***********************************************************************
*                         GLOBAL VARIABLES                             *
************************************************************************/

int comp_id = -1;	/* -1 means hal_init() not called yet */
int hal_flag = 0;	/* used to indicate that halcmd might have the
			   hal mutex, so the sig handler can't just
			   exit, instead it must set 'done' */
int halcmd_done = 0;		/* used to break out of processing loop */
int scriptmode = 0;	/* used to make output "script friendly" (suppress headers) */
int prompt_mode = 0;	/* when getting input from stdin, print a prompt */
int echo_mode = 0;
char comp_name[HAL_NAME_LEN+1];	/* name for this instance of halcmd */
int autoload = 1;  // on newinst, if comp not loaded, loadrt it

static void quit(int);

static pid_t pid_of(const char *fmt, ...)
{
    char line[LINELEN];
    FILE *cmd;
    pid_t pid;
    va_list ap;

    strcpy(line, "pidof ");
    va_start(ap, fmt);
    vsnprintf(line + strlen(line), sizeof(line) - strlen(line), fmt, ap);
    va_end(ap);
    cmd = popen(line, "r");
    if (!fgets(line, sizeof(line), cmd))
	pid = -1;
    else
	pid = strtoul(line, NULL, 10);
    pclose(cmd);
    return pid;
}

static char *getuuid(void)
{
    FILE *inifp = NULL;
    const char *mkinifile;

    mkinifile = getenv("MACHINEKIT_INI");
    /* use default if not specified by user */
    if (mkinifile == NULL) {
	mkinifile = MACHINEKIT_INI;
    }
    if ((inifp = fopen(mkinifile,"r")) == NULL) {
	fprintf(stderr, "halsh: FATAL cant open inifile '%s'\n",
		mkinifile);
	return NULL;
    }
    char *s = (char *)iniFind(inifp, "MKUUID","MACHINEKIT");
    if (s) {
	s = strdup(s);
    }
    fclose(inifp);
    return s;
}

int halcmd_startup(int quiet)
{
    char *uuid = getuuid();
    if (uuid == NULL)
	return -EINVAL;
    return halcmd_startup_uuid(quiet, NULL, uuid);
}

int halcmd_startup_uuid(int quiet, char *uri, const char *svc_uuid)
{
    int msg_lvl_save=rtapi_get_msg_level();
    /* register signal handlers - if the process is killed
       we need to call hal_exit() to free the shared memory */
    signal(SIGINT, quit);
    signal(SIGTERM, quit);
    signal(SIGPIPE, SIG_IGN);
    /* at this point all options are parsed, connect to HAL */
    /* create a unique module name, to allow for multiple halcmd's */
    snprintf(comp_name, sizeof(comp_name), "halcmd%d", getpid());
    /* tell the signal handler that we might have the mutex */
    hal_flag = 1;
    if (quiet) rtapi_set_msg_level(RTAPI_MSG_NONE);

    // make sure the rtapi demon is up and responding
    // this closes a race condition where hal_init() would be called
    // before rtapi_app is done
    int retval;
    if ((retval = rtapi_connect(rtapi_instance, uri, svc_uuid))) {
        if (!quiet) {
            fprintf(stderr, "halcmd: cant connect to rtapi_app: %d (uri=%s uuid=%s): %s\n\n",
		    retval, uri ? uri:"", svc_uuid, rtapi_rpcerror());

	    const char *const logfile = HAL_LOG_FILE;

	    if (pid_of("rtapi:%d", rtapi_instance) < 0)
		fprintf(stderr, "halcmd: the rtapi:%d RT demon is not running - please investigate %s\n",
			rtapi_instance, logfile);

	    if (pid_of("msgd:%d", rtapi_instance) < 0)
		fprintf(stderr, "halcmd: the msgd:%d logger demon is not running - please investigate %s\n",
			rtapi_instance, logfile);
        }
	return -EINVAL;
    }

    /* connect to the HAL */
    comp_id = hal_init(comp_name);
    if (quiet) rtapi_set_msg_level(msg_lvl_save);
    /* done with mutex */
    hal_flag = 0;
    /* check result */
    if (comp_id < 0) {
        if (!quiet) {
            fprintf(stderr, "halcmd: hal_init() failed: %d\n", comp_id );
            fprintf(stderr, "NOTE: 'rtapi' module must be loaded\n" );
        }
	return -EINVAL;
    }
    hal_ready(comp_id);

    return 0;
}

void halcmd_shutdown(void) {
    rtapi_cleanup();
    /* tell the signal handler we might have the mutex */
    hal_flag = 1;
    hal_exit(comp_id);
}

/* parse_cmd() decides which command has been invoked, gathers any
   arguments that are needed, and calls a function to execute the
   command.
*/

#define FUNCT(x) ((halcmd_func_t)x)

struct halcmd_command halcmd_commands[] = {
    {"addf",    FUNCT(do_addf_cmd),    A_TWO | A_PLUS },
    //    {"alias",   FUNCT(do_alias_cmd),   A_THREE },
    {"delf",    FUNCT(do_delf_cmd),    A_TWO | A_OPTIONAL },
    {"delsig",  FUNCT(do_delsig_cmd),  A_ONE },
    {"echo",    FUNCT(do_echo_cmd),    A_ZERO },
    {"delthread",  FUNCT(do_delthread_cmd),  A_ONE },
    {"getp",    FUNCT(do_getp_cmd),    A_ONE },
    {"gets",    FUNCT(do_gets_cmd),    A_ONE },
    {"ptype",   FUNCT(do_ptype_cmd),   A_ONE },
    {"stype",   FUNCT(do_stype_cmd),   A_ONE },
    {"help",    FUNCT(do_help_cmd),    A_ONE | A_OPTIONAL },
    {"linkpp",  FUNCT(do_linkpp_cmd),  A_TWO | A_REMOVE_ARROWS },
    {"linkps",  FUNCT(do_linkps_cmd),  A_TWO | A_REMOVE_ARROWS },
    {"linksp",  FUNCT(do_linksp_cmd),  A_TWO | A_REMOVE_ARROWS },
    {"list",    FUNCT(do_list_cmd),    A_ONE | A_PLUS },
    {"loadrt",  FUNCT(do_loadrt_cmd),  A_ONE | A_PLUS },
    {"loadusr", FUNCT(do_loadusr_cmd), A_PLUS | A_TILDE },
    {"lock",    FUNCT(do_lock_cmd),    A_ONE | A_OPTIONAL },
    {"log",     FUNCT(do_log_cmd),     A_TWO | A_OPTIONAL},
    {"net",     FUNCT(do_net_cmd),     A_ONE | A_PLUS | A_REMOVE_ARROWS },
    {"newsig",  FUNCT(do_newsig_cmd),  A_TWO },
    {"ping",    FUNCT(do_ping_cmd), A_ZERO },
    {"save",    FUNCT(do_save_cmd),    A_TWO | A_OPTIONAL | A_TILDE },
    {"setexact_for_test_suite_only", FUNCT(do_setexact_cmd), A_ZERO },
    {"setp",    FUNCT(do_setp_cmd),    A_TWO },
    {"sets",    FUNCT(do_sets_cmd),    A_TWO },
    {"sete",    FUNCT(do_sete_cmd),    A_TWO },
    {"show",    FUNCT(do_show_cmd),    A_ONE | A_OPTIONAL | A_PLUS},
    {"sweep",   FUNCT(do_sweep_cmd),   A_ONE | A_OPTIONAL },
    {"shutdown",FUNCT(do_shutdown_cmd), A_ZERO },
    {"sleep",   FUNCT(do_sleep_cmd),  A_ONE },
    {"source",  FUNCT(do_source_cmd),  A_ONE | A_TILDE },
    {"start",   FUNCT(do_start_cmd),   A_ZERO},
    {"status",  FUNCT(do_status_cmd),  A_ONE | A_OPTIONAL },
    {"stop",    FUNCT(do_stop_cmd),    A_ZERO},
    //    {"unalias", FUNCT(do_unalias_cmd), A_TWO },
    {"unecho",  FUNCT(do_unecho_cmd),  A_ZERO },
    {"unlinkp", FUNCT(do_unlinkp_cmd), A_ONE },
    {"unload",  FUNCT(do_unload_cmd),  A_ONE },
    {"unloadrt", FUNCT(do_unloadrt_cmd), A_ONE },
    {"unloadusr", FUNCT(do_unloadusr_cmd), A_ONE },
    {"unlock",  FUNCT(do_unlock_cmd),  A_ONE | A_OPTIONAL },
    {"waitusr", FUNCT(do_waitusr_cmd), A_TWO | A_OPTIONAL  },

    {"newthread",FUNCT(do_newthread_cmd), A_ONE |  A_PLUS},
    {"newg",    FUNCT(do_newg_cmd),    A_ONE |  A_PLUS},
    {"delg",    FUNCT(do_delg_cmd),    A_ONE },
    {"newm",    FUNCT(do_newm_cmd),    A_TWO | A_OPTIONAL | A_PLUS},
    {"delm",    FUNCT(do_delm_cmd),    A_TWO },

    {"setrmb", FUNCT(do_setrmb_cmd), A_ONE },
    {"setwmb", FUNCT(do_setwmb_cmd), A_ONE },

    {"clearrmb", FUNCT(do_clear_rmb_cmd), A_ONE },
    {"clearwmb", FUNCT(do_clear_wmb_cmd), A_ONE },


    {"newring", FUNCT(do_newring_cmd), A_TWO |  A_PLUS},
    {"delring", FUNCT(do_delring_cmd), A_ONE },
    {"ringdump", FUNCT(do_ringdump_cmd), A_ONE },
    {"ringflush", FUNCT(do_ringflush_cmd), A_ONE},
    {"ringwrite", FUNCT(do_ringwrite_cmd), A_ONE | A_OPTIONAL |  A_PLUS},
    {"newcomp", FUNCT(do_newcomp_cmd), A_ONE |  A_PLUS},
    {"newpin",  FUNCT(do_newpin_cmd), A_THREE |  A_PLUS},
    {"ready",   FUNCT(do_ready_cmd),    A_ONE | A_OPTIONAL },
    {"waitbound", FUNCT(do_waitbound_cmd), A_ONE| A_OPTIONAL  },
    {"waitexists", FUNCT(do_waitexists_cmd), A_ONE },
    {"waitunbound", FUNCT(do_waitunbound_cmd), A_ONE| A_OPTIONAL  },
    {"newinst",  FUNCT(do_newinst_cmd),  A_TWO | A_PLUS },
    {"delinst",  FUNCT(do_delinst_cmd),  A_ONE },
    {"call",  FUNCT(do_callfunc_cmd),  A_ONE | A_PLUS },
    {"autoload", FUNCT(do_autoload_cmd),  A_ONE | A_OPTIONAL },
};
int halcmd_ncommands = (sizeof(halcmd_commands) / sizeof(halcmd_commands[0]));

static int sort_command(const void *a, const void *b) {
    const struct halcmd_command *ca = a, *cb = b;
    return strcmp(ca->name, cb->name);
}

static int compare_command(const void *namep, const void *commandp) {
    const char *name = namep;
    const struct halcmd_command *command = commandp;
    return strcmp(name, command->name);
}


pid_t hal_systemv_nowait(char *const argv[]) {
    pid_t pid;
    int n;

    /* now we need to fork, and then exec .... */
    pid = fork();
    if ( pid < 0 ) {
	/* fork failed */
	halcmd_error("fork() failed\n");
	return -1;
    }
    if ( pid == 0 ) {
	/* child process */
	/* print debugging info if "very verbose" (-V) */
        for(n=0; argv[n] != NULL; n++) {
	    rtapi_print_msg(RTAPI_MSG_DBG, "%s ", argv[n] );
	}
        if (n == 0) {
            halcmd_error("hal_systemv_nowait: empty argv array passed in\n");
            exit(1);
        }
	rtapi_print_msg(RTAPI_MSG_DBG, "\n" );


        /* call execv() to invoke command */
	execvp(argv[0], argv);
	/* should never get here */
	halcmd_error("execv(%s): %s\n", argv[0], strerror(errno) );
	exit(1);
    }
    /* parent process */
    return pid;
}

int hal_systemv(char *const argv[]) {
    pid_t pid;
    int status;
    int retval;

    /* do the fork */
    pid = hal_systemv_nowait(argv);
    /* this is the parent process, wait for child to end */
    retval = waitpid ( pid, &status, 0 );
    if (comp_id < 0) {
	fprintf(stderr, "halcmd: hal_init() failed after systemv: %d\n", comp_id );
	exit(-1);
    }
    //XXX FIXME hal_ready(comp_id);
    /* check result of waitpid() */
    if ( retval < 0 ) {
	halcmd_error("waitpid(%d) failed: %s\n", pid, strerror(errno) );
	return -1;
    }
    if (WIFSIGNALED(status) && WTERMSIG(status))
    {
	halcmd_error("child %s caught signal %s\n", 
		     argv[0], strsignal(WTERMSIG(status)));
    }
    if (WIFSIGNALED(status) && WCOREDUMP(status))
    {
	halcmd_error("child %s created a core dump, signal %s\n", 
		     argv[0], strsignal(WTERMSIG(status)));
	return -1;
    }
    if ( WIFEXITED(status) == 0 ) {
	halcmd_error("child did not exit normally\n");
	return -1;
    }
    retval = WEXITSTATUS(status);
    if ( retval != 0 ) {
	halcmd_error("exit value: %d\n", retval );
	return -1;
    }
    return 0;
}


/***********************************************************************
*                            MAIN PROGRAM                              *
************************************************************************/

/* signal handler */
static void quit(int sig)
{
    if ( hal_flag ) {
	/* this process might have the hal mutex, so just set the
	   'done' flag and return, exit after mutex work finishes */
	halcmd_done = 1;
    } else {
	/* don't have to worry about the mutex, but if we just
	   return, we might return into the fgets() and wait 
	   all day instead of exiting.  So we exit from here. */
	if ( comp_id > 0 ) {
	    hal_exit(comp_id);
	}
	_exit(1);
    }
}
/* main() is responsible for parsing command line options, and then
   parsing either a single command from the command line or a series
   of commands from a file or standard input.  It breaks the command[s]
   into tokens, and passes them to parse_cmd() which does the actual
   work for each command.
*/

static int count_args(char **argv) {
    int i = 0;
    while(argv[i] && argv[i][0]) i++;
    return i;
}

#define ARG(i) (argc > i ? argv[i] : 0)
#define REST(i) (argc > i ? argv + i : argv + argc)

static int parse_cmd1(char **argv) {
    struct halcmd_command *command = bsearch(argv[0],
                halcmd_commands, halcmd_ncommands,
		sizeof(struct halcmd_command), compare_command);
    int argc = count_args(argv);

    if(argc == 0)
        return 0;

    if(!command) {
	// special case: sig = newvalue
	if(argc == 3 && strcmp(argv[1], "=")) {
	    return do_setp_cmd(argv[0], argv[2]);
	} else {
            halcmd_error("Unknown command '%s'\n", argv[0]);
            return -EINVAL;
        }
    } else {
	int result = -EINVAL;
	int is_optional = command->type & A_OPTIONAL,
	    is_plus = command->type & A_PLUS,
	    nargs = command->type & 0xff,
	    posargs;

	if(command->type & A_REMOVE_ARROWS) {
	    int s, d;
	    for(s=d=0; argv[s] && argv[s][0]; s++) {
		if(argv[s][0] == '<' || argv[s][0] == '=') {
		    continue;
		} else {
		    argv[d++] = argv[s];
		}
	    }
	    argv[d] = 0;
	    argc = d;
	}

        posargs = argc - 1;
	if(posargs < nargs && !is_optional) {
	    halcmd_error("%s requires %s%d arguments, %d given\n",
		command->name, is_plus ? "at least " : "", nargs, posargs);
	    return -EINVAL;
	}

        if(posargs > nargs && !is_plus) {
	    halcmd_error("%s requires %s%d arguments, %d given\n",
		command->name, is_optional ? "at most " : "", nargs, posargs);
	    return -EINVAL;
        }

#ifndef NO_INI
	if(command->type & A_TILDE)
	{
	    int i;
	    for(i=0; i<argc; i++)
	    {
		char *buf = malloc(LINELEN);
		TildeExpansion(argv[i], buf, LINELEN);
		argv[i] = buf;
	    }
	}
#endif
	if(!strcmp(command->name, "echo")) {echo_mode = 1;}
	if(!strcmp(command->name, "unecho")) {echo_mode = 0;}
	switch(nargs | is_plus) {
	case A_ZERO: {
	    result = command->func();
	    break;
	}

	case A_PLUS: {
	    int(*f)(char **args) = (int(*)(char**))command->func;
	    result = f(REST(1));
	    break;
	}

	case A_ONE: {
	    int(*f)(char *arg) = (int(*)(char*))command->func;
	    result = f(ARG(1));
	    break;
	}

	case A_ONE | A_PLUS: {
	    int(*f)(char *arg, char **rest) =
                (int(*)(char*,char**))command->func;
	    result = f(ARG(1), REST(2));
	    break;
	}

	case A_TWO: {
	    int(*f)(char *arg, char *arg2) =
                (int(*)(char*,char*))command->func;
	    result = f(ARG(1), ARG(2));
	    break;
	}

	case A_TWO | A_PLUS: {
	    int(*f)(char *arg, char *arg2, char **rest) =
                (int(*)(char*,char*,char**))command->func;
	    result = f(ARG(1), ARG(2), REST(3));
	    break;
	}

	case A_THREE: {
	    int(*f)(char *arg, char *arg2, char *arg3) =
                (int(*)(char*,char*,char*))command->func;
	    result = f(ARG(1), ARG(2), ARG(3));
	    break;
	}

	case A_THREE | A_PLUS: {
	    int(*f)(char *arg, char *arg2, char *arg3, char **rest) =
                (int(*)(char*,char*,char*,char**))command->func;
	    result = f(ARG(1), ARG(2), ARG(3), REST(4));
	    break;
	}

	default:
	    halcmd_error("BUG: unchandled case: command=%s type=0x%x",
		command->name, command->type);
	    result = -EINVAL;
	}

#ifndef NO_TILDE
	if(command->type & A_TILDE)
	{
	    int i;
	    for(i=0; i<argc; i++)
	    {
		free(argv[i]);
	    }
	}
#endif

        return result;
    }
}

int halcmd_parse_cmd(char *tokens[])
{
    int retval;
    static int first_time = 1;

    if(first_time) {
        /* ensure that commands is sorted when it is searched later */
        qsort(halcmd_commands, halcmd_ncommands,
                sizeof(struct halcmd_command), sort_command);
        first_time = 0;
    }

    hal_flag = 1;
    retval = parse_cmd1(tokens);
    hal_flag = 0;
    return retval;
}

/* tokenize() sets an array of pointers to each non-whitespace
   token in the input line.  It expects that variable substitution
   and comment removal have already been done, and that any
   trailing newline has been removed.
*/
static int tokenize(char *cmd_buf, char **tokens)
{
    enum { BETWEEN_TOKENS,
           IN_TOKEN,
	   SINGLE_QUOTE,
	   DOUBLE_QUOTE,
	   END_OF_LINE } state;
    char *cp1;
    int m;
    /* convert a line of text into individual tokens */
    m = 0;
    cp1 = cmd_buf;
    state = BETWEEN_TOKENS;
    while ( m < MAX_TOK ) {
        if(*cp1 == '\r') 
        {
            char nextc = *(cp1+1);
            if(nextc == '\n' || nextc == '\0') 
            {
                static int warned=0;
                if(!warned) 
                    halcmd_warning("File contains DOS-style line endings.\n");
                warned = 1;
            }
            else 
            {
                halcmd_error("File contains embedded carriage returns.\n");
                return -1;
            }
        }
	switch ( state ) {
	case BETWEEN_TOKENS:
	    if ( *cp1 == '\0' ) {
		/* end of the line */
		state = END_OF_LINE;
	    } else if ( isspace(*cp1) ) {
		/* whitespace, skip it */
		cp1++;
	    } else if ( *cp1 == '\'' ) {
		/* start of a quoted string and a new token */
		tokens[m] = cp1++;
		state = SINGLE_QUOTE;
	    } else if ( *cp1 == '\"' ) {
		/* start of a quoted string and a new token */
		tokens[m] = cp1++;
		state = DOUBLE_QUOTE;
	    } else {
		/* first char of a token */
		tokens[m] = cp1++;
		state = IN_TOKEN;
	    }
	    break;
	case IN_TOKEN:
	    if ( *cp1 == '\0' ) {
		/* end of the line */
		m++;
		state = END_OF_LINE;
	    } else if ( *cp1 == '\'' ) {
		/* start of a quoted string */
		cp1++;
		state = SINGLE_QUOTE;
	    } else if ( *cp1 == '\"' ) {
		/* start of a quoted string */
		cp1++;
		state = DOUBLE_QUOTE;
	    } else if ( isspace(*cp1) ) {
		/* end of the current token */
		*cp1++ = '\0';
		m++;
		state = BETWEEN_TOKENS;
	    } else {
		/* ordinary character */
		cp1++;
	    }
	    break;
	case SINGLE_QUOTE:
	    if ( *cp1 == '\0' ) {
		/* end of the line */
		m++;
		state = END_OF_LINE;
	    } else if ( *cp1 == '\'' ) {
		/* end of quoted string */
		cp1++;
		state = IN_TOKEN;
	    } else {
		/* ordinary character */
		cp1++;
	    }
	    break;
	case DOUBLE_QUOTE:
	    if ( *cp1 == '\0' ) {
		/* end of the line */
		m++;
		state = END_OF_LINE;
	    } else if ( *cp1 == '\"' ) {
		/* end of quoted string */
		cp1++;
		state = IN_TOKEN;
	    } else {
		/* ordinary character, copy to buffer */
		cp1++;
	    }
	    break;
	case END_OF_LINE:
	    tokens[m++] = cp1;
	    break;
	default:
	    /* should never get here */
	    state = BETWEEN_TOKENS;
	}
    }
    if ( state != END_OF_LINE ) {
	halcmd_error("too many tokens on line\n");
	return -1;
    }
    return 0;
}

/* strip_comments() removes any comment in the string.  It also
   removes any trailing newline.  Single- or double-quoted strings
   are respected - a '#' inside a quoted string does not indicate
   a comment.
*/
static int strip_comments ( char *buf )
{
    enum { NORMAL,
	   SINGLE_QUOTE,
	   DOUBLE_QUOTE
	 } state;
    char *cp1;

    cp1 = buf;
    state = NORMAL;
    while ( 1 ) {
	switch ( state ) {
	case NORMAL:
	    if (( *cp1 == '#' ) || ( *cp1 == '\n' ) || ( *cp1 == '\0' )) {
		/* end of the line */
		*cp1 = '\0';
		return 0;
	    } else if ( *cp1 == '\'' ) {
		/* start of a quoted string */
		cp1++;
		state = SINGLE_QUOTE;
	    } else if ( *cp1 == '\"' ) {
		/* start of a quoted string */
		cp1++;
		state = DOUBLE_QUOTE;
	    } else {
		/* normal character */
		cp1++;
	    }
	    break;
	case SINGLE_QUOTE:
	    if (( *cp1 == '\n' ) || ( *cp1 == '\0' )) {
		/* end of the line, unterminated quoted string */
		*cp1 = '\0';
		return -1;
	    } else if ( *cp1 == '\'' ) {
		/* end of quoted string */
		cp1++;
		state = NORMAL;
	    } else {
		/* ordinary character */
		cp1++;
	    }
	    break;
	case DOUBLE_QUOTE:
	    if (( *cp1 == '\n' ) || ( *cp1 == '\0' )) {
		/* end of the line, unterminated quoted string */
		*cp1 = '\0';
		return -1;
	    } else if ( *cp1 == '\"' ) {
		/* end of quoted string */
		cp1++;
		state = NORMAL;
	    } else {
		/* ordinary character */
		cp1++;
	    }
	    break;
	default:
	    /* should never get here */
	    state = NORMAL;
	}
    }
}

/* strlimcpy:
   a wrapper for strncpy which has two improvements:
   1) takes two variables for limits, and uses the lower of the two for the limit
   2) subtracts the number of characters copied from the second limit

   This allows one to keep track of remaining buffer size and use the correct limits
   simply

   Parameters are:
   pointer to destination string pointer
   source string pointer
   source length to copy
   pointer to remaining destination space

   return value:
   -1 if the copy was limited by destination space remaining
   0 otherwise
   Side Effects:
	the value of *destspace will be reduced by the copied length
	The value of *dest will will be advanced by the copied length

*/
static int strlimcpy(char **dest, char *src, int srclen, int *destspace) {
    if (*destspace < srclen) {
	return -1;
    } else {
	strncpy(*dest, src, srclen);
	(*dest)[srclen] = '\0';
	srclen = strlen(*dest);		/* use the actual number of bytes copied */
	*destspace -= srclen;
	*dest += srclen;
    }
    return 0;
}

/* replace_vars:
   replaces environment and ini var references in source_str.
   This routine does string replacement only
   return value is 0 on success (ie, no variable lookups failed)
   The source string is not modified

   In case of an error, dest_str will contain everything that was
   successfully replaced, but will not contain anything past the
   var that caused the error.

   environment vars are in the following formats:
   $envvar<whitespace>
   $(envvar)<any char>

   ini vars are in the following formats:
   [SECTION]VAR<whitespace>
   [SECTION](VAR)<any char>
   
   return values:
   0	success
   -1	missing close parenthesis
   -2	null variable name (either environment or ini varname)
   -3	missing close square bracket
   -4	environment variable not found
   -5	ini variable not found
   -6	replacement would overflow output buffer
   -7	var name exceeds limit
*/
static int replace_vars(char *source_str, char *dest_str, int max_chars, char **detail)
{
    int retval = 0, loopcount=0;
    int next_delim, remaining, buf_space;
    char *replacement, sec[128], var[128];
    static char info[256];
    char *sp=source_str, *dp=dest_str, *secP, *varP;
    const char 
	* words = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-_";

    dest_str[max_chars-1] = '\0';	/* make sure there's a terminator */
    *dest_str='\0';			/* return null string if input is null string */
    buf_space = max_chars-1;		/* leave space for terminating null */
    while ((remaining = strlen(sp)) > 0) {
	loopcount++;
	next_delim=strcspn(sp, "$[");
	if (strlimcpy(&dp, sp, next_delim, &buf_space) < 0)
	    return -6;
	sp += next_delim;
	if (next_delim < remaining) {		/* a new delimiter was found */
	    switch (*sp++) {	/* this is the found delimiter, since sp was incremented */
	    case '$':
		varP = sp;
		if (*sp=='(') {		/* look for a parenthesized var */
		    varP=++sp;
		    next_delim=strcspn(varP, ")");
		    if (next_delim >= strlen(varP))	/* error - no matching parens */
			return -1;
		    sp++;
		} else next_delim = strspn(varP, words);
		if (next_delim == 0)		/* null var name */
		    return -2;
		if (next_delim > 127)		/* var name too long */
		    return -7;
		strncpy(var, varP, next_delim);
		var[next_delim]='\0';
		replacement = getenv(var);
		if (replacement == NULL) 
                {
                    snprintf(info, sizeof(info), "%s", var);
                    *detail = info;
		    return -4;
                }
		if (strlimcpy(&dp, replacement, strlen(replacement), &buf_space) <0)
		    return -6;
		sp += next_delim;
		break;
	    case '[':
		secP = sp;
		next_delim = strcspn(secP, "]");
		if (next_delim >= strlen(secP))	/* error - no matching square bracket */
		    return -3;
		if (next_delim > 127)		/* section name too long */
		    return -7;
		strncpy(sec, secP, next_delim);
		sec[next_delim]='\0';
		sp += next_delim+1;
		varP = sp;		/* should point past the ']' now */
		if (*sp=='(') {		/* look for a parenthesized var */
		    varP=++sp;
		    next_delim=strcspn(varP, ")");
		    if (next_delim > strlen(varP))	/* error - no matching parens */
			return -1;
		    sp++;
		} else next_delim = strspn(varP, words);
		if (next_delim == 0)
		    return -2;
		if (next_delim > 127)		/* var name too long */
		    return -7;
		strncpy(var, varP, next_delim);
		var[next_delim]='\0';
		if ( strlen(sec) > 0 ) {
		/* get value from ini file */
		/* cast to char ptr, we are discarding the 'const' */
		    replacement = (char *) iniFind(halcmd_inifile, var, sec);
		} else {
		/* no section specified */
		    replacement = (char *) iniFind(halcmd_inifile, var, NULL);
		}
		if (replacement==NULL) {
                    *detail = info;
                    if (snprintf(info, sizeof(info)-1, "[%s]%s", sec, var) < 0) {
                      return -7;                 // var name too long
                    }
		    return -5;
                }
		if (strlimcpy(&dp, replacement, strlen(replacement), &buf_space) < 0)
		    return -6;
		sp += next_delim;
		break;
	    }
	}
    }
    return retval;
}


static const char *replace_errors[] = {
	"Missing close parenthesis.\n",
	"Empty variable name.\n",
	"Missing close square bracket.\n",
	"Environment variable '%s' not found.\n",
	"Ini variable '%s' not found.\n",
	"Line too long.\n",
	"Variable name too long.\n",
};


int halcmd_preprocess_line ( char *line, char **tokens )
{
    int retval;
    char *detail = NULL;
    static char cmd_buf[2*MAX_CMD_LEN];

    /* strip comments and trailing newline (if any) */
    retval = strip_comments(line);
    if (retval != 0) {
	halcmd_error("unterminated quoted string\n");
	return -1;
    }
    /* copy to cmd_buf while doing variable replacements */
    retval = replace_vars(line, cmd_buf, sizeof(cmd_buf)-2, &detail);
    if (retval != 0) {
	if ((retval < 0) && (retval >= -7)) {  /* print better replacement errors */
            if(detail) {
                halcmd_error(replace_errors[(-retval) -1], detail);
            } else {
		halcmd_error("%s", replace_errors[(-retval) -1]);
            }
	} else {
		halcmd_error("unknown variable replacement error\n");
	}
	return -2;
    }
    /* split cmd_buff into tokens */
    retval = tokenize(cmd_buf, tokens);
    if (retval != 0) {
	return -3;
    }
    /* tokens[] contains MAX_TOK+1 elements so there is always
       at least one empty one at the end... make it empty now */
    tokens[MAX_TOK] = "";
    return 0;
}

int halcmd_parse_line(char *line) {
    char *tokens[MAX_TOK+1];
    int result = halcmd_preprocess_line(line, tokens);
    if(result < 0) return result;
    return halcmd_parse_cmd(tokens);
}

static int linenumber=0;
static char *filename=NULL;

void halcmd_set_filename(const char *new_filename) {
    if(filename) free(filename);
    filename = strdup(new_filename);
}
const char *halcmd_get_filename(void) { return filename; }

void halcmd_set_linenumber(int new_linenumber) { linenumber = new_linenumber; }
int halcmd_get_linenumber(void) { return linenumber; }

/* vim:sts=4:sw=4:et
 */

void halcmd_output(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    vfprintf(stdout, format, ap);
    va_end(ap);
}

void halcmd_warning(const char *format, ...) {
    va_list ap;
    fprintf(stderr, "%s:%d: Warning: ", halcmd_get_filename(), halcmd_get_linenumber());
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
}

void halcmd_error(const char *format, ...) {
    va_list ap;
    fprintf(stderr, "%s:%d: ", halcmd_get_filename(), halcmd_get_linenumber());
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
}

void halcmd_info(const char *format, ...) {
    va_list ap;
    if(rtapi_get_msg_level() < RTAPI_MSG_INFO) return;
    fprintf(stderr, "%s:%d: ", halcmd_get_filename(), halcmd_get_linenumber());
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
}

void halcmd_echo(const char *format, ...) {
    va_list ap;
    fprintf(stderr, "(%d)<echo>: ", halcmd_get_linenumber());
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
}
