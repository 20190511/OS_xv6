#include "types.h"
#include "user.h"
#include "date.h"

int main(void)
{
    struct rtcdate r;
    date(&r);
    printf(1, "Current time : %d-%d-%d %d:%d:%d\n", r.year, r.month, r.day, r.hour, r.minute, r.second);
    exit();
}
