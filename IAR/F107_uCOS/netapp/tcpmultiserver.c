/**
* @file tcpmultiserver.c
* @brief A brief file description.
* @details
*     A more elaborated file description.
* @author Wang Mengyin
* @date 2010Aug2 16:00:25
* @note
*               Copyright 2010 Wang Mengyin.ALL RIGHTS RESERVED.
*                            http://tigerwang202.blogbus.com
*    This software is provided under license and contains proprietary and
* confidential material which is the property of Company Name tech.
*/


#define __TCPMULTISERVER_C


/* Includes ------------------------------------------------------------------*/
#include "tcpmultiserver.h"
#include "lwip/mem.h" /* 为了分配内存 */
#include <lwip/sockets.h> /* 使用BSD Socket接口必须包含sockets.h这个头文件 */
#include "includes.h"

#include "app_cfg.h"

/* Private typedef -----------------------------------------------------------*/


/* Private define ------------------------------------------------------------*/
#ifndef TCPMULTISERVER_STK_SIZE          /* tcpserver 使用堆栈大小 */
#define TCPMULTISERVER_STK_SIZE     300
#endif

#ifndef LWIP_TCPMULTISERVER_PRIO         /* tcpserver 任务优先级 */
#define LWIP_TCPMULTISERVER_PRIO    15
#endif

#define LWIP_TCPMULTISERVER_PORTNO  5000    /* 服务器监听端口号 */

/* Private macro -------------------------------------------------------------*/


/* Private variables ---------------------------------------------------------*/
static char send_data[] = "Hello Client[ ]\n"; /* 发送用到的数据 */
static OS_STK   TcpServerStack[TCPMULTISERVER_STK_SIZE];

/* Private function prototypes -----------------------------------------------*/
static void tcpserv(void* parameter);
static void tcpmultiserver_thread(void* arg);

/* Private functions ---------------------------------------------------------*/
/**
* @brief tcpserver accept tcp connect receive data and output on debug thermal.
* @param void* parameter :unused.
*/
static void tcpserv(void* parameter)
{
   char *recv_data; /* 用于接收的指针，后面会做一次动态分配以请求可用内存 */
   u32_t sin_size;
   int listener, connected, bytes_received;
   struct sockaddr_in server_addr, client_addr;
   bool stop = FALSE; /* 停止标志 */
   
   fd_set master; /* master file descriptor */
   fd_set read_fds; /* temp file descriptor list for select */
   int fdmax; /* maximum file descriptor number */
   int i, j;

   FD_ZERO(&master);    /* clear the master and temp sets */
   FD_ZERO(&read_fds);
   
   recv_data = mem_malloc(1024); /* 分配接收用的数据缓冲 */
   if (recv_data == NULL)
   {
       printf("No memory\n");
       return;
   }

   /* 一个socket在使用前，需要预先创建出来，指定SOCK_STREAM为TCP的socket */
   if ((listener = socket(AF_INET, SOCK_STREAM, 0)) == -1)
   {
       /* 创建失败的错误处理 */
       printf("Socket error\n");

       /* 释放已分配的接收缓冲 */
       mem_free(recv_data);
       return;
   }

   /* 初始化服务端地址 */
   server_addr.sin_family = AF_INET;
   server_addr.sin_port = htons(LWIP_TCPMULTISERVER_PORTNO); /* 服务端工作的端口 */
   server_addr.sin_addr.s_addr = INADDR_ANY;
   memset(&(server_addr.sin_zero),8, sizeof(server_addr.sin_zero));

   /* 绑定socket到服务端地址 */
   if (bind(listener, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1)
   {
       /* 绑定失败 */
       printf("Unable to bind\n");

       /* 释放已分配的接收缓冲 */
       mem_free(recv_data);
       return;
   }

   /* 在socket上进行监听 */
   if (listen(listener, 5) == -1)
   {
       printf("Listen error\n");

       /* release recv buffer */
       mem_free(recv_data);
       return;
   }

   /* add the listener to the master set */
   FD_SET(listener, &master);

   /* keep track of the biggest file descriptor */
   fdmax = listener + 1;

   printf("\nTCPServer Waiting for client on port 5000...\n");
   while(stop != TRUE)
   {
       read_fds = master; /* copy it */
       /* block & waiting for connect or received data */
       if(lwip_select(fdmax, &read_fds, 0, 0, 0) == 0)
       {
           printf("select error.\n");
           continue;
       }
       
       for(i = 0; i < fdmax; i++)
       {
           if(FD_ISSET(i, &read_fds))
           { // we got one.
               if(i == listener)
               {   /* handle new connections */
                   sin_size = sizeof(struct sockaddr_in);
                   /* 接受一个客户端连接socket的请求，这个函数调用是阻塞式的 */
                   connected = accept(i, (struct sockaddr *)&client_addr, &sin_size);
                   /* 返回的是连接成功的socket */
                   if(connected == -1)
                       printf("accept connect error.\n");
                   else
                   {
                       FD_SET(connected, &master); /* add to master set */
                       if((connected + 1) > fdmax) /* keep track of the max */
                           fdmax = connected + 1;
                       /* 接受返回的client_addr指向了客户端的地址信息 */
                       printf("I got a connection from (%s , %d)\n",
                              inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                   }
               }
               else
               {    // handle data from a client
                   /* 从connected socket中接收数据，接收buffer是1024大小，
                   \但并不一定能够收到1024大小的数据 */
                   bytes_received = recv(i, recv_data, 1024, 0);
                   getpeername(i, (struct sockaddr *)&client_addr, &sin_size);
                   
                   if (bytes_received <= 0)
                   {
                       if(bytes_received < 0)
                           printf("received data failed, close client(%s, %d) connection.\n",
                                  inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                       else
                           printf("disconnect client(%s, %d).\n", 
                                  inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                       /* 接收失败，关闭这个connected socket */
                       lwip_close(i);
                       FD_CLR(i, &master); /* remove from master set */
                       continue;
                   }

                   /* 有接收到数据，把末端清零 */
                   recv_data[bytes_received] = '\0';                   
                   if (strcmp(recv_data , "q") == 0 || strcmp(recv_data , "Q") == 0)
                   {
                       /* 如果是首字母是q或Q，关闭这个连接 */
                       printf("receive \"q\" command, close client(%s, %d) connection.\n",
                              inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                       lwip_close(i);
                       FD_CLR(i, &master); /* remove from master set */
                   }
                   else if (strcmp(recv_data, "exit") == 0)
                   {
                       /* 如果接收的是exit，则关闭整个服务端 */
                       printf("receive \"exit\" command, exit tcpserver task.\n");
                       stop = TRUE;
                   }
                   else if (strcmp(recv_data, "list") == 0)
                   {    /* 如果接收的是list，列出已连接的客户端 */
                       printf("Socket\tAddress\t\tPort\n");
                       for(j = 0; j < fdmax; j++)
                        if(FD_ISSET(j, &master) && j != listener)
                            printf("[%d]\t%s\t%d\n",
                                   j, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                   }
                   else
                   {    /* 返回欢迎字符串 */
                       send_data[13] = i + '0'; /* Socket 编号 */
                       send(i, send_data, sizeof(send_data) - 1, 0);
                        /* 在控制终端显示收到的数据 */
                       printf("RECIEVED DATA from client(%s, %d)\n%s\n",
                              inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), recv_data);
                   }
               } // End of Handle data from client
           } // End of new incoming connection
       } // end of loop through file descriptors
   } // end of while(stop != TRUE)

   
   /* 退出服务 */
   for(i = 0; i < fdmax; i++)
       if(FD_ISSET(i, &master))
           lwip_close(i);
   
   /* clear the master and temp sets */
   FD_ZERO(&master);
   FD_ZERO(&read_fds);
   
   /* 释放接收缓冲 */
   mem_free(recv_data);

   return ;
}

/**
* @brief tcpmultiserver thread
* @param void *arg :unused.
*/
static void tcpmultiserver_thread(void *arg)
{
    tcpserv(arg);
    
    printf("Delete Tcpmultiserver thread.\n");
    OSTaskDel(OS_PRIO_SELF);
}

/**
* @brief start tcpmultiserver
*/
void tcpmultiserver_init()
{
    CPU_INT08U  os_err;
    
    OSTaskCreate(tcpmultiserver_thread, 
                 NULL, 
                 &TcpServerStack[TCPMULTISERVER_STK_SIZE - 1], 
                 LWIP_TCPMULTISERVER_PRIO
                     );
#if (OS_TASK_NAME_SIZE >= 11)
    OSTaskNameSet(LWIP_TCPMULTISERVER_PRIO, (CPU_INT08U *)"tcpmulti", &os_err);
#endif
}

/*-- File end --*/

