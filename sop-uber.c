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

#define MSG_DRIVER_MAX_NAME 64
#define MSG_DRIVER_NAME "uber_results_"
#define MSG_SIZE_DRIVER sizeof(mess_drive)
#define MSG_MAX_DRIVER 10

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

typedef struct mess_drive 
{
    unsigned int length;
    pid_t pid;
} mess_drive;

typedef struct mq_data
{
    mqd_t mqd;
    char name[MSG_DRIVER_MAX_NAME];
} mq_data;

volatile sig_atomic_t end = 0;

void last_signal(int signo) { end = signo; }

int set_handler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        ERR("sigaction");
    return 0;
}

void usage(const char* name)
{
    fprintf(stderr, "USAGE: %s N T\n", name);
    fprintf(stderr, "N: 1 <= N - number of drivers\n");
    fprintf(stderr, "T: 5 <= T - simulation duration\n");
    exit(EXIT_FAILURE);
}

void driver_change_queue_attr(mqd_t mqd)
{
    struct mq_attr attr;
    memset(&attr, 0, sizeof(attr));
    if(mq_setattr(mqd, &attr, NULL) < 0)
        ERR("mq_setattr");
}

void mq_data_getname(pid_t pid, mq_data *data)
{
    snprintf(data->name, MSG_DRIVER_MAX_NAME,"/%s%d", MSG_DRIVER_NAME, pid);
}

mqd_t driver_open_queue(pid_t pid, int nonblock) 
{
    char buf[MSG_DRIVER_MAX_NAME];
    snprintf(buf, MSG_DRIVER_MAX_NAME,"/%s%d", MSG_DRIVER_NAME, pid);

    struct mq_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.mq_msgsize = MSG_SIZE_DRIVER;
    attr.mq_maxmsg = MSG_MAX_DRIVER;

    mqd_t mqd;
    if((mqd = mq_open(buf, O_RDWR | O_CREAT | nonblock, 0666, &attr)) < 0)
        ERR("mq_open");
    return mqd; 
} 

void driver_job(mqd_t mqd, mqd_t mqd_driver)
{
    pos p;
    mess_pos pos_;
    mess_drive drive;
    drive.pid = getpid();
    
    unsigned int prio;

    unsigned long mstime;
    struct timespec time;
    while(1) 
    {
        if(mq_receive(mqd, (char*) &pos_, MSG_SIZE, &prio) < 0)
            ERR("mq_receive");
        if(prio)
            break;

        p = pos_.s;
        printf("[%d] Read task: (%d, %d) -> (%d, %d)\n", getpid(), pos_.s.x, pos_.s.y, pos_.f.x, pos_.f.y);
        
        drive.length = abs(pos_.f.x - pos_.s.x) + abs(pos_.f.y - pos_.s.y);
        mstime = abs(pos_.s.x - p.x) + abs(pos_.s.y - p.y);
        mstime += drive.length; 
        
        time.tv_sec = mstime / 1000000;
        time.tv_nsec = (mstime % 1000000) * 1000000;
        nanosleep(&time, NULL);

        // send results to server
        if(mq_send(mqd_driver, (char*) &drive, MSG_SIZE_DRIVER, 0) < 0)
            ERR("mq_send");
        p = pos_.f;
    }
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

void server_comp_course(sigval_t data)
{
    mqd_t mqd = data.sival_int;
    
    struct sigevent sigev;
    memset(&sigev, 0, sizeof(sigev));
    sigev.sigev_notify = SIGEV_THREAD;
    sigev.sigev_notify_function = server_comp_course;
    sigev.sigev_value.sival_int = mqd;
    if(mq_notify(mqd, &sigev) < 0)
        ERR("mq_notify");
   
    mess_drive drive;
    while(1)
    {
        if(mq_receive(mqd, (char*) &drive, MSG_SIZE_DRIVER, NULL) < 0)
        {
            if(errno != EAGAIN)
                ERR("mq_receive");
            break;
        }
        printf("The driver [%d] drove a distance of [%d].\n", drive.pid, drive.length);
    }
}

void create_drivers(int n, mqd_t mqd, mq_data *data)
{
    pid_t pid;
    mqd_t mqd_driver;
    while(n-- > 0)
    {
        switch((pid = fork()))
        {
            case 0:
                free(data);
                mqd_driver = driver_open_queue(getpid(), 0);
                driver_change_queue_attr(mqd);
                driver_job(mqd, mqd_driver);
                if(mq_close(mqd) < 0)
                    ERR("mq_close");
                if(mq_close(mqd_driver) < 0)
                    ERR("mq_close");
                exit(EXIT_SUCCESS);
            case -1:
                ERR("fork");
            default:
                mq_data_getname(pid, &data[n]);
                data[n].mqd = driver_open_queue(pid, O_NONBLOCK);

                // set notification
                struct sigevent sigev;
                memset(&sigev, 0, sizeof(sigev));
                sigev.sigev_notify = SIGEV_THREAD;
                sigev.sigev_notify_function = server_comp_course;
                sigev.sigev_value.sival_int = data[n].mqd;
                if(mq_notify(data[n].mqd, &sigev) < 0)
                    ERR("mq_notify");
                break;
        }
    }
}

void server_job(mqd_t mqd)
{
    mess_pos pos_;

    unsigned long mstime;
    struct timespec time;
    while(!end)
    {
        // printf("Sending message...\n");
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
                fprintf(stderr, "[Server] Queue full. Dropping drive...\n");
                continue;
            }
            ERR("mq_send");
        }
    }
}

int main(int argc, char* argv[])
{
    if(argc != 3)
        usage(argv[0]);
    int n = atoi(argv[1]);
    if(n < 1)
        usage(argv[0]);
    int t = atoi(argv[2]);
    if(t < 1)
        usage(argv[0]);

    mq_data *data = malloc(sizeof(mq_data)*n);
    if(!data)
        ERR("malloc");

    set_handler(last_signal, SIGALRM);
    alarm(t);

    mqd_t mqd = create_queue();
    create_drivers(n, mqd, data);
    server_job(mqd);

    // send message to end job
    mess_drive drive = { .pid = 0, .length = 0 };
    for(int i = 0; i < n; ++i)
        if(mq_send(mqd, (char*) &drive, MSG_SIZE_DRIVER, 1) < 0)
            ERR("mq_send");

    while(wait(NULL) > 0);
    for(int i = 0; i < n; ++i)
    {
        if(mq_close(data[i].mqd) < 0)
            ERR("mq_close");
        if(mq_unlink(data[i].name) < 0)
            ERR("mq_unlink");
    }
    if(mq_close(mqd) < 0)
        ERR("mq_close");
    if(mq_unlink(MQ_NAME) < 0)
        ERR("mq_unlink");
    free(data);
    return EXIT_SUCCESS;
}
