#include "scp.h"
#include "hashmap.h"
#include "queue.h"
#include "common.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "heap.h"
#include "lttit_config.h"
#include "schedule.h"

static void scp_send_window_probe(struct scp_stream *ss);
static void scp_retransmit(struct scp_stream *ss);
static int scp_output(struct scp_stream *ss, int flags);
void scp_output_data(struct scp_stream *ss, struct scp_buf *sb,
                     uint32_t offset, uint32_t frag_len);

static struct rb_root scp_timer_tree;
static struct hashmap scp_stream_map;
static struct list_node scp_stream_queue;

#ifdef SCP_RUN_DEBUG
    #define SCP_PRINT(...) printf(__VA_ARGS__)
#else
    #define SCP_PRINT(...) ((void)0)
#endif


uint32_t scp_now_time(void)
{
    return rtos_now_time();
}

static void scp_debug_hex(const char *tag, const void *buf, size_t len)
{
    const uint8_t *p = buf;

    printf("---- %s (%zu bytes) ----\n", tag, len);

    for (size_t i = 0; i < len; i++) {
        printf("%02X ", p[i]);
        if ((i + 1) % 16 == 0)
            printf("\n");
    }
    if (len % 16 != 0)
        printf("\n");

    printf("-----------------------------\n");
}

static int a = 0;
static void scp_debug_dump_tx(const char *reason,
                              const void *buf, size_t len)
{
#if SCP_DEBUG
    printf("\n[SCP TX] %s, a:%d\n", reason, a++);
    scp_debug_hex("TX Packet", buf, len);
#endif
}

static int b = 0;
static void scp_debug_dump_rx(const void *buf, size_t len)
{
#if SCP_DEBUG
    printf("\n[SCP RX] %d\n", b++);
    scp_debug_hex("RX Packet", buf, len);
#endif
}


static void scp_dump_hdr(struct scp_stream *ss,
                         const char *dir,
                         const struct scp_hdr *h)
{
#if SCP_DUMP
    uint32_t seq = ntohl(h->seq);
    uint32_t ack = ntohl(h->ack);
    uint32_t wnd = ntohs(h->wnd);
    uint32_t len = ntohs(h->len);
    uint32_t sack = ntohl(h->sack);

    int sndq = 0, rcvq = 0;

    struct list_node *n;
    for (n = ss->snd_q.next; n != &ss->snd_q; n = n->next) sndq++;

    struct rb_node *rn;
    for (rn = rb_first(&ss->rcv_buf_q); rn != NULL; rn = rb_next(rn)) rcvq++;

    uint32_t flight = ss->snd_nxt - ss->snd_una;

    printf("{\"t\":%u,"
           "\"dir\":\"%s\","
           "\"seq\":%u,"
           "\"ack\":%u,"
           "\"sack\":%u,"
           "\"len\":%u,"
           "\"wnd\":%u,"
           "\"flags\":%u,"

           "\"snd_una\":%u,"
           "\"snd_seq_q\":%u,"
           "\"snd_nxt\":%u,"
           "\"rcv_nxt\":%u,"

           "\"snd_wnd\":%u,"
           "\"rcv_wnd\":%u,"

           "\"snd_q\":%d,"
           "\"rcv_q\":%d,"

           "\"cwnd\":%u,"
           "\"ssthresh\":%u,"
           "\"flight\":%u,"

           "\"srtt\":%u,"
           "\"rto\":%u,"
           "\"dup_acks\":%u,"
           "\"sb_cc\":%u,"
           "\"packet_bytes\":%u,"
           "\"packet_count\":%u"
           "}\n",

           scp_now_time(),
           dir,
           seq,
           ack,
           sack,
           len,
           wnd,
           h->flags,

           ss->snd_una,
           ss->snd_seq_q,
           ss->snd_nxt,
           ss->rcv_nxt,

           ss->snd_wnd,
           ss->rcv_wnd,

           sndq,
           rcvq,

           ss->cwnd,
           ss->ssthresh,
           flight,

           ss->srtt,
           ss->rto,
           ss->dup_acks,
           ss->sb_cc,
           ss->packet_bytes,
           ss->packet_count
    );
#endif
}

//write by yourself
void *scp_malloc(size_t want_size)
{
    return heap_malloc(want_size);
}

void scp_free(void *ptr)
{
    heap_free(ptr);
}

void scp_timer_init(void)
{
    rb_root_init(&scp_timer_tree);
}

void scp_timer_node_init(struct scp_timer *t)
{
    if (!t) return;

    rb_node_init(&t->node);   
    t->expire = 0;           
    t->timeout = 0;        
    t->cb = NULL;           
    t->arg = NULL;         
    t->active = 0;        
}

static inline void scp_timer_add(struct scp_timer *t)
{
    t->expire = scp_now_time() + t->timeout;
    t->node.value = t->expire;
    rb_insert_node(&scp_timer_tree, &t->node);
    t->active = 1;
}

/*
 * We must make sure the node in tree.
 * So we use t->active flags.
 */
static inline void scp_timer_remove(struct scp_timer *t)
{
    if (!t->active) return;

    rb_remove_node(&scp_timer_tree, &t->node);
    t->active = 0;
}

scp_timer_handle_t scp_timer_create(struct scp_timer *t,
                                    scp_timer_cb_t cb,
                                    void *arg,
                                    uint32_t timeout)
{
    if (!t) return NULL;

    if (t->active) scp_timer_remove(t); 

    rb_node_init(&t->node);
    t->timeout = timeout;
    t->cb = cb;
    t->arg = arg;
    t->active = 0;

    scp_timer_add(t);
    return t;
}

/*
 *malloc or free is not good,We just init it in struct stream.
*/
void scp_timer_delete(scp_timer_handle_t h)
{
    if (!h) return;
    scp_timer_remove(h);
}

/*
 * We use uint32_t, if clock update, that is bad.
 * So we use SEG_GT.
 */
void scp_timer_process(void)
{
    struct rb_node *n;

    for (;;) {
        n = scp_timer_tree.first_node;
        if (!n) break;
    
        struct scp_timer *t =
            container_of(n, struct scp_timer, node);

        if (SEQ_GT(t->expire, scp_now_time())) break;

        rb_remove_node(&scp_timer_tree, &t->node);
        t->active = 0;

        t->cb(t->arg);  
    }
}

/*
 *Update cwnd
 *If retrans too many, we close.
 *The timer is period.
*/
static void scp_timer_retrans_cb(void *arg)
{
    struct scp_stream *ss = arg;
    if (!ss || ss->state == SCP_CLOSED) return;

    uint32_t flight = ss->snd_nxt - ss->snd_una;
    uint32_t mss = MTU - sizeof(struct scp_hdr);
    if (mss == 0) mss = 1;

    uint32_t half = flight / 2;
    uint32_t min_thresh = 2 * mss;
    ss->ssthresh = (half > min_thresh) ? half : min_thresh;

    ss->cwnd = mss;
    ss->dup_acks = 0;
    ss->fr_active = 0;

    scp_retransmit(ss);
    ss->timeout_count++;

    if (ss->timeout_count > RETRANS_COUNT_MAX) {
        SCP_PRINT("RETRANS FAIL!\r\n");

        ss->state = SCP_CLOSED;
        scp_stream_free(ss);
        return;
    }

    uint32_t next_rto = ss->rto * 2; 
    if (next_rto > SCP_RTO_MAX) 
        next_rto = SCP_RTO_MAX; 
    ss->rto = next_rto;

    scp_timer_create(&ss->t_retrans,
                scp_timer_retrans_cb,
                ss,                      
                ss->rto 
                );

}

/*
 *In zero windows, we perisit remote.
 *If don't connect in a time, it is keeplive timer.
*/
static void scp_timer_persist_cb(void *arg)
{
    struct scp_stream *ss = arg;
    if (!ss || ss->state == SCP_CLOSED)
        return;

    uint32_t now = scp_now_time();
    int need_probe = 0;

    uint32_t idle = now - ss->last_active;

    if (idle >= IDLE_TIMEOUT * (ss->idle_failures + 1)) {
        need_probe = 1;
        ss->idle_failures++;

        if (ss->idle_failures > MAX_IDLE_FAIL) {
            ss->state = SCP_CLOSED;
            SCP_PRINT("IDLE FAIL!\r\n");
            scp_stream_free(ss);
            return;
        }
    }

    if (!list_empty(&ss->snd_q) && ss->snd_wnd <= MTU) {
        need_probe = 1;
    }

    if (need_probe) {
        scp_send_window_probe(ss);
    }

    scp_timer_create(&ss->t_persist,
        scp_timer_persist_cb,
        ss,
        PERSIST_INTERVAL
    );
}

/*
 *For safe, iss must random.
*/
static uint32_t random32(void)
{
    uint32_t r = ((uint32_t)rand() << 16) ^ (uint32_t)rand();
    return r ? r : 1;  
}

/*
 *If our data is non orider, must rcv_buf_q first and rcv_data_q is not neibor
 *So (rcv_data_q last, rcv_buf_q first) is we need.
 */
static inline uint32_t scp_calc_sack(struct scp_stream *ss)
{
    if (ss->rcv_buf_q.rb_node == NULL)
        return ss->rcv_nxt;

    struct rb_node *n = rb_first(&ss->rcv_buf_q);
    struct scp_buf *first = container_of(n, struct scp_buf, rb);
    return first->seq;
}

/*
 * Just alloc, you can replace memory alloc.
*/
static struct scp_buf *scp_buf_alloc(uint32_t len)
{
    struct scp_buf *sb = scp_malloc(len);
    if (!sb) return NULL;

    memset(sb, 0, len);
    sb->data = (uint8_t *)sb + sizeof(struct scp_buf);

    return sb;
}

static void scp_buf_free(struct scp_buf *b)
{
    if (!b) return;
    scp_free(b);
}

/*
* Init it, if have many stream, we link it.
*/
struct scp_stream *scp_stream_alloc(struct scp_transport_class *st_class, int src_fd, int dst_fd)
{
    struct scp_stream *ss = scp_malloc(sizeof(struct scp_stream));
    if (!ss) {
        return NULL;
    }

    *ss = (struct scp_stream) {
            .src_fd   = src_fd,
            .dst_fd   = dst_fd,
            .rto = SCP_RTO_MIN,
            .rto_recovery = 0,
            .sb_hiwat = SCP_RECV_LIMIT,
            .rcv_wnd  = RECV_WIN_INIT,
            .snd_wnd  = SEND_WIN_INIT,
            .st_class = st_class,
            .state = SCP_CLOSED,
            .iss = 0,
            .rcv_nxt = 0,
            .snd_seq_q = 0,
            .snd_nxt = 0,
            .snd_una = 0,
            .cwnd = MTU, 
            .ssthresh = SSTHRESH_INIT,
            .dup_acks = 0,
            .last_gap_rexmit_ack = 0,
            .fr_active = 0,
            .packet_bytes = 0,
            .packet_count = 0,
    };

    scp_timer_node_init(&ss->t_retrans);
    scp_timer_node_init(&ss->t_persist);
    scp_timer_node_init(&ss->t_hs);
    scp_timer_node_init(&ss->t_fin);

    list_node_init(&ss->node);
    list_node_init(&ss->snd_q);
    rb_root_init(&ss->rcv_buf_q);
    list_node_init(&ss->rcv_data_q);

    queue_enqueue(&scp_stream_queue, &ss->node);
    hashmap_put(&scp_stream_map, (void *)(uintptr_t)(ss->src_fd), ss);

    return ss;
}

int scp_stream_free(struct scp_stream *ss)
{
    if (!ss) return -1;

    scp_timer_remove(&ss->t_retrans);
    scp_timer_remove(&ss->t_persist);
    scp_timer_remove(&ss->t_hs);
    scp_timer_remove(&ss->t_fin);

    list_remove(&ss->node);
    hashmap_remove(&scp_stream_map, (void *)(uintptr_t)(ss->src_fd));

    scp_free(ss);
    return 0;
}

/*
 * A fd is a stream, we need to record them.
 * list init and hashmap, list link stream.
 * hashmap put fd.
*/
int scp_init(size_t max_streams)
{
    list_node_init(&scp_stream_queue);
    hashmap_init(&scp_stream_map, max_streams, HASHMAP_KEY_INT);
    return 0;
}

/*
 * RTT Estimation (RFC 6298 style, Karn’s Algorithm)
 *
 * We need to konw the time from send to acked,
 * and estimate average time and tolrent timeout.
 *
 * All intermediate calculations use signed 32‑bit integers to avoid
 * wrap‑around when (abs_delta - rttvar) becomes negative.
 */
static void scp_update_rtt(struct scp_stream *s, uint32_t rtt_sample)
{
    if (s->srtt == 0) {
        s->srtt   = rtt_sample;
        s->rttvar = rtt_sample >> 1;
        s->rto    = s->srtt + (s->rttvar << 2);
        if (s->rto < SCP_RTO_MIN) s->rto = SCP_RTO_MIN;
        if (s->rto > SCP_RTO_MAX) s->rto = SCP_RTO_MAX;
        return;
    }

    int32_t delta = (int32_t)rtt_sample - (int32_t)s->srtt;

    s->srtt = s->srtt + (delta >> 3);  // srtt += delta/8

    int32_t abs_delta = delta >= 0 ? delta : -delta;
    int32_t rttvar_i  = (int32_t)s->rttvar;
    int32_t diff      = abs_delta - rttvar_i;

    rttvar_i = rttvar_i + (diff >> 2); // rttvar += (|delta|-rttvar)/4
    if (rttvar_i < 1) rttvar_i = 1;   

    s->rttvar = (uint32_t)rttvar_i;

    uint32_t rto = s->srtt + (s->rttvar << 2);
    if (rto < SCP_RTO_MIN) rto = SCP_RTO_MIN;
    if (rto > SCP_RTO_MAX) rto = SCP_RTO_MAX;
    s->rto = rto;
}


/*
* sb_hiwat is limit, and can't overout.
*/
static inline void scp_update_rcv_wnd(struct scp_stream *s)
{
    if (s->sb_cc >= s->sb_hiwat)
        s->rcv_wnd = 0;
    else
        s->rcv_wnd = s->sb_hiwat - s->sb_cc;
}

/*
 * When no respond, we need to get information from remote.
 * So ping it, get wnd or other.
 */
static void scp_send_window_probe(struct scp_stream *ss)
{
    struct scp_hdr hdr = {
            .seq   = htonl(ss->snd_nxt),
            .ack   = htonl(ss->rcv_nxt),
            .sack  = htonl(scp_calc_sack(ss)),
            .wnd   = htons((uint16_t)ss->rcv_wnd),
            .len   = 0,
            .cksum = 0,
            .flags = SCP_FLAG_PING,
            .fd    = ss->dst_fd,
    };

    hdr.cksum = in_checksum(&hdr, sizeof(hdr));
    scp_dump_hdr(ss, "WINDOWS_PING", &hdr);
    ss->st_class->send(ss->st_class->user, &hdr, sizeof(hdr));
}

/*
 * SACK-based selective retransmit.
 * Resend all bytes in [gap_start, gap_end) from snd_q.
 * rely on cwnd path for congestion control.
 */
void scp_retransmit_gap(struct scp_stream *ss,
                        uint32_t gap_start,
                        uint32_t gap_end)
{
    struct list_node *n;

    uint32_t mss = MTU - sizeof(struct scp_hdr);
    if (mss == 0)
        mss = 1;

    for (n = ss->snd_q.next; n != &ss->snd_q; n = n->next) {

        struct scp_buf *sb = container_of(n, struct scp_buf, list);
        uint32_t total = sb->len - sizeof(struct scp_hdr);
        if (total == 0)
            continue;

        uint32_t seg_start = sb->seq;
        uint32_t seg_end   = sb->seq + total;

        // skip segments fully before gap
        if (SEQ_LEQ(seg_end, gap_start))
            continue;

        // stop when segment starts after gap
        if (SEQ_GEQ(seg_start, gap_end))
            return;

        // segment overlaps gap
        uint32_t start = 0;
        if (SEQ_GT(gap_start, seg_start))
            start = gap_start - seg_start;

        int sent_frags = 0;

        while (start < total && sent_frags < RETRANS_GAP_MAX) { 
            uint32_t remain = total - start;
            uint32_t frag   = (remain > mss) ? mss : remain;

            uint32_t frag_seq = seg_start + start;
            uint32_t frag_end = frag_seq + frag;

            // stop if beyond gap
            if (SEQ_GEQ(frag_seq, gap_end))
                return;
            if (SEQ_GT(frag_end, gap_end))
                frag = gap_end - frag_seq;

            if (frag == 0)
                return;

            scp_output_data(ss, sb, start, frag); 

            start += frag;
            sent_frags++;
        }
    }
}

/*
 * RTO-based retransmit (safe version).
 * Only retransmit the first unacked segment (snd_una).
 * This avoids queue explosion and RTO storms.
 */
static void scp_retransmit(struct scp_stream *ss)
{
    uint32_t mss = MTU - sizeof(struct scp_hdr);
    if (mss == 0) mss = 1;

    uint32_t max_frags = 1 << ss->rto_recovery;

    if (max_frags > RETRANS_RECO_MAX)
        max_frags = RETRANS_RECO_MAX;

    uint32_t sent = 0;

    struct list_node *n;
    for (n = ss->snd_q.next; n != &ss->snd_q; n = n->next) {

        if (sent >= max_frags)
            break;

        struct scp_buf *sb = container_of(n, struct scp_buf, list);
        uint32_t total = sb->len - sizeof(struct scp_hdr);
        if (total == 0)
            continue;

        uint32_t seg_start = sb->seq;
        uint32_t seg_end   = sb->seq + total;

        if (SEQ_LEQ(seg_end, ss->snd_una))
            continue;

        uint32_t start = 0;
        if (SEQ_LT(seg_start, ss->snd_una)) {
            uint32_t trim = ss->snd_una - seg_start;
            if (trim >= total)
                continue;
            start = trim;
        }

        uint32_t remain = total - start;
        uint32_t frag   = (remain > mss) ? mss : remain;

        scp_output_data(ss, sb, start, frag);
        sent++;
    }
}

/*
* Just for swap iss.
*/
static void scp_output_connect(struct scp_stream *ss)
{
    struct scp_hdr hdr = {
        .seq = htonl(ss->iss),
        .ack = 0,
        .sack = htonl(scp_calc_sack(ss)),
        .wnd = htons((uint16_t)ss->rcv_wnd),
        .len = 0,
        .flags = SCP_FLAG_CONNECT,
        .fd = ss->dst_fd,
    };
    hdr.cksum = in_checksum(&hdr, sizeof(hdr));
    scp_debug_dump_tx("CONNECT", &hdr, sizeof(hdr));
    scp_dump_hdr(ss, "CONNECT", &hdr);
    ss->st_class->send(ss->st_class->user, &hdr, sizeof(hdr));
}

/*
* Swap iss.
*/
static void scp_output_connect_ack(struct scp_stream *ss)
{
    struct scp_hdr hdr = {
        .seq = htonl(ss->snd_nxt),
        .ack = htonl(ss->rcv_nxt),
        .sack = htonl(scp_calc_sack(ss)),
        .wnd = htons((uint16_t)ss->rcv_wnd),
        .len = 0,
        .flags = SCP_FLAG_CONNECT_ACK,
        .fd = ss->dst_fd,
    };
    hdr.cksum = 0;
    hdr.cksum = in_checksum(&hdr, sizeof(hdr));
    scp_debug_dump_tx("CONNECT_ACK", &hdr, sizeof(hdr));
    scp_dump_hdr(ss, "CONNECT_ACK", &hdr);
    ss->st_class->send(ss->st_class->user, &hdr, sizeof(hdr));
}

static void scp_output_fin_first(struct scp_stream *ss)
{
    struct scp_hdr hdr;

    hdr.seq  = htonl(ss->snd_nxt);
    hdr.ack  = htonl(ss->rcv_nxt);
    hdr.sack = htonl(scp_calc_sack(ss));
    hdr.wnd  = htons((uint16_t)ss->rcv_wnd);
    hdr.len  = 0;
    hdr.flags = SCP_FLAG_FIN | SCP_FLAG_ACK;
    hdr.fd   = ss->dst_fd;
    hdr.cksum = 0;
    hdr.cksum = in_checksum(&hdr, sizeof(hdr));

    scp_debug_dump_tx("FIN_ACK", &hdr, sizeof(hdr));
    scp_dump_hdr(ss, "FIN_ACK", &hdr);
    ss->st_class->send(ss->st_class->user, &hdr, sizeof(hdr));

    ss->snd_nxt++;   
}

static void scp_output_fin_retrans(struct scp_stream *ss)
{
    struct scp_hdr hdr;

    hdr.seq  = htonl(ss->snd_nxt - 1);  
    hdr.ack  = htonl(ss->rcv_nxt);
    hdr.sack = htonl(scp_calc_sack(ss));
    hdr.wnd  = htons((uint16_t)ss->rcv_wnd);
    hdr.len  = 0;
    hdr.flags = SCP_FLAG_FIN | SCP_FLAG_ACK;
    hdr.fd   = ss->dst_fd;
    hdr.cksum = 0;
    hdr.cksum = in_checksum(&hdr, sizeof(hdr));

    scp_debug_dump_tx("FIN_RETRANS", &hdr, sizeof(hdr));
    scp_dump_hdr(ss, "FIN_RETRANS", &hdr);
    ss->st_class->send(ss->st_class->user, &hdr, sizeof(hdr));
}


/*
*For handshake timeout, we need to retrans it.
*/
static void scp_handle_handshake_timeout(void *arg)
{
    struct scp_stream *ss = arg;
    if (!ss) return;

    if (ss->state != SCP_SYN_SENT && ss->state != SCP_SYN_RECV) {
        scp_timer_delete(&ss->t_hs);  
        return;
    }

    if (ss->retry++ < 5) {

        if (ss->state == SCP_SYN_SENT)
            scp_output_connect(ss);
        else
            scp_output_connect_ack(ss);

        scp_timer_delete(&ss->t_hs);
        scp_timer_create(&ss->t_hs,
            scp_handle_handshake_timeout,
            ss,
            ss->rto << ss->retry
        );

        return;
    }

    ss->state = SCP_CLOSED;

    scp_timer_delete(&ss->t_hs);
}

static void scp_handle_fin_timeout(void *arg)
{
    struct scp_stream *ss = arg;
    if (!ss) return;

    if (ss->state != SCP_FIN_WAIT && ss->state != SCP_LAST_ACK) {
        scp_timer_delete(&ss->t_fin);
        return;
    }

    if (ss->retry++ < 5) {
        scp_output_fin_retrans(ss);

        scp_timer_delete(&ss->t_fin);
        scp_timer_create(&ss->t_fin,
            scp_handle_fin_timeout,
            ss,
            ss->rto << ss->retry
        );

        return;
    }

    ss->state = SCP_CLOSED;

    scp_timer_delete(&ss->t_fin);
    scp_stream_free(ss);
}

/* Trim or drop, this is a question.
 * A data segment may arrive in order, out of order, or overlapping.
 * We trim or drop anything already consumed and buffer future data.
 * When a segment fills the next gap, we deliver it and advance rcv_nxt.
 * Every update reflects the receiver’s ability to accept more data.
 */
static void scp_process_data(struct scp_stream *s, struct scp_buf *sb)
{
    struct scp_hdr *sh = (struct scp_hdr *)sb->data;
    uint32_t seq         = ntohl(sh->seq);
    uint32_t payload_len = ntohs(sh->len);

    // if no payload, drop
    if (payload_len == 0) {
        scp_buf_free(sb);
        return;
    }

    sb->seq = seq;
    uint32_t end = seq + payload_len;

    // if buffered segments fully behind rcv_nxt, drop them
    {
        struct rb_node *n = rb_first(&s->rcv_buf_q);
        while (n) {
            struct scp_buf *b = container_of(n, struct scp_buf, rb);
            struct rb_node *next = rb_next(n);

            uint32_t b_end = b->seq + (b->len - sizeof(struct scp_hdr));
            if (SEQ_LEQ(b_end, s->rcv_nxt)) {
                uint32_t plen = b->len - sizeof(struct scp_hdr);
                s->sb_cc = (s->sb_cc >= plen) ? (s->sb_cc - plen) : 0;

                rb_remove_node(&s->rcv_buf_q, &b->rb);
                scp_buf_free(b);
                n = next;
                continue;
            }
            break;
        }
    }

    // if segment fully before rcv_nxt, drop it
    if (SEQ_LEQ(end, s->rcv_nxt)) {
        scp_output(s, SCP_FLAG_ACK);
        scp_buf_free(sb);
        return;
    }

    // if segment overlaps left edge, trim to rcv_nxt
    if (SEQ_LT(seq, s->rcv_nxt)) {
        uint32_t trim = s->rcv_nxt - seq;
        seq         += trim;
        payload_len -= trim;

        uint8_t *payload = sb->data + sizeof(struct scp_hdr);
        memmove(payload, payload + trim, payload_len);

        sh->seq = htonl(seq);
        sh->len = htons((uint16_t)payload_len);

        sb->seq = seq;
        sb->len = sizeof(struct scp_hdr) + payload_len;

        end = seq + payload_len;
    }

    // if segment exactly matches rcv_nxt, deliver in-order
    if (SEQ_EQ(seq, s->rcv_nxt)) {
        s->rcv_nxt += payload_len;
        queue_enqueue(&s->rcv_data_q, &sb->list);
        s->sb_cc += payload_len;
        scp_update_rcv_wnd(s);

        // pull next in-order buffered segments
        for (;;) {
            struct rb_node *n = rb_first(&s->rcv_buf_q);
            if (!n)
                break;

            struct scp_buf *b = container_of(n, struct scp_buf, rb);
            uint32_t b_seq  = b->seq;
            uint32_t b_plen = b->len - sizeof(struct scp_hdr);

            // if next buffered is not exactly rcv_nxt, stop
            if (!SEQ_EQ(b_seq, s->rcv_nxt))
                break;

            rb_remove_node(&s->rcv_buf_q, &b->rb);
            queue_enqueue(&s->rcv_data_q, &b->list);
            s->rcv_nxt += b_plen;
            scp_update_rcv_wnd(s);
        }

        scp_output(s, SCP_FLAG_ACK);
        return;
    }

    // find insertion point for out-of-order segment
    struct rb_node *pos = rb_first_greater(&s->rcv_buf_q, seq);
    struct scp_buf *b;

    // if overlaps previous segment, trim left overlap
    if (pos) {
        struct rb_node *prev_n = rb_prev(pos);
        if (prev_n) {
            struct scp_buf *prev = container_of(prev_n, struct scp_buf, rb);
            uint32_t p_seq = prev->seq;
            uint32_t p_end = p_seq + (prev->len - sizeof(struct scp_hdr));

            // if fully covered by previous, drop
            if (SEQ_GT(p_end, seq)) {
                if (SEQ_GEQ(p_end, end)) {
                    scp_buf_free(sb);
                    return;
                }

                uint32_t trim = p_end - seq;
                seq         += trim;
                payload_len -= trim;

                uint8_t *payload = sb->data + sizeof(struct scp_hdr);
                memmove(payload, payload + trim, payload_len);

                sh->seq = htonl(seq);
                sh->len = htons((uint16_t)payload_len);

                sb->seq = seq;
                sb->len = sizeof(struct scp_hdr) + payload_len;

                end = seq + payload_len;
            }
        }
    } else {
        // if overlaps last segment, trim left overlap
        struct rb_node *last = rb_last(&s->rcv_buf_q);
        if (last) {
            struct scp_buf *prev = container_of(last, struct scp_buf, rb);
            uint32_t p_seq = prev->seq;
            uint32_t p_end = p_seq + (prev->len - sizeof(struct scp_hdr));

            if (SEQ_GT(p_end, seq)) {
                if (SEQ_GEQ(p_end, end)) {
                    scp_buf_free(sb);
                    return;
                }

                uint32_t trim = p_end - seq;
                seq         += trim;
                payload_len -= trim;

                uint8_t *payload = sb->data + sizeof(struct scp_hdr);
                memmove(payload, payload + trim, payload_len);

                sh->seq = htonl(seq);
                sh->len = htons((uint16_t)payload_len);

                sb->seq = seq;
                sb->len = sizeof(struct scp_hdr) + payload_len;

                end = seq + payload_len;
            }
        }
    }

    // if overlaps next segment, trim right overlap
    if (pos) {
        b = container_of(pos, struct scp_buf, rb);
        uint32_t b_seq = b->seq;
        uint32_t b_end = b_seq + (b->len - sizeof(struct scp_hdr));

        if (SEQ_GT(b_seq, seq) && SEQ_GT(end, b_seq)) {
            uint32_t new_len = b_seq - seq;
            if (new_len == 0) {
                scp_buf_free(sb);
                return;
            }

            payload_len = new_len;
            sh->len     = htons((uint16_t)payload_len);
            sb->len     = sizeof(struct scp_hdr) + payload_len;
            end         = seq + payload_len;
        }
    }

    // if nothing left, drop
    if (payload_len == 0) {
        scp_buf_free(sb);
        return;
    }

    // insert as out-of-order segment
    rb_node_init(&sb->rb);
    sb->rb.value = sb->seq;
    rb_insert_node(&s->rcv_buf_q, &sb->rb);

    s->sb_cc += payload_len;
    scp_update_rcv_wnd(s);

    scp_output(s, SCP_FLAG_ACK);
}

/*
*When get ack, remote has acked, must free buf.
*/
void scp_snd_buf_free(struct scp_stream *ss, uint32_t ack)
{
    struct list_node *cur = ss->snd_q.next;

    while (cur != &ss->snd_q) {
        struct scp_buf *sb = container_of(cur, struct scp_buf, list);
        struct list_node *next = cur->next;

        uint32_t payload_len = sb->len - sizeof(struct scp_hdr);
        uint32_t end_seq     = sb->seq + payload_len;

        if (SEQ_LEQ(end_seq, ack)) {
            list_remove(cur);
            scp_buf_free(sb);
            cur = next;
            continue;
        }

        break;
    }
}

/* 
 * An ACK tells us the peer has received data.
 * We update RTT from this sample and free sent buffers.
 * The ACK also reflects the peer’s congestion state.
 * Handle ACK: update RTT, free buffers, run Reno + FR  
 */
static void scp_process_ack(struct scp_stream *ss,
                            uint32_t ack,
                            uint32_t wnd,
                            uint32_t sack)
{
    if (SEQ_LT(ack, ss->snd_una) || SEQ_GT(ack, ss->snd_nxt))
        return;

    uint32_t old_una = ss->snd_una;

    // RTT Karn
    if (ss->rtt_ts && SEQ_GT(ack, old_una)) {
        uint32_t rtt = scp_now_time() - ss->rtt_ts;
        if (!rtt) rtt = 1;
        scp_update_rtt(ss, rtt);
        ss->rtt_ts = 0;
        ss->rto_recovery = 0;
    }
 
    ss->snd_una = ack;
    ss->snd_wnd = wnd;

    if (ss->rto == SCP_RTO_MAX) {
        ss->rto_recovery++;
    }

    scp_snd_buf_free(ss, ack);

    if (SEQ_GT(sack, ack)) {
        if (SEQ_GT(ack, ss->last_gap_rexmit_ack)) {
            scp_retransmit_gap(ss, ack, sack);
            ss->last_gap_rexmit_ack = ack;
        }
    }

    // New data ACKed
    if (SEQ_GT(ss->snd_una, old_una)) {

        uint32_t acked = ss->snd_una - old_una;
        uint32_t mss   = MTU - sizeof(struct scp_hdr);
        if (!mss) mss = 1;

        // Exit Fast Recovery 
        if (ss->fr_active) {
            ss->fr_active = 0;
            ss->cwnd = ss->ssthresh + mss;
        }

        ss->timeout_count = 0;
        ss->dup_acks = 0;

        // Normal Reno growth (SS/CA) 
        if (!ss->fr_active) {
            if (ss->cwnd < ss->ssthresh) {
                uint32_t inc = acked;
                if (inc < mss)  inc = mss;
                if (inc > 2*mss) inc = 2*mss;
                ss->cwnd += inc;
            } else {
                uint32_t base = ss->cwnd ? ss->cwnd : 1;
                uint32_t inc  = (mss*mss)/base;
                if (!inc) inc = 1;
                ss->cwnd += inc;
            }
        }

        scp_timer_delete(&ss->t_retrans);
        if (ss->snd_una != ss->snd_nxt) {
            scp_timer_create(&ss->t_retrans,
                             scp_timer_retrans_cb,
                             ss,
                             ss->rto);
        }

        scp_output(ss, SCP_FLAG_DATA);
        return;
    }

    // No new ACK: dupACK path 
    if (SEQ_LT(ss->snd_una, ss->snd_nxt)) {
        ss->dup_acks++;

        uint32_t mss = MTU - sizeof(struct scp_hdr);
        if (!mss) mss = 1;

        // Enter Fast Recovery 
        if (ss->dup_acks == 3 && !ss->fr_active) {

            uint32_t flight = ss->snd_nxt - ss->snd_una;
            uint32_t half = flight / 2;
            uint32_t min_th = 2*mss;

            ss->ssthresh = (half > min_th) ? half : min_th;
            ss->cwnd = ss->ssthresh + 3*mss;
            ss->fr_active = 1;

            scp_retransmit_gap(ss, ack, sack); 
            ss->last_gap_rexmit_ack = ack; 

            scp_timer_delete(&ss->t_retrans);
            scp_timer_create(&ss->t_retrans,
                             scp_timer_retrans_cb,
                             ss,
                             ss->rto);
        }
        // FR ongoing: inflate cwnd 
        else if (ss->fr_active && ss->dup_acks > 3) {
            ss->cwnd += mss;
        }
        scp_output(ss, SCP_FLAG_DATA);
    }
}

/*
 *If we send data or ping, return ack.
 */
void scp_output_ack(struct scp_stream *ss)
{
    struct scp_hdr sh;
    memset(&sh, 0, sizeof(sh));

    sh.seq = htonl(ss->snd_nxt);
    sh.ack = htonl(ss->rcv_nxt);
    sh.sack = htonl(scp_calc_sack(ss));
    sh.wnd   = htons((uint16_t)ss->rcv_wnd);
    sh.len   = 0;
    sh.flags = SCP_FLAG_ACK;
    sh.fd    = ss->dst_fd;

    sh.cksum = 0;
    sh.cksum = in_checksum(&sh, sizeof(struct scp_hdr));

    scp_debug_dump_tx("ACK", &sh, sizeof(struct scp_hdr));
    scp_dump_hdr(ss, "ACK", &sh);
    ss->st_class->send(ss->st_class->user, &sh, sizeof(struct scp_hdr));
}

/* Build a transmit-ready packet for this fragment.
 * sb->data holds only payload; every send needs a fresh header.
 * Fragments may start at any offset, so we must assemble [hdr][payload].
 * sb->data is persistent state and must not be modified in-place.
 */
void scp_output_data(struct scp_stream *ss, struct scp_buf *sb,
                     uint32_t offset, uint32_t frag_len)
{
    uint32_t pkt_len = sizeof(struct scp_hdr) + frag_len;

    uint8_t small_buf[64];
    uint8_t *pkt;

    if (pkt_len <= sizeof(small_buf)) {
        pkt = small_buf;
    } else {
        pkt = scp_malloc(pkt_len);
        if (!pkt) return;
    }

    uint32_t seq = sb->seq + offset;   
    uint32_t end_seq = seq + frag_len;

    struct scp_hdr hdr;
    uint8_t *payload_base = sb->data + sizeof(struct scp_hdr);
    uint8_t *frag_payload = payload_base + offset;

    hdr.seq   = htonl(seq);
    hdr.ack   = htonl(ss->rcv_nxt);
    hdr.sack  = htonl(scp_calc_sack(ss));
    hdr.wnd   = htons((uint16_t)ss->rcv_wnd);
    hdr.len   = htons((uint16_t)frag_len);
    hdr.cksum = 0;
    hdr.flags = SCP_FLAG_DATA;
    hdr.fd    = ss->dst_fd;

    memcpy(pkt, &hdr, sizeof(struct scp_hdr));
    memcpy(pkt + sizeof(struct scp_hdr), frag_payload, frag_len);

    hdr.cksum = in_checksum(pkt, pkt_len);
    memcpy(pkt, &hdr, sizeof(struct scp_hdr));

    scp_debug_dump_tx("DATA", pkt, pkt_len);
    scp_dump_hdr(ss, "DATA", &hdr);
    ss->st_class->send(ss->st_class->user, pkt, pkt_len);

    ss->packet_count++;
    ss->packet_bytes += frag_len;
    if (SEQ_GT(end_seq, ss->snd_nxt)) {
        ss->snd_nxt = end_seq;
    }

    if (pkt != small_buf)
        scp_free(pkt);
}

/*
 * Output data:
 *  - ACK-only fast path if flags == ACK
 *  - Data path is hard-gated by min(cwnd, snd_wnd, flight)
 */
static int scp_output(struct scp_stream *ss, int flags)
{
    if (flags == SCP_FLAG_ACK) {
        scp_output_ack(ss);
        return 0;
    }

    uint32_t flight_before = ss->snd_nxt - ss->snd_una;

    uint32_t mss = MTU - sizeof(struct scp_hdr);
    if (mss == 0) mss = 1;

    uint32_t now = scp_now_time();

    // effective send window
    uint32_t effective_win = ss->snd_wnd;
    if (ss->cwnd < effective_win)
        effective_win = ss->cwnd;

    if (effective_win == 0 || flight_before >= effective_win)
        return 0;

    uint32_t flight = flight_before;

    struct list_node *n;
    for (n = ss->snd_q.next; n != &ss->snd_q; n = n->next) {

        struct scp_buf *sb = container_of(n, struct scp_buf, list);
        uint32_t total = sb->len - sizeof(struct scp_hdr);
        uint32_t sent  = sb->sent_off;

        if (sent >= total)
            continue;

        while (sent < total) {

            if (ss->snd_wnd == 0)
                goto out;

            // recompute window
            uint32_t win = ss->snd_wnd;
            if (ss->cwnd < win)
                win = ss->cwnd;

            int32_t swnd = (int32_t)win - (int32_t)flight;
            if (swnd <= 0)
                goto out;

            uint32_t remain   = total - sent;
            uint32_t frag_len = min(mss, remain);
            if (frag_len > (uint32_t)swnd)
                frag_len = (uint32_t)swnd;

            if (frag_len == 0)
                goto out;

            uint32_t seg_seq = sb->seq + sent;
            uint32_t end_seq = seg_seq + frag_len;

            // RTT sample
            if (ss->rtt_ts == 0 && SEQ_EQ(seg_seq, ss->snd_una)) {
                ss->rtt_ts  = now;
                ss->rtt_seq = end_seq;
            }

            scp_output_data(ss, sb, sent, frag_len);

            sb->sent_off += frag_len;
            sent         += frag_len;
            flight       += frag_len;

            if (SEQ_GT(end_seq, ss->snd_nxt))
                ss->snd_nxt = end_seq;
        }
    }

out:
    if (flight_before == 0 && flight > 0) {
        scp_timer_create(&ss->t_retrans,
                         scp_timer_retrans_cb,
                         ss,
                         ss->rto);
        ss->timeout_count = 0;
    }

    return 0;
}

/*
* swap iss. start timer.
*/
int scp_connect(int fd)
{
    struct scp_stream *ss = hashmap_get(&scp_stream_map, (void *)(uintptr_t)fd);
    if (!ss || ss->state != SCP_CLOSED)
        return -1;

    ss->iss      = random32();
    ss->snd_seq_q  = ss->iss;
    ss->snd_una  = ss->iss;
    ss->snd_nxt = ss->iss;

    ss->state = SCP_SYN_SENT;
    ss->retry = 0;

    scp_output_connect(ss);

    scp_timer_delete(&ss->t_hs);
    scp_timer_create(&ss->t_hs,
        scp_handle_handshake_timeout, 
        ss,                         
        ss->rto                      
    );

    return 0;
}

/*Copy or not copy, this is a question.
* Just enqueue.
*/
int scp_send(int fd, void *buf, size_t len)
{
    struct scp_stream *ss = hashmap_get(&scp_stream_map, (void *)(uintptr_t)fd);
    if (!ss) return -1;
    if (ss->state != SCP_ESTABLISHED) return -1;

    uint32_t flight = ss->snd_nxt - ss->snd_una;

    uint32_t win = ss->snd_wnd;
    if (flight >= win) return -2;

    uint32_t seq_base = ss->snd_seq_q;
    ss->snd_seq_q += len;

    struct scp_buf *sb = scp_buf_alloc(sizeof(struct scp_buf) + sizeof(struct scp_hdr) + len);
    if (!sb) return -1;

    sb->data = (uint8_t *)sb + sizeof(struct scp_buf);
    sb->len  = sizeof(struct scp_hdr) + len;
    sb->seq  = seq_base;

    uint8_t *pure_data = (uint8_t *)sb->data + sizeof(struct scp_hdr);
    memcpy(pure_data, buf, len);
    queue_enqueue(&ss->snd_q, &sb->list);

    scp_output(ss, SCP_FLAG_DATA);

    return 0;
}

/*
*To swap iss, respond syn+ack.
*/
static void scp_listen_like_process(struct scp_stream *ss,
                              struct scp_hdr *sh,
                              struct scp_buf *sb,
                              uint32_t ack,
                              uint32_t wnd)
{
    if (sh->flags & SCP_FLAG_CONNECT) {

        ss->state = SCP_SYN_RECV;

        ss->irs     = ntohl(sh->seq);
        ss->rcv_nxt = ss->irs;

        ss->iss      = random32();
        ss->snd_seq_q  = ss->iss;
        ss->snd_una  = ss->iss;
        ss->snd_nxt = ss->iss;

        scp_output_connect_ack(ss);

        ss->retry = 0;

        scp_timer_delete(&ss->t_hs);
        scp_timer_create(&ss->t_hs,
            scp_handle_handshake_timeout,  
            ss,                           
            ss->rto                        
        );
    }

    scp_buf_free(sb);
}

/*
* handshake has done, delete t_hs.  
* start persist for zeor wnd and keeplive.
*/
static void scp_syn_sent_process(struct scp_stream *ss,
                                 struct scp_hdr *sh,
                                 struct scp_buf *sb,
                                 uint32_t ack,
                                 uint32_t wnd)
{
    if (sh->flags & SCP_FLAG_CONNECT_ACK) {

        ss->irs = ntohl(sh->seq);
        ss->rcv_nxt = ss->irs;

        scp_output_ack(ss);

        ss->state = SCP_ESTABLISHED;

        scp_timer_delete(&ss->t_hs);

        scp_timer_create(&ss->t_persist,
                scp_timer_persist_cb,
                ss,
                PERSIST_INTERVAL
            );
    }

    scp_buf_free(sb);
}

static void scp_syn_recv_process(struct scp_stream *ss,
                                 struct scp_hdr *sh,
                                 struct scp_buf *sb,
                                 uint32_t ack,
                                 uint32_t wnd)
{
    if (sh->flags & SCP_FLAG_ACK) {
        ss->state = SCP_ESTABLISHED;

        scp_timer_delete(&ss->t_hs);

        scp_timer_create(&ss->t_persist,
                scp_timer_persist_cb,
                ss,
                PERSIST_INTERVAL
            );

        if (sh->flags & SCP_FLAG_DATA) {
            scp_process_data(ss, sb);
            return;
        }

        scp_buf_free(sb);
        return;
    }

    scp_buf_free(sb);
}

/* Process ACK/PING first to update send state immediately.
 * FIN consumes one sequence number and must be acknowledged reliably.
 * DATA is delivered or buffered according to rcv_nxt.
 * Any unhandled packet is freed after state‑specific processing.
 */
static void scp_est_process(struct scp_stream *ss,
                            struct scp_hdr *sh,
                            struct scp_buf *sb,
                            uint32_t ack,
                            uint32_t wnd,
                            uint32_t sack)
{
    if (sh->flags & SCP_FLAG_ACK) {
        scp_process_ack(ss, ack, wnd, sack);
    }

    if (sh->flags & SCP_FLAG_PING) {
        scp_output_ack(ss);
    }

    if (sh->flags & SCP_FLAG_FIN) {
        uint32_t seq = ntohl(sh->seq);

        if (SEQ_LT(seq, ss->rcv_nxt)) {
            scp_output_ack(ss);
            scp_buf_free(sb);
            return;
        }

        ss->rcv_nxt = seq + 1;
        scp_output_ack(ss);

        ss->state = SCP_LAST_ACK;
        ss->retry = 0;

        scp_timer_create(&ss->t_fin,
            scp_handle_fin_timeout,
            ss,
            ss->rto
        );

        scp_output_fin_first(ss);
        scp_buf_free(sb);
        return;
    }

    if (sh->flags & SCP_FLAG_DATA) {
        scp_process_data(ss, sb);
        return;
    }

    scp_buf_free(sb);
}

static void scp_fin_wait_process(struct scp_stream *ss,
                                 struct scp_hdr *sh,
                                 struct scp_buf *sb,
                                 uint32_t ack,
                                 uint32_t wnd,
                                 uint32_t sack)
{
    if (sh->flags & SCP_FLAG_ACK) {
        scp_process_ack(ss, ack, wnd, sack);
    }

    if (sh->flags & SCP_FLAG_PING) {
        scp_output_ack(ss);
    }

    if (sh->flags & SCP_FLAG_DATA) {
        scp_output_ack(ss);
        scp_buf_free(sb);
        return;
    }

    if (sh->flags & SCP_FLAG_FIN) {
        uint32_t seq = ntohl(sh->seq);
        if (SEQ_GEQ(seq, ss->rcv_nxt)) {
            ss->rcv_nxt = seq + 1;
        }

        scp_output_ack(ss);

        scp_timer_delete(&ss->t_fin);

        ss->state = SCP_CLOSED;
        scp_buf_free(sb);
        scp_stream_free(ss);
        return;
    }

    scp_buf_free(sb);
}

static void scp_last_ack_process(struct scp_stream *ss,
                                 struct scp_hdr *sh,
                                 struct scp_buf *sb,
                                 uint32_t ack,
                                 uint32_t wnd,
                                 uint32_t sack)
{
    if (sh->flags & SCP_FLAG_PING) {
        scp_output_ack(ss);
    }

    if (sh->flags & SCP_FLAG_FIN) {
        scp_output_ack(ss);
        scp_buf_free(sb);
        return;
    }

    if (sh->flags & SCP_FLAG_ACK) {
        scp_timer_delete(&ss->t_fin);

        ss->state = SCP_CLOSED;
        scp_buf_free(sb);
        scp_stream_free(ss);
        return;
    }

    scp_buf_free(sb);
}

/*
*Copy or not copy, this is a question.
*We need record all data, then update keeplive
*/
int scp_input(void *ctx, void *buf, size_t len)
{
    struct scp_buf *sb;
    struct scp_hdr *sh;
    struct scp_stream *ss;

    scp_debug_dump_rx(buf, len);

    sb = scp_buf_alloc(sizeof(struct scp_buf) + len);
    if (!sb) return -1;

    memcpy(sb->data, buf, len);
    sb->len = len;

    sh = (struct scp_hdr *)sb->data;

    uint16_t calc = in_checksum(buf, len);
    if (calc != 0) {
        scp_buf_free(sb);
        return -1;
    }

    uint32_t ack = ntohl(sh->ack);
    uint32_t wnd = ntohs(sh->wnd);
    uint32_t sack = ntohl(sh->sack);

    ss = hashmap_get(&scp_stream_map, (void *)(uintptr_t)sh->fd);
    if (!ss) {
        scp_buf_free(sb);
        return -1;
    }

    ss->last_active = scp_now_time(); 
    ss->idle_failures = 0;

    switch (ss->state) {
    case SCP_CLOSED:
        scp_listen_like_process(ss, sh, sb, ack, wnd);
        break;
    case SCP_SYN_SENT:
        scp_syn_sent_process(ss, sh, sb, ack, wnd);
        break;
    case SCP_SYN_RECV:
        scp_syn_recv_process(ss, sh, sb, ack, wnd);
        break;
    case SCP_ESTABLISHED:
        scp_est_process(ss, sh, sb, ack, wnd, sack);
        break;
    case SCP_FIN_WAIT:
        scp_fin_wait_process(ss, sh, sb, ack, wnd, sack);
        break;
    case SCP_LAST_ACK:
        scp_last_ack_process(ss, sh, sb, ack, wnd, sack);
        break;
    default:
        scp_buf_free(sb);
        break;
    }

    return 0;
}

/*
* We need to close all in all state.
*/
void scp_close(int fd)
{
    struct scp_stream *ss = hashmap_get(&scp_stream_map, (void *)(uintptr_t)fd);
    if (!ss) return;

    switch (ss->state) {

    case SCP_ESTABLISHED:
        ss->state = SCP_FIN_WAIT;
        ss->retry = 0;

        scp_timer_delete(&ss->t_fin);
        scp_timer_create(&ss->t_fin,
            scp_handle_fin_timeout,
            ss,
            ss->rto
        );

        scp_output_fin_first(ss);
        break;

    case SCP_SYN_SENT:
    case SCP_SYN_RECV:
        ss->state = SCP_CLOSED;
        scp_stream_free(ss);
        break;

    case SCP_FIN_WAIT:
    case SCP_LAST_ACK:
        break;

    case SCP_CLOSED:
    default:
        scp_stream_free(ss);
        break;
    }
}

/*
*Get data from rcv_data_q, update sb_cc and wnd.
*If we are zeor wnd, must update.
*/
int scp_recv(int fd, void *buf, size_t len)
{
    struct scp_stream *s = hashmap_get(&scp_stream_map, (void *)(uintptr_t)fd);
    if (!s) return -1;

    uint8_t *dst = buf;
    size_t copied = 0;

    while (copied < len && !list_empty(&s->rcv_data_q)) {

        struct list_node *n = s->rcv_data_q.next;
        struct scp_buf *sb = container_of(n, struct scp_buf, list);

        uint32_t total_payload = sb->len - sizeof(struct scp_hdr);
        uint32_t payload_len   = total_payload - sb->payload_off;

        uint32_t take = (uint32_t)min(len - copied, payload_len);
        uint8_t *payload = sb->data + sizeof(struct scp_hdr) + sb->payload_off;

        memcpy(dst + copied, payload, take);
        copied += take;

        if (take == payload_len) {
            list_remove(n);
            scp_buf_free(sb);
        } else {
            sb->payload_off += take;
        }
    }

    if (copied > s->sb_cc) {
        s->sb_cc = 0;
    } else {
        s->sb_cc -= copied;
    }

    uint32_t old_wnd = s->rcv_wnd;
    scp_update_rcv_wnd(s);

    if (old_wnd == 0 && s->rcv_wnd > 0) {
        scp_output(s, SCP_FLAG_ACK);
    }

    return copied;
}