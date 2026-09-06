#include <stdio.h>
#include <stdlib.h>

#include "ussr.h"

extern FILE *yyin;

int yyparse(void);

static int run_file(const char *filename)
{
    FILE *file;
    int result;

    file = fopen(filename, "r");

    if (file == NULL)
    {
        perror(filename);
        return EXIT_FAILURE;
    }

    yyin = file;

    result = yyparse();

    fclose(file);

    return result == 0
        ? EXIT_SUCCESS
        : EXIT_FAILURE;
}

static int run_repl(void)
{
    int result;

    printf("USSR Unified Shell Script REPL\n");
    printf("USSR v0.1\n");
    printf("Enter a command list or press Ctrl-D to exit.\n\n");

    while (!feof(stdin))
    {
        printf("ussr> ");
        fflush(stdout);

        result = yyparse();

        if (result != 0)
            return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    int result;

    ussr_init();

    if (argc > 1)
        result = run_file(argv[1]);
    else
        result = run_repl();

    ussr_cleanup();

    return result;
}