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
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

SharedMem *sm = NULL;
int shmid = -1;
int semid_global = -1;
int send_count = 0;

int seq_in_window(uint8_t seq, uint8_t base, int size){
    for(int i=0; i<size; i++) {
        uint8_t w = (base-1+i)%MAX_SEQ + 1;
        if(w==seq) return 1;
    }
    return 0;
}

void* thread_R(void *arg){
    (void)arg;
    srand((unsigned)time(NULL));

    while(1){
        // handling pending bind/close requests from user processes
        for(int i=0; i<MAX_KTP_SOCKETS; i++){
            sem_lock(i);
            if(!sm->sock[i].is_free && sm->sock[i].bind_requested){
                int fd = socket(AF_INET, SOCK_DGRAM, 0);
                if(fd>=0 && bind(fd, (struct sockaddr*)&sm->sock[i].src_addr, sizeof(struct sockaddr_in))==0){
                    sm->sock[i].udp_sockfd = fd;
                    sm->sock[i].bound = 1;
                } else {
                    if(fd>=0) close(fd);
                }
                sm->sock[i].bind_requested = 0;
            }
            if(!sm->sock[i].is_free && sm->sock[i].close_requested){
                close(sm->sock[i].udp_sockfd);
                sm->sock[i].is_free = 1;
                sm->sock[i].pid = 0;
                sm->sock[i].bound = 0;
                sm->sock[i].close_requested = 0;
            }
            sem_unlock(i);
        }

        fd_set rset;
        FD_ZERO(&rset);
        int maxfd = -1;
        for(int i=0; i<MAX_KTP_SOCKETS; i++){
            sem_lock(i);
            if(!sm->sock[i].is_free && sm->sock[i].bound){
                FD_SET(sm->sock[i].udp_sockfd, &rset);
                if(sm->sock[i].udp_sockfd>maxfd) maxfd = sm->sock[i].udp_sockfd;
            }
            sem_unlock(i);
        }

        struct timeval tv = {T/2, (T%2)*500000};
        int nready = 0;
        if(maxfd>=0) nready = select(maxfd+1, &rset, NULL, NULL, &tv);

        if(nready<0 && errno!=EINTR) continue;

        if(nready==0){
            for(int i=0; i<MAX_KTP_SOCKETS; i++){
                sem_lock(i);
                if(!sm->sock[i].is_free && sm->sock[i].bound && sm->sock[i].nospace){
                    int free_slots = RECV_BUF_SIZE - sm->sock[i].recv_buf_count;
                    if(free_slots>0){
                        sm->sock[i].rwnd.size = free_slots;
                        // handling the bug highlighted in the assignment
                        // details mentioned in documentation.txt
                        KTPMessage ack;
                        ack.hdr.type = KTP_DUPACK;
                        ack.hdr.seq = sm->sock[i].last_ack_seq;
                        ack.hdr.rwnd = free_slots;
                        sendto(sm->sock[i].udp_sockfd, &ack, sizeof(KTPHeader), 0,
                               (struct sockaddr*)&sm->sock[i].dst_addr,
                               sizeof(struct sockaddr_in));
                    }
                }
                sem_unlock(i);
            }
            continue;
        }

        for(int i=0; i<MAX_KTP_SOCKETS && nready>0; i++){
            sem_lock(i);
            int fd = sm->sock[i].udp_sockfd;
            if(sm->sock[i].is_free || !sm->sock[i].bound || !FD_ISSET(fd, &rset)){
                sem_unlock(i);
                continue;
            }
            sem_unlock(i);
            nready--;

            KTPMessage msg;
            struct sockaddr_in from_addr;
            socklen_t from_len = sizeof(from_addr);
            ssize_t n = recvfrom(fd, &msg, sizeof(msg), 0,
                                 (struct sockaddr*)&from_addr, &from_len);
            if(n<(ssize_t)sizeof(KTPHeader)) continue;

            if(dropMessage(DROP_PROB)){
                continue;
            }

            sem_lock(i);

            if(msg.hdr.type==KTP_DATA){
                uint8_t seq = msg.hdr.seq;
                uint8_t base = sm->sock[i].last_ack_seq%MAX_SEQ + 1;
                int wnd_size = sm->sock[i].rwnd.size;

                // checking for duplicate
                int dup = 0;
                for(int j=0; j<RECV_BUF_SIZE; j++) {
                    if(sm->sock[i].recv_buf_valid[j] && sm->sock[i].recv_buf_seq[j]==seq) {
                        dup = 1; break;
                    }
                }
                // also checking if seq is behind base (already consumed)
                int diff = seq-base;
                if(diff<0) diff += MAX_SEQ;
                if(diff>=(MAX_SEQ/2)) dup = 1;
                // Since, in selective repeat ARQ, the window can be at most MAX_SEQ/2, if seq is >= base+MAX_SEQ/2, it means it's behind base

                if(!dup && seq_in_window(seq, base, wnd_size) && sm->sock[i].recv_buf_count<RECV_BUF_SIZE){
                    for(int j=0; j<RECV_BUF_SIZE; j++){
                        if(!sm->sock[i].recv_buf_valid[j]){
                            memcpy(sm->sock[i].recv_buf[j], msg.data, MSG_SIZE);
                            sm->sock[i].recv_buf_valid[j] = 1;
                            sm->sock[i].recv_buf_seq[j] = seq;
                            sm->sock[i].recv_buf_count++;
                            break;
                        }
                    }

                    int free_slots = RECV_BUF_SIZE - sm->sock[i].recv_buf_count;
                    sm->sock[i].rwnd.size = free_slots;
                    sm->sock[i].nospace = (free_slots==0) ? 1 : 0;

                    // recomputing last_ack_seq
                    uint8_t cur = sm->sock[i].last_ack_seq;
                    while(1){
                        uint8_t nxt = cur%MAX_SEQ + 1;
                        int found = 0;
                        for(int j=0; j<RECV_BUF_SIZE; j++){
                            if(sm->sock[i].recv_buf_valid[j] && sm->sock[i].recv_buf_seq[j]==nxt){
                                found = 1;
                                break;
                            }
                        }
                        if(!found) break;
                        cur = nxt;
                    }
                    sm->sock[i].last_ack_seq = cur;

                    // ACKing only if the msg is in order
                    if(seq==base){
                        KTPMessage ack;
                        memset(&ack, 0, sizeof(ack));
                        ack.hdr.type = KTP_ACK;
                        ack.hdr.seq = sm->sock[i].last_ack_seq;
                        ack.hdr.rwnd = free_slots;
                        sendto(fd, &ack, sizeof(KTPHeader), 0,
                               (struct sockaddr*)&sm->sock[i].dst_addr,
                               sizeof(struct sockaddr_in));
                    }
                }
                else if(dup){
                    KTPMessage ack;
                    memset(&ack, 0, sizeof(ack));
                    ack.hdr.type = KTP_ACK;
                    ack.hdr.seq = sm->sock[i].last_ack_seq;
                    ack.hdr.rwnd = sm->sock[i].rwnd.size;
                    sendto(fd, &ack, sizeof(KTPHeader), 0,
                           (struct sockaddr*)&sm->sock[i].dst_addr,
                           sizeof(struct sockaddr_in));
                }
            }
            else if(msg.hdr.type==KTP_ACK){
                uint8_t ack_seq = msg.hdr.seq;
                uint8_t new_rwnd = msg.hdr.rwnd;

                int removed = 0;
                int new_count = 0;
                uint8_t new_seqs[RECV_BUF_SIZE];
                for(int j=0; j<sm->sock[i].swnd.count; j++){
                    uint8_t s = sm->sock[i].swnd.seq[j];
                    int diff2 = ack_seq-s;
                    if(diff2<0) diff2 += MAX_SEQ;
                    if(diff2>=(MAX_SEQ/2)) new_seqs[new_count++] = s;
                    else removed++;
                }
                sm->sock[i].swnd.count = new_count;
                memcpy(sm->sock[i].swnd.seq, new_seqs, new_count*sizeof(uint8_t));
                sm->sock[i].send_buf_head = (sm->sock[i].send_buf_head + removed) % SEND_BUF_SIZE;
                sm->sock[i].send_buf_count -= removed;
                if(sm->sock[i].send_buf_count<0) sm->sock[i].send_buf_count = 0;
                sm->sock[i].swnd.size = new_rwnd;
            }
            else if(msg.hdr.type==KTP_DUPACK){
                sm->sock[i].swnd.size = msg.hdr.rwnd;
            }

            sem_unlock(i);
        }
    }
    return NULL;
}

void* thread_S(void *arg){
    (void)arg;

    while(1){
        usleep(T*500000); // sleep for T/2 seconds
        time_t now = time(NULL);

        for(int i=0; i<MAX_KTP_SOCKETS; i++){
            sem_lock(i);
            if(sm->sock[i].is_free || !sm->sock[i].bound){
                sem_unlock(i);
                continue;
            }

            // retransmiting all unacked msgs if timeout
            if(sm->sock[i].swnd.count>0 && sm->sock[i].swnd.last_send_time>0 && (now - sm->sock[i].swnd.last_send_time)>=T){
                for(int j=0; j<sm->sock[i].swnd.count; j++) {
                    int buf_idx = (sm->sock[i].send_buf_head + j) % SEND_BUF_SIZE;
                    KTPMessage msg;
                    msg.hdr.type = KTP_DATA;
                    msg.hdr.seq = sm->sock[i].swnd.seq[j];
                    msg.hdr.rwnd = 0;
                    memcpy(msg.data, sm->sock[i].send_buf[buf_idx], MSG_SIZE);
                    sendto(sm->sock[i].udp_sockfd, &msg, sizeof(msg), 0,
                           (struct sockaddr *)&sm->sock[i].dst_addr,
                           sizeof(struct sockaddr_in));
                    send_count++;
                }
                sm->sock[i].swnd.last_send_time = now;
            }

            // sending new msgs from send buffer if window has room
            while(sm->sock[i].swnd.count < sm->sock[i].swnd.size && sm->sock[i].send_buf_count > sm->sock[i].swnd.count){
                int buf_idx = (sm->sock[i].send_buf_head + sm->sock[i].swnd.count) % SEND_BUF_SIZE;
                KTPMessage msg;
                msg.hdr.type = KTP_DATA;
                msg.hdr.seq = sm->sock[i].next_seq;
                msg.hdr.rwnd = 0;
                memcpy(msg.data, sm->sock[i].send_buf[buf_idx], MSG_SIZE);
                sendto(sm->sock[i].udp_sockfd, &msg, sizeof(msg), 0,
                       (struct sockaddr *)&sm->sock[i].dst_addr,
                       sizeof(struct sockaddr_in));
                send_count++;
                sm->sock[i].swnd.seq[sm->sock[i].swnd.count] = sm->sock[i].next_seq;
                sm->sock[i].swnd.count++;
                sm->sock[i].next_seq = sm->sock[i].next_seq%MAX_SEQ + 1;
                sm->sock[i].swnd.last_send_time = now;
            }

            sem_unlock(i);
        }
    }
    return NULL;
}

void garbage_collector(){
    while(1){
        sleep(5);
        for(int i=0; i<MAX_KTP_SOCKETS; i++){
            sem_lock(i);
            if(!sm->sock[i].is_free && sm->sock[i].pid>0){
                if(kill(sm->sock[i].pid, 0)<0 && errno==ESRCH){
                    // process died without calling k_close, let thread_R clean up the fd
                    sm->sock[i].close_requested = 1;
                }
            }
            sem_unlock(i);
        }
    }
}

void cleanup(int sig){
    (void)sig;
    if(shmid>=0) shmctl(shmid, IPC_RMID, NULL);
    if(semid_global>=0) semctl(semid_global, 0, IPC_RMID);
    printf("initksocket exiting. Total msgs sent: %d\n", send_count);
    exit(0);
}

int main() {
    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);

    shmid = shmget(ftok("/home", 1), sizeof(SharedMem), IPC_CREAT|0666);
    if(shmid<0){
        perror("shmget");
        exit(1);
    }

    sm = (SharedMem *)shmat(shmid, NULL, 0);
    if(sm==(void *)-1){
        perror("shmat");
        exit(1);
    }
    memset(sm, 0, sizeof(SharedMem));

    semid_global = semget(ftok("/home", 2), MAX_KTP_SOCKETS, IPC_CREAT|0666);
    if(semid_global<0){
        perror("semget");
        exit(1);
    }

    for(int i=0; i<MAX_KTP_SOCKETS; i++) {
        semctl(semid_global, i, SETVAL, 1);
        sm->sock[i].is_free = 1;
    }

    pid_t gc_pid = fork();
    if(gc_pid<0){
        perror("fork");
        exit(1);
    }
    if(gc_pid==0) {
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        sm = (SharedMem*)shmat(shmid, NULL, 0);
        garbage_collector();
        exit(0);
    }

    pthread_t tid_R, tid_S;
    pthread_create(&tid_R, NULL, thread_R, NULL);
    pthread_create(&tid_S, NULL, thread_S, NULL);

    printf("initksocket running (shmid=%d, semid=%d). Ctrl+C to stop.\n", shmid, semid_global);

    pthread_join(tid_R, NULL);
    pthread_join(tid_S, NULL);

    waitpid(gc_pid, NULL, 0);
    cleanup(0);
    return 0;
}
