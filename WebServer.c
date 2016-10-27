#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <errno.h>
#include <pthread.h>
#include <dirent.h>

void* handle_client(void *sock); 

char* folder;
char* port;

int main(int argc, char** argv) {	

	if(argc<3) { printf("Usage: hw2 <port> <folder>\n"); exit(1); }

	port = argv[1];
	folder = argv[2];
	
	int server_sock = socket(AF_INET6, SOCK_STREAM, 0);
	if(server_sock < 0) {
		perror("Creating socket failed: ");
		exit(1);
	}
	
	int reuse_true = 1;
	setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &reuse_true, sizeof(reuse_true));

	struct sockaddr_in6 addr; 	// internet socket address data structure
	addr.sin6_family = AF_INET6;
	addr.sin6_port = htons(atoi(port)); // byte order is significant
	addr.sin6_addr = in6addr_any; // listen to all interfaces
	
	int res = bind(server_sock, (struct sockaddr*)&addr, sizeof(addr));
	if(res < 0) {
		perror("Error binding to port");
		exit(1);
	}

	struct sockaddr_in remote_addr;
	unsigned int socklen = sizeof(remote_addr); 

	// wait for a connection
	res = listen(server_sock,0);
		if(res < 0) {
			perror("Error listening for connection");
			exit(1);
		}
	while(1) {

		
		int sock;
		sock = accept(server_sock, (struct sockaddr*)&remote_addr, &socklen);
		if(sock < 0) {
			perror("Error accepting connection");
			exit(1);
		}
		
		pthread_t client;
		pthread_create(&client,0,handle_client,(void*)sock);
	}

	shutdown(server_sock,SHUT_RDWR);
}

int is_directory(char* path) {
	struct stat filestat;
	if(!stat(path,&filestat))
		return filestat.st_mode & S_IFDIR;
	else return 0;
}

int file_exists(char* path) {
	struct stat filestat;
	return !stat(path,&filestat);
}

void send_file(int sock, char* path) {
	char buf[1400];
	int read_bytes;
	FILE *data=fopen(path,"r");
	
	while((read_bytes=fread(buf,1,sizeof(buf),data))) {
		send(sock,buf,read_bytes,0);
	}
	fclose(data);
}

char* content_type_for_path(char* path) {
	if(strcasestr(path,".html")) 
		return "text/html";
	else if(strcasestr(path,".txt")) 
		return "text/plain";
	else if(strcasestr(path,".gif")) 
		return "image/gif";
	else if(strcasestr(path,".jpg")) 
		return "image/jpeg";
 	else if(strcasestr(path,".png")) 
		return "image/png";				
	else if(strcasestr(path,".ico"))
		return "image/x-icon";
	else
		return "text/plain";
}

void *handle_client(void* arg) {
	int sock = (int)arg;
	
	char request[2000], response[10000], path[255], fullpath[255];
	
	memset(request,0,sizeof(request));

	// keep receiving the request until we find the empty line 
	int recv_accum = 0;
	while(!strstr(request,"\r\n\r\n")) {
		int recv_count = recv(sock, request+recv_accum, sizeof(request)-recv_accum, 0);
		if(recv_count<0) { perror("Receive failed");	exit(1); }
		recv_accum+=recv_count;
	}

	// parse out the path from the request
	if(!sscanf(request,"GET %[^ ] HTTP/1.",path)) {
		sprintf(response,"HTTP/1.0 500 Error: bad request\r\n\r\n");
	}
	else {
		sprintf(fullpath,"%s%s",folder,path);
		// see if the path provided is a directory. if so, use the index.html file.		
		if(is_directory(fullpath)) {
			printf("Path is a directory, appending index.html\n");
			sprintf(fullpath+strlen(fullpath),"index.html");
		}

		printf("Reading file %s\n",fullpath);

		// if the file does not exist, compose 404 error response
		if(!file_exists(fullpath) && !(strstr(fullpath, "index.html"))) {	
			sprintf(response,"HTTP/1.0 404 File non-existent.\r\n"
						         "Content-Type: text/html\r\n\r\n"
						         "<html><body><h1>Error 404: The file your requested does not exist.</h1>"
						         "</body></html>");
		}
		// determine the appropriate content-type header, and compose response header
		else {
			char *content_type=content_type_for_path(fullpath);
			sprintf(response,"HTTP/1.0 200 OK\r\nContent-Type: %s\r\n\r\n",content_type);
		}
	}

	// send off the response header
	send(sock,response,strlen(response),0);

	// send the contents of the file, if a file exists
	if(file_exists(fullpath)) send_file(sock, fullpath);
	else {
		if(strstr(fullpath, "index.html")) {
			DIR * dir;
			int len = strlen(fullpath)-strlen("index.html");
			char pathBuff[255];
			char tmpBuff[255];
			strncpy(pathBuff, fullpath, len);
			pathBuff[len] = '\0';
			dir = opendir(pathBuff);
			if(!dir){
				fprintf(stderr, "pathBuff: %s\n", pathBuff);
				fprintf(stderr, "Cannot open directory '%s': %s\n", fullpath, strerror(errno));
				exit(EXIT_FAILURE);
			}

			sprintf(response, "<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\"><html>\r\n"
					"<title>Directory listing for %s</title>\r\n"
					"<body>\r\n"
					"<h2>Directory listing for %s</h2>\r\n"
					"<hr>\r\n"
					"<ul>\r\n", path, path);
			send(sock,response,strlen(response),0);
			while(1){
				struct dirent * entry;
				entry=readdir(dir);
				if(!entry)
					break;
				strcpy(tmpBuff, pathBuff);
				strcat(tmpBuff, entry->d_name);
				if(is_directory(tmpBuff))
					sprintf(response, "<li><a href=\"%s/\">%s/</a>\r\n", entry->d_name, entry->d_name);
				else
					sprintf(response, "<li><a href=\"%s\">%s</a>\r\n", entry->d_name, entry->d_name);
				send(sock,response,strlen(response),0);
			}
			close(dir);
			sprintf(response, "</ul>\r\n"
					"<hr>\r\n"
					"</body>\r\n"
					"</html>\r\n");
			send(sock,response,strlen(response),0);
		}
	}

	close(sock);
	return 0;
}

