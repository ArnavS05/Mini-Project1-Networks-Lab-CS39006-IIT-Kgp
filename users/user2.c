/*
=====================================
Mini Project 1 Submission
Group Details:
Member 1 Name: Arnav Singh
Member 1 Roll number: 23CS30009
=====================================
*/

// usage: ./user2 <src_ip> <src_port> <dst_ip> <dst_port> <output_file>

#include "ksocket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#define IDLE_TIMEOUT (12*T) 

int main(int argc, char *argv[]){
    if(argc!=6){
        fprintf(stderr, "Usage: %s <src_ip> <src_port> <dst_ip> <dst_port> <outfile>\n", argv[0]);
        return 1;
    }

    char *src_ip = argv[1];
    int src_port = atoi(argv[2]);
    char *dst_ip = argv[3];
    int dst_port = atoi(argv[4]);
    char *outfname = argv[5];

    FILE *fp = fopen(outfname, "wb");
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
    time_t last_recv = time(NULL);

    while(1){
        int n = k_recvfrom(sock, buf, MSG_SIZE);
        if(n>0){
            fwrite(buf, 1, n, fp);
            total_chunks++;
            printf("received chunk %d\n", total_chunks);
            last_recv = time(NULL);
        }
        else{
            if((time(NULL) - last_recv)>=IDLE_TIMEOUT) break;
            usleep(10000);
        }
    }

    printf("user2: received %d chunks, written to '%s'\n", total_chunks, outfname);
    fclose(fp);
    k_close(sock);
    return 0;
}
