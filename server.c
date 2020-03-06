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

typedef struct
{
    int status;//online or offline
    int sfd;
    char client_id[16];
    struct sockaddr_in sockaddr;
}_client_addr;

typedef struct
{
    int status;//send data or idle
    int dest_fd;
	int source_fd;
    char dest_id[16];
    char source_id[16];
    char messages[1024];
}_msg_info;

_client_addr client_addr[32] = {0};//最大连接32个客户端
_msg_info msg_info[32] = {0};//最大缓存消息条数

char buffer[2048] = {0};
void thread_process(void *arg)
{
	int dest_count = 0;
	int client_count = 0;
	int source_count = 0;
    int fd = *((int *)arg);

    while(1)
    { 
		for(client_count = 0; client_count < 32; client_count++)
        {
			if(client_addr[client_count].status == STATUS_CLIENT_WORK)//当前是否在线
			{
				memset(buffer, 0, sizeof(buffer));
				if(recv(client_addr[client_count].sfd, buffer, 2048, 0) > 0)
				{
					if(strstr((char *)buffer, "ees#@hands#@") != 0)//每分钟接收心跳数据证明是否在线，加超时判断
					{
						//ees#@hands#@source_id#@end
						memset(client_addr[client_count].client_id, 0, sizeof(client_addr[client_count].client_id));
						strncpy(client_addr[client_count].client_id, &buffer[strlen("ees#@hands#@")], 16);
						printf("receved hands msg from:%s\r\n", client_addr[client_count].client_id);
					}
					else if(strstr((char *)buffer, "ees#@send#@") != 0)
					{
						//ees#@send#@dest_id#@message#@end
						for(source_count = 0; source_count < 32; source_count++)//把数据放在空闲的条目中准备发送
						{
							if(msg_info[source_count].status == STATUS_CLIENT_IDLE)//判断当前条目是否为空
							{
								//准备数据
								memset(msg_info[source_count].dest_id, 0, sizeof(msg_info[source_count].dest_id));
								memset(msg_info[source_count].source_id, 0, sizeof(msg_info[source_count].source_id));
								memset(msg_info[source_count].messages, 0, sizeof(msg_info[source_count].messages));
								
								strncpy(msg_info[source_count].dest_id, &buffer[strlen("ees#@send#@")], 16);
								strcpy(msg_info[source_count].source_id, client_addr[client_count].client_id);
								strcpy(msg_info[source_count].messages, &buffer[strlen("ees#@send#@") + 18]);
								msg_info[source_count].source_fd = client_addr[client_count].sfd;
								for(dest_count = 0; dest_count < 32; dest_count++)
								{
									if(strstr((char *)client_addr[dest_count].client_id, (const char *)msg_info[source_count].dest_id) != 0)
									{
										msg_info[source_count].dest_fd = client_addr[dest_count].sfd;
										break;
									}
								}
								if(dest_count == 32)
								{
									//没找到目标地址，放弃此数据
									printf("not found dest\r\n");
									msg_info[source_count].status = STATUS_CLIENT_IDLE;
								}
								else 
								{
									//数据准备就绪
									msg_info[source_count].status = STATUS_CLIENT_SEND;
								}
								break;
							}
						}
						if(source_count == 32)
						{
							//缓存条目已满，无法存储更多消息
							printf("msg full!\r\n");
						}
					}
				}
			}
			
			if(msg_info[client_count].status == STATUS_CLIENT_SEND)//判断是否有数据需要发送
			{
				//ees#@send#@source_id#@message#@end
				memset(buffer, 0, sizeof(buffer));
				strcpy(buffer, "ees#@send#@");
				strcat(buffer, msg_info[client_count].source_id);
				strcat(buffer, "#@");
				strcat(buffer, msg_info[client_count].messages);
				send(msg_info[client_count].dest_fd, buffer, strlen(buffer), 0);
				msg_info[client_count].status = STATUS_CLIENT_IDLE;
			}
        }		
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
    pthread_t thread_id = {0};
	
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

    /* set NONBLOCK */  
    flags = fcntl(sockfd, F_GETFL, 0);  
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK); 
    
    printf("Waiting for client ...\n");

    ret = pthread_create(&thread_id, NULL, (void *)thread_process, &fd);
    if(ret != 0)
    {
        perror("pthread_create\n");
        return -1;
    }
    printf("pthread_create thread status %d\n", ret);
	
    pthread_detach(thread_id);
	
    while(1)
    {
        for(client_count = 0; client_count < 32; client_count++)
        {
			fd = accept(sockfd, (struct sockaddr *)&client_addr[client_count].sockaddr, &sin_size);
			if((fd == -1) && (errno == EAGAIN))break;
			else if(fd == -1)
			{
				perror("Accept error.");
				exit(1);
			}
			client_addr[client_count].sfd = fd;
			printf("add a client online:%d\r\n", client_addr[client_count].sfd);
			client_addr[client_count].status = STATUS_CLIENT_WORK;
        }
    }

    exit(0);
}
