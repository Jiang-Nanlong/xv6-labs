#include "kernel/types.h"
#include "user/user.h"

void func(int p[])
{
    close(p[1]);
    int prime;
    if (read(p[0], &prime, sizeof(int)) == sizeof(int)) // 从上一个管道读出第一个数，一定是素数，作为除数
    {
        printf("prime %d\n", prime);
        int p1[2];
        pipe(p1);
        if (fork() > 0) // 当前进程
        {
            close(p1[0]);
            int i;
            while (read(p[0], &i, sizeof(int))) // 继续读取上一个管道内的值
            {
                if (i % prime != 0) // 判断这个值是否为素数，如果是的话就存入下一个管道
                {
                    if (write(p1[1], &i, sizeof(int)) != sizeof(int))
                    {
                        printf("pipe write failed\n");
                        exit(1);
                    }
                }
            }
            close(p1[1]);
            wait(0);
        }
        else // 递归子进程
        {
            func(p1);
        }
    }
}

int main(int argc, char **argv)
{
    int p[2];
    pipe(p);
    if (fork() > 0)
    {
        close(p[0]);
        printf("prime 2\n");
        for (int i = 3; i <= 35; i++)
        {
            if (i % 2 != 0)
            {
                if (write(p[1], &i, sizeof(int)) != sizeof(int))
                {
                    printf("pipe write failed\n");
                    exit(1);
                }
            }
        }
        close(p[1]);
        wait(0);
    }
    else
    {
        func(p);
    }
    exit(0);
}
