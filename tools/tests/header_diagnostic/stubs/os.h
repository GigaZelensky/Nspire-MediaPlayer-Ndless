#ifndef HEADER_TEST_OS_H
#define HEADER_TEST_OS_H
#include <stddef.h>
typedef struct NUC_FILE NUC_FILE;
NUC_FILE *nuc_fopen(const char *,const char *);
size_t nuc_fread(void *,size_t,size_t,NUC_FILE *);
int nuc_fseek(NUC_FILE *,long,int);
long nuc_ftell(NUC_FILE *);
int nuc_fclose(NUC_FILE *);
#endif
