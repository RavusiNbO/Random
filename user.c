#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include "rngdriver.h"

static int read_full(int fd, unsigned char *p, size_t n)
{
    size_t done = 0;
    while (done < n) {
        ssize_t r = read(fd, p + done, n - done);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) { perror("read"); return -1; }
        if (r == 0) break;
        done += (size_t)r;
    }
    return (int)done;
}

static void tests(const unsigned char *p, size_t n)
{
    uint64_t ones = 0, runs = 0;
    int prev = -1;
    long double sum = 0, sq = 0;
    size_t i;
    int b;
    uint64_t bits = (uint64_t)n * 8;

    for (i = 0; i < n; i++) {
        sum += p[i];
        sq += (long double)p[i] * p[i];
        for (b = 7; b >= 0; --b) {
            int v = (p[i] >> b) & 1;
            ones += v;
            if (prev < 0 || prev != v) runs++;
            prev = v;
        }
    }
    long double mean = sum / n;
    long double variance = sq / n - mean * mean;

    printf("\\nFrequency: zeros=%llu (%.2Lf%%), ones=%llu (%.2Lf%%)\\n",
        (unsigned long long)(bits - ones),
        100.0L * (bits - ones) / bits,
        (unsigned long long)ones, 100.0L * ones / bits);
    printf("Runs: %llu, runs/bit=%.6Lf\\n",
        (unsigned long long)runs, (long double)runs / bits);
    printf("Byte mean=%.4Lf (uniform expectation 127.5)\\n", mean);
    printf("Byte variance=%.4Lf (uniform expectation about 5461.25)\\n",
        variance);
}

int main(int argc, char **argv)
{
    size_t want = argc > 1 ? strtoul(argv[1], NULL, 10) : 4096;
    unsigned char *buf;
    struct rngdriver_stats st;
    int fd, got, i;

    if (!want || want > 64 * 1024 * 1024) {
        fprintf(stderr, "Usage: %s [bytes: 1..67108864]\\n", argv[0]);
        return 1;
    }
    buf = malloc(want);
    if (!buf) { perror("malloc"); return 1; }

    fd = open("/dev/rngdriver", O_RDONLY);
    if (fd < 0) { perror("open /dev/rngdriver"); free(buf); return 1; }

    printf("Waiting for keyboard/network events; read may block...\\n");
    got = read_full(fd, buf, want);
    if (got <= 0) {
        if (!got) fprintf(stderr, "No data read\\n");
        close(fd); free(buf); return 1;
    }

    printf("Read %d bytes. First bytes: ", got);
    for (i = 0; i < got && i < 32; i++) printf("%02x ", buf[i]);
    printf("\\n");
    test9s(buf, (size_t)got);

    if (ioctl(fd, RNG_IOC_GET_STATS, &st) == 0) {
        printf("\\nDriver counters:\\n");
        printf(" keyboard events: %llu\\n", (unsigned long long)st.keyboard_events);
        printf(" network packets: %llu\\n", (unsigned long long)st.network_packets);
        printf(" dropped events: %llu\\n", (unsigned long long)st.dropped_events);
        printf(" mixed input bytes: %llu\\n", (unsigned long long)st.mixed_input_bytes);
        printf(" pool updates: %llu\\n", (unsigned long long)st.pool_updates);
        printf("NOTE: mixed bytes/events are not entropy-bit estimates.\\n");
    } else perror("ioctl GET_STATS");

    close(fd);
    free(buf);
    return 0;
}
