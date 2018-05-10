#include "servercommon.hpp"

#include <iostream>
#include <sstream>
#include <cstring>

// Implementation for the exit usage method.
void exitUsage(int argc, char** argv, int exitCode) {
	std::cerr << "ChatServer - A simple chatroom server.\n";
	std::cerr << "Usage: " << argv[0] << " <serverPort> [<listenQueue>=10]\n";
	exit(exitCode);
}

// Implementation for the exit POSIX method.
void exitPosix(const char* message, int exitCode) {
	std::cerr << message;
	perror("");	// Just print out the POSIX error.
	exit(exitCode);
}

// Implementation for parsing the server arguments.
void parseArguments(int argc, char** argv, int& serverPort, long& listenQueue) {
	// Default value for listen queue.
	listenQueue = 10;
	
	// Make sure the server port is specified.
	if(argc <= 1) {
		// Report an error if the argument could not be parsed.
		std::cerr << "Error: the server port should be specified.\n\n";
		exitUsage(argc, argv, eNoServerPort);
	}
	
	// Parse the server port argument.
	if(sscanf(argv[1], "%d", &serverPort) == 0) {
		std::cerr << "Error: the server port should be an integer.\n\n";
		exitUsage(argc, argv, eServerPortNotNumber);
	}
	
	// Parse the listen queue argument.
	if(argc >= 3 && sscanf(argv[2], "%ld", &listenQueue) == 0) {
		std::cerr << "Error: the listen queue should be an integer.\n\n";
		exitUsage(argc, argv, eListenQueueNotNumber);
	}
}

// Implementation for formating an socket address.
std::string ipPort(const struct sockaddr_in& address) {
	std::stringstream result;
	result << inet_ntoa(address.sin_addr) << ":" << ntohs(address.sin_port);
	return result.str();
}

// Implementation for creating a server socket.
int createServerSocket(int argc, char** argv, struct sockaddr_in& serverAddress) {
	// Parse the arguments.
	int serverPort;
	long listenQueue;
	parseArguments(argc, argv, serverPort, listenQueue);
	
	// Attempt to create the socket, and configure the server socket so that the 
	// port will be resuable soon after the socket is closed.
	int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
	int optValSoReuseAddr = 1;
	if(serverSocket < 0 || (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR,
		&optValSoReuseAddr, sizeof(optValSoReuseAddr)) < 0)) exitPosix(
			"The server socket cannot be created!\n", eServerSocketCreation);
	
	// Defines the address for the server to bind onto.
	size_t sockAddrSize = sizeof(struct sockaddr_in);
	memset(&serverAddress, 0, sockAddrSize);
	serverAddress.sin_family      = AF_INET;           // IPv4
	serverAddress.sin_addr.s_addr = htonl(INADDR_ANY); // * or 0.0.0.0
	serverAddress.sin_port        = htons(serverPort); // serverPort
	
	// Attempt to bind and listen on the server port.
	if((bind(serverSocket, (struct sockaddr*)&serverAddress, sockAddrSize)) < 0) 
		exitPosix("The server socket cannot bind to port!\n", eServerSocketBinding);
	if((listen(serverSocket, listenQueue)) < 0) exitPosix(
		"The server socket cannot listen on the port!\n", eServerSocketListen);
		
	return serverSocket;
}