#ifndef LTTIT_CONFIG_H
#define LTTIT_CONFIG_H

#define RPC_DEBUG 0

#define SCP_DEBUG 0
#define SCP_DUMP 0
#define SCP_RUN_DEBUG 0

//Set by yourself.
#define RETRANS_COUNT_MAX 12
#define MIN_SEG 8
#define SCP_RTO_MIN 100
#define SCP_RTO_MAX 1000
#define RETRANS_RECO_MAX 16
#define RETRANS_GAP_MAX  8
#define SCP_RECV_LIMIT 0xFFFF
#define SEND_WIN_INIT 0xFFFF
#define RECV_WIN_INIT     0xFFFF
#define SSTHRESH_INIT 0xFFFF
#define MTU 200
#define PERSIST_INTERVAL 200
#define MAX_IDLE_FAIL  3
#define IDLE_TIMEOUT 100000


#define CCNET_DEBUG 0

#define CONFIG_HEAP     (15 * 1024)

#endif
