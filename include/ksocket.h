/*
=====================================
Mini Project 1 Submission
Group Details:
Member 1 Name: Arnav Singh
Member 1 Roll number: 23CS30009
=====================================
*/

#ifndef KSOCKET_H
#define KSOCKET_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/sem.h>

#define T 5
#define DROP_PROB 0.1f

#define SOCK_KTP 100
#define MAX_KTP_SOCKETS 10
#define MSG_SIZE 512
#define RECV_BUF_SIZE 10
#define SEND_BUF_SIZE 100
#define MAX_SEQ 255

#define KTP_DATA 1
#define KTP_ACK 2
#define KTP_DUPACK 3

#define ENOSPACE 1
#define ENOTBOUND 2
#define ENOMESSAGE 3
#define ESHM 4
#define EBIND 5
#define EINVAL 6
#define ESOCK 7


typedef struct{
    uint8_t type;
    uint8_t seq;
    uint8_t rwnd; // piggybacked receiver window
} KTPHeader;

typedef struct{
    KTPHeader hdr;
    char data[MSG_SIZE];
} KTPMessage;

// sender window
typedef struct{
    int size;
    uint8_t seq[RECV_BUF_SIZE]; // unacked seqs
    int count;
    time_t last_send_time;
} SWnd;

// receiver window
typedef struct{
    int size;
    uint8_t base; // next expected seq
} RWnd;

typedef struct{
    int is_free;
    pid_t pid;
    int udp_sockfd;
    struct sockaddr_in src_addr;
    struct sockaddr_in dst_addr;
    int bound;
    char send_buf[SEND_BUF_SIZE][MSG_SIZE];
    int send_buf_head;
    int send_buf_tail;
    int send_buf_count;
    char recv_buf[RECV_BUF_SIZE][MSG_SIZE];
    int recv_buf_valid[RECV_BUF_SIZE];
    uint8_t recv_buf_seq[RECV_BUF_SIZE];
    int recv_buf_count;
    SWnd swnd;
    RWnd rwnd;
    uint8_t last_ack_seq;
    int nospace;
    uint8_t next_seq;
    int bind_requested;
    int close_requested;
} KTPSocketEntry;

typedef struct{
    KTPSocketEntry sock[MAX_KTP_SOCKETS];
} SharedMem;

int k_socket(int domain, int type, int protocol);
int k_bind(int sockfd, char *src_ip, int src_port, char *dst_ip, int dst_port);
int k_sendto(int sockfd, char *buf, size_t len);
int k_recvfrom(int sockfd, char *buf, size_t len);
int k_close(int sockfd);
int k_pending(int sockfd);
int dropMessage(float p);
void sem_lock(int i);
void sem_unlock(int i);
SharedMem *get_shm(void);

extern int k_errno;

#endif
