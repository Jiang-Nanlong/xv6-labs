#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char **argv)
{
    int parent_child[2], child_parent[2];
    pipe(parent_child); // 创建父进程写，子进程读的管道
    pipe(child_parent); // 创建子进程写，父进程读的管道
    char buf[1];

    if (fork() == 0) // 子进程
    {
        close(parent_child[1]); // 关闭不需要的文件描述符，开始的时候我还在想这里关闭了，会不会影响父进程使用
        // 后来发现不会，因为子进程和父进程是两个独立的进程，都拥有各自的文件描述符表，关闭文件描述符只会影响当前进程的文件描述符表，不会影响其他进程。
        close(child_parent[0]);

        if (read(parent_child[0], buf, 1) == 1)
        {
            printf("%d receive ping\n", getpid());
        }
        else
        {
            printf("%d dont receive ping\n", getpid());
            exit(1);
        }

        if (write(child_parent[1], buf, 1) != 1)
        {
            printf("child write to parent failed\n");
            exit(1);
        }
        exit(0);
    }
    else // 父进程
    {
        close(parent_child[0]);
        close(child_parent[1]);
        if (write(parent_child[1], buf, 1) != 1) // 父进程的读写顺序和子进程的读写顺序相反，如果相同就会陷入死锁，互相等待读取对方写入的数据
        {
            printf("parent write to child failed\n");
            exit(1);
        }

        if (read(child_parent[0], buf, 1) == 1)
        {
            printf("%d receive pong\n", getpid());
        }
        else
        {
            printf("%d dont receive pong\n", getpid());
            exit(1);
        }
        wait(0); // 等待子进程执行完，有无都可
        exit(0);
    }
}

