#include <stdio.h>

// int fibonacci(int i) {
//     if (i <= 1) {
//         return 1;
//     }
//     return fibonacci(i-1) + fibonacci(i-2);
// }

// int main()
// {
//     int i;
//     i = 0;
//     while (i <= 10) {
//         printf("fibonacci(%2d) = %d\n", i, fibonacci(i));
//         i = i + 1;
//     }
//     return 0;
// }



int global;



int funciton1(int a)
{
    int global;
    global = 1 ;
    printf("%d\n", global);
}

int funciton2(int a)
{
    
    int global; 
    global = 2;
    funciton1(global);
    printf("%d\n", global);
}



int main()
{
    global = 0;
    funciton2(global);
    printf("%d\n", global);
}