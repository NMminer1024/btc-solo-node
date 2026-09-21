#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

static int ok(void) {
    int fd = open("/sys/bus/iio/devices/iio:device0/in_voltage3_raw", O_RDONLY);
    if (fd < 0)
        return 0;
    int v[3] = {-1, -1, -1};
    for (int i = 0; i < 3; i++) {
        char buf[16] = {0};
        lseek(fd, 0, SEEK_SET);
        if (read(fd, buf, 15) > 0)
            v[i] = atoi(buf);
        usleep(20000);
    }
    close(fd);
    if (v[0] > v[1]) { int t = v[0]; v[0] = v[1]; v[1] = t; }
    if (v[1] > v[2]) { int t = v[1]; v[1] = v[2]; v[2] = t; }
    if (v[0] > v[1]) { int t = v[0]; v[0] = v[1]; v[1] = t; }
    return v[1] >= 750 && v[1] <= 820;
}

int main(void) {
    if (ok())
        return 0;
    static const char* argv[] = { "rockbase-lcdui", NULL };
    execv("/usr/bin/rockbase-lcdui", const_cast<char* const*>(argv));
    return 0;
}
