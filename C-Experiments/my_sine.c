#include <stdio.h>
#include <math.h>

#define PI 3.141592653589793

double my_sin(double x)
{
    // Use Taylor series expansion for approximation
    double result = x;
    double term = x;
    int sign = -1;
    for (int i = 3; i <= 20; i += 2)
    {
        term *= (x * x) / ((i - 1) * i);
        result += sign * term;
        sign *= -1;
    }
    return result;
}

int main()
{
    double angle = PI / 6; // 30 degrees in radians
    printf("sin(%f) = %f\n", angle, my_sin(angle));
    return 0;
}