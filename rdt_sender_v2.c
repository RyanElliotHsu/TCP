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
#define WINDOW_SIZE 10

int next_seqno=0;
int send_base=0;

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer; 
tcp_packet *sndpkt;
tcp_packet *recvpkt;
sigset_t sigmask;

//array of packet pointers being sent
tcp_packet *window[WINDOW_SIZE];
//number of packets in window  
int pktsStored=0;

//check for EOF
int end_reached = 0;
    


void resend_packets(int sig)
{
    if (sig == SIGALRM)
    {
        //Resend all packets range between 
        //sendBase and nextSeqNum
        VLOG(INFO, "Timout happend");
        for (int i=0; i<WINDOW_SIZE; i++)
        {
            printf("Resending packet with sequence no %d\n", window[i]->hdr.seqno);
            if(sendto(sockfd, window[i], TCP_HDR_SIZE + get_data_size(window[i]), 0,
                    ( const struct sockaddr *)&serveraddr, serverlen) < 0)
            {
                error("sendto");
            }
        }
    }
    if (sig == 2) {
        //Resend all packets range between
        //sendBase and nextSeqNum
        VLOG(INFO, "Three Duplicate ACK received for sequence no %d\n", window[0]->hdr.seqno);
        int filled_window = (int)(sizeof(window)/sizeof(window[0]))
        for (int i=0; i<filled_window; i++)
        {
            printf("Resending packet with sequence no %d\n", window[i]->hdr.seqno);
            if(sendto(sockfd, window[i], TCP_HDR_SIZE + get_data_size(window[i]), 0,
                    ( const struct sockaddr *)&serveraddr, serverlen) < 0)
            {
                error("sendto");
            }
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

void add_packet(FILE *fp, int len)
{
    len = fread(buffer, 1, DATA_SIZE, fp);
    if (len <= 0) {
        end_reached = 1;
        break;
    } else {
        send_base = next_seqno;
        next_seqno = send_base + len;
        sndpkt = make_packet(len);
        memcpy(sndpkt->data, buffer, len);
        sndpkt->hdr.seqno = send_base;
    }
    window[pktsStored] = sndpkt;
    pktsStored++;
}

void send_packets()
{
    //get size of filled array - in the end window can not be fully filled
    //addition of packets in start will maintain 10 window size at all times when possible
    int filled_window = (int)(sizeof(window)/sizeof(window[0]))
    for (int i=0; i<filled_window; i++)
    {
        //check if not sent already - wont be needed but for extra security here
        if (window[i]->hdr.sent_flag==0)
        {
            if(sendto(sockfd, window[i], TCP_HDR_SIZE + get_data_size(window[i]), 0,
                    ( const struct sockaddr *)&serveraddr, serverlen) < 0)
            {
                error("sendto");
            }
            else{
                window[i]->hdr.sent_flag = 1;
                printf("Sending packet with sequence no %d\n", window[i]->hdr.seqno);
            }

            //timer started as soon as first packet in the window sent
            if (i==0){
                start_timer();
            }
        }
    }
}


int main (int argc, char **argv)
{
    int portno;
    int len = 1;
    int next_seqno;
    char *hostname;
    char buffer[DATA_SIZE];
    FILE *fp;
    int lastACKed=0;
    int ackCount;
    int deleted;
    

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
    
    while (1)
    {
        //print array
        for (int i=0; i<WINDOW_SIZE; i++) {
            if (window[i]!=NULL) {
                printf("%d |", window[i]->hdr.seqno);
            }
        }
        printf("\n");
        
        //fill the (array of packets sent) to (window size)
        while (pktsStored<=WINDOW_SIZE)
        {
            add_packet(fp,len);
        }
        
        //end program if EOF
        if (end_reached == 1) {
            sndpkt = make_packet(0);
            VLOG(INFO, "End Of File has been reached");
            sendto(sockfd, sndpkt, TCP_HDR_SIZE,  0,
                    (const struct sockaddr *)&serveraddr, serverlen);
            break;
        }
        
        //send all packets in array
        send_packets();
        
        //receive
        if(recvfrom(sockfd, buffer, MSS_SIZE, 0,
                            (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0)
                {
                    error("recvfrom");
                }
        recvpkt = (tcp_packet *)buffer;
        
        //recieve new ack
        if (recvpkt->hdr.ackno != lastACKed) //ackno is the next expected
        {
            ackCount = 1;
            lastACKed = recvpkt->hdr.ackno;
            printf("Acknowledgment upto sequence no %d\n", lastACKed);
            //remove all packets up to ACK received
            //could (maybe) make a seperate function for this
            //remember to free pkt pointers while removing them
            for (int i = 0; i < WINDOW_SIZE; i++)
            {
                if (window[i]->hdr.seqno < lastACKed)
                {
                    //decrease the num of packets stored
                    pktsStored--;
                }
            }
            //keep count of how many deleted
            deleted = WINDOW_SIZE-pktsStored;
            for (int i = 0; i < WINDOW_SIZE-deleted; i++)
            {
                //shift based on number of those deleted
                window[i] = window[i+deleted];
            }
            //refill the window
            for (int i = WINDOW_SIZE-deleted; i < WINDOW_SIZE; i++)
            {
                add_packet(fp,len);
            }
        }
        //increment count of lastest ACK if ACK is duplicate
        else if (recvpkt->hdr.ackno == lastACKed)
        {
            ackCount += 1;
            //three duplicate ACKS => fast retransmission
            if (ackCount>=3)
            {
                resend_packets(2);
            }
        }
        
        stop_timer();
        resend_packets(SIGALRM);
    }
    
    //free all pointers to packets
    for (int i=0; i<WINDOW_SIZE; i++)
    {
        if(window[i]!=NULL)
        {
            free(window[i]);
        }
    }

    return 0;
    
}

        
        
        
        
        
        
        
        
        
        
        

//            //if end of file reached, send pkt notifying receiver
//            if ( len <= 0)
//            {
//                VLOG(INFO, "End Of File has been reached");
//                sndpkt = make_packet(0);
//                sendto(sockfd, sndpkt, TCP_HDR_SIZE,  0,
//                        (const struct sockaddr *)&serveraddr, serverlen);
//                break;
//            }
//
//            //add packets to array
//            for (int i=pktsStored-1; i<WINDOW_SIZE; i++)
//            {
//                len = fread(buffer, 1, DATA_SIZE, fp);
//                send_base = next_seqno;
//                next_seqno = send_base + len;
//                sndpkt = make_packet(len);
//                memcpy(sndpkt->data, buffer, len);
//                sndpkt->hdr.seqno = send_base;
//                window[i] = sndpkt;
//            }
//        }
//
//        //send all packages in window that are not yet sent
//        for (int i=0; i<WINDOW_SIZE; i++)
//        {
//            if (window[i]->hdr.sent_flag==0)
//            {
//                if(sendto(sockfd, window[i], TCP_HDR_SIZE + get_data_size(window[i]), 0,
//                        ( const struct sockaddr *)&serveraddr, serverlen) < 0)
//                {
//                    error("sendto");
//                }
//                else{
//                    window[i]->hdr.sent_flag = 1;
//                }
//
//                if (i==0){
//                    start_timer();
//                }
//            }
//        }
//
//
//        //Wait for ACK
//        if(recvfrom(sockfd, buffer, MSS_SIZE, 0,
//                            (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0)
//                {
//                    error("recvfrom");
//                }
//        recvpkt = (tcp_packet *)buffer;
//
//
//        //increment count of lastest ACK if ACK is duplicate
//        if (recvpkt->hdr.ackno == lastACKed) //ackno is the next expected
//        {
//            ackCount += 1;
//
//            //three duplicate ACKS => fast retransmission
//            if (ackCount>=3)
//            {
//                fast_retransmission_resend();
//            }
//        }
//        //new ACK received
//        else
//        {
//            ackCount = 1;
//            lastACKed = recvpkt->hdr.ackno;
//            //remove all packets up to ACK received
//            //could (maybe) make a seperate function for this
//            //remember to free pkt pointers while removing them
//            for (int i = 0; i < WINDOW_SIZE; i++)
//            {
//                if (window[i]->hdr.seqno < lastACKed)
//                {
//                    free(window[i]);
//                    //decrease the num of packets stored
//                    pktsStored--;
//                }
//            }
//            //keep count of how many deleted
//            deleted = WINDOW_SIZE-pktsStored;
//            for (int i = 0; i < WINDOW_SIZE-deleted; i++)
//            {
//                //shift based on number of those deleted
//                window[i] = window[i+deleted];
//            }
//            for (int i = WINDOW_SIZE; i < WINDOW_SIZE-deleted; i--)
//            {
//                //free the shifted packets in the array
//                free(window[i]);
//            }
//        }
//
//        //not sure of placement
//        stop_timer();
//        resend_packets(SIGALRM);
//
//
//
//        // do {
//
//        //     VLOG(DEBUG, "Sending packet %d to %s",
//        //             send_base, inet_ntoa(serveraddr.sin_addr));
//        //     /*
//        //      * If the sendto is called for the first time, the system will
//        //      * will assign a random port number so that server can send its
//        //      * response to the src port.
//        //      */
//        //     if(sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0,
//        //                 ( const struct sockaddr *)&serveraddr, serverlen) < 0)
//        //     {
//        //         error("sendto");
//        //     }
//
//        //     start_timer();
//        //     //ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
//        //     //struct sockaddr *src_addr, socklen_t *addrlen);
//
//        //     do
//        //     {
//        //         if(recvfrom(sockfd, buffer, MSS_SIZE, 0,
//        //                     (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0)
//        //         {
//        //             error("recvfrom");
//        //         }
//
//        //         recvpkt = (tcp_packet *)buffer;
//        //         printf("%d \n", get_data_size(recvpkt));
//        //         assert(get_data_size(recvpkt) <= DATA_SIZE);
//        //     }while(recvpkt->hdr.ackno < next_seqno);    //ignore duplicate ACKs
//        //     stop_timer();   //triggers timer which calls resend function
//        //     /*resend pack if don't recv ACK */
//        // } while(recvpkt->hdr.ackno != next_seqno);
//
//        //free(sndpkt);
//    }
//
//    //free all pointers to packets
//    for (int i=0; i<WINDOW_SIZE; i++)
//    {
//        if(window[i]!=NULL)
//        {
//            free(window[i]);
//        }
//    }
//
//    return 0;
//
//}



