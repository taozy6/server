#include<stdio.h>
#include<dirent.h>
#include<ctype.h>
#include<unistd.h>
#include<stdlib.h>
#include<string.h>
#include<sys/epoll.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<arpa/inet.h>
#include<sys/socket.h>
#include<fcntl.h>
#include"epoll_server.h"
#include"threadpool.h"
#define MAXSIZE 2000

typedef struct poolCs
{
	int fd;
	int epfd;
}poolCs;

void epoll_run(int port)
{
	threadpool_t*thp=threadpool_create(5,2000,2000);
	//创建epoll数的根节点
	int epfd=epoll_create(MAXSIZE);
	if(epfd==-1)
	{
		perror("epoll_creat error:");
		exit(1);
	}

	//往epoll树上添加要监听的lfd
	int lfd=init_listen_fd(epfd,port);

	//委托内核检测epoll树上添加的文件描述符、
	struct epoll_event all[MAXSIZE];
	int ret;
	while(1)
	{
		ret=epoll_wait(epfd,all,MAXSIZE,-1);
		if(ret==-1)
		{
			perror("epoll_wait error:");
			exit(1);
		}

		//遍历发生变化的节点
		for(int i=0;i<ret;++i)
		{
			//只处理EPOLLIN事件
			struct epoll_event *pev=&all[i];
			if(!pev->events&EPOLLIN)
			{
				//!优先级高，但它是从右往左运算，故可以不加（）
				continue;
			}
			if(pev->data.fd==lfd)
			{
				poolCs pool;
				pool.fd=pev->data.fd;
				pool.epfd=epfd;
				threadpool_add(thp,do_accept,(void*)&pool);
				//接受连接
				//	do_accept(lfd,epfd);
			}else
			{

				poolCs pool;
				pool.fd=pev->data.fd;
				pool.epfd=epfd;
				threadpool_add(thp,do_read,(void*)&pool);
				//读数据并处理
				//do_read(pev->data.fd,epfd);
			}
		}
	}
	threadpool_destroy(thp);
}

//void do_read(int cfd,int epfd)
void *do_read(void*p)
{
	poolCs*pool=(poolCs*)p;
	//将浏览器发过来的数据存到buf中
	char line[1024]={0};
	//读请求行
	int len=get_line(pool->fd,line,sizeof(line));
	if(len==0)
	{
		printf("客户端断开连接！\n");
		//关闭套接字，cfd从epoll树上del
		disconnect(pool->fd,pool->epfd);
	}else
	{
		printf("请求行数据:%s\n",line);
		printf("============请求头数据============\n");
		//主要使用请求行数据，请求头只做打印显示
		while(1)
		{
			char buf[1024]={0};
			//非阻塞的cfd读缓冲区读完后强行读取数据返回-1，但我自己写的get_line函数只会返回0或>0
			len=get_line(pool->fd,buf,sizeof(buf));
			if(len>0)
			{
				printf("-----:%s\n",buf);
			}else//len=0
			{
				break;
			}
		}

		//请求行：get /xxx http/1.1
		//判断时候不是get请求
		if(strncasecmp("get",line,3)==0)
		{
			//处理http请求
			http_request(line,pool->fd);
			disconnect(pool->fd,pool->epfd);
		}
	}
	return NULL;
}

void http_request(const char *request,int cfd)
{
	char method[12]={0};
	char path[1024]={0};
	char protocol[12]={0};
	sscanf(request,"%[^ ] %[^ ] %[^ ]",method,path,protocol);
	printf("method=%s,path=%s,protocol=%s\n",method,path,protocol);
	//解码 将不能识别的中文乱码（%23 %34 %4f）-->中文
	decode_str(path,path);
	//去掉path中的/（资源路径）
	char*file=path+1;
	//如果没有指定访问的资源，默认显示资源目录中的内容
	if(strcmp(path,"/")==0)
	{
		file="./";
	}
	//判断是目录还是文件
	struct stat st;
	int ret=stat(file,&st);
	if(ret==-1)
	{
		//show 404
		printf("显示404错误信息！！！\n");
		send_respond_head(cfd,404,"File Not Found",get_file_type(".html"));
		send_file(cfd,"404.html");

		return;
	}

	if(S_ISDIR(st.st_mode))
	{

		send_respond_head(cfd,200,"OK",get_file_type(".html"));
		send_dir(cfd,file);
	}else if(S_ISREG(st.st_mode))
	{
		//文件
		//发送响应消息前三个（状态行 消息报头 空行 响应正文）
		send_respond_head(cfd,200,"OK",get_file_type(file));
		//发送响应正文
		send_file(cfd,file);
	}
}
void send_dir(int cfd,const char*file)
{
	//拼一个html网页
	char buf[BUFSIZ]={0};
	sprintf(buf,"<html><head><title>目录名：%s</title></head>",file);
	sprintf(buf+strlen(buf),"<body><h1>当前目录：%s</h1><table>",file);

	char enstr[1024]={0};
	char path[1024]={0};
	//目录项二级指针
	struct dirent**ptr;
	int num=scandir(file,&ptr,NULL,alphasort);
	for(int i=0;i<num;++i)
	{
		char*name=ptr[i]->d_name;
		sprintf(path,"%s/%s",file,name);
		printf("path=%s====================\n",path);
		struct stat st;
		stat(path,&st);
		//编码超链接内容，浏览器可以直接识别
		encode_str(enstr,sizeof(enstr),name);
		if(S_ISREG(st.st_mode))
		{
			sprintf(buf+strlen(buf),"<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>"
					,enstr,name,(long)st.st_size);

		}else if(S_ISDIR(st.st_mode))
		{
			sprintf(buf+strlen(buf),"<tr><td><a href=\"%s/\">%s/</a></td><td>%ld</td></tr>"
					,enstr,name,st.st_size);
		}
		send(cfd,buf,strlen(buf),0);
		memset(buf,0,sizeof(buf));
	}
	sprintf(buf+strlen(buf),"</table></body></html>");
	send(cfd,buf,strlen(buf),0);
	printf("dir message send OK!!!\n");
#if 0
	DIR*dir=opendir(file);
	if(dir==NULL)
	{
		perror("opendir error:");
		exit(1);
	}
	struct dirent*ptr=NULL;
	while((ptr=readdir(dir))!=NULL)
	{
		char*name=ptr->d_name;
		....
	}
	closedir(dir);
#endif

}
void send_file(int cfd,const char*file)
{
	int fd=open(file,O_RDONLY);
	if(fd==-1)
	{
		perror("open error:");
		return;
	}
	//循环读文件
	char buf[4096]={0};
	int len=0,ret=0;
	while((len=read(fd,buf,sizeof(buf)))>0)
	{
		ret=send(cfd,buf,len,0);
		if(ret==-1)
		{	
			perror("send_file send error:");
			exit(1);
		}
		printf("send len=%d\n",ret);
	}
	if(len==-1)
	{
		perror("read file error:");
		exit(1);	
	}

	close(fd);
}
const char *get_file_type(const char *name)
{
	char* dot;
	//自右向左查找‘.’字符，如不存在返回NULL
	dot = strrchr(name, '.');   
	if (dot == NULL)
		return "text/plain; charset=utf-8";
	if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
		return "text/html; charset=utf-8";
	if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
		return "image/jpeg";
	if (strcmp(dot, ".gif") == 0)
		return "image/gif";
	if (strcmp(dot, ".png") == 0)
		return "image/png";
	if (strcmp(dot, ".css") == 0)
		return "text/css";
	if (strcmp(dot, ".au") == 0)
		return "audio/basic";
	if (strcmp( dot, ".wav" ) == 0)
		return "audio/wav";
	if (strcmp(dot, ".avi") == 0)
		return "video/x-msvideo";
	if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
		return "video/quicktime";
	if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
		return "video/mpeg";
	if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
		return "model/vrml";
	if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
		return "audio/midi";
	if (strcmp(dot, ".mp3") == 0)
		return "audio/mpeg";
	if (strcmp(dot, ".ogg") == 0)
		return "application/ogg";
	if (strcmp(dot, ".pac") == 0)
		return "application/x-ns-proxy-autoconfig";

	return "text/plain; charset=utf-8";
}
//发送响应消息，no为状态码，desp为状态码的描述，type为文件类型,len为发送数据长度(不知道传-1)
void send_respond_head(int cfd,int no,const char*desp,const char*type)
{
	//状态行
	char buf[1024]={0};
	sprintf(buf,"http/1.1 %d %s\r\n",no,desp);
	send(cfd,buf,strlen(buf),0);
	//消息报头
	sprintf(buf,"Content-Type:%s\r\n",type);
	//sprintf(buf+strlen(buf),"Content-Length:%ld\r\n",len);
	send(cfd,buf,strlen(buf),0);
	//空行
	send(cfd,"\r\n",2,0);
	printf("send_responed_head send successed!!\n");
}

//16进制转十进制（如a--》10），非ASCII转换
int hexit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;

	return 0;
}
/*
 *  这里的内容是处理%20之类的东西！是"解码"过程。
 *  %20 URL编码中的‘ ’(space)
 *  %21 '!' %22 '"' %23 '#' %24 '$'
 *  %25 '%' %26 '&' %27 ''' %28 '('......
 *  相关知识html中的‘ ’(space)是&nbsp     
 */
void decode_str(char *to, char *from)
{
	for ( ; *from != '\0'; ++to, ++from  ) 
	{
		if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2])) 
		{ 

			*to = hexit(from[1])*16 + hexit(from[2]);

			from += 2;                      
		} 
		else
		{
			*to = *from;

		}

	}
	*to = '\0';

}
void encode_str(char* to, int tosize, const char* from)
{
	int tolen;

	for (tolen = 0; *from != '\0' && tolen + 4 < tosize; ++from) 
	{
		if (isalnum(*from) || strchr("/_.-~", *from) != (char*)0) 
		{
			*to = *from;
			++to;
			++tolen;
		} 
		else 
		{
			sprintf(to, "%%%02x", (int) *from & 0xff);
			to += 3;
			tolen += 3;
		}

	}
	*to = '\0';
}
void disconnect(int cfd,int epfd)
{
	int ret=epoll_ctl(epfd,EPOLL_CTL_DEL,cfd,NULL);
	if(ret==-1)
	{
		perror("epoll_ctl del error:");
		exit(1);
	}
	close(cfd);

}
//解析http请求消息中每一行的内容
//这样写读到的行多一个换行，自己改后：最后一个字符不要
int get_line(int cfd,char*buf,int size)
{
	int i=0,n;
	char c='\0';
	while((i<size)&&(c!='\n'))
	{
		n=recv(cfd,&c,1,0);
		if(n>0)
		{
			if(c=='\r')
			{
				n=recv(cfd,&c,1,MSG_PEEK);
				if((n>0)&&(c=='\n'))
				{
					recv(cfd,&c,1,0);
				}else
				{
					c='\n';
				}
			}
			buf[i]=c;
			i++;
		}else//cfd非阻塞，如果没有数据，强行读取，n将为-1
		{
			c='\n';	
		}
	}
	if(i>0)
	{
		i--;//最后一个字符不为换行
	}
	buf[i]='\0';
	return i;
}
//void do_accept(int lfd,int epfd)
void *do_accept(void *p)
{	
	poolCs*pool=(poolCs*)p;
	struct sockaddr_in client;
	int len=sizeof(client);
	int cfd=accept(pool->fd,(struct sockaddr*)&client,&len);
	if(cfd==-1)
	{
		perror("accept lfd error:");
		exit(1);
	}

	//打印客户端信息
	char ip[64]={0};
	printf("New client IP:%s,Port:%d,cfd=%d\n",
			inet_ntop(AF_INET,&client.sin_addr.s_addr,ip,sizeof(ip)),client.sin_port,cfd);

	//设置cfd非阻塞
	int flags=fcntl(cfd,F_GETFL);
	flags|=O_NONBLOCK;
	fcntl(cfd,F_SETFL,flags);
	//将cfd添加到epoll树上
	struct epoll_event ev;
	ev.events=EPOLLIN|EPOLLET;//边沿非阻塞模式
	ev.data.fd=cfd;
	int ret=epoll_ctl(pool->epfd,EPOLL_CTL_ADD,cfd,&ev);
	if(ret==-1)
	{
		perror("epoll_ctl error:");
		exit(1);
	}
	return NULL;
}

int init_listen_fd(int epfd,int port)
{
	int lfd=socket(AF_INET,SOCK_STREAM,0);
	if(lfd==-1)
	{
		perror("socket error:");
		exit(1);
	}
	//lfd绑定IP和port
	struct sockaddr_in serv;
	memset(&serv,0,sizeof(serv));
	serv.sin_family=AF_INET;
	serv.sin_port=htons(port);
	serv.sin_addr.s_addr=htonl(INADDR_ANY);
	//设置端口复用
	int opt=1;
	setsockopt(lfd,SOL_SOCKET,SO_REUSEPORT,&opt,sizeof(opt));
	//绑定
	int ret=bind(lfd,(struct sockaddr*)&serv,sizeof(serv));
	if(ret==-1)
	{	
		perror("bind error:");
		exit(1);
	}
	//监听
	ret=listen(lfd,128);
	if(ret==-1)
	{
		perror("listen error:");
		exit(1);
	}
	//lfd添加到epoll树上
	struct epoll_event ev;
	ev.events=EPOLLIN;
	ev.data.fd=lfd;
	ret=epoll_ctl(epfd,EPOLL_CTL_ADD,lfd,&ev);
	if(ret==-1)
	{
		perror("epoll_ctl error:");
		exit(1);
	}
	return lfd;
}

