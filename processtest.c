#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

int main(int argc, char *argv[])
{

    int pid;
    int s, a, b, c;
    pid = fork();
    if (pid == 0)
    {
        exec(argv[1], argv);
        printf(1, "exec %s failed\n", argv[1]);
    }
    else
    {
        s = wait2(&a, &b, &c);
        printf(1, "Ready Time = %d Run Time = %d Sleep Time = %d with pid %d \n", a, b, c, s);
    }

    exit();
}