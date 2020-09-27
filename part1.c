#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
int main()
{
  sleep(1);
  sleep(1);
  sleep(1);
  sleep(1);
  int fd = open("part1.o", O_WRONLY|O_CREAT, 0666);
  pid_t pid = fork();
  if(pid == 0)
  {
    close(1);
    dup(fd);
    close(fd);
    char *args[] = {"/bin/ls", "./",  "part1.o", NULL};
    execv(args[0], args);
    printf("part1 test...");
  }
  else
  {
    waitpid(pid, NULL, 0);
  }
}
