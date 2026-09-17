/* JoyEMU NDS: ordered UDP first-arrival delivery with a reliable TCP copy.
 * This header is private to the Apple netplay host. No emulated Wi-Fi changes.
 * TCP carries capability/barrier/data; UDP never advances past a missing seq.
 */
#ifndef JOY_NDS_DUAL_TRANSPORT_H
#define JOY_NDS_DUAL_TRANSPORT_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>
#ifndef JE_NDS_TRACE
#define JE_NDS_TRACE(...) ((void)0)
#endif
#define JE_NDS_CMD 0x4a4e4431u
#define JE_NDS_HEADER 32u
#define JE_NDS_MAX 4096u
#define JE_NDS_WINDOW 256u
#define JE_NDS_OFFER 1
#define JE_NDS_BARRIER 2
#define JE_NDS_DATA 3
/* UDP-only extensions: a v13 peer can safely ignore them. */
#define JE_NDS_ACK 4
#define JE_NDS_NACK 5
#define JE_NDS_HELLO 6
struct je_nds_retry {
   uint64_t seq; size_t len; unsigned attempts; int64_t due, sent_at; bool nack;
   uint8_t data[JE_NDS_HEADER+JE_NDS_MAX];
};
struct je_nds_latency { uint64_t count,total_us,max_us,blocked; };
struct je_nds_slot { uint64_t seq; int64_t arrived_at; size_t len; bool udp,blocked; uint8_t data[JE_NDS_MAX]; };
struct je_nds_dual {
   int fd;
   int timestamp_error;
   uint64_t kernel_missing;
   int64_t trace_window; unsigned trace_count;
   struct je_nds_latency kernel_latency[4],order_latency[4];
   struct sockaddr_storage remote;
   socklen_t remote_len;
   uint16_t port;
   int tcp_service, udp_service, tcp_service_error, udp_service_error;
   uint64_t local_token, remote_token, tx_seq, rx_seq;
   bool tx_ready, rx_ready;
   uint64_t udp_sent, udp_received, udp_delivered, tcp_delivered, duplicates, rejected;
   uint64_t tcp_backfills, udp_send_errors, max_gap;
   uint64_t udp_wouldblock, udp_enobufs, udp_other_errors, udp_suppressed;
   int udp_last_errno; unsigned udp_pressure_streak;
   int64_t udp_resume_at, udp_data_resume_at;
   unsigned udp_data_pressure_streak;
   uint64_t udp_data_suppressed, nack_ack_advance, nack_cache_miss;
   int64_t report_at, poll_at, max_poll_gap;
   bool repair_ready, ack_pending;
   uint64_t submitted_seq, ack_next, rx_highest;
   uint64_t retry_timer, retry_nack, ack_received, nack_received, retry_limited, cache_pressure;
   int64_t ack_rtt_us, retry_rto_us;
   int64_t tick_at, hello_at, ack_at, nack_at, rate_at, retry_at;
   int64_t gap_at, max_gap_us, max_retry_late_us;
   unsigned hello_count, rate_packets; size_t rate_bytes;
   struct je_nds_retry retries[JE_NDS_WINDOW];
   struct je_nds_slot slots[JE_NDS_WINDOW];
};
typedef void (*je_nds_deliver_fn)(void*, const void*, size_t);
static inline uint64_t je_nds_read64(const uint8_t *p) {
   uint64_t v=0; unsigned i; for(i=0;i<8;i++) v=(v<<8)|p[i]; return v;
}
static inline void je_nds_write64(uint8_t *p,uint64_t v) {
   unsigned i;for(i=0;i<8;i++) {p[7-i]=(uint8_t)v;v>>=8;}
}
static inline void je_nds_header(uint8_t *p,unsigned kind,uint64_t token,uint64_t seq,size_t len) {
   memset(p,0,JE_NDS_HEADER);memcpy(p,"JENDS13",7);p[8]=(uint8_t)kind;p[9]=1;
   je_nds_write64(p+12,token);je_nds_write64(p+20,seq);
   p[28]=(uint8_t)(len>>24);p[29]=(uint8_t)(len>>16);p[30]=(uint8_t)(len>>8);p[31]=(uint8_t)len;
}
/* All repair timers use this clock, independent of frontend/core clocks. */
static inline int64_t je_nds_now(void) {
   struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
   return (int64_t)t.tv_sec*1000000+t.tv_nsec/1000;
}
/* Measurement only: classify frontend payload, never change delivery order. */
static inline unsigned je_nds_latency_kind(const uint8_t *p,size_t n) {
   if(n<10)return 3;
   if(p[9]==1 || p[9]==4)return 1;
   if(p[9]==2 && n>=24) {
      unsigned fc=p[22]|((unsigned)p[23]<<8);
      if(fc==0x0228)return 0;
      if(fc==0x0218)return 2;
   }
   return 3;
}
static inline void je_nds_latency_add(struct je_nds_latency *m,uint64_t us,bool blocked) {
   ++m->count;m->total_us+=us;if(us>m->max_us)m->max_us=us;m->blocked+=blocked;
}
static inline void je_nds_trace_round(struct je_nds_dual *s,const char *stage,const uint8_t *p,size_t n) {
   uint64_t cmd;unsigned seq;int64_t now;struct timeval wall;
   if(n>=46 && p[9]==2 && p[22]==0x28 && p[23]==2) {
      cmd=je_nds_read64(p);seq=(p[44]|((unsigned)p[45]<<8))>>4;
   } else if(n>=20 && p[9]==4) {cmd=je_nds_read64(p+10);seq=((unsigned)p[18]<<8)|p[19];}
   else return;
   if(seq%128)return;
   now=je_nds_now();if(now-s->trace_window>=2000000){s->trace_window=now;s->trace_count=0;}
   if(s->trace_count>=12)return;++s->trace_count;gettimeofday(&wall,NULL);
   JE_NDS_TRACE("[JE-NDS-TRANSPORT-ROUND] build=nds-dual-v31 port=%u wallMs=%lld monotonicUs=%lld stage=%s cmd=%llu seq=%u\n",
      (unsigned)s->port,(long long)wall.tv_sec*1000+wall.tv_usec/1000,(long long)now,stage,(unsigned long long)cmd,seq);
   (void)stage;(void)cmd;
}
static inline int je_nds_enable_timestamp(int fd) {
#ifdef SO_TIMESTAMP
   int on=1;return setsockopt(fd,SOL_SOCKET,SO_TIMESTAMP,&on,sizeof(on))<0?errno:0;
#else
   (void)fd;return ENOTSUP;
#endif
}
static inline ssize_t je_nds_recv_datagram(int fd,void *buf,size_t capacity,int64_t *age) {
   union {struct cmsghdr align;uint8_t bytes[CMSG_SPACE(sizeof(struct timeval))];} control;
   struct iovec iov={buf,capacity};struct msghdr msg;struct cmsghdr *c;ssize_t n;
   memset(&msg,0,sizeof(msg));msg.msg_iov=&iov;msg.msg_iovlen=1;
   msg.msg_control=control.bytes;msg.msg_controllen=sizeof(control.bytes);*age=-1;
   n=recvmsg(fd,&msg,0);if(n<0)return n;
#ifdef SCM_TIMESTAMP
   if(!(msg.msg_flags&MSG_CTRUNC))for(c=CMSG_FIRSTHDR(&msg);c;c=CMSG_NXTHDR(&msg,c)) {
      if(c->cmsg_level==SOL_SOCKET&&c->cmsg_type==SCM_TIMESTAMP&&c->cmsg_len>=CMSG_LEN(sizeof(struct timeval))) {
         struct timeval arrived,now;int64_t delta;memcpy(&arrived,CMSG_DATA(c),sizeof(arrived));gettimeofday(&now,NULL);
         delta=((int64_t)now.tv_sec-arrived.tv_sec)*1000000+now.tv_usec-arrived.tv_usec;
         /* Wall clock can jump: missing/implausible ages stay unknown, not zero. */
         if(arrived.tv_usec>=0&&arrived.tv_usec<1000000&&delta>=0&&delta<=5000000)*age=delta;
      }
   }
#else
   (void)c;
#endif
   return n;
}
/* DATA and control share socket pressure: do not keep pushing a full kernel
 * buffer from every core poll. TCP remains queued before original UDP DATA. */
static inline bool je_nds_udp_write(struct je_nds_dual *s,const void *p,size_t n) {
   int64_t now=je_nds_now();
   if(now<s->udp_resume_at){++s->udp_suppressed;return false;}
   ssize_t sent=s->fd>=0?send(s->fd,p,n,0):-1;
   if(sent==(ssize_t)n){s->udp_resume_at=0;s->udp_pressure_streak=0;return true;}
   int error=s->fd<0?EBADF:(sent<0?errno:EMSGSIZE);
   ++s->udp_send_errors;s->udp_last_errno=error;
   if(error==EAGAIN || error==EWOULDBLOCK || error==ENOBUFS) {
      if(error==ENOBUFS) {
         ++s->udp_enobufs;
         /* Tiny control packets may succeed while the data queue is still full.
          * Keep bulk recovery separate; queued TCP originals continue normally. */
         if(s->udp_data_pressure_streak<4)++s->udp_data_pressure_streak;
         s->udp_data_resume_at=now+((int64_t)100000<<(s->udp_data_pressure_streak-1));
      } else ++s->udp_wouldblock;
      if(s->udp_pressure_streak<6)++s->udp_pressure_streak;
      s->udp_resume_at=now+((int64_t)1000<<s->udp_pressure_streak);
   } else ++s->udp_other_errors;
   return false;
}
static inline bool je_nds_udp_data_write(struct je_nds_dual *s,const void *p,size_t n) {
   if(je_nds_now()<s->udp_data_resume_at){++s->udp_data_suppressed;return false;}
   bool sent=je_nds_udp_write(s,p,n);
   if(sent){s->udp_data_resume_at=0;s->udp_data_pressure_streak=0;}
   return sent;
}
static inline bool je_nds_control(struct je_nds_dual *s,unsigned kind,uint64_t seq) {
   uint8_t p[JE_NDS_HEADER];je_nds_header(p,kind,s->remote_token,seq,0);
   return je_nds_udp_write(s,p,sizeof(p));
}
static inline int64_t je_nds_retry_delay(const struct je_nds_dual *s,unsigned attempts) {
   int64_t base=s->retry_rto_us?s->retry_rto_us:40000;
   int64_t delay=base << (attempts<3?attempts:3);
   return delay<500000?delay:500000;
}
/* Retain unacknowledged holes until ACK, including beyond four fast attempts.
 * Slow recovery remains bounded: one packet/ms, 32 packets/32 KiB per 100 ms;
 * NACK accelerates only after a per-packet cooldown (100 ms after four tries). */
static inline void je_nds_tick(struct je_nds_dual *s,int64_t now) {
   unsigned i;struct je_nds_retry *r=NULL;
   if(!s->tx_ready || !s->rx_ready || now<s->udp_resume_at)return;
   /* TCP may advance RX between two polls in the same frame. Its ACK must not
    * wait another frame merely because the retry scan already ran. */
   if(s->repair_ready && s->ack_pending && now-s->ack_at>=1000) {
      if(je_nds_control(s,JE_NDS_ACK,s->rx_seq))s->ack_pending=false;
      s->ack_at=now;
      if(now<s->udp_resume_at)return;
   }
   if(now-s->tick_at<1000)return;
   s->tick_at=now;
   if(s->hello_count<8 && (!s->hello_at || now-s->hello_at>=100000)) {
      if(je_nds_control(s,JE_NDS_HELLO,0))++s->hello_count;
      s->hello_at=now;
   }
   if(!s->repair_ready || now<s->udp_resume_at)return;
   if(s->rx_highest>=s->rx_seq && now-s->nack_at>=20000) {
      je_nds_control(s,JE_NDS_NACK,s->rx_seq);s->nack_at=now;
   }
   if(now<s->udp_resume_at || now<s->udp_data_resume_at || now-s->retry_at<1000)return;
   for(i=0;i<JE_NDS_WINDOW;i++) {
      struct je_nds_retry *candidate=&s->retries[i];
      if(candidate->seq && candidate->seq>=s->ack_next && candidate->due<=now &&
            (!r || candidate->seq<r->seq))r=candidate;
   }
   if(!r)return;
   if(now-s->rate_at>=100000) {s->rate_at=now;s->rate_packets=0;s->rate_bytes=0;}
   if(s->rate_packets>=32 || s->rate_bytes+r->len>32768) {++s->retry_limited;return;}
   if(now-r->due>s->max_retry_late_us)s->max_retry_late_us=now-r->due;
   bool sent=je_nds_udp_data_write(s,r->data,r->len);
   if(r->nack)++s->retry_nack;else ++s->retry_timer;
   if(!sent && (s->udp_resume_at>now || s->udp_data_resume_at>now)) {
      /* Kernel backpressure is not a transmitted/lost retry. Preserve the
       * NACK and retry promptly when the shared socket cooldown expires. */
      r->due=s->udp_resume_at>s->udp_data_resume_at?s->udp_resume_at:s->udp_data_resume_at;
   } else {
      if(r->attempts<255)++r->attempts;
      r->nack=false;r->sent_at=now;r->due=now+je_nds_retry_delay(s,r->attempts);
   }
   s->retry_at=now;++s->rate_packets;s->rate_bytes+=r->len;
}
/* DS command/reply traffic is delay-sensitive interactive media, not bulk.
 * Use the documented Darwin service category on both race paths. This is a
 * scheduling hint, not a latency guarantee; preserve the connection on failure.
 * Apply only once on the dedicated NDS connection, never from the poll loop. */
static inline int je_nds_set_interactive_service(int fd,int *actual) {
   *actual=-1;
#if defined(__APPLE__) && defined(SO_NET_SERVICE_TYPE) && defined(NET_SERVICE_TYPE_RV)
   const int requested=NET_SERVICE_TYPE_RV;
   socklen_t len=sizeof(*actual);
   int error=0;
   if(setsockopt(fd,SOL_SOCKET,SO_NET_SERVICE_TYPE,&requested,sizeof(requested))<0)error=errno;
   if(getsockopt(fd,SOL_SOCKET,SO_NET_SERVICE_TYPE,actual,&len)<0 && !error)error=errno;
   return error;
#else
   (void)fd;return ENOPROTOOPT;
#endif
}
static inline struct je_nds_dual *je_nds_create(int tcp_fd) {
   struct je_nds_dual *s;struct sockaddr_storage local; socklen_t len;
   s=(struct je_nds_dual*)calloc(1,sizeof(*s));if(!s)return NULL;
   s->fd=-1;s->remote_len=sizeof(s->remote);
   if(getpeername(tcp_fd,(struct sockaddr*)&s->remote,&s->remote_len)<0)goto fail;
   if(s->remote.ss_family!=AF_INET && s->remote.ss_family!=AF_INET6)goto fail;
   s->fd=socket(s->remote.ss_family,SOCK_DGRAM,0);if(s->fd<0)goto fail;
   if(fcntl(s->fd,F_SETFL,fcntl(s->fd,F_GETFL,0)|O_NONBLOCK)<0)goto fail;
   memset(&local,0,sizeof(local));local.ss_family=s->remote.ss_family;
   len=local.ss_family==AF_INET?sizeof(struct sockaddr_in):sizeof(struct sockaddr_in6);
   if(bind(s->fd,(struct sockaddr*)&local,len)<0 || getsockname(s->fd,(struct sockaddr*)&local,&len)<0)goto fail;
   s->timestamp_error=je_nds_enable_timestamp(s->fd);
   s->port=ntohs(local.ss_family==AF_INET?((struct sockaddr_in*)&local)->sin_port:((struct sockaddr_in6*)&local)->sin6_port);
   arc4random_buf(&s->local_token,sizeof(s->local_token));if(!s->local_token)s->local_token=1;
   s->udp_service_error=je_nds_set_interactive_service(s->fd,&s->udp_service);
   s->tcp_service_error=je_nds_set_interactive_service(tcp_fd,&s->tcp_service);
   s->tx_seq=s->rx_seq=s->ack_next=1;return s;
fail:
   if(s->fd>=0)close(s->fd);free(s);return NULL;
}
static inline void je_nds_destroy(struct je_nds_dual *s) {if(s){close(s->fd);free(s);}}
static inline void je_nds_offer(struct je_nds_dual *s,uint8_t *buf) {
   je_nds_header(buf,JE_NDS_OFFER,s->local_token,s->port,0);
}
/* Call only after the barrier has been queued behind all earlier raw TCP data. */
static inline void je_nds_barrier(struct je_nds_dual *s,uint8_t *buf) {
   je_nds_header(buf,JE_NDS_BARRIER,s->remote_token,0,0);
}
static inline size_t je_nds_encode(struct je_nds_dual *s,const void *data,size_t len,uint8_t *out) {
   if(!s->tx_ready || !len || len>JE_NDS_MAX || s->tx_seq==UINT64_MAX)return 0;
   je_nds_header(out,JE_NDS_DATA,s->remote_token,s->tx_seq++,len);
   memcpy(out+JE_NDS_HEADER,data,len);return len+JE_NDS_HEADER;
}
/* 1 = accepted/ignored data, 2 = offer accepted (caller must queue barrier),
 * -1 = invalid TCP envelope, 0 = rejected UDP. Never discard a required TCP seq. */
static inline int je_nds_receive(struct je_nds_dual *s,const uint8_t *p,size_t n,bool udp,je_nds_deliver_fn deliver,void *ctx) {
   uint64_t token,seq;size_t len;unsigned kind;struct je_nds_slot *slot;
   if(n<JE_NDS_HEADER || memcmp(p,"JENDS13\0",8) || p[9]!=1 || p[10] || p[11])goto bad;
   token=je_nds_read64(p+12);seq=je_nds_read64(p+20);kind=p[8];
   len=((uint32_t)p[28]<<24)|((uint32_t)p[29]<<16)|((uint32_t)p[30]<<8)|p[31];
   if(n!=JE_NDS_HEADER+len || len>JE_NDS_MAX)goto bad;
   if(kind==JE_NDS_OFFER && !udp && !len && token && seq && seq<=65535) {
      if(s->remote_token) return s->remote_token==token?1:-1;
      s->remote_token=token;
      if(s->remote.ss_family==AF_INET)((struct sockaddr_in*)&s->remote)->sin_port=htons((uint16_t)seq);
      else ((struct sockaddr_in6*)&s->remote)->sin6_port=htons((uint16_t)seq);
      /* connect restricts incoming UDP to the TCP peer's negotiated endpoint. */
      if(connect(s->fd,(struct sockaddr*)&s->remote,s->remote_len)<0) {
         /* TCP envelopes still work when UDP cannot connect. */
         close(s->fd);s->fd=-1;
      }
      return 2;
   }
   if(token!=s->local_token)goto bad;
   if(kind==JE_NDS_BARRIER && !udp && !len && !seq) {s->rx_ready=true;return 1;}
   if(udp && !len && s->tx_ready && s->rx_ready) {
      if(kind==JE_NDS_HELLO && !seq) {
         int64_t now=je_nds_now();s->repair_ready=true;s->ack_pending=true;
         /* Bounded response permits asymmetric/lost HELLO recovery, no ping-pong. */
         if(s->hello_count<16 && now-s->hello_at>=10000) {
            if(je_nds_control(s,JE_NDS_HELLO,0))++s->hello_count;
      s->hello_at=now;
         }
         return 1;
      }
      if(s->repair_ready && kind==JE_NDS_ACK && seq && seq<=s->submitted_seq+1) {
         ++s->ack_received;
         if(seq>s->ack_next) {
            /* Karn: do not time ACKs of retransmitted packets. Use the newest
             * newly acknowledged original, so a cumulative ACK is one sample. */
            struct je_nds_retry *sample=NULL;unsigned i;int64_t now=je_nds_now();
            for(i=0;i<JE_NDS_WINDOW;i++) {
               struct je_nds_retry *r=&s->retries[i];
               if(r->seq>=s->ack_next && r->seq<seq && !r->attempts && r->sent_at &&
                     (!sample || r->seq>sample->seq))sample=r;
            }
            if(sample && now>sample->sent_at) {
               int64_t rtt=now-sample->sent_at;
               if(rtt>500000)rtt=500000;
               s->ack_rtt_us=s->ack_rtt_us?(7*s->ack_rtt_us+rtt)/8:rtt;
               s->retry_rto_us=s->ack_rtt_us*2;
               if(s->retry_rto_us<20000)s->retry_rto_us=20000;
               if(s->retry_rto_us>250000)s->retry_rto_us=250000;
            }
            s->ack_next=seq;
         }
         return 1;
      }
      if(s->repair_ready && kind==JE_NDS_NACK && seq>=s->ack_next && seq && seq<=s->submitted_seq) {
         struct je_nds_retry *r=&s->retries[seq%JE_NDS_WINDOW];
         ++s->nack_received;
         /* NACK names the receiver's next missing sequence, hence cumulatively
          * confirms every earlier DATA even when its standalone ACK was lost.
          * Do not acknowledge the missing sequence or derive RTT from this. */
         if(seq>s->ack_next){s->ack_next=seq;++s->nack_ack_advance;}
         if(r->seq!=seq)++s->nack_cache_miss;
         if(r->seq==seq) {
            int64_t now=je_nds_now();
            int64_t earliest=r->sent_at+(r->attempts<4?10000:100000);
            if(earliest<now)earliest=now;
            if(earliest<r->due)r->due=earliest;
            r->nack=true;
         }
         return 1;
      }
   }
   if(kind!=JE_NDS_DATA || !s->rx_ready || !seq || !len)goto bad;
   if(udp)++s->udp_received;
   if(seq<s->rx_seq){++s->duplicates;if(udp)s->ack_pending=true;return 1;}
   if(seq-s->rx_seq>=JE_NDS_WINDOW)goto bad;
   if(seq-s->rx_seq>s->max_gap)s->max_gap=seq-s->rx_seq;
   slot=&s->slots[seq%JE_NDS_WINDOW];
   if(slot->seq==seq){++s->duplicates;return 1;}
   if(slot->seq)goto bad;
   if(seq>s->rx_highest)s->rx_highest=seq;
   if(seq>s->rx_seq && !s->gap_at)s->gap_at=je_nds_now();
   slot->arrived_at=je_nds_now();slot->blocked=seq>s->rx_seq;
   je_nds_trace_round(s,udp?"udp_first":"tcp_first",p+JE_NDS_HEADER,len);
   slot->seq=seq;slot->len=len;slot->udp=udp;memcpy(slot->data,p+JE_NDS_HEADER,len);
   if(!udp && s->slots[(seq+1)%JE_NDS_WINDOW].seq==seq+1)++s->tcp_backfills;
   while((slot=&s->slots[s->rx_seq%JE_NDS_WINDOW])->seq==s->rx_seq) {
      if(slot->udp)++s->udp_delivered;else ++s->tcp_delivered;
      int64_t residence=je_nds_now()-slot->arrived_at;
      je_nds_latency_add(&s->order_latency[je_nds_latency_kind(slot->data,slot->len)],residence>0?(uint64_t)residence:0,slot->blocked);
      je_nds_trace_round(s,"deliver",slot->data,slot->len);
      /* Delivery callback queues core data; it must not tear down this object. */
      deliver(ctx,slot->data,slot->len);slot->seq=0;++s->rx_seq;s->ack_pending=true;
      if(s->gap_at) {
         int64_t elapsed=je_nds_now()-s->gap_at;
         if(elapsed>s->max_gap_us)s->max_gap_us=elapsed;
         s->gap_at=s->rx_highest>=s->rx_seq?je_nds_now():0;
      }
   }
   return 1;
bad:
   ++s->rejected;return udp?0:-1;
}
static inline void je_nds_send_udp(struct je_nds_dual *s,const uint8_t *buf,size_t n) {
   /* Caller has already queued the reliable TCP copy. Retain even when UDP fails. */
   uint64_t seq=je_nds_read64(buf+20);struct je_nds_retry *r=&s->retries[seq%JE_NDS_WINDOW];
   /* Never evict an unacknowledged hole for a newer packet at the same index.
    * The new packet still has its original UDP send and reliable TCP copy. */
   if(!r->seq || r->seq<s->ack_next) {
      r->seq=seq;r->len=n;r->attempts=0;r->nack=false;r->sent_at=je_nds_now();
      r->due=r->sent_at+je_nds_retry_delay(s,0);memcpy(r->data,buf,n);
   } else ++s->cache_pressure;
   s->submitted_seq=seq;
   je_nds_trace_round(s,"submit",buf+JE_NDS_HEADER,n-JE_NDS_HEADER);
   if(je_nds_udp_data_write(s,buf,n))++s->udp_sent;
}
static inline void je_nds_poll_udp(struct je_nds_dual *s,je_nds_deliver_fn deliver,void *ctx) {
   uint8_t buf[JE_NDS_HEADER+JE_NDS_MAX+1];unsigned i;
   if(s->fd<0 || !s->remote_token)return;
   for(i=0;i<64;i++) {
      int64_t age;ssize_t n=je_nds_recv_datagram(s->fd,buf,sizeof(buf),&age);if(n<0)break;
      uint64_t seq=n>=JE_NDS_HEADER?je_nds_read64(buf+20):0;
      bool fresh=seq>=s->rx_seq && seq-s->rx_seq<JE_NDS_WINDOW && s->slots[seq%JE_NDS_WINDOW].seq!=seq;
      int accepted=je_nds_receive(s,buf,(size_t)n,true,deliver,ctx);
      if(accepted>0&&n>JE_NDS_HEADER&&buf[8]==JE_NDS_DATA&&fresh) {
         if(age>=0)je_nds_latency_add(&s->kernel_latency[je_nds_latency_kind(buf+JE_NDS_HEADER,(size_t)n-JE_NDS_HEADER)],(uint64_t)age,false);
         else ++s->kernel_missing;
      }
   }
   je_nds_tick(s,je_nds_now());
}
#endif
