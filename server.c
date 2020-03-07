#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <termios.h>
#include <pthread.h>
#include <sys/ipc.h>

#define SERVPORT 28335
#define STATUS_CLIENT_IDLE 0
#define STATUS_CLIENT_SEND 1
#define STATUS_CLIENT_WORK 2 

#define RECEVE_MAX_COUNT 32*64//最大缓存2048条消息

typedef struct
{
    int status;//online or offline
    int sfd;
    char client_id[17];
    struct sockaddr_in sockaddr;
}_client_addr;

typedef struct
{
    int status;//send data or idle
    int dest_fd;
    char dest_id[17];
    char source_id[17];
    char messages[1024];
}_msg_info;

_client_addr client_addr[32] = {0};//最大连接32个客户端
_msg_info msg_info[RECEVE_MAX_COUNT] = {0};//最大缓存消息条数

char buffer[2048] = {0};
int mGetClientId(char *buffer, char *sourceid);

int mGetClientId(char *buffer, char *sourceid)
{
    int i = 0;
    int j = 0;
    while(1)
    {
        if((sourceid[i] == '#') && (sourceid[i + 1] == '@'))
        {
            break;
        }
        buffer[j] = sourceid[i];
        j++;
        i++;
    }
    return 1;
}

void thread_process_read(void *arg)
{
    int dest_count = 0;
    int client_count = 0;
    int source_count = 0;
    fd_set ser_fdset;   
    struct timeval tv = {1, 0};//1s timeout
    int fd = *((int *)arg);
    int max_fd = 1;
    int ret = 0;

    while(1)
    { 
        FD_ZERO(&ser_fdset); 
        if(max_fd < 0)
        {
            max_fd = 0;
        }	

        //add client
        for(client_count = 0; client_count < 32; client_count++)  //用数组定义多个客户端fd
        {
            if(client_addr[client_count].status == STATUS_CLIENT_WORK)
            {
                FD_SET(client_addr[client_count].sfd,&ser_fdset);
                if(max_fd < client_addr[client_count].sfd)
                {
                    max_fd = client_addr[client_count].sfd;
                }
            }
        }
		
        ret = select(max_fd + 1, &ser_fdset, NULL, NULL, &tv);
        if(ret == 0)
        {
            continue;
        }
		
        for(client_count = 0; client_count < 32; client_count++)
        {
            if(FD_ISSET(client_addr[client_count].sfd, &ser_fdset))//当前是否在集合中
            {
                memset(buffer, 0, sizeof(buffer));
                ret = recv(client_addr[client_count].sfd, buffer, 2048, 0);
                if(ret == 0 || (ret < 0) && (errno == ECONNRESET))//error
                {
                    //client offline
                    //printf("remove offline client fd:%d--->client id:%s\r\n", client_addr[client_count].sfd, client_addr[client_count].client_id);
                    close(client_addr[client_count].sfd);
                    FD_CLR(client_addr[client_count].sfd, &ser_fdset);
                    client_addr[client_count].status == STATUS_CLIENT_IDLE;
                    memset(client_addr[client_count].client_id, 0, sizeof(client_addr[client_count].client_id));
                    client_addr[client_count].sfd = 0;
                }
                else if(strstr((char *)buffer, "test") != 0)
                {
                    printf("receved test msg:%s\r\n", buffer);
                    send(client_addr[client_count].sfd, buffer, strlen(buffer), 0);
                }
                else if(strstr((char *)buffer, "ees#@setid#@") != 0)//set client id
                {
                    //ees#@setid#@source_id#@end
                    memset(client_addr[client_count].client_id, 0, sizeof(client_addr[client_count].client_id));
                    mGetClientId(client_addr[client_count].client_id, &buffer[strlen("ees#@setid#@")]);
                    //strncpy(client_addr[client_count].client_id, &buffer[strlen("ees#@setid#@")], 16);
                    //printf("receved setid:%s\r\n", client_addr[client_count].client_id);
                    //ees#@getid#@source_id#@end
                    memset(buffer, 0, sizeof(buffer));
                    strcpy(buffer, "ees#@getid#@");
                    strcat(buffer, client_addr[client_count].client_id);
                    strcat(buffer, "#@ok#@end");
                    send(client_addr[client_count].sfd, buffer, strlen(buffer), 0);
                }
                else if(strstr((char *)buffer, "ees#@send#@") != 0)
                {
                    //ees#@send#@dest_id#@message#@end
                    for(source_count = 0; source_count < RECEVE_MAX_COUNT; source_count++)//把数据放在空闲的条目中准备发送
                    {
                        if(msg_info[source_count].status == STATUS_CLIENT_IDLE)//判断当前条目是否为空
                        {
                            //准备数据
                            memset(msg_info[source_count].dest_id, 0, sizeof(msg_info[source_count].dest_id));
                            memset(msg_info[source_count].source_id, 0, sizeof(msg_info[source_count].source_id));
                            memset(msg_info[source_count].messages, 0, sizeof(msg_info[source_count].messages));

                            //strncpy(msg_info[source_count].dest_id, &buffer[strlen("ees#@send#@")], 16);
                            mGetClientId(msg_info[source_count].dest_id, &buffer[strlen("ees#@send#@")]);
                            //printf("receved dest id:%s\r\n", msg_info[source_count].dest_id);
                            strcpy(msg_info[source_count].source_id, client_addr[client_count].client_id);
                            //printf("receved source id:%s\r\n", msg_info[source_count].source_id);
                            strcpy(msg_info[source_count].messages, &buffer[strlen("ees#@send#@") + strlen(msg_info[source_count].dest_id) + 2]);
                            //printf("receved msg:%s\r\n", msg_info[source_count].messages);
                            //ees#@recvmsg#@source_id#@dest_id#@ok#@end
                            memset(buffer, 0, sizeof(buffer));
                            strcpy(buffer, "ees#@recvmsg#@");
                            strcat(buffer, msg_info[source_count].source_id);
                            strcat(buffer, "#@");
                            strcat(buffer, msg_info[source_count].dest_id);
                            strcat(buffer, "#@ok#@end");
                            send(client_addr[client_count].sfd, buffer, strlen(buffer), 0);
                            msg_info[source_count].status = STATUS_CLIENT_SEND;
                            break;
                        }
                    }
                    //if(source_count == RECEVE_MAX_COUNT)
                    //{
                        //缓存条目已满，无法存储更多消息
                        //printf("msg buffer full!\r\n");
                    //}
                }
            }
        }		
    }
}

void thread_process_write(void *arg)
{
    int dest_count = 0;
    int client_count = 0;
    int fd = *((int *)arg);
    int ret = 0;

    while(1)
    { 
        for(client_count = 0; client_count < RECEVE_MAX_COUNT; client_count++)
        {
            if(msg_info[client_count].status == STATUS_CLIENT_SEND)//判断是否有数据需要发送
            {
                //printf("dest id:%s\r\n", msg_info[client_count].dest_id);
                //printf("client id:");
                for(dest_count = 0; dest_count < 32; dest_count++)
                {
                    //printf("%s-", client_addr[dest_count].client_id);
                    if(strstr((char *)client_addr[dest_count].client_id, (const char *)msg_info[client_count].dest_id) != 0)
                    {
                        msg_info[client_count].dest_fd = client_addr[dest_count].sfd;
                        //准备要发送的数据
                        //ees#@send#@source_id#@message#@end
                        memset(buffer, 0, sizeof(buffer));
                        strcpy(buffer, "ees#@send#@");
                        strcat(buffer, msg_info[client_count].source_id);
                        strcat(buffer, "#@");
                        strcat(buffer, msg_info[client_count].messages);
                        send(msg_info[client_count].dest_fd, buffer, strlen(buffer), 0);
                        msg_info[client_count].status = STATUS_CLIENT_IDLE;//清除发送标志
                        break;
                    }
                }
                //printf("\r\n");
                //if(dest_count >= 32)
                //{
                    //printf("not found dest\r\n");
                //}						
            }
        }
        sleep(1);
    }
}

int main(void)
{
    struct sockaddr_in server_sockaddr = {0};
    int client_count = 0;
    int sockfd = 0;
    int sin_size = 0;
    int flags = 0;
    int opt = SO_REUSEADDR;
    int fd = 0;
    int ret = 0;
    pthread_t thread_id1 = {0};
    pthread_t thread_id2 = {0};

    sin_size = sizeof(struct sockaddr_in);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd == -1)
    {
        perror("Creating socket failed.");
        exit(1);
    }

    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    bzero(&server_sockaddr, sizeof(server_sockaddr));
    server_sockaddr.sin_family = AF_INET;
    server_sockaddr.sin_port = htons(SERVPORT);
    server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if(bind(sockfd, (struct sockaddr *)&server_sockaddr, sizeof(struct sockaddr)) == -1)
    {
        perror("Bind error.");
        exit(1);
    }

    if(listen(sockfd, 1) == -1)
    {
        perror("Listen error.");
        exit(1);
    }

    //printf("Waiting for client ...\n");

    ret = pthread_create(&thread_id1, NULL, (void *)thread_process_read, &fd);
    if(ret != 0)
    {
        perror("pthread_create1\n");
        return -1;
    }
    //printf("pthread_create read thread status %d\n", ret);
	
    ret = pthread_create(&thread_id2, NULL, (void *)thread_process_write, &fd);
    if(ret != 0)
    {
        perror("pthread_create2\n");
        return -1;
    }
    //printf("pthread_create write thread status %d\n", ret);
    pthread_detach(thread_id1);
    pthread_detach(thread_id2);
	
    for(client_count = 0; client_count < 32; client_count++)client_addr[client_count].status = STATUS_CLIENT_IDLE;

    while(1)
    {
        for(client_count = 0; client_count < 32; client_count++)
        {
            if(client_addr[client_count].status == STATUS_CLIENT_IDLE)
            {
                fd = accept(sockfd, (struct sockaddr *)&client_addr[client_count].sockaddr, &sin_size);
                if((fd == -1) && ((errno == EAGAIN) || (errno == EWOULDBLOCK)))
                {
                    continue;
                }
                else if(fd == -1)
                {
                    perror("Accept error.");
                    exit(1);
                }
                client_addr[client_count].sfd = fd;
                //printf("add a client online:%d\r\n", client_addr[client_count].sfd);
                client_addr[client_count].status = STATUS_CLIENT_WORK;
            }
        }
    }

    exit(0);
}
