// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/atomic.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/crypto.h>
#include <crypto/hash.h>
#include <linux/byteorder/generic.h>
#include <linux/poll.h>
#include <net/net_namespace.h>
#include "rngdriver.h"

#define QSIZE 1024
#define POOL_SIZE 32
enum { SRC_KEYBOARD = 1, SRC_NETWORK = 2 };

struct rng_event {
    __le64 time_ns;
    __le32 source;
    __le32 sequence;
};

struct rng_ctx {
    u8 pool[POOL_SIZE];
    u64 output_counter;
    struct crypto_shash *tfm;
    struct shash_desc *desc;
    struct mutex state_lock;
    wait_queue_head_t read_wq;
    atomic_t ready;
    spinlock_t qlock;
    struct rng_event queue[QSIZE];
    unsigned int head, tail, count;
    u32 sequence;
    struct work_struct work;
    struct rngdriver_stats stats;
    atomic64_t dropped_events;
};
static struct rng_ctx r;

static void enqueue_event(u32 source)
{
    struct rng_event ev;
    unsigned long flags;
    bool added = false;

    ev.time_ns = cpu_to_le64(ktime_get_ns());
    ev.source = cpu_to_le32(source);

    spin_lock_irqsave(&r.qlock, flags);
    ev.sequence = cpu_to_le32(++r.sequence);
    if (r.count < QSIZE) {
        r.queue[r.head] = ev;
        r.head = (r.head + 1) % QSIZE;
        r.count++;
        added = true;
    } else {
        atomic64_inc(&r.dropped_events);
    }
    spin_unlock_irqrestore(&r.qlock, flags);

    if (added)
        schedule_work(&r.work);
}

static bool dequeue_event(struct rng_event *ev)
{
    unsigned long flags;
    bool found = false;

    spin_lock_irqsave(&r.qlock, flags);
    if (r.count) {
        *ev = r.queue[r.tail];
        r.tail = (r.tail + 1) % QSIZE;
        r.count--;
        found = true;
    }
    spin_unlock_irqrestore(&r.qlock, flags);
    return found;
}

/* state_lock must be held */
static int sha256(const u8 *in, unsigned int len, u8 out[POOL_SIZE])
{
    return crypto_shash_digest(r.desc, in, len, out);
}

/* state_lock must be held */
static int mix_event(const struct rng_event *ev)
{
    u8 input[POOL_SIZE + sizeof(*ev)];
    u8 next[POOL_SIZE];
    u32 source = le32_to_cpu(ev->source);
    int ret;

    memcpy(input, r.pool, POOL_SIZE);
    memcpy(input + POOL_SIZE, ev, sizeof(*ev));
    ret = sha256(input, sizeof(input), next);
    if (ret)
        return ret;

    memcpy(r.pool, next, POOL_SIZE);
    if (source == SRC_KEYBOARD)
        r.stats.keyboard_events++;
    else if (source == SRC_NETWORK)
        r.stats.network_packets++;
    r.stats.mixed_input_bytes += sizeof(*ev);
    r.stats.pool_updates++;

    atomic_set(&r.ready, 1);
    wake_up_interruptible(&r.read_wq);
    return 0;
}

static void mix_worker(struct work_struct *work)
{
    struct rng_event ev;

    while (dequeue_event(&ev)) {
        mutex_lock(&r.state_lock);
        if (mix_event(&ev))
            pr_err("rngdriver: SHA-256 failed\n");
        mutex_unlock(&r.state_lock);
    }
}

static const struct input_device_id input_ids[] = {
    {
        .flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_KEYBIT,
        .evbit = { [BIT_WORD(EV_KEY)] = BIT_MASK(EV_KEY) },
        .keybit = { [BIT_WORD(KEY_A)] = BIT_MASK(KEY_A) },
    },
    { }
};
MODULE_DEVICE_TABLE(input, input_ids);

static void input_event_cb(struct input_handle *handle,
                           unsigned int type, unsigned int code, int value)
{
    if (type == EV_KEY && value == 1)
        enqueue_event(SRC_KEYBOARD);
}

static int input_connect(struct input_handler *handler, struct input_dev *dev,
                         const struct input_device_id *id)
{
    struct input_handle *handle;
    int ret;

    handle = kzalloc(sizeof(*handle), GFP_KERNEL);
    if (!handle)
        return -ENOMEM;
    handle->dev = dev;
    handle->handler = handler;
    handle->name = "rngdriver-input";

    ret = input_register_handle(handle);
    if (ret)
        goto fail;
    ret = input_open_device(handle);
    if (ret) {
        input_unregister_handle(handle);
        goto fail;
    }
    pr_info("rngdriver: attached keyboard input %s\n", dev->name);
    return 0;
fail:
    kfree(handle);
    return ret;
}

static void input_disconnect(struct input_handle *handle)
{
    input_close_device(handle);
    input_unregister_handle(handle);
    kfree(handle);
}

static struct input_handler rng_input_handler = {
    .event = input_event_cb,
    .connect = input_connect,
    .disconnect = input_disconnect,
    .name = "rngdriver",
    .id_table = input_ids,
};

static unsigned int net_hook(void *priv, struct sk_buff *skb,
                             const struct nf_hook_state *state)
{
    if (skb)
        enqueue_event(SRC_NETWORK);
    return NF_ACCEPT;
}

static struct nf_hook_ops nf_ops = {
    .hook = net_hook,
    .pf = NFPROTO_IPV4,
    .hooknum = NF_INET_LOCAL_IN,
    .priority = NF_IP_PRI_FIRST,
};

/* state_lock held; one read returns one SHA-256 output block, then rekeys */
static int generate_block(u8 out[POOL_SIZE])
{
    u8 in[POOL_SIZE + 8 + 3];
    u8 rekey[POOL_SIZE + 8 + 5 + POOL_SIZE];
    u8 next[POOL_SIZE];
    __le64 counter = cpu_to_le64(++r.output_counter);
    int ret;

    memcpy(in, r.pool, POOL_SIZE);
    memcpy(in + POOL_SIZE, &counter, 8);
    memcpy(in + POOL_SIZE + 8, "OUT", 3);
    ret = sha256(in, sizeof(in), out);
    if (ret)
        return ret;

    memcpy(rekey, r.pool, POOL_SIZE);
    memcpy(rekey + POOL_SIZE, &counter, 8);
    memcpy(rekey + POOL_SIZE + 8, "REKEY", 5);
    memcpy(rekey + POOL_SIZE + 13, out, POOL_SIZE);
    ret = sha256(rekey, sizeof(rekey), next);
    if (ret)
        return ret;

    memcpy(r.pool, next, POOL_SIZE);
    atomic_set(&r.ready, 0);
    return 0;
}

static ssize_t rng_read(struct file *file, char __user *buf,
                        size_t count, loff_t *ppos)
{
    u8 out[POOL_SIZE];
    size_t n;
    int ret;

    if (!count)
        return 0;

    for (;;) {
        if (file->f_flags & O_NONBLOCK) {
            if (!atomic_read(&r.ready))
                return -EAGAIN;
        } else {
            ret = wait_event_interruptible(r.read_wq, atomic_read(&r.ready));
            if (ret)
                return ret;
        }

        mutex_lock(&r.state_lock);
        if (!atomic_read(&r.ready)) {
            mutex_unlock(&r.state_lock);
            continue;
        }
        ret = generate_block(out);
        mutex_unlock(&r.state_lock);
        if (ret)
            return ret;

        n = min_t(size_t, count, POOL_SIZE);
        if (copy_to_user(buf, out, n))
            return -EFAULT;
        return n;
    }
}

static long rng_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct rngdriver_stats st;
    unsigned long flags;

    switch (cmd) {
    case RNG_IOC_GET_STATS:
        mutex_lock(&r.state_lock);
        st = r.stats;
        st.dropped_events = atomic64_read(&r.dropped_events);
        mutex_unlock(&r.state_lock);
        return copy_to_user((void __user *)arg, &st, sizeof(st)) ? -EFAULT : 0;
    case RNG_IOC_RESET:
        flush_work(&r.work);
        mutex_lock(&r.state_lock);
        memset(r.pool, 0, sizeof(r.pool));
        memset(&r.stats, 0, sizeof(r.stats));
        atomic64_set(&r.dropped_events, 0);
        r.output_counter = 0;
        atomic_set(&r.ready, 0);
        spin_lock_irqsave(&r.qlock, flags);
        r.head = r.tail = r.count = 0;
        spin_unlock_irqrestore(&r.qlock, flags);
        mutex_unlock(&r.state_lock);
        return 0;
    default:
        return -ENOTTY;
    }
}

static __poll_t rng_poll(struct file *file, poll_table *wait)
{
    poll_wait(file, &r.read_wq, wait);
    return atomic_read(&r.ready) ? EPOLLIN | EPOLLRDNORM : 0;
}

static const struct file_operations rng_fops = {
    .owner = THIS_MODULE,
    .read = rng_read,
    .unlocked_ioctl = rng_ioctl,
    .poll = rng_poll,
    .llseek = noop_llseek,
};

static struct miscdevice rng_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "rngdriver",
    .fops = &rng_fops,
    .mode = 0600,
};

static int __init rng_init(void)
{
    int ret;

    memset(&r, 0, sizeof(r));
    mutex_init(&r.state_lock);
    spin_lock_init(&r.qlock);
    init_waitqueue_head(&r.read_wq);
    atomic_set(&r.ready, 0);
    atomic64_set(&r.dropped_events, 0);
    INIT_WORK(&r.work, mix_worker);

    r.tfm = crypto_alloc_shash("sha256", 0, 0);
    if (IS_ERR(r.tfm))
        return PTR_ERR(r.tfm);

    r.desc = kzalloc(sizeof(*r.desc) + crypto_shash_descsize(r.tfm),
                     GFP_KERNEL);
    if (!r.desc) {
        ret = -ENOMEM;
        goto err_tfm;
    }
    r.desc->tfm = r.tfm;

    ret = misc_register(&rng_misc);
    if (ret)
        goto err_desc;

    ret = nf_register_net_hook(&init_net, &nf_ops);
    if (ret)
        goto err_misc;

    ret = input_register_handler(&rng_input_handler);
    if (ret)
        goto err_net;

    pr_info("rngdriver: loaded, SHA-256 mixer initialized\n");
    return 0;

err_net:
    nf_unregister_net_hook(&init_net, &nf_ops);
err_misc:
    misc_deregister(&rng_misc);
err_desc:
    kfree(r.desc);
err_tfm:
    crypto_free_shash(r.tfm);
    return ret;
}

static void __exit rng_exit(void)
{
    input_unregister_handler(&rng_input_handler);
    nf_unregister_net_hook(&init_net, &nf_ops);
    cancel_work_sync(&r.work);
    wake_up_interruptible_all(&r.read_wq);
    misc_deregister(&rng_misc);
    kfree(r.desc);
    crypto_free_shash(r.tfm);
    pr_info("rngdriver: unloaded\n");
}

module_init(rng_init);
module_exit(rng_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Fakhretdinov Ravil");
MODULE_DESCRIPTION("Random driver using sha-256");
