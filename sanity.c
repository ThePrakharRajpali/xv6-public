#include "types.h"
#include "user.h"

int main(int argc, char *argv[])
{
    int i, n, j = 0, k;
    if (argc != 2)
    {
        printf(1, "should get number(n) as parameter!\n");
        exit();
    }

    n = atoi(argv[1]);
    i = n;
    int pd;
    for (i = 0; i < 3 * n; i++)
    {
        j = (j == 2) ? 0 : 3;
        pd = fork();
        if (pd == 0)
        {
            switch (j)
            {
            case (0):
                for (k = 0; k < 100; k++)
                {
                    for (j = 0; j < 1000000; j++)
                    {
                    }
                }
                printf(1, "yo!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!11\n");
                break;
            case (1):
                for (k = 0; k < 100; k++)
                {
                    for (j = 0; j < 1000000; j++)
                    {
                    }
                }
                break;

            case (2):
                for (k = 0; k < 100; k++)
                {
                    sleep(1);
                }
                break;
            }
        }
        exit();
        if (pd > 0)
        {
            continue;
        }
        printf(1, "panic!!! fork falild in sanity!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    }
    exit();
}
