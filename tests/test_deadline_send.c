#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include "deadline_send.h"
#include "esp_timer.h"
static void *reader(void *arg)
{
    int socket=*(int *)arg; char data[4096]; size_t total=0;
    while(total<1024*1024) {
        ssize_t n=recv(socket,data,sizeof(data),0); assert(n>0);
        for(ssize_t i=0;i<n;++i) assert(data[i]=='V');
        total+=n;
    }
    return NULL;
}
int main(void)
{
    signal(SIGPIPE,SIG_IGN);
    int pair[2]; assert(socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0);
    const char hello[]="native-video"; char copy[sizeof(hello)];
    assert(deadline_send(pair[0],hello,sizeof(hello),0,esp_timer_get_time()+100000)==sizeof(hello));
    assert(recv(pair[1],copy,sizeof(copy),0)==sizeof(copy) && !memcmp(hello,copy,sizeof(copy)));
    assert(deadline_send(pair[0],hello,sizeof(hello),0,esp_timer_get_time()-1)<0 && errno==ETIMEDOUT);
    assert(deadline_send(-1,hello,sizeof(hello),0,esp_timer_get_time()+1000)<0 && errno==EBADF);
    int small=4096; assert(setsockopt(pair[0],SOL_SOCKET,SO_SNDBUF,&small,sizeof(small))==0);
    char *large=malloc(1024*1024); assert(large); memset(large,'V',1024*1024);
    pthread_t consumer; assert(pthread_create(&consumer,NULL,reader,&pair[1])==0);
    size_t offset=0; int64_t deadline=esp_timer_get_time()+2000000;
    while(offset<1024*1024) {
        int n=deadline_send(pair[0],large+offset,1024*1024-offset,0,deadline); assert(n>0);offset+=n;
    }
    assert(pthread_join(consumer,NULL)==0);
    /* A receiver that stops reading must expire one budget across partial writes. */
    int64_t start=esp_timer_get_time(); deadline=start+50000; offset=0; int n;
    do { n=deadline_send(pair[0],large+offset,1024*1024-offset,0,deadline); if(n>0)offset+=n; } while(n>0 && offset<1024*1024);
    int64_t elapsed=esp_timer_get_time()-start;
    assert(n<0 && errno==ETIMEDOUT && offset<1024*1024 && elapsed>=50000 && elapsed<500000);
    free(large);close(pair[0]);close(pair[1]);
    puts("PASS: deadline socket sender: data integrity, partial writes, stalled receiver and absolute frame budget");
}
