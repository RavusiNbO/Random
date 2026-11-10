#ifndef RNGDRIVER_H
#define RNGDRIVER_H
#include <linux/ioctl.h>
#include <linux/types.h>
struct rngdriver_stats {
    __u64 keyboard_events;
    __u64 network_packets;
    __u64 dropped_events;
    __u64 mixed_input_bytes;
    __u64 pool_updates;
};
#define RNG_IOC_MAGIC 'R'
#define RNG_IOC_GET_STATS _IOR(RNG_IOC_MAGIC, 1, struct rngdriver_stats)
#define RNG_IOC_RESET _IO(RNG_IOC_MAGIC, 2)
#endif
