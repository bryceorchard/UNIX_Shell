//
// Created by Paula on 2024-07-27.
//

#include "parser.h"
#include "utils.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define READ_CHAR *(*cur_buf)++ = *(*cur)++
#define SKIP_CHAR (*cur)++

/**
 * @brief Reads a single quote
 * @param cur
 * @param cur_buf
 */
static void read_single_quote(char ** cur, char ** cur_buf) {
    SKIP_CHAR;
    while(1) {
        char c = **cur;
        switch(c) {
            case '\'':
                SKIP_CHAR;
            return;
            case '\0':
                fprintf(stderr, "Missing closing '\n");
            return;
            default:
                READ_CHAR;
            break;
        }
    }
}

/**
 * @brief Reads double quotes
 * @param cur
 * @param cur_buf
 */
static void read_double_quote(char ** cur, char ** cur_buf) {
    SKIP_CHAR;
    while(1) {
        char c = **cur;
        switch(c) {
            case '"':
                SKIP_CHAR;
            return;
            case '\\':
                SKIP_CHAR;
            READ_CHAR;
            break;
            case '\0':
                fprintf(stderr, "Missing closing \"\n");
            return;
            default:
                READ_CHAR;
            break;
        }
    }
}

/**
 * @brief
 * @param cur
 * @param cur_buf
 */
static void read_word(char ** cur, char ** cur_buf) {
    while(1) {
        char c = **cur;
        switch (c) {
            case '\0':
            case ' ':
            case '\t':
            case '<':
            case '>':
            case '|':
            case '&':
                **cur_buf = '\0';
            return;
            case '\'':
                read_single_quote(cur, cur_buf);
            break;
            case '"':
                read_double_quote(cur, cur_buf);
            break;
            case '\\':
                SKIP_CHAR;
            READ_CHAR;
            break;
            default:
                READ_CHAR;
            break;
        }
    }
}

/**
 * @brief Split the string in words, according to the simple shell grammar. */
static char **split_in_words(char *line)
{
    char *cur = line;
    char *buf = xmalloc(strlen(line) + 1);
    char *cur_buf;
    char **tab = 0;
    size_t l = 0;
    char c;

    while ((c = *cur) != 0) {
        char *w = 0;
        switch (c) {
            case ' ':
            case '\t':
                /* Ignore any whitespace */
                cur++;
            break;
            case '&':
                w = "&";
            cur++;
            break;
            case '<':
                w = "<";
            cur++;
            break;
            case '>':
                w = ">";
            cur++;
            break;
            case '|':
                w = "|";
            cur++;
            break;
            default:
                /* Another word */
                    cur_buf = buf;
            read_word(&cur, &cur_buf);
            w = strdup(buf);
        }
        if (w) {
            tab = xrealloc(tab, (l + 1) * sizeof(char *));
            tab[l++] = w;
        }
    }
    tab = xrealloc(tab, (l + 1) * sizeof(char *));
    tab[l++] = 0; //last word is zero to signal the end of the command
    free(buf);
    return tab;
}

static void freeseq(char ***seq)
{
    int i, j;

    for (i=0; seq[i]!=0; i++) {
        char **cmd = seq[i];

        for (j=0; cmd[j]!=0; j++) free(cmd[j]);
        free(cmd);
    }
    free(seq);
}


/* Free the fields of the structure but not the structure itself */
static void freecmd(struct cmdline *s)
{
    if (s->in) free(s->in);
    if (s->out) free(s->out);
    if (s->seq) freeseq(s->seq);
}

/**
 * @brief Parses the line into a command line structure - Separates the command into a sequence of commands, in, out and if it should run in background
 * @param pline the user input
 * @return cmdline structure containing the sequence of commands, in file, out file, err file and whether it should run in the background
 */
struct cmdline *parsecmd(char **pline) {
    char *line = *pline; //get the input from the user in the command line

    /*create return value */
    static struct cmdline *static_cmdline = 0;
    struct cmdline *s = static_cmdline;
    if (s == 0) {
        /* if s has not been allocated memory */
        static_cmdline = s = xmalloc(sizeof(struct cmdline));
    }
    else {
        /** if s has been allocated, then free its fields*/
        freecmd(s);
    }

    s->err = 0;
    s->in = 0;
    s->out = 0;
    s->seq = 0;
    s->bg = 0;


    if (line == NULL) {
        freecmd(s);
        free(s);
        return static_cmdline = 0;
    }

    char** words = split_in_words(line);
    free(line);
    *pline = NULL;

    /*To save each command in user input, initially an empty command (lenght 0) */
    char **cmd = xmalloc(sizeof(char *));
    cmd[0] = 0;
    size_t cmd_len = 0;

    /* to save the sequence (a list) of commands, initially empty (lenght 0)*/
    char ***seq = xmalloc(sizeof(char **));
    seq[0] = 0;
    size_t seq_len = 0;


    /* iterate over words, last word is 0 */
    int i = 0;
    char *w;

    while ((w = words[i++]) != 0) {
        switch (w[0]) {
		case '<':
			/* Tricky : the word can only be "<", defines input file, which should come next */
			if (s->in != 0) { //input file already define
				s->err = "only one input file supported";
				goto error;
			}
			if (words[i] == 0) { //next word is empty
				s->err = "filename missing for input redirection";
				goto error;
			}
			switch(words[i][0]){
				case '<':
				case '>':
				case '&':
				case '|':
				  s->err = "incorrect filename for input redirection";
				  goto error;
				default:
					break;
			}
        	s->in = words[i++]; //define input file and go to next word
        	break;
		case '>':
			/* Tricky : the word can only be ">", defines the output file */
			if (s->out != 0) { //output file already defined
				s->err = "only one output file supported";
				goto error;
			}
			if (words[i] == 0) { //next word is empty
				s->err = "filename missing for output redirection";
				goto error;
			}
			switch(words[i][0]){ //next word is a "reserved word"
				case '<':
				case '>':
				case '&':
				case '|':
					s->err = "incorrect filename for output redirection";
					goto error;
				default:
					break;
			}
			s->out = words[i++]; //get output file from words[i] and go to the next word
			break;
		case '&':
			/* Tricky : the word can only be "&", defines that it should run in background*/
			if (cmd_len == 0 || words[i] != 0) { //&must come before or after a command
				s->err = "misplaced ampersand";
				goto error;
			}
			if (s->bg == 1) { //had already defined to run in background
				s->err = "only one ampersand supported";
				goto error;
			}
			s->bg = 1;
			break;
		case '|':
			/* Tricky : the word can only be "|", defines a piped process*/
			if (cmd_len == 0) { //before a | there must be a command.
				s->err = "misplaced pipe";
				goto error;
			}
			if (words[i] == 0) { //next word is empty
				s->err = "second command missing for pipe redirection";
				goto error;
			}
			switch(words[i][0]){
				case '<':
				case '>':
				case '&':
				case '|':
					s->err = "incorrect pipe usage";
					goto error;
				default:
					break;
			}
        	/* add command to the sequence */
			seq = xrealloc(seq, (seq_len + 2) * sizeof(char **));
			seq[seq_len++] = cmd;
			seq[seq_len] = 0;

			cmd = xmalloc(sizeof(char *));
			cmd[0] = 0;
			cmd_len = 0;
			break;
		default:
			/* the word is part of a command, add it to the command*/
			cmd = xrealloc(cmd, (cmd_len + 2) * sizeof(char *));
			cmd[cmd_len++] = w;
			cmd[cmd_len] = 0;
		}
	}

	if (cmd_len != 0) { //add the last command to the sequence of commands to execute
		seq = xrealloc(seq, (seq_len + 2) * sizeof(char **));
		seq[seq_len++] = cmd;
		seq[seq_len] = 0;
	} else if (seq_len != 0) { //if cmd_len is 0, seq len must be 0 too
		s->err = "misplaced pipe end";
		i--;
		goto error;
	} else
		free(cmd);
	free(words);
	s->seq = seq;
	return s;
error:
	/* free words memory and s fields, return with error filled only*/
	while ((w = words[i++]) != 0) {
		switch (w[0]) {
		case '&':
		case '<':
		case '>':
		case '|':
			break;
		default:
			free(w);
		}
	}
	free(words);
	freeseq(seq);
	for (i=0; cmd[i]!=0; i++) free(cmd[i]);
	free(cmd);
	if (s->in) {
		free(s->in);
		s->in = 0;
	}
	if (s->out) {
		free(s->out);
		s->out = 0;
	}
	return s;
}
