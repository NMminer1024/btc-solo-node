// touchswipe: inject a swipe gesture into /dev/input/event0
// usage: touchswipe <x0> <y0> <x1> <y1> [steps]
// raw coords are ft5x06 ABS_X/ABS_Y values (physical 480x800 panel).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>

static int fd;

static void ev(int type, int code, int value)
{
    struct input_event e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    e.code = code;
    e.value = value;
    if (write(fd, &e, sizeof(e)) != sizeof(e))
        perror("write");
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s <raw_x> <raw_y_start> <raw_y_end> [steps]\n", argv[0]);
        return 1;
    }
    int x0 = atoi(argv[1]);
    int y0 = atoi(argv[2]);
    int x1 = atoi(argv[3]);
    int y1 = atoi(argv[4]);
    int steps = argc > 5 ? atoi(argv[5]) : 10;

    fd = open("/dev/input/event0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    // touch down
    ev(EV_ABS, ABS_X, x0);
    ev(EV_ABS, ABS_Y, y0);
    ev(EV_KEY, BTN_TOUCH, 1);
    ev(EV_SYN, SYN_REPORT, 0);
    usleep(80 * 1000);

    // move
    for (int i = 1; i <= steps; i++) {
        int x = x0 + (x1 - x0) * i / steps;
        int y = y0 + (y1 - y0) * i / steps;
        ev(EV_ABS, ABS_X, x);
        ev(EV_ABS, ABS_Y, y);
        ev(EV_SYN, SYN_REPORT, 0);
        usleep(30 * 1000);
    }
    usleep(50 * 1000);

    // touch up
    ev(EV_KEY, BTN_TOUCH, 0);
    ev(EV_ABS, ABS_MT_TRACKING_ID, -1);
    ev(EV_SYN, SYN_REPORT, 0);

    close(fd);
    printf("swipe done: (%d,%d) -> (%d,%d) (%d steps)\n", x0, y0, x1, y1, steps);
    return 0;
}
