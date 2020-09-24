#include<stdio.h>
#include<unistd.h>
#include<stdlib.h>
#include"epoll_server.h"


int main(int argc,const char*argv[])
{
	//argv[1]为服务器端口号   argv[2]为创建的服务器资源目录
	if(argc!=3)
	{
		printf("eg:./a.out port path\n");
		exit(1);
	}

	//端口
	int port=atoi(argv[1]);
	
	//修改进程工作目录
	int ret=chdir(argv[2]);
	if(ret==-1)
	{
		perror("chdir error:");
		exit(1);
	}
	
	//启动epoll模型
	epoll_run(port);

	return 0;
}


