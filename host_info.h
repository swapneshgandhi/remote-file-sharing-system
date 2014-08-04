/* A class to hold the information about a peer
 @author: Swapnesh Gandhi
  */

#include <arpa/inet.h>
class host_info{
	public:
	char *hostname;							//hostname of the peer e.g. timberlake.cse.buffalo.edu
	char *port;							//port no.
	char ipstr[INET6_ADDRSTRLEN];			//IP address e.g. 192.168.1.1
	int file_descriptor;					//file descriptor
	bool connection_on;						//to find whether a download from the given peer is ON. This is used to serialize the
											//downloads with the same peer, Downloads with different peers will happen in parallel.
	double download_st_time;				//holds the download start time for a given file
	host_info& operator= (const host_info b);	// '=' operator for the class assignments.
};
