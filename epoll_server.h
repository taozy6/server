#ifndef _EPOLL_SERVER_
#define _EPOLL_SERVER_

void epoll_run(int port);
int init_listen_fd(int epfd,int port);
void *do_accept(void *p);
int get_line(int cfd,char*buf,int size);
void disconnect(int cfd,int epfd);
int hexit(char c);
void encode_str(char* to, int tosize, const char* from);
void decode_str(char *to, char *from);
void* do_read(void*p);
const char *get_file_type(const char *name);
void send_respond_head(int cfd,int no,const char*desp,const char*type);
void send_file(int cfd,const char*file);
void send_dir(int cfd,const char*file);
 void http_request(const char *request,int cfd);






#endif




