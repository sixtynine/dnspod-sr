/* Copyright (c) 2006-2012, DNSPod Inc.
 * All rights reserved.

 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met: 
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer. 
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 * The views and conclusions contained in the software and documentation are those
 * of the authors and should not be interpreted as representing official policies, 
 * either expressed or implied, of the FreeBSD Project.
 */



#include "event.h"
#include <sys/epoll.h>


struct iner_event
{
 int epfd;
 char *buf;
 struct epoll_event e[0];
};


struct event* create_event(int size)
{
 struct event *ev = malloc(sizeof(struct event) + sizeof(struct event_data) * size);
 int epfd = epoll_create(size);
 if(epfd == 0)
	dns_error(0,"epoll create");
 ev->size = size;
 ev->ie = malloc(sizeof(struct iner_event) + sizeof(struct epoll_event) * (ev->size));
 if(ev->ie == NULL)
	dns_error(0,"alloc iner event");
 memset(ev->ie,0,sizeof(struct iner_event) + sizeof(struct epoll_event) * (ev->size));
 ev->ie->epfd = epfd;
 return ev;
}


int add_event(struct event *ev,struct event_help *help)
{
 struct epoll_event e;
 int ret = 0;
 int epfd = ev->ie->epfd;
 e.data.fd = help->fd;
 if(e.data.fd < 0)
	return -1;
 if(help->type == ET_READ)
 	e.events = EPOLLIN;// | EPOLLET;
 if(help->type == ET_WRITE)
	e.events = EPOLLOUT;// | EPOLLET;
 ev->data[help->fd].cb = help->cb;
 if(help->ext != NULL)
	ev->data[help->fd].ext = help->ext;
 ret = epoll_ctl(epfd,EPOLL_CTL_ADD,help->fd,&e);
 if(ret < 0)
	{
	 printf("fd is %d\n",help->fd);
	 perror("epoll_ctl");
	}
 return ret;
}


int del_event(struct event *ev,struct event_help *help)
{
 struct epoll_event e; 
 struct iner_event *ie = ev->ie;
 int ret = 0;
 e.data.fd = help->fd;
 ret = epoll_ctl(ie->epfd,EPOLL_CTL_DEL,help->fd,&e);
 return ret;
}


int handle_event(struct event *ev,int to)
{
 int num = 0,i;
 static ulong fake_count = 0;
 static int tm = 0;
 struct iner_event *ie = ev->ie;
 if(to == 0)
	to = -1;
 else
	to = to * 100;
 ev->size = 100;
 while(1)
	{
	 num = epoll_wait(ie->epfd,ie->e,ev->size,to);
	 if(num >= 0)
		break;
	 if(num < 0 && errno == EINTR)
		 continue;
	}
 return num;
}


int cb_get_tcp_msg(struct event_data *data,void *v,int idx)
{
 int ret,szhdr = sizeof(dnsheader);
 struct msgcache *mc;
 struct fetcher *f = (struct fetcher*)v;
 struct sockinfo si;
 struct seninfo *se = NULL;
 mc = f[idx].mc;
 pthread_mutex_lock(&mc->lock);
 if(mc->tail + 512 > mc->size)
	 mc->tail = 0;
 if(mc->tail < mc->head && mc->tail + 512 > mc->head) //query msg should small than 300 bytes
	{
	 close(data->fd);  //we should return a SERVER_ERROR.
	 pthread_mutex_unlock(&mc->lock);
	 return 0;
	}
 se = (struct seninfo*)(mc->data + mc->tail);
 se->type = TCP;
 si.fd = data->fd;
 si.buf = mc->data + mc->tail + sizeof(struct seninfo);
 si.buflen = 512;
 si.socktype = TCP; //not used
 ret = tcp_read_dns_msg(&si,512,0); //epoll return and no blocked here
 if(ret < szhdr)
	{
	 pthread_mutex_unlock(&mc->lock);
	 return -1;
	}
 se->len = ret; //data len.
 se->fd = si.fd;
 mc->tail = mc->tail + ret + sizeof(struct seninfo);
 pthread_mutex_unlock(&mc->lock);
 return 0;
}


int fake_recv(struct event_data *data,void *v,int idx)
{
 struct fetcher *f = (struct fetcher*)v;
 struct sockaddr_in addr;
 uchar buffer[512] = {0};
 int ret;
 idx = 0;
 socklen_t len = sizeof(struct sockaddr_in);
 while(1)
	{
	 ret = recvfrom(data->fd,buffer,512,0,(SA*)&addr,&len);
	 if(ret > 0)
		 f[idx].pkg ++;
	 else
		return 0;
	}
 return 0;
}


int cb_get_udp_msg(struct event_data* data,void *v,int idx)
{
 int ret,szhdr = sizeof(dnsheader);
 struct msgcache *mc = NULL;
 struct fetcher *f = (struct fetcher*)v;
 struct sockinfo si;
 struct seninfo *se = NULL;
 //printf("call back\n");
 while(1)//we use epoll et and non-block mode.
{
 mc = f[idx].mc;
 pthread_mutex_lock(&mc->lock);
 if(mc->tail + 512 > mc->head && mc->tail < mc->head)
	{
	 f[idx].miss ++;
	 pthread_mutex_unlock(&mc->lock);
	 return 0;
	}
 f[idx].pkg ++;
 se = (struct seninfo*)(mc->data + mc->tail);
 se->type = UDP;
 si.fd = data->fd;
 si.buf = mc->data + mc->tail + sizeof(struct seninfo);
 si.buflen = 512;
 si.socktype = UDP;
 ret = udp_read_msg(&si,0); //epoll return and no blocking here
 if(ret < szhdr)	
	{
	 pthread_mutex_unlock(&mc->lock);
	 return -1;
	}
 memcpy(&(se->addr),&(si.addr),sizeof(struct sockaddr_in));
 //if check addr == false,pthread_unlock,return.
 se->len = ret; //data len.
 mc->tail = mc->tail + ret + sizeof(struct seninfo);
 if(mc->tail + 512 > mc->size)
	mc->tail = 0;
 pthread_mutex_unlock(&mc->lock);
}
 return 0;
}


int insert_events(struct event *ev,int fd,int type)
{
 struct event_help h;
 if(fd > 0)
	{
	 memset(&h,0,sizeof(struct event_help));
	 h.type = ET_READ;
	 h.fd = fd;
	 if(type == UDP)
		 h.cb = cb_get_udp_msg;
	 else	
		 h.cb = cb_get_tcp_msg;
	 if(add_event(ev,&h) < 0)
		dns_error(1,"add event notify");
	}
 return 0;
}


int run_sentinel(struct server *s)
{
 int num,i,ls,connfd,ret,fidx = 0; 
 struct sockaddr_in addr;
 struct event_help h;
 socklen_t len = sizeof(addr);
 struct fetcher *f = s->fetchers;
 struct event *ev = create_event(SENTINEL_EVENT); // 1 for udp,999 for tcp.
 if(ev == NULL)
	dns_error(0,"create event st");
 insert_events(ev,s->ludp,UDP);
 insert_events(ev,s->ltcp,TCP);
 ls = s->ltcp;
 while(1)
{
 num = handle_event(ev,1);
 global_cron(s);
 for(i = 0;i < num;i ++)
	{
	 int fd = ev->ie->e[i].data.fd;
	 noti_chain_callback cb = ev->data[fd].cb;
	 ev->data[fd].fd = fd;
	 if(fd == ls)
		{
		 connfd = accept(fd,(SA*)&addr,&len);
		 set_non_block(connfd);
		 insert_events(ev,connfd,TCP);
		 continue;
		}
	 else //udp or connected tcp
	 if(cb != NULL)
		{
		 //s->pkg ++;
		 fidx ++;
		 fidx = fidx % FETCHER_NUM;
		 if(fidx >= FETCHER_NUM)
			fidx = FETCHER_NUM - 1;
		 //fake_recv(ev->data + fd,f,fidx);
		 ret = (*cb) (ev->data + fd,f,fidx);
		 if(cb == cb_get_tcp_msg)
			{
			 if(ret == -1)//read data error
				 close(fd);
			 h.fd = fd;
			 del_event(ev,&h); //not listen this socket
			}
		}
	 else
		dns_error(1,"call back func is null");
	}
 }
 return 0;
}
