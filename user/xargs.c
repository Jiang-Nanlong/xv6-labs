#include "kernel/types.h"
#include "kernel/stat.h"
#include "user.h"
#include "kernel/fs.h"
#include "kernel/param.h"

void getsubstr(char s[], char *sub, int pos, int len)
{
    int c = 0;
    while (c < len)
    {
        *(sub + c) = s[pos + c];
        c++;
    }
    *(sub + c) = '\0';
}

// 截断每一行之后的'\n'
char *
cutoffinput(char *buf)
{
    if (strlen(buf) > 1 && buf[strlen(buf) - 1] == '\n')
    {
        char *subbuff = (char *)malloc(sizeof(char) * (strlen(buf) - 1));
        getsubstr(buf, subbuff, 0, strlen(buf) - 1);
        return subbuff;
    }
    else
    {
        return buf;
    }
}

int main(int argc, char *argv[])
{
    int pid;
    char buf[MAXPATH];
    char *args[MAXARG];

    int args_num = 0; // 记录标准输入了多少行
    while (1)         // 把所有的标准输入都存到args里,ctrl+D结束
    {
        memset(buf, 0, sizeof(buf));
        gets(buf, MAXPATH);
        char *subbuff = cutoffinput(buf);

        if (strlen(subbuff) == 0 || args_num >= MAXARG)
        {
            break;
        }
        args[args_num] = subbuff;
        args_num++;
    }

    char *argv2exec[MAXARG];
    argv2exec[0] = argv[1];
    int argstartpos = 1;

    for (int i = 2; i < argc; i++, argstartpos++) // 把argv中的数据存入argv2exec
    {
        argv2exec[argstartpos] = argv[i];
    }

    for (int i = 0; i < args_num; i++) // 把args中的数据存入argv2exec
    {
        argv2exec[i + argstartpos] = args[i];
    }
    argv2exec[args_num + argstartpos] = 0;

    if ((pid = fork()) == 0)
    {
        exec(argv[1], argv2exec);
    }
    else
    {
        wait(0);
    }
    exit(0);
}

