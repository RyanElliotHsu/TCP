#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <assert.h>

#include"packet.h"
#include"common.h"

#define STDIN_FD    0
#define RETRY  120 //millisecond
//#define WINDOW_SIZE 10 //packets

int next_seqno=0;
int send_base=0;
// int window_size = 1;

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer;
tcp_packet *sndpkt;
tcp_packet *recvpkt;
sigset_t sigmask;

//tcp_packet *Sending[WINDOW_SIZE];

//counter latest sequence acknowledged
int last_seq_ack;
//counter for number of time the last sequence is acknowledged
int times_last_seq_ack = 0;

void resend_packets(int sig)
{
    if (sig == SIGALRM)
    {
        //Resend all packets range between
        //sendBase and nextSeqNum
        VLOG(INFO, "Timeout happend");
        if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0,
                    ( const struct sockaddr *)&serveraddr, serverlen) < 0)
        {
            error("sendto");
        }
    }
}


void start_timer()
{
    sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
    setitimer(ITIMER_REAL, &timer, NULL);
}


void stop_timer()
{
    sigprocmask(SIG_BLOCK, &sigmask, NULL);
}


/*
 * init_timer: Initialize timer
 * delay: delay in milliseconds
 * sig_handler: signal handler function for re-sending unACKed packets
 */
void init_timer(int delay, void (*sig_handler)(int))
{
    signal(SIGALRM, resend_packets);
    timer.it_interval.tv_sec = delay / 1000;    // sets an interval of the timer
    timer.it_interval.tv_usec = (delay % 1000) * 1000;
    timer.it_value.tv_sec = delay / 1000;       // sets an initial value
    timer.it_value.tv_usec = (delay % 1000) * 1000;

    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGALRM);
}


int main (int argc, char **argv)
{
    int portno, len;
    int next_seqno;
    char *hostname;
    char buffer[DATA_SIZE];
    FILE *fp;

    /* check command line arguments */
    if (argc != 4) {
        fprintf(stderr,"usage: %s <hostname> <port> <FILE>\n", argv[0]);
        exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);
    fp = fopen(argv[3], "r");
    if (fp == NULL) {
        error(argv[3]);
    }

    /* create structure to get file size and make packets */
    // struct stat filestat;
    // fstat(fp,&filestat);
    // int total_size = filestat.st_size;
    // printf("File Size = %d\n", size);
    // int total_num_packets = total_size/DATA_SIZE;
    // if (total_size%DATA_SIZE > 0)
    // {
    //     total_num_packets++;
    // }

    // array to store all packet
    // tcp_packet* all_packets[total_num_packets];
    // int last_packet_num_sent = 0;
    // int last_packet_num_ack = 0;


    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");


    /* initialize server server details */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serverlen = sizeof(serveraddr);

    /* covert host into network byte order */
    if (inet_aton(hostname, &serveraddr.sin_addr) == 0) {
        fprintf(stderr,"ERROR, invalid host %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(portno);

    assert(MSS_SIZE - TCP_HDR_SIZE > 0);

    //Stop and wait protocol

    init_timer(RETRY, resend_packets);
    next_seqno = 0;
    while (1)
    {
//        for (int i = 0; i < WINDOW_SIZE; ++i)
//        {
        len = fread(buffer, 1, DATA_SIZE, fp);
        if ( len <= 0)
        {
            VLOG(INFO, "End Of File has been reached");
            Sending[i] = make_packet(0);
            sendto(sockfd, sndpkt, TCP_HDR_SIZE,  0,
                    (const struct sockaddr *)&serveraddr, serverlen);
            break;
        }
        send_base = next_seqno;
        next_seqno = send_base + len;
        sndpkt = make_packet(len);
        memcpy(sndpkt->data, buffer, len);
        sndpkt->hdr.seqno = send_base;

        //Wait for ACK
        do {

            VLOG(DEBUG, "Sending packet %d to %s",
                    send_base, inet_ntoa(serveraddr.sin_addr));
            /*
             * If the sendto is called for the first time, the system will
             * will assign a random port number so that server can send its
             * response to the src port.
             */
            if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0,
                        ( const struct sockaddr *)&serveraddr, serverlen) < 0)
            {
                error("sendto");
            }

            start_timer();
            //ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
            //struct sockaddr *src_addr, socklen_t *addrlen);

            do
            {
                if(recvfrom(sockfd, buffer, MSS_SIZE, 0,
                            (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0)
                {
                    error("recvfrom");
                }

                recvpkt = (tcp_packet *)buffer;
                printf("%d \n", get_data_size(recvpkt));
                last_seq_ack = recvpkt->hdr.ackno - len; //the last sequence acked is one less than the sequence number in the acknowledgment num of the received packet
                times_last_seq_ack++;
                
                //if receiving three duplicate acks
                if (times_last_seq_ack>=3) {
                    resend_packets(SIGALRM);
                    times_last_seq_ack=0;
                    start_timer();
                }
                
                //if same sequence acked again -> increase counter
                if (recvpkt->hdr.ackno == last_seq_ack) {
                    times_last_seq_ack++;
                }
                
                assert(get_data_size(recvpkt) <= DATA_SIZE);
                
            }while(recvpkt->hdr.ackno < next_seqno);    //ignore duplicate ACKs
            
            stop_timer();
            
            /*resend pack if don't recv ACK */
            if (last_seq_ack + len < next_seqno ) {
                resend_packets(SIGALRM);
            }
            
        } while(recvpkt->hdr.ackno != next_seqno);


        free(sndpkt);
        //}
    }

    return 0;

}
