/*
=====================================
Mini Project 1 Submission
Group Details:
Member 1 Name: Arnav Singh
Member 1 Roll number: 23CS30009
=====================================
*/

// usage: ./user1 <src_ip> <src_port> <dst_ip> <dst_port> <filename>

#include "ksocket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[]){
    if(argc!=6){
        fprintf(stderr, "Usage: %s <src_ip> <src_port> <dst_ip> <dst_port> <file>\n", argv[0]);
        return 1;
    }

    char *src_ip = argv[1];
    int src_port = atoi(argv[2]);
    char *dst_ip = argv[3];
    int dst_port = atoi(argv[4]);
    char *fname = argv[5];

    FILE *fp = fopen(fname, "rb");
    if(!fp){
        perror("fopen");
        return 1;
    }

    int sock = k_socket(AF_INET, SOCK_KTP, 0);
    if(sock<0){
        fprintf(stderr, "k_socket failed: k_errno=%d\n", k_errno);
        return 1;
    }

    if(k_bind(sock, src_ip, src_port, dst_ip, dst_port)<0) {
        fprintf(stderr, "k_bind failed: k_errno=%d\n", k_errno);
        k_close(sock);
        return 1;
    }

    char buf[MSG_SIZE];
    int total_chunks = 0;
    int ret;

    while(1){
        memset(buf, 0, MSG_SIZE);
        size_t n = fread(buf, 1, MSG_SIZE, fp);
        if(n==0) break;

        while((ret = k_sendto(sock, buf, MSG_SIZE))<0){
            if(k_errno==ENOSPACE){
                usleep(50000);
            }
            else{
                fprintf(stderr, "k_sendto error: k_errno=%d\n", k_errno);
                fclose(fp);
                k_close(sock);
                return 1;
            }
        }
        total_chunks++;
        printf("sent chunk %d\n", total_chunks);
    }

    printf("user1: sent %d chunks from '%s'\n", total_chunks, fname);
    fclose(fp);
    // waiting until thread_S has sent everything and all ACKs are received
    while(k_pending(sock) > 0)
        sleep(1);
    sleep(T + 2); // extra margin for the final ACK round-trip
    k_close(sock);
    return 0;
}
