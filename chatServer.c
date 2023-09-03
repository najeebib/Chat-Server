#include "chatServer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/select.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

static int end_server = 0;
void removeMsgs(conn_t* ptr);
void intHandler(int SIG_INT) {//change the flag to end the loop
	end_server = 1;
	if(SIG_INT==SIGINT)
        return;
}

int main (int argc, char *argv[])
{
	if(argc != 2)
	{
		perror("Usage: ./server <port>\nEnter correct input\n");
		exit(EXIT_FAILURE);
	}
	signal(SIGINT, intHandler);
   
	conn_pool_t* pool = malloc(sizeof(conn_pool_t));
	init_pool(pool);
	
	int port = atoi(argv[1]);//get the port from input
	int rc;
	struct sockaddr_in servaddr,clientAddr;//set up the server
	socklen_t sockLen = sizeof(struct sockaddr_in);
	int socket_fd;
	socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);

	int on = 1;
	rc = ioctl(socket_fd,(int)FIONBIO,(char*)&on);

	bind(socket_fd,(struct sockaddr*)&servaddr, sizeof(servaddr));//bind

	listen(socket_fd,5);//listen to incoming connections

	FD_SET(socket_fd,&(pool->read_set));//put the master socket in the set
	pool->maxfd = socket_fd;
	do
	{
		pool->ready_read_set = pool->read_set;
		pool->ready_write_set = pool->write_set;
		printf("Waiting on select()...\nMaxFd %d\n", pool->maxfd);
		rc = select(pool->maxfd+1,&(pool->ready_read_set),&(pool->ready_write_set),NULL,NULL);//select the working sets
		if(rc < 0)//select error
		{
			conn_t* del = pool->conn_head;//delete and free all connections
			while(del != NULL)
			{
				removeMsgs(del);
				conn_t* tmp = del;
				del = del->next;
				free(tmp);
				close(del->fd);
			}	
			free(pool);	
			perror ("select");
          	exit (EXIT_FAILURE);
		}
		pool->nready = rc;//number of ready connections

		for (int i = 0;i<pool->maxfd+1;i++)
		{
			if (FD_ISSET(i,&(pool->ready_read_set)))
			{
				if(i == socket_fd)
				{
					
					int sd;
					sd = accept(socket_fd, (struct sockaddr*)&clientAddr, &sockLen);//accept new client connection
					printf("New incoming connection on sd %d\n", sd);
					add_conn(sd,pool);//add connection to the pool
				}
				else
				{
					char buf[BUFFER_SIZE+1];
					memset(buf , '\0' , BUFFER_SIZE+1);
					int bytes;
					printf("Descriptor %d is readable\n", i);
					bytes = read(i,buf,BUFFER_SIZE);//read from client
					printf("%ld bytes received from sd %d\n", strlen(buf), i);
					if(bytes<=0)//connection closed by client
					{
						printf("Connection closed for sd %d\n",i);
						remove_conn(i,pool);		
						printf("removing connection with sd %d \n", i);
					}
					else//add the message to all other connections
						add_msg(i,buf,strlen(buf),pool);
					
				}
			} 
			if (FD_ISSET(i,&(pool->ready_write_set)))//when the connection is ready to write, write the message
				write_to_client(i,pool);
      	}

   } while (end_server == 0);
	conn_t* del = pool->conn_head;//after the loop is over, free and close all connections
	while(del != NULL)
	{
		removeMsgs(del);
		conn_t* tmp = del;
		close(del->fd);
		del = del->next;
		free(tmp);
	}
	FD_ZERO(&(pool->read_set));
	FD_ZERO(&(pool->write_set));
	FD_ZERO(&(pool->ready_read_set));
	FD_ZERO(&(pool->ready_write_set));
	free(pool);
	return 0;
}

void updateMaxFD(int sd,conn_pool_t* pool)//helper function to update the max fd
{
	conn_t* max = pool->conn_head;
	int newMax = 0;
	while(max != NULL)//pass all the connection in the pool and find the max fd in it
	{
		if(max->fd > newMax)
			newMax = max->fd;
		max = max->next;
	}
	pool->maxfd = newMax;
		
}

int init_pool(conn_pool_t* pool) {
	//initialized all fields
	pool->maxfd = 0;
	pool->nready = 0;
	FD_ZERO(&(pool->read_set));
	FD_ZERO(&(pool->write_set));
	FD_ZERO(&(pool->ready_read_set));
	FD_ZERO(&(pool->ready_write_set));
	pool->nr_conns = 0;
	pool->conn_head = NULL;
	return 0;
}

int add_conn(int sd, conn_pool_t* pool) {
	
	conn_t* newConn = malloc(sizeof(conn_t));//alloc new connection
	if(newConn == NULL)
		return -1;
	(pool->nr_conns)++;
	FD_SET(sd,&(pool->read_set));
	
	if(pool->conn_head == NULL)//if the pool is empty, set the new connection as the head
	{
		newConn->fd = sd;
		newConn->next = NULL;
		newConn->prev = NULL;
		newConn->write_msg_head = NULL;
		newConn->write_msg_tail = NULL;
		pool->conn_head = newConn;
		updateMaxFD(sd,pool);
	}
	else//but the new connection at the end of the pool
	{
		newConn->fd = sd;
		newConn->next = NULL;
		conn_t* ptr = pool->conn_head;
		while(ptr->next != NULL)//find the last connection in the pool to put the new connection after it
			ptr = ptr->next;
		newConn->write_msg_head = NULL;
		newConn->write_msg_tail = NULL;
		newConn->prev = ptr;
		ptr->next = newConn;
		if(sd > pool->maxfd)//update the maxfd if needed
			updateMaxFD(sd,pool);
	}
	return 0;
}

void removeMsgs(conn_t* ptr)//helper func to remove all messages in connection
{
	msg_t* tmp= ptr->write_msg_head;
		while(tmp != NULL)//pass through all messages and free and each message
		{
			ptr->write_msg_head = ptr->write_msg_head->next;
			free(tmp->message);
			free(tmp);
			tmp = ptr->write_msg_head;
		}		
}

int remove_conn(int sd, conn_pool_t* pool) {
	
	conn_t* ptr = pool->conn_head;
	if(pool->conn_head->fd == sd)//if the connection is the head, remove it make the next connection the new head
	{
		conn_t* next = pool->conn_head->next;
		if(next == NULL)
		{
			free(pool->conn_head);
			pool->conn_head = NULL;
			pool->maxfd = 0;
			(pool->nr_conns)--;
			close(sd);
		}
		else
		{
			pool->conn_head->next->prev = NULL;
			pool->conn_head->next = NULL;
			pool->conn_head = next;
			removeMsgs(ptr);
			FD_CLR(sd,&(pool->read_set));
			FD_CLR(sd,&(pool->write_set));
			updateMaxFD(sd,pool);
			free(ptr);
			close(sd);
		}
		
	}
	ptr = ptr->next;//pass the pool and find the connection we nedd to remove
	while(ptr != NULL)
	{
		if(ptr->fd == sd)
		{
			if(ptr->next == NULL)//if the connection is the last, remove it and sever the link with the last node
			{
				ptr->prev->next = NULL;
				ptr->prev = NULL;
				removeMsgs(ptr);
				FD_CLR(sd,&(pool->read_set));
				FD_CLR(sd,&(pool->write_set));
				updateMaxFD(sd,pool);
				free(ptr);
			}
			else//remove the connection and link the previous one with the one after it
			{
				ptr->prev->next = ptr->next;
				ptr->next->prev = ptr->prev;
				removeMsgs(ptr);
				FD_CLR(sd,&(pool->read_set));
				FD_CLR(sd,&(pool->write_set));
				updateMaxFD(sd,pool);
				free(ptr);
			}
		}
		ptr = ptr->next;
	}
	return 0;
}

int add_msg(int sd,char* buffer,int len,conn_pool_t* pool) {
	
	conn_t* ptr = pool->conn_head;
	while(ptr != NULL)
	{
		if(ptr->fd != sd)
		{
			msg_t* newMsg = (msg_t*)malloc(sizeof(msg_t));
			newMsg->message = (char*)malloc(sizeof(char)*len);
			newMsg->size = len;
			strcpy(newMsg->message,buffer);
			if(ptr->write_msg_head == NULL)//if the head is empty make the message the head
			{
				newMsg->next = NULL;
				newMsg->prev = NULL;
				ptr->write_msg_head = newMsg;
				ptr->write_msg_tail = newMsg;
				FD_SET(ptr->fd,&(pool->write_set));
			}
			else//add the message to the end of the list
			{
				newMsg->next = NULL;
				newMsg->prev = ptr->write_msg_tail;
				ptr->write_msg_tail->next = newMsg;
				ptr->write_msg_tail = newMsg;
				FD_SET(ptr->fd,&(pool->write_set));
			}

		}
		ptr = ptr->next;
	}
	return 0;
}

int write_to_client(int sd,conn_pool_t* pool) {
	
	
	conn_t* ptr = pool->conn_head;
	while(ptr != NULL)//pass through the list
	{
		if(ptr->fd == sd)//found the client
		{
			msg_t* msgPtr = ptr->write_msg_head;
			while(msgPtr != NULL)
			{
				int len = msgPtr->size;
				write(sd,msgPtr->message,len);//write the message to the client
				if(msgPtr->next == NULL)//if the message is the last one in the list, free the list
				{
					msg_t* freeMsg = msgPtr;
					msgPtr = msgPtr->next;
					FD_CLR(ptr->fd,&(pool->write_set));
					free(freeMsg->message);
					free(freeMsg);
					return 0;
					
				}
				else
				{//get the next message and free the last one
					msg_t* next = msgPtr->next;
					next->prev = NULL;
					msgPtr->next = NULL;
					msg_t* del = msgPtr;
					msgPtr = next;
					free(del->message);
					free(del);
				}
				
			}
		}
		ptr = ptr->next;
	}
	return 0;
}

