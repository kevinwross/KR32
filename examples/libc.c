// KR32 libc demonstration: sizeof/offsetof, formatted output, and heap reuse.

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

struct Point {
    char tag;
    int x;
    double y;
};

_Static_assert(sizeof(struct Point) == 16, "Point layout changed");
_Static_assert(offsetof(struct Point, y) == 8, "Point.y moved");

int main(void) {
    struct Point point;
    point.tag = 'P';
    point.x = 7;
    point.y = 3.25;

    printf("Point size=%d y-offset=%d\n",
           sizeof(struct Point), offsetof(struct Point, y));
    printf("point x=%d y=%.2f\n", point.x, point.y);
    printf("float %.3f  exp %.2e  general %g\n", 3.14159, 12345.0, 0.000123);
    printf("hex=%#x  char=%c  text=%s\n", 255, 'K', "KR32");

    char *first = (char *)malloc(128);
    char *second = (char *)malloc(256);
    if (first == 0 || second == 0) {
        printf("allocation failed\n");
        return 1;
    }
    first[0] = 'A';
    second[0] = 'B';
    free(first);
    char *reused = (char *)malloc(64);
    if (reused != first) {
        printf("heap reuse failed\n");
        return 2;
    }
    free(second);
    free(reused);
    printf("heap reuse: ok\n");
    return 0;
}
