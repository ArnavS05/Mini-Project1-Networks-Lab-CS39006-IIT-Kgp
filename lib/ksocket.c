/*
=====================================
Mini Project 1 Submission
Group Details:
Member 1 Name: Arnav Singh
Member 1 Roll number: 23CS30009
=====================================
*/

#include "ksocket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <arpa/inet.h>

int k_errno = 0;

SharedMem *get_shm(){
    int shmid = shmget(ftok("/home", 1), sizeof(SharedMem), 0666);
    if(shmid<0) return NULL;
    void *addr = shmat(shmid, NULL, 0);
    if(addr==(void *)-1) return NULL;
    return (SharedMem *)addr;
}

void sem_lock(int i){
    int sid = semget(ftok("/home", 2), 0, 0666);
    struct sembuf op = {(unsigned short)i, -1, 0};
    semop(sid, &op, 1);
}

void sem_unlock(int i){
    int sid = semget(ftok("/home", 2), 0, 0666);
    struct sembuf op = {(unsigned short)i, 1, 0};
    semop(sid, &op, 1);
}

int dropMessage(float p){
    float r = (float)rand()/(float)RAND_MAX;
    return (r<p) ? 1 : 0;
}

int k_socket(int domain, int type, int protocol){
    if(type!=SOCK_KTP) {
        k_errno = EINVAL;
        return -1;
    }

    SharedMem *sm = get_shm();
    if(!sm) {
        k_errno = ESHM;
        return -1;
    }

    // Finding a free slot
    int idx = -1;
    for(int i=0; i<MAX_KTP_SOCKETS; i++){
        sem_lock(i);
        if(sm->sock[i].is_free){
            idx = i;
            sm->sock[i].is_free = 0;
            sm->sock[i].pid = getpid();
            sem_unlock(i);
            break;
        }
        sem_unlock(i);
    }

    if(idx<0){
        shmdt(sm);
        k_errno = ENOSPACE;
        return -1;
    }

    (void)domain;
    (void)protocol;

    sem_lock(idx);
    sm->sock[idx].bound = 0;
    sm->sock[idx].send_buf_head = 0;
    sm->sock[idx].send_buf_tail = 0;
    sm->sock[idx].send_buf_count = 0;
    sm->sock[idx].recv_buf_count = 0;
    sm->sock[idx].last_ack_seq = 0;
    sm->sock[idx].nospace = 0;
    memset(sm->sock[idx].recv_buf_valid, 0, sizeof(sm->sock[idx].recv_buf_valid));
    memset(sm->sock[idx].recv_buf_seq, 0, sizeof(sm->sock[idx].recv_buf_seq));
    sm->sock[idx].swnd.size = RECV_BUF_SIZE;
    sm->sock[idx].swnd.count = 0;
    sm->sock[idx].swnd.last_send_time = 0;
    sm->sock[idx].rwnd.size = RECV_BUF_SIZE;
    sm->sock[idx].rwnd.base = 1;
    sm->sock[idx].next_seq = 1;
    sm->sock[idx].bind_requested = 0;
    sm->sock[idx].close_requested = 0;
    sem_unlock(idx);
    shmdt(sm);
    return idx + 1000;
}

int k_bind(int sockfd, char *src_ip, int src_port, char *dst_ip, int dst_port) {
    int idx = sockfd - 1000;
    if(idx<0 || idx>=MAX_KTP_SOCKETS){
        k_errno = EINVAL;
        return -1;
    }

    SharedMem *sm = get_shm();
    if(!sm){
        k_errno = ESHM;
        return -1;
    }

    sem_lock(idx);

    struct sockaddr_in src, dst;
    src.sin_family = AF_INET;
    src.sin_port = htons(src_port);
    inet_pton(AF_INET, src_ip, &src.sin_addr);

    dst.sin_family = AF_INET;
    dst.sin_port = htons(dst_port);
    inet_pton(AF_INET, dst_ip, &dst.sin_addr);

    sm->sock[idx].src_addr = src;
    sm->sock[idx].dst_addr = dst;
    sm->sock[idx].bind_requested = 1;

    sem_unlock(idx);
    shmdt(sm);

    // spin-wait for initksocket to perform the actual bind in its own process
    // since fds are local to a given process, initksocket needs to do the bind 
    // and share the fd back through shared memory
    while(1){
        usleep(10000);
        sm = get_shm();
        if(!sm){
            k_errno = ESHM;
            return -1;
        }
        sem_lock(idx);
        int req  = sm->sock[idx].bind_requested;
        int done = sm->sock[idx].bound;
        sem_unlock(idx);
        shmdt(sm);
        if(!req){
            if(done) return 0;
            k_errno = EBIND;
            return -1;
        }
    }
}

int k_sendto(int sockfd, char *buf, size_t len){
    int idx = sockfd - 1000;
    if(idx<0 || idx>=MAX_KTP_SOCKETS){
        k_errno = EINVAL;
        return -1;
    }

    SharedMem *sm = get_shm();
    if(!sm){
        k_errno = ESHM;
        return -1;
    }

    sem_lock(idx);

    if(!sm->sock[idx].bound){
        sem_unlock(idx);
        shmdt(sm);
        k_errno = ENOTBOUND;
        return -1;
    }
    if(sm->sock[idx].send_buf_count>=SEND_BUF_SIZE){
        sem_unlock(idx);
        shmdt(sm);
        k_errno = ENOSPACE;
        return -1;
    }

    int tail = sm->sock[idx].send_buf_tail;
    memset(sm->sock[idx].send_buf[tail], 0, MSG_SIZE);
    memcpy(sm->sock[idx].send_buf[tail], buf, len<MSG_SIZE ? len : MSG_SIZE);
    sm->sock[idx].send_buf_tail = (tail+1)%SEND_BUF_SIZE;
    sm->sock[idx].send_buf_count++;

    sem_unlock(idx);
    shmdt(sm);
    return (len<MSG_SIZE ? len : MSG_SIZE);
}

int k_recvfrom(int sockfd, char *buf, size_t len){
    int idx = sockfd - 1000;
    if(idx<0 || idx>=MAX_KTP_SOCKETS){
        k_errno = ENOMESSAGE;
        return -1;
    }

    SharedMem *sm = get_shm();
    if(!sm){
        k_errno = ESHM;
        return -1;
    }

    sem_lock(idx);

    uint8_t expected = sm->sock[idx].rwnd.base;
    int found = -1;
    for(int i=0; i<RECV_BUF_SIZE; i++) {
        if(sm->sock[idx].recv_buf_valid[i] && sm->sock[idx].recv_buf_seq[i]==expected){
            found = i;
            break;
        }
    }

    if(found<0){
        sem_unlock(idx);
        shmdt(sm);
        k_errno = ENOMESSAGE;
        return -1;
    }
    int copy_len = (len<MSG_SIZE) ? len : MSG_SIZE;
    memcpy(buf, sm->sock[idx].recv_buf[found], copy_len);

    sm->sock[idx].recv_buf_valid[found] = 0;
    sm->sock[idx].recv_buf_count--;
    sm->sock[idx].recv_buf_seq[found] = 0;
    sm->sock[idx].rwnd.base = expected%MAX_SEQ + 1;
    sm->sock[idx].rwnd.size++;

    sem_unlock(idx);
    shmdt(sm);
    return copy_len;
}

int k_pending(int sockfd){
    int idx = sockfd - 1000;
    if(idx<0 || idx>=MAX_KTP_SOCKETS){ k_errno = EINVAL; return -1; }
    SharedMem *sm = get_shm();
    if(!sm){ k_errno = ESHM; return -1; }
    sem_lock(idx);
    int pending = sm->sock[idx].send_buf_count + sm->sock[idx].swnd.count;
    sem_unlock(idx);
    shmdt(sm);
    return pending;
}

int k_close(int sockfd){
    int idx = sockfd - 1000;
    if(idx<0 || idx>=MAX_KTP_SOCKETS){
        k_errno = EINVAL;
        return -1;
    }

    SharedMem *sm = get_shm();
    if(!sm){
        k_errno = ESHM;
        return -1;
    }

    sem_lock(idx);
    sm->sock[idx].close_requested = 1;
    sem_unlock(idx);
    shmdt(sm);
    return 0;
}
