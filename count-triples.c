#include <stdio.h>
#include "turtle_parser.h"

static int count = 0;

static int triple_handler( void *arg,
    const char *subject, const char *predicate, const char *object,
    const char *lexical, const char *datatype, const char *language )
{
    ++count;
    return 0;
}

int main(int argc, char *argv[])
{
    int r;
    FILE *fp;

    if(argc != 2)
    {
        fprintf(stderr, "usage: %s <file>\n", argv[0]);
        return (argc == 1) ? 0 : 1;
    }

    fp = fopen(argv[1], "r");
    if(fp == NULL)
    {
        fprintf(stderr, "Unable to open file \"%s\" for reading!\n", argv[1]);
        return 1;
    }

    if(parse_turtle(fp_reader, (void*)fp, triple_handler, NULL) != 0)
    {
        fprintf(stderr, "An error occured while parsing file \"%s\"!\n", argv[1]);
        return 1;
    }

    printf("%d\n", count);
    
    return 0;
}
