HOSTNAME=$(shell hostname)
ifeq (${HOSTNAME},highgate.cse.buffalo.edu)
CC:=/usr/bin/g++
else
CC:=g++
endif

all: swapnesh_proj1.o server.o client.o
	${CC} -o swapnesh_proj1 swapnesh_proj1.o server.o client.o -lrt -g
	
swapnesh_proj1.o: swapnesh_proj1.cpp server.cpp server.h client.cpp client.h
	${CC} -c swapnesh_proj1.cpp -o swapnesh_proj1.o -g
	
server.o: server.cpp server.h
	${CC} -c server.cpp -o server.o -g 
	 
client.o: client.cpp client.h server.h
	${CC} -c client.cpp -o client.o -g
	
clean:
	rm -f *.o
	rm swapnesh_proj1