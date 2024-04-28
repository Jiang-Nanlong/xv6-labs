#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char *fmtname(char *path) // 截取路径最后的文件名
{
    static char buf[DIRSIZ + 1];
    char *p;
    for (p = path + strlen(path); p >= path && *p != '/'; p--)
        ;
    p++;
    memmove(buf, p, strlen(p) + 1);
    return buf;
}

void find(char *path, char *findName)
{
    char buf[512], *p;
    int fd;
    struct dirent de; // 这个指的是目录项这一结构体（在kernel/fs.h中定义），其实目录也是一种文件，里面就是存放了一系列的目录项
    struct stat st;   // 这个指的是文件的统计信息（在kernel/stat.h中定义），包含文件类型（目录或文件）/inode/文件引用nlink/文件大小/存放fs的disk dev

    if ((fd = open(path, 0)) < 0) // 打开文件，第二个参数指示的是打开方式，0代表的是O_RDONLY只读的形式。返回值是file descriptor >=0，<0说明open失败
    {
        fprintf(2, "ls: cannot open %s\n", path);
        return;
    }

    if (fstat(fd, &st) < 0) // 获取文件描述符所引用的inode的信息
    {
        fprintf(2, "ls: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch (st.type)
    {
    case T_FILE: // 如果要查找的路径是文件类型，就比较最后的文件名是否相同
        if (strcmp(fmtname(path), findName) == 0)
            printf("%s\n", path);
        break;

    case T_DIR:                                         // 如果是目录
        if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) // 检查缓存有没有溢出
        {
            printf("find: path too long\n");
            break;
        }
        strcpy(buf, path);
        p = buf + strlen(buf);
        *p++ = '/';                                     // buf是一个绝对路径，p是一个文件名，并通过加"/"前缀拼接在buf的后面
        while (read(fd, &de, sizeof(de)) == sizeof(de)) // 在树层查找
        {
            if (de.inum == 0) // 如果目录项的索引节点号为0，也就是没有内容
            {
                continue;
            }
            memmove(p, de.name, DIRSIZ);                                 // memmove, 把de.name信息复制p,其中de.name是char name[255],代表文件名
            p[strlen(de.name)] = 0;                                      // 设置文件名结束符
            if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) // 不要进入.和..
            {
                continue;
            }
            find(buf, findName); // 进入树枝，递归调用下一层
        }
        break;
    }
    close(fd);
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        printf("error parameter num");
        exit(0);
    }
    find(argv[1], argv[2]);
    exit(0);
}
