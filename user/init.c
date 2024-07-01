// init: The initial user-level program

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/spinlock.h"
#include "kernel/sleeplock.h"
#include "kernel/fs.h"
#include "kernel/file.h"
#include "user/user.h"
#include "kernel/fcntl.h"

char *argv[] = { "sh", 0 };

int
main(void)
{
  int pid, wpid;

  if(open("console", O_RDWR) < 0){  // �����console����һ���ļ����Զ�д�ķ�ʽ�򿪣���Ϊ�ǵ�һ���򿪵��ļ��������ļ�������Ϊ0
    mknod("console", CONSOLE, 0);  //ʹ��mknod���ļ�consoleע����豸
    open("console", O_RDWR);
  }
  dup(0);  // stdout �����0�ͱ�ʾ�ϱߴ򿪵ĵ�һ���ļ������ﷵ���ļ�������1
  dup(0);  // stderr �����ļ�������2

  for(;;){
    printf("init: starting sh\n");
    pid = fork();
    if(pid < 0){
      printf("init: fork failed\n");
      exit(1);
    }
    if(pid == 0){
      exec("sh", argv);
      printf("init: exec sh failed\n");
      exit(1);
    }

    for(;;){
      // this call to wait() returns if the shell exits,
      // or if a parentless process exits.
      wpid = wait((int *) 0);
      if(wpid == pid){
        // the shell exited; restart it.
        break;
      } else if(wpid < 0){
        printf("init: wait returned an error\n");
        exit(1);
      } else {
        // it was a parentless process; do nothing.
      }
    }
  }
}
