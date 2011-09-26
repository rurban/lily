#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "lily_lexer.h"
#include "lily_parser.h"
#include "lily_emitter.h"
#include "lily_symtab.h"

/* fs_main.c :
 * Since lily will be run from a server for most of the time, this emulates a
 * server...kind of. */

/* The page scanner uses this to send HTML chunks out. Since this isn't a real
   server, it does nothing. */
void lily_impl_send_html(char *htmldata)
{

}

void lily_impl_debugf(char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vprintf(format, ap);
    va_end(ap);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fputs("Usage : lily_fs <filename>\n", stderr);
        exit(EXIT_FAILURE);
    }

    lily_interp *interp = lily_init_interp();
    if (interp == NULL) {
        fputs(interp->excep_msg, stderr);
        exit(EXIT_FAILURE);
    }

    if (lily_parse_file(interp, argv[1]) == 0) {
        fputs(interp->excep_msg, stderr);
        exit(EXIT_FAILURE);
    }

    exit(EXIT_SUCCESS);
}
