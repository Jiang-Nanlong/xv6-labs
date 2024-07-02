#include "kernel/param.h"
#include "kernel/types.h"
#include "user.h"
#include "kernel/sysinfo.h"

int
main(int argc, char**argv){
    if(argc != 1){
        fprintf(2,"param too many\n");
        exit(1);
    }
    struct sysinfo info;
    sysinfo(&info);
    printf("free memmory: %d\n",info.freemem);
    printf("used process:%d\n",info.nproc);
    exit(0);
}
