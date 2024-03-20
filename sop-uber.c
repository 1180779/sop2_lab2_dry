#include <asm-generic/errno-base.h>
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <mqueue.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

#define MQ_NAME "/uber_tasks"

#define TIME_MIN 500
#define TIME_MAX 2000

#define XMIN -1000
#define XMAX 1000

#define MSG_SIZE sizeof(mess_pos)
#define MSG_MAX 10

typedef struct pos
{
    int x;
    int y;
} pos;

typedef struct mess_pos
{
    pos s;
    pos f;
} mess_pos;

void usage(const char* name)
{
    fprintf(stderr, "USAGE: %s N T\n", name);
    fprintf(stderr, "N: 1 <= N - number of drivers\n");
    fprintf(stderr, "T: 5 <= T - simulation duration\n");
    exit(EXIT_FAILURE);
}

void driver_job(mqd_t mqd)
{
    pos p;
    mess_pos pos_;

    unsigned long mstime;
    struct timespec time;
    while(1) 
    {
        if(mq_receive(mqd, (char*) &pos_, MSG_SIZE, NULL) < 0)
        {
            if(errno == EAGAIN)
            {
                sleep(1);
                continue;
            }
            ERR("mq_receive");
        }
        p = pos_.s;
        printf("[%d] Read task: (%d, %d) -> (%d, %d)\n", getpid(), pos_.s.x, pos_.s.y, pos_.f.x, pos_.f.y);
        
        mstime = abs(pos_.s.x - p.x) + abs(pos_.s.y - p.y);
        mstime += abs(pos_.f.x - pos_.s.x) + abs(pos_.f.y - pos_.s.y);
        
        time.tv_sec = mstime / 1000000;
        time.tv_nsec = (mstime % 1000000) * 1000000;
        nanosleep(&time, NULL);
    }
    
    nanosleep(&time, NULL);
    p = pos_.f;
}

mqd_t create_queue() 
{
    struct mq_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.mq_msgsize = MSG_SIZE;
    attr.mq_maxmsg = MSG_MAX;

    mqd_t mqd;
    mq_unlink(MQ_NAME);
    if((mqd = mq_open(MQ_NAME, O_RDWR | O_CREAT | O_NONBLOCK, 0666, &attr)) < 0)
        ERR("mq_open");
    return mqd;
}

void create_drivers(int n, mqd_t mqd)
{
    while(n-- > 0)
    {
        switch(fork())
        {
            case 0:
                driver_job(mqd);
                mq_close(mqd);
                exit(EXIT_SUCCESS);
            case -1:
                ERR("fork");
            default:
                // parent
                break;
        }
    }
}

void server_job(mqd_t mqd)
{
    mess_pos pos_;

    unsigned long mstime;
    struct timespec time;
    while(1)
    {
        // printf("[Server] Sleeping...\n");

        mstime = rand() % (TIME_MAX - TIME_MIN + 1) + TIME_MIN;
        time.tv_sec = 0;
        time.tv_nsec = mstime * 1000000;
        nanosleep(&time, NULL);

        // printf("[Server] Sending order coordiates...\n");
        pos_.s.x = rand() % (XMAX - XMIN + 1) + XMIN;
        pos_.s.y = rand() % (XMAX - XMIN + 1) + XMIN;
        pos_.f.x = rand() % (XMAX - XMIN + 1) + XMIN;
        pos_.f.y = rand() % (XMAX - XMIN + 1) + XMIN;
        if(mq_send(mqd, (char*) &pos_, sizeof(pos_), 0) < 0)
        {
            if(errno == EAGAIN)
            {
                fprintf(stderr, "[Server] Queue full. Waiting...\n");
                continue;
            }
            ERR("mq_send");
        }
    }
}

int main(int argc, char* argv[])
{
    if(argc != 2)
        usage(argv[0]);
    int n = atoi(argv[1]);
    if(n < 1)
        usage(argv[0]);

    mqd_t mqd = create_queue();
    create_drivers(n, mqd);
    server_job(mqd);

    while(wait(NULL) > 0);
    return EXIT_SUCCESS;
}
