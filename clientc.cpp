#include <cstdlib>
#include <iostream>
#include <cstdio>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <climits>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <ctime>
#include <sys/epoll.h>
#include <fstream>
#include <errno.h>
#include "server.h"
#include "client.h"
#include <iomanip>
#include <cstring>

// get sockaddr, IPv4 or IPv6:
#define maxPeers 10
#define BACKLOG 10

#define default_server "timberlake.cse.buffalo.edu"

extern const char* server_port;

host_info& host_info:: operator= (const host_info b){
	file_descriptor=b.file_descriptor;
	strcpy(hostname,b.hostname);
	strcpy(ipstr,b.ipstr);
	strcpy(port,b.port);
	return *this;

}

void client_operations ::add_connection_list(int file_desc, const char* port){

	struct sockaddr_storage addr;
	socklen_t len = sizeof addr;
	char hostname[MAXMSGSIZE];
	char ipstr[INET6_ADDRSTRLEN];
	char service[20];
	getpeername(file_desc, (struct sockaddr*)&addr, &len);

	if (addr.ss_family == AF_INET) {
		struct sockaddr_in *s = (struct sockaddr_in *)&addr;

		inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof ipstr);
	} else { // AF_INET6
		struct sockaddr_in6 *s = (struct sockaddr_in6 *)&addr;

		inet_ntop(AF_INET6, &s->sin6_addr, ipstr, sizeof ipstr);
	}

	getnameinfo((struct sockaddr*)&addr,sizeof addr,hostname,sizeof hostname, service, sizeof service,0);
	std::cout<<"connected to "<<hostname<<std::endl;
	connected_list.at(connected_list_idx).hostname=new char [strlen(hostname)];
	strcpy(connected_list.at(connected_list_idx).hostname,hostname);
	connected_list.at(connected_list_idx).port=new char [strlen(port)];
	strcpy(connected_list.at(connected_list_idx).port,port);
	strcpy(connected_list.at(connected_list_idx).ipstr,ipstr);
	connected_list.at(connected_list_idx).file_descriptor=file_desc;
	connected_list_idx++;
}

void client_operations ::send_download_command(int file_desc,const char *send_cmd){

	set_connection_on(file_desc,true);
	int count=sendall(file_desc,(unsigned char *)send_cmd,MAXMSGSIZE);
	if(count!=0){
		perror("send");

	}
}


char* client_operations ::remove_from_connected_list(int file_desc,char *host){

	for(int i=0;i<connected_list_idx;i++){
		if(connected_list.at(i).file_descriptor==file_desc){
			strcpy(host,connected_list.at(i).hostname);
			connected_list.at(i)=connected_list.at(--connected_list_idx);

		}
	}
	return (char *)host;
}

void client_operations ::set_connection_on(int clientfd ,bool val){
	for(int i=0;i<connected_list_idx;i++){
		if(connected_list.at(i).file_descriptor==clientfd){
			connected_list.at(i).connection_on=val;

		}
	}

}

bool client_operations ::is_download_on(int clientfd){
	for(int i=0;i<connected_list_idx;i++){
		if(connected_list.at(i).file_descriptor==clientfd){
			return connected_list.at(i).connection_on;
		}
	}
	return false;
}

bool client_operations ::is_connection_present(const char* host, const char* port){

	for(int i=0;i<connected_list_idx;i++){
		if(!strcmp(connected_list.at(i).hostname,host) && !strcmp(connected_list.at(i).port,port) ){
			return true;
		}
	}
	return false;
}


bool client_operations ::is_valid_peer(const char* host){
	char temp_host[46]="::ffff:";
	strcat(temp_host,host);
	for (int i=0;i<peer_max;i++){
		if(!strcmp(peer_list.at(i).hostname,host) || !strcmp(peer_list.at(i).ipstr,host) || !strcmp(peer_list.at(i).ipstr,temp_host)){
			return true;
		}
	}
	return false;
}

client_operations ::client_operations(){
	peer_max=0;
	peer_list.resize(10);
	connected_list.resize(10);
	connected_list_idx=0;
}


int client_operations ::connect_to_port(const char* host,const char * port)
{
	int sockfd, numbytes;
	char hostname[MAXMSGSIZE];
	char service[20];
	struct addrinfo hints, *servinfo, *p;
	int rv;
	int error;
	char s[INET6_ADDRSTRLEN];

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if(is_connection_present(host,port)){
		fprintf(stderr,"Connection is already present between peers");
		return -1;
	}

	if ((rv = getaddrinfo(host,port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 2;
	}

	// loop through all the results and connect to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("client: socket");
			continue;
		}

		error=connect(sockfd, p->ai_addr, p->ai_addrlen);
		if(error==-1){
			close(sockfd);
			perror("client: connect");
			continue;
		}
		break;
	}

	if (p == NULL) {
		fprintf(stderr, "client: failed to connect\n");
		return 2;
	}

	inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
			s, sizeof s);

	printf("client: connecting to %s\n", s);
	add_connection_list(sockfd,port);
	freeaddrinfo(servinfo); // all done with this structure
	return sockfd;
}

void client_operations::listen_to_requests(int sfd){
	int eventfd;
	int s;
	int infd;
	int max_host=0;
	struct epoll_event event;
	struct epoll_event* event_array =new epoll_event[10] ;
	struct sockaddr_storage in_addr;
	struct sigaction sa;
	if (listen(sfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}
	eventfd = epoll_create (maxPeers);
	if (eventfd == -1)
	{
		perror ("epoll_create");
		abort ();
	}
	make_entry_to_epoll(0,eventfd);		//listen to stdin
	make_entry_to_epoll(sfd,eventfd);
	printf("server: waiting for connections...\n");

	wait_for_event(eventfd,event_array,sfd);

	delete event_array;

	close (sfd);
}

void client_operations::unpack_peers(char *token){
	char host[MAXMSGSIZE];
	char ipstr[MAXMSGSIZE];
	char port[20];
	strcpy(host,strtok(token,"|"));
	strcpy(ipstr,strtok(NULL,"|"));
	strcpy(port,strtok(NULL,"|"));


	peer_list.at(peer_max).hostname=new char [strlen(host)];
	strcpy(peer_list.at(peer_max).hostname,host);
	strcpy(peer_list.at(peer_max).ipstr,ipstr);
	peer_list.at(peer_max).port=new char [strlen(port)];
	strcpy(peer_list.at(peer_max).port,port);
	peer_list.at(peer_max).file_descriptor=-1;
	peer_max++;

}

void client_operations ::handle_rem_downloads(int clientfd){
	set_connection_on(clientfd,false);
	while(!is_download_on(clientfd) && !(send_cmd_buffer.empty())){


		send_download_command(clientfd,send_cmd_buffer.front().c_str());
		send_cmd_buffer.erase(send_cmd_buffer.begin());

	}
}



void client_operations ::recv_and_write_file(int clientfd,const char* filename, const char* file_size, unsigned char* rem_buf){

	char file_n [MAXMSGSIZE];
	int size;
	int total;
	int count;
	size = strtoull(file_size, NULL, 0);
	unsigned char msg_buf[MAXMSGSIZE];
	unsigned char data_buf[PACKET_SIZE];


	if(size==-1){
		fprintf(stderr,"\nFile %s not found at the peer\n",filename);
		handle_rem_downloads(clientfd);
		return;
	}

	struct timespec tstart={0,0}, tend={0,0};		//http://stackoverflow.com/questions/16275444/c-how-to-print-time-difference-in-accuracy-of-milliseconds-and-nanoseconds
	//everything related to finding time difference I got from here^.


	clock_gettime(CLOCK_MONOTONIC, &tstart);
	memcpy(msg_buf,rem_buf,MAXMSGSIZE-9-strlen(filename)-strlen(file_size));
	FILE* File=fopen(filename,"wb");

	strcpy(file_n,filename);
	if(msg_buf){

		count=fwrite(msg_buf,1,MAXMSGSIZE-9-strlen(filename)-strlen(file_size),File);
		total=count;
	}
	fprintf(stderr,"\ntotal %d...\n",total);
	make_socket_blocking(clientfd);
	while(total<size){

		count=recv(clientfd,data_buf,PACKET_SIZE,0);
		data_buf[count-1]='\0';
		if (count == 0)
		{
			/* End of file. The remote has closed the connection. */

			strcpy((char *)data_buf,remove_from_connected_list(clientfd, (char *)data_buf));
			fprintf(stderr,"\n%s closed the connection.\n",data_buf);
			close(clientfd);
			make_socket_non_blocking(clientfd);
			return;
			break;
		}

		else if(count >0){
			fwrite(data_buf,1,count-1,File);

			total+=(count-1);
		}
		else{
			fprintf(stderr,"\nstuck here\n");
		}
	}

	clock_gettime(CLOCK_MONOTONIC, &tend);

	double time=((double)tend.tv_sec + 1.0e-9*tend.tv_nsec) -((double)tstart.tv_sec + 1.0e-9*tstart.tv_nsec);

	size=8*size;
	fprintf(stderr,"\nFile %s received successfully...\n",file_n);
	fprintf(stderr,"\nFile %d time %.2f\n",size,time);

	time= (double)size/time;

	if (time< 1024)
	{
		fprintf(stderr,"\nThe file downloaded at rate of %.2fbits per second...\n",time);
	}
	else if (time < 1048000)
	{
		time=(double) time/1024;
		fprintf(stderr,"\nThe file downloaded at rate of %.2fKbps...\n",time);
	}
	else {
		time=(double) time/(1024*1024);
		fprintf(stderr,"\nThe file downloaded at rate of %.2fMbps...\n",time);
	}
	make_socket_non_blocking(clientfd);
	fprintf(stderr,"\ntotal %d...\n",total);
	fclose (File);
	handle_rem_downloads(clientfd);
}


void client_operations ::send_file_over_socket(int clientfd,const char* filename){

	int count;
	unsigned char msg_buffer[MAXMSGSIZE];
	unsigned char data_buffer[PACKET_SIZE];

	size_t file_size;
	struct stat filestatus;							//http://www.cplusplus.com/forum/unices/3386/
	stat( filename, &filestatus );

	struct timespec tstart={0,0}, tend={0,0};		//http://stackoverflow.com/questions/16275444/c-how-to-print-time-difference-in-accuracy-of-milliseconds-and-nanoseconds
	clock_gettime(CLOCK_MONOTONIC, &tstart);

	FILE* File=fopen(filename,"rb");

	if (!File) {
		perror ("Error opening file");
		sprintf((char *)msg_buffer,"File %s %d \r",filename,-1);

		msg_buffer[MAXMSGSIZE-1]='\0';
		count=sendall(clientfd,msg_buffer,sizeof(msg_buffer));

		if(count!=0){
			perror ("send");
			close(clientfd);
		}
		return;
	}
	fprintf(stderr,"\nSending file now...\n");
	make_socket_blocking(clientfd);
	sprintf((char *)msg_buffer,"File %s %d \r",filename,(int) filestatus.st_size);

	int total= MAXMSGSIZE-strlen((char *)msg_buffer)-1;
	count=fread (msg_buffer+strlen((char *)msg_buffer) , 1, total,File);

	fprintf(stderr,"\ntotal %d...\n",total);
	msg_buffer[MAXMSGSIZE-1]='\0';


	count=sendall(clientfd,msg_buffer,sizeof(msg_buffer));

	if(count!=0){
		perror ("send");
		close(clientfd);
	}

	while(1){
		count=fread (data_buffer , 1, PACKET_SIZE-1,File);

		data_buffer[count]='\0';
		total+=count;
		count=sendall(clientfd,data_buffer,count+1);
		if(count!=0){
			perror ("send");

			close(clientfd);
			break;
		}
		if(total >= (int) filestatus.st_size){


			clock_gettime(CLOCK_MONOTONIC, &tend);

			double time=((double)tend.tv_sec + 1.0e-9*tend.tv_nsec) -((double)tstart.tv_sec + 1.0e-9*tstart.tv_nsec);


			file_size=8*filestatus.st_size;
			fprintf(stderr,"\nFile %s sent successfully...\n",filename);

			fprintf(stderr,"\nFile %d time %.2f\n",file_size,time);
			time=(double) file_size/time;


			if (time< 1024)
			{
				fprintf(stderr,"\nThe file uploaded at rate of %.2fbits per second...\n",time);
			}
			else if (time < 1048000)
			{
				time= (double)time/1024;
				fprintf(stderr,"\nThe file uploaded at rate of %.2fKbps...\n",time);
			}
			else {
				time= (double)time/(1024*1024);
				fprintf(stderr,"\nThe file uploaded at rate of %.2fMbps...\n",time);
			}

			break;
		}

	}

	fprintf(stderr,"\ntotal %d...\n",total);
	fclose(File);
	make_socket_non_blocking(clientfd);
}

int client_operations ::return_first_occr(const char* str, char c){

	for (int i=0;i<MAXMSGSIZE;i++){
		if(str[i]==c){
			return i;
		}
	}
	return -1;
}

void client_operations :: split_return(const char* str, char c, char *token){

	int i=5;
	int j=0;
	while(str[i]!=c){
		*(token+j)=str[i++];
		j++;
	}
	token[j]='\0';
}

void client_operations ::recv_requests_client(int clientfd){
	char split_token[MAXMSGSIZE];
	int done = 0;

	ssize_t count;
	unsigned char buf[MAXMSGSIZE];

	char token_arr[MAXMSGSIZE];
	char* token=&token_arr[0];
	count = recv (clientfd, buf, MAXMSGSIZE, 0);
	buf[count-1]='\0';
	if (count == -1)
	{
		/* If errno == EAGAIN, that means we have read all
				                         data. So go back to the main loop. */
		if (errno != EAGAIN)
		{
			perror ("read");

		}

	}
	else if (count == 0)
	{
		/* End of file. The remote has closed the
				                         connection. */

		strcpy((char *)buf,remove_from_connected_list(clientfd, (char *)buf));
		fprintf(stderr,"\n%s closed the connection.\n",buf);
		close(clientfd);

	}
	else{
		char host_arr[MAXMSGSIZE];

		char ipstr[MAXMSGSIZE];
		char port[20];
		token=strtok((char *)buf," ");
		char *host=&host_arr[0];
		if(token){

			if (!strcmp(token,"Peers")){
				strcpy(token,strtok(NULL,"\n"));
				host=strtok(token,"|");
				while(host!=NULL){

					strcpy(ipstr,strtok(NULL,"|"));
					strcpy(port,strtok(NULL,"|\n"));

					peer_list.at(peer_max).hostname=new char [strlen(host)];
					peer_list.at(peer_max).port=new char [strlen(port)];

					strcpy(peer_list.at(peer_max).ipstr,ipstr);
					strcpy(peer_list.at(peer_max).port,port);
					strcpy(peer_list.at(peer_max).hostname,"");
					strcpy(peer_list.at(peer_max).hostname,host);
					peer_list.at(peer_max).file_descriptor=-1;
					peer_max++;
					host=strtok(NULL,"|\n");
				}
			}

			else if (!strcmp(token,"File")){
				char file_size[20];


				split_return((char *)(buf),' ',split_token);
				split_return((char *)(buf+strlen(split_token)+1),' ',file_size);

				fprintf(stderr,"\nReceiving file now...\n");
				recv_and_write_file(clientfd,split_token,file_size,(buf+return_first_occr((char *)buf,'\r')+1));
			}

			else if (!strcmp(token,"Send")){
				strcpy(split_token,strtok(NULL,"\n"));
				send_file_over_socket(clientfd,split_token);
			}

			else {
				fprintf(stderr,"The server does not recognize %s command\n",token);
				close(clientfd);
			}
		}
	}

}



void inline client_operations ::recv_stdin_client(int eventfd){
	int clientfd;

	ssize_t count;
	char buf[MAXMSGSIZE];
	char send_cmd[MAXMSGSIZE];
	char firstarg_arr[MAXMSGSIZE];
	char secondarg_arr[MAXMSGSIZE];
	char *firstarg=firstarg_arr;
	char *secondarg=secondarg_arr;
	char *token=new char[MAXMSGSIZE];
	errno=0;


	count= read (0,buf,sizeof buf);

	if (count == -1)
	{
		/* If errno == EAGAIN, that means we have read all
			                         data. So go back to the main loop. */
		if (errno != EAGAIN)
		{
			perror ("read");

		}

	}
	else{
		if(buf!=NULL){
			token=strtok(buf," \r\n");
			if(token!=NULL){
				server_operations::toupper(token);
				firstarg=strtok(NULL," \r\n");
				secondarg=strtok(NULL," \r\n");

				if (!strcmp(token,"HELP")){
					std::cout<<"Command Help"<<std::endl;
					std::cout<<"Help"<<std::setw(10)<<"Displays this help"<<std::endl;

					std::cout<<"MYIP"<<std::setw(10)<<"Display the IP address of this process."<<std::endl;
					std::cout<<"MYPORT"<<std::setw(10)<<"MYPORT Display the port on which this process is listening for incoming connections."<<std::endl;
					std::cout<<"REGISTER <server IP> <port_no>"<<std::setw(10)<<"Register the client to the server at timberlake at port_no ."<<std::endl;
					std::cout<<"CONNECT <destination> <port no>"<<std::setw(10)<<"Connect to the destination at port_no ."<<std::endl;
					std::cout<<"LIST"<<std::setw(10)<<"LIST all the available peers of the connection"<<std::endl;
					std::cout<<"TERMINATE <connection id>"<<std::setw(10)<<"Terminate the connection "<<std::endl;
					std::cout<<"EXIT"<<std::setw(10)<<"Exit the process"<<std::endl;
					std::cout<<"UPLOAD <connection id> <file name>"<<std::setw(10)<<"Upload file file_name to peer"<<std::endl;
					std::cout<<"DOWNLOAD <connection id 1> <file1>"<<std::setw(10)<<"Download file file_name from peer"<<std::endl;
					std::cout<<"CREATOR"<<std::setw(10)<<"Display creator's name and relevant info."<<std::endl;

				}
				else if (!strcmp(token,"CREATOR")){
					std::cout<<"Name: Swapnesh Gandhi UBIT ID: swapnesh EMAIL: swapnesh@buffalo.edu"<<std::endl;
				}


				else if (!strcmp(token,"REGISTER")){

					if(firstarg!=NULL){

						int server_sock=connect_to_port(default_server,firstarg);
						if (server_sock>2){
							strcpy(send_cmd,"REGISTER ");
							strcat(send_cmd,server_port);
							strcat(send_cmd,"\n");
							make_socket_non_blocking(server_sock);
							make_entry_to_epoll(server_sock,eventfd);

							count=sendall(server_sock,(unsigned char *)send_cmd,sizeof send_cmd);
							if(count!=0){
								perror ("send");
							}

						}

					}
					else{
						fprintf(stderr,"Invalid command; use help for command help\n");

					}
					return;
				}

				else if (!strcmp(token,"MYPORT")){
					fprintf(stderr, "MY listening port is %s\n",server_port);

				}
				else if (!strcmp(token,"MYIP")){
					strcpy(buf,my_ip(buf));
					if(strcmp(buf,"error")){
						fprintf(stderr, "MY IP is %s\n",buf);
					}
					else{
						fprintf(stderr, "Error occurred while retrieving IP\n");

					}
					return;
				}
				else if (!strcmp(token,"UPLOAD")){
					if (firstarg !=NULL && secondarg!=NULL){
						char *endptr;
						int connection_id = strtol(firstarg, &endptr, 10);
						if ((errno == ERANGE && (connection_id == INT_MAX || connection_id == INT_MIN))
								|| (errno != 0 && connection_id == 0)) {
							perror("strtol");

						}

						if (endptr == firstarg) {
							fprintf(stderr, "No digits were found\n");
							fprintf(stderr,"Invalid command use help for command help\n");

						}
						else{

							if(connection_id<=connected_list_idx && connection_id!=1){
								clientfd=connected_list.at(connection_id-1).file_descriptor;
							}
							else{
								fprintf(stderr,"The connection id specified by you either does not exist or trying to download from server\n");
								return;
							}

							send_file_over_socket(clientfd,secondarg);
						}
					}

					else{
						fprintf(stderr,"Invalid command use help for command help\n");
					}
					return;
				}
				else if (!strcmp(token,"DOWNLOAD")){
					char *filename;

					if (firstarg !=NULL && secondarg!=NULL){
						redo_loop:		char *endptr;
						int connection_id = strtol(firstarg, &endptr, 10);
						if ((errno == ERANGE && (connection_id == INT_MAX || connection_id == INT_MIN))
								|| (errno != 0 && connection_id == 0)) {
							perror("strtol");

						}
						if (endptr == firstarg) {
							fprintf(stderr, "No digits were found\n");
							fprintf(stderr,"Invalid command use help for command help\n");

						}
						filename=secondarg;
						if(connection_id<=connected_list_idx && connection_id!=1){
							strcpy(send_cmd,"Send ");
							strcat(send_cmd,filename);
							strcat(send_cmd,"\n");

							clientfd=connected_list.at(connection_id-1).file_descriptor;
							if(!is_download_on(clientfd)){

								//find clientfd for connection_id
								send_download_command(clientfd,send_cmd);

							}
							else{
								std::string str(send_cmd);
								send_cmd_buffer.push_back(str);

							}
							firstarg=strtok(NULL," \r\n");
							secondarg=strtok(NULL," \r\n");
							if (firstarg !=NULL && secondarg!=NULL){
								goto redo_loop;
							}

						}
						else{
							fprintf(stderr,"The connection id specified by you either does not exist or trying to download from server\n");
							return;
						}

					}
					else{
						fprintf(stderr,"Invalid command use help for command help\n");


					}
					return;
				}
				else if (!strcmp(token,"CONNECT")){
					if(firstarg!=NULL && secondarg!=NULL){
						if(connected_list_idx==4){
							std::cout<<"\nReached limit of maximum connections terminate some connections first to add a new connection\n";
							return;
						}
						if (is_valid_peer(firstarg)){
							clientfd= connect_to_port(firstarg,secondarg);

							if(clientfd <=2){
								return;
							}
							else {
								make_entry_to_epoll(clientfd,eventfd);
								make_socket_non_blocking(clientfd);

							}
						}
						else{
							fprintf(stderr,"\nThe peer specified is not a valid peer \n");
						}
					}
					else{
						fprintf(stderr,"Invalid command use help for command help\n");
					}

					return;
				}

				else if (!strcmp(token,"TERMINATE")){
					if (firstarg!=NULL){
						char *endptr;
						int connection_id = strtol(firstarg, &endptr, 10);
						if ((errno == ERANGE && (connection_id == INT_MAX || connection_id == INT_MIN))
								|| (errno != 0 && connection_id == 0)) {
							perror("strtol");
							return;

						}
						if (endptr == firstarg) {
							fprintf(stderr, "No digits were found\n");
							std::cout<<"Invalid command use help for command help\n";
							return;
						}

						if(connection_id<=connected_list_idx && connection_id!=1){
							terminate_client(connection_id-1);

						}
						else{
							fprintf(stderr,"Connection id entered is either not valid or you trying to terminate connection with server\n");

						}
					}
					else{
						fprintf(stderr,"Invalid command use help for command help\n");


					}
					return;
				}

				else if (!strcmp(token,"LIST")){
					std::cout<<"\nConnected peers are:\n";
					for (int i=0;i<connected_list_idx;i++){
						std::cout<<i+1<<"\t" <<connected_list.at(i).hostname<<"\t"<<connected_list.at(i).ipstr<< "\t" << connected_list.at(i).port<<"\n";

					}
					return;
				}

				else if(!strcmp(token,"EXIT")){
					fprintf(stderr,"The Client is exiting..\n");
					exit(0);
				}

				else {
					fprintf(stderr,"Invalid command use help for command help\n");

				}

			}
		}
	}
}



void client_operations ::terminate_client(int i){

	close(connected_list.at(i).file_descriptor);
	connected_list.at(i)=connected_list.at(connected_list_idx);
	connected_list_idx--;

}

char * client_operations ::return_port_from_peer_list(int peer_fd){
	struct sockaddr_storage addr;
	socklen_t len = sizeof addr;
	char hostname[MAXMSGSIZE];
	char ipstr[INET6_ADDRSTRLEN];
	char service[20];
	getpeername(peer_fd, (struct sockaddr*)&addr, &len);
	if (addr.ss_family == AF_INET) {
		struct sockaddr_in *s = (struct sockaddr_in *)&addr;

		inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof ipstr);
	} else { // AF_INET6
		struct sockaddr_in6 *s = (struct sockaddr_in6 *)&addr;

		inet_ntop(AF_INET6, &s->sin6_addr, ipstr, sizeof ipstr);
	}

	getnameinfo((struct sockaddr*)&addr,sizeof addr,hostname,sizeof hostname, service, sizeof service,0);

	for (int i=0;i<peer_max;i++){
		if(!strcmp(peer_list.at(i).hostname,hostname)){
			return peer_list.at(i).port;
		}
	}
	return NULL;
}

void inline client_operations ::wait_for_event(int eventfd, struct epoll_event* event_array, int sfd){
	struct sockaddr_storage in_addr;
	char *port=new char[20];
	while (1)
	{
		int n, i;
		int infd;
		n = epoll_wait (eventfd, event_array, maxPeers, -1);
		for (i = 0; i < n; i++)
		{

			if(event_array[i].events & EPOLLRDHUP){
				//***********
				char hostname [265];
				strcpy(hostname,remove_from_connected_list(event_array[i].data.fd,hostname));

				fprintf (stderr,"\nThe client %s closed connection",hostname);
				continue;

			}
			else if ((event_array[i].events & EPOLLERR) || (event_array[i].events & EPOLLHUP) || (!(event_array[i].events & EPOLLIN)))
			{
				/* An error has occured on this fd, or the socket is not
			                 ready for reading (why were we notified then?) */
				fprintf (stderr, "epoll error\n");
				close (event_array[i].data.fd);
				continue;
			}
			else if (sfd == event_array[i].data.fd)
			{
				/* We have a notification on the listening socket, which
			                 means one or more incoming connections. */
				socklen_t in_len;

				in_len = sizeof in_addr;
				infd = accept (sfd, (struct sockaddr *)&in_addr, &in_len);
				if (infd == -1)
				{
					perror ("accept");
					continue;
				}
				port=return_port_from_peer_list(infd);
				if(!port){
					fprintf (stderr, "Invalid connection rejected\n");
					close(infd);
					continue;
				}
				add_connection_list(infd,port);
				make_socket_non_blocking(infd);
				make_entry_to_epoll(infd,eventfd);
				continue;

			}

			else if (event_array[i].data.fd==0){
				recv_stdin_client(eventfd);

			}
			else
			{
				/* We have data on the fd waiting to be read. Read and
			                 display it. We must read whatever data is available
			                 completely, as we are running in edge-triggered mode
			                 and won't get a notification again for the same
			                 data. */

				recv_requests_client(event_array[i].data.fd);


			}
		}

	}
}

