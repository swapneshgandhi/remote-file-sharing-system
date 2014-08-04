/*The main program for application
 * calls server or client methods depending on 1st argument.
 *
 * @author: Swapnesh Gandhi
 */


#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include <cstring>
#include "server.h"
#include "client.h"

using namespace std;

//Server listening port, global variable shared with server and client files (better alternative than passing server_port to functions)
const char* server_port;

int main(int argc, char* argv []){

	//Server object to start listening to the port.



	if(argc!=3){
		std::cout<<"Usage: sys <s or c> <port_no> \n";
		exit(1);
	}

	//The second argument the listening port.
	server_port=argv[2];

	//If the first argument is s then the program will run as server.
	if(!strcmp(argv[1],"s")){
		server_operations server;

		//setup the server at the listening port.
		int sockfd=server.server_setup(argv[2]);

		if (sockfd<=2){
			exit(1);
		}

		//make the port non blocking, I have used epoll interface for the async I/O,
		//it's less complicated to use non blocking sockets with epoll, even man page of epoll advises the same.
		server.make_socket_non_blocking (sockfd);

		//listen to requests on the server socket.
		server.listen_to_requests(sockfd);
	}

	//If the first argument is c then the program will run as client.
	else if(!strcmp(argv[1],"c")){

		client_operations client;

		//setup the server at the listening port.
		int sockfd=client.server_setup(argv[2]);
		if (sockfd<=2){
			exit(1);
		}
		//make the port non blocking
		client.make_socket_non_blocking (sockfd);
		//listen to requests on the server socket.
		client.listen_to_requests(sockfd);

	}

	else{
		exit(1);
	}


	return 0;

}

