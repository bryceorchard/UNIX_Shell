//
// Created by Paula on 2024-07-27.
//

#ifndef PARSER_H
#define PARSER_H
#endif //PARSER_H

struct cmdline *parsecmd(char **pline) ;

/* Structure returned by parsecmd() function. seq is the sequence of commands */
struct cmdline {
    char *err;	    /* If not null, it is an error message that should be
                        displayed. The other fields are null. */
    char *in;	    /* If not null : name of file for input redirection. */
    char *out;	    /* If not null : name of file for output redirection. */
    int   bg;       /* If set the command must run in background */
    char ***seq;	/* See comment below */
};

/* Field seq of struct cmdline :
A command line is a sequence of commands whose output is linked to the input
of the next command by a pipe. To describe such a structure :
A command is an array of strings (char **), whose last item is a null pointer.
(first pointer to the array, second pointer to the string)
A sequence is an array of commands (char ***), whose last item is a null
pointer.
When the user enters an empty line, seq[0] is NULL.
*/
