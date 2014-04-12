/*
 * Copyright (c) 2013 JST DEOS R&D Center
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
/*
 * dxfeed.c:
 */

#define true            1
#define false           0
#define mb()            asm volatile ("mfence" ::: "memory")
#define my_cpuid()      1
#define print_info(...)
#define print_detail1(...)

#include "veidtdefs.h"
#include "exception.h"
#include "qemu-lock.h"
#include "dsm_main.h"
#include <string.h>
#include <pthread.h>



struct dxf_chunk {
    struct dxf_chunk *next;
    u32 len;
    u8 buf[VEIDT_DXFEED_MAXDATASIZE];
};
static struct dxf_chunk _dxf_chunks[32];

struct dxf_channel {
    spinlock_t lock;
    struct dxf_chunk *head;
    struct dxf_chunk *tail;
};
static struct dxf_channel dxf_ch[VEIDT_NUM_MAX_GUEST];
static struct dxf_channel dxf_free = {head:NULL, tail:NULL};

static pthread_mutex_t mutex;
static pthread_cond_t cond;

static bool_t
dxf_channel_is_empty(struct dxf_channel *chp)
{
    return (chp->head == NULL) ? true : false;
}

static void
dxf_channel_insert_tail(struct dxf_channel *chp, struct dxf_chunk *dp)
{
    dp->next = NULL;
    if (chp->head == NULL)
        chp->head = dp;
    else
        chp->tail->next = dp;
    chp->tail = dp;
    mb();
    return;
}

static struct dxf_chunk *
dxf_channel_remove_head(struct dxf_channel *chp)
{
    struct dxf_chunk *dp;

    dp = chp->head;
    chp->head = dp->next;
    if (chp->head == NULL)
        chp->tail = NULL;
    mb();
    dp->next = NULL;
    return dp;
}


static struct dxf_chunk *
dxf_get_chunk(void)
{
    bool_t underrun = false;
    struct dxf_chunk *dp;

retry:
    spin_lock(&dxf_free.lock);
    if (dxf_channel_is_empty(&dxf_free)) {
        /*
         * currently there is no free dxf_chunk available.  we must
         * wait (spin) until at least one dxf_chunk will be freed by
         * any other entity.
         */
        spin_unlock(&dxf_free.lock);
        if (! underrun) {
            print_info("dxfeed no free data chunk, spinning.\n");
            underrun = true;
        }
        goto retry;
    }
    if (underrun) {
        print_info("dxfeed free data chunk found, go ahead.\n");
    }

    /* get a data chunk from free list and return it. */
    dp = dxf_channel_remove_head(&dxf_free);
    spin_unlock(&dxf_free.lock);

    dp->len = 0;
    return dp;
}

static void
dxf_put_chunk(struct dxf_chunk *dp)
{
    if (dp != NULL) {
        spin_lock(&dxf_free.lock);
        dxf_channel_insert_tail(&dxf_free, dp);
        spin_unlock(&dxf_free.lock);
    }
    return;
}

/*
 * dxfeed_init():
 *        initialization function of DXFEED module.
 */
void
dxfeed_init(void)
{
    const int nr_dxf_chunks = sizeof(_dxf_chunks) / sizeof(_dxf_chunks[0]);
    const int nr_dxf_chs = sizeof(dxf_ch) / sizeof(dxf_ch[0]);
    int idx;

    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);

    /* first we must initialize all dxf_channels and free list. */
    for (idx = 0; idx < nr_dxf_chs; idx++) {
        //spinlock_init(&dxf_ch[idx].lock);
        dxf_ch[idx].head = NULL;
        dxf_ch[idx].tail = NULL;
    }
    //spinlock_init(&dxf_free.lock);
    dxf_free.head = NULL;
    dxf_free.tail = NULL;

    /* then we insert all dxf_chunks into free list. */
    for (idx = 0; idx < nr_dxf_chunks; idx++) {
        dxf_put_chunk(&_dxf_chunks[idx]);
    }
    return;
}

/*
 * dxfeed_exception():
 *        interrupt handler of VEIDT_VECTOR_DXFEED.  directly called from
 *        user-land process running on the User Guest OS via. appropriate
 *        software interrupt instruction.
 */
void
dxfeed_exception(regs_t *regs, void *arg)
{
    struct dxf_chunk *dp;

    if (dxf_free.head == NULL && dxf_free.tail == NULL) {
        return;
    }

    /*
     * first of all, we must check so that the caller (user-land process)
     * is actually our friend by examining magic number signature passed
     * by EAX.
     */
    if (regs->eax != VEIDT_DXFEED_MAGIC) {
        print_info("untrusted dxfeed call on cpu%d, "
                        "magic 0x%08x\n", regs->eax, my_cpuid());
        regs->eax = -VEIDTCALL_EINVALID;
        return;
    }
    /*
     * also we must check if the caller supplied data buffer size specified
     * by ECX does not exceed our internal limitation or not.
     */
    if (regs->ecx > sizeof(dp->buf)) {
        regs->eax = -VEIDTCALL_EINVALID;
        return;
    }

    /*
     * allocate a dxf_chunk buffer and copy user-land supplied data into
     * it.  note that 'get_dxf_chunk()' never fails so we need not to do
     * NULL check on dxf_chunk pointer.  data buffer pointer and size are
     * passed by EBX and ECX respectively.
     */
    dp = dxf_get_chunk();
    dp->len = regs->ecx;
    memcpy(dp->buf, (void *)regs->ebx, regs->edx);
    if (regs->edx < dp->len) {
        memcpy(dp->buf + regs->edx, arg, dp->len - regs->edx);
    }

    /*
     * now we can insert dxf_chunk data into our dxf_channel.  observer
     * entity may retreave the chunk in the future.
     */
    spin_lock(&dxf_ch[my_cpuid()].lock);
    dxf_channel_insert_tail(&dxf_ch[my_cpuid()], dp);
    spin_unlock(&dxf_ch[my_cpuid()].lock);

    /*
     * and then we would like to notify to observer processor that new
     * DXFEED data chunk has been added to my channel.  note that observer
     * (watcher) cpuid is always 0.
     */
//    vmsignal_send(watcher_cpuid, VMSIGNAL_DXFEED_NOTIFY);
    pthread_mutex_lock(&mutex);
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);
    regs->eax = 0;

    return;
}

/*
 * veidtcall_util_read_dxfeed():
 *        hypervisor call (veidtcall) to read existing data chunk from DXFEED
 *        channel.  this call will return number of bytes copied into caller
 *        supplied buffer, or zero if there is no valid data chunk exist.
 *
 *        note that if caller does not supply buffer, i.e. passed NULL as the
 *        buffer pointer, this call returns number of bytes possible to be read
 *        without removing data chunks from the channel.  this means that it can
 *        be used to poll readability of the channel.
 */
int
veidtcall_util_read_dxfeed(regs_t *regs, cpuid_t cpuid, char *buf, size_t len)
{
    struct dxf_chunk *dp;
    struct dxf_channel *chp = &dxf_ch[cpuid];
    int retval;

    print_detail1("%p: veidtcall_util_read_dxfeed(cpuid = %d)\n",
                    (void *)regs->eip, cpuid);

    if (len > sizeof(dp->buf))
        return -VEIDTCALL_EINVALID;

    if (dxf_free.head == NULL && dxf_free.tail == NULL) {
        return 0;
    }
    pthread_mutex_lock(&mutex);
    while (dxf_channel_is_empty(chp)) {
        pthread_cond_wait(&cond, &mutex);
    }
    pthread_mutex_unlock(&mutex);

    dp = chp->head;
    retval = dp->len;

    if (buf == NULL) {
        /* poll action, return size of current data chunk. */
        spin_unlock(&chp->lock);
        return retval;
    }

    /*
     * check if the supplied buffer is large enough to copy current data
     * chunk into it.
     */
    if (len < retval) {
        /* we treat this case as EINVALID. */
        spin_unlock(&chp->lock);
        return -VEIDTCALL_EINVALID;
    }

    /*
     * now we can remove current data chunk from channel, copy contents of
     * it into caller supplied buffer, then release (put) the data chunk
     * into free list.  number of bytes copied must be returned from the
     * function.
     */
    dp = dxf_channel_remove_head(chp);
    spin_unlock(&chp->lock);

    memcpy(buf, dp->buf, retval);
    dxf_put_chunk(dp);
    return retval;
}
