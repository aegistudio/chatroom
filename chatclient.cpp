#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>

#include <set>
#include <poll.h>
#include <iostream>

#include "util.hpp"

enum CsExErrorCode {
	// Argument errors.
	eNoServerAddr = 1,
	eNoServerPort,
	eServerAddressInvalid,
	eServerPortNotNumber,
	eNoClientName,
	eClientNameTooLong,
	
	// Runtime errors.
	eClientSocketCreation,
	eClientSocketConnect,
};

/// Used to print out the usage when there's no need to launch the server.
void exitUsage(int argc, char** argv, int exitCode) {
	std::cerr << "ChatClient - A simple chatroom client.\n";
	std::cerr << "Usage: " << argv[0] << " <serverAddress> <serverPort> <clientName>\n";
	exit(exitCode);
}

/// Used to print out the POSIX error and exit.
void exitPosix(const char* message, int exitCode) {
	std::cerr << message;
	perror("");	// Just print out the POSIX error.
	exit(exitCode);
}

// Process a command line input from the user. Return whether there's write error.
bool processCommandLine(CsDtFileStream& socket, std::string command) {
	CsDtWriteBuffer packet;

	// Single '/' will be treated as command.
	int packetId = 0;
	if(command[0] == '/') {
		command = command.substr(1, command.length() - 1);
		packetId = 1;
		
		// Double '//' will be treated as normal chat, and the 
		// first slash will be ignored.
		if(command.length() > 0 && command[0] == '/') 
			packetId = 0;
	}
	
	// Send only when there's command to send.
	packet.write(packetId);
	packet.write(command);
	
	// Write out the packet.
	if(socket.write(packet.size()) < 0) return false;
	if(packet.writeTo(socket) < 0) return false;

	return true;
}

/// Used to parse the argument of the chat server program.
const char* errorArgument[] = { "server ip", "server port", "client name" };
const int errorExitCode[] = { eNoServerAddr, eNoServerPort, eNoClientName };
void parseArguments(int argc, char** argv, int& serverIp, 
		int& serverPort, char*& clientName, int& clientNameLength) {

	// Make sure the server port and client name are specified.
	if(argc <= 3) {
		// Report an error if the argument could not be parsed.
		std::cerr << "Error: the " << errorArgument[argc - 1] << " should be specified.\n\n";
		exitUsage(argc, argv, errorExitCode[argc]);
	}
	
	// Parse the server address argument.
	serverIp = inet_addr(argv[1]);
	if(serverIp == INADDR_NONE) {
		std::cerr << "Error: the server address specified is invalid.\n\n";
		exitUsage(argc, argv, eServerAddressInvalid);
	}
	
	// Parse the server port argument.
	if(sscanf(argv[2], "%d", &serverPort) == 0) {
		std::cerr << "Error: the server port should be an integer.\n\n";
		exitUsage(argc, argv, eServerPortNotNumber);
	}
	
	// Parse the client name.
	clientName = argv[3];
	if((clientNameLength = strlen(clientName)) >= 64) {
		std::cerr << "Error: the client name is too long.\n\n";
		exitUsage(argc, argv, eClientNameTooLong);
	}
}

/// Used in the sigaction function.
void emptySaHandler(int sig) {}

int main(int argc, char** argv) {
	// Parse the arguments.
	int serverIp, serverPort, clientNameLength; char* clientName;
	parseArguments(argc, argv, serverIp, serverPort, clientName, clientNameLength);
	
	// Attempt to create the socket.
	int clientSocket = socket(AF_INET, SOCK_STREAM, 0);
	if(clientSocket < 0) exitPosix("The server socket cannot be created!\n", 
			eClientSocketCreation);
	
	// Defines the address for the server to bind onto.
	struct sockaddr_in serverAddress;
	size_t sockAddrSize = sizeof(struct sockaddr_in);
	memset(&serverAddress, 0, sockAddrSize);
	serverAddress.sin_family      = AF_INET;           // IPv4
	serverAddress.sin_addr.s_addr = serverIp;          // serverIp
	serverAddress.sin_port        = htons(serverPort); // serverPort
	
	// Attempt to connect to the server port.
	if(connect(clientSocket, (struct sockaddr*)&serverAddress, sockAddrSize) < 0)
		exitPosix("Cannot connect to specified server address!\n", eClientSocketConnect);
	
	// Make the input stream non-blocking, and hold the command buffer.
	fcntl(fileno(stdin), F_SETFL, O_NONBLOCK);
	std::string command;

	// Construct the file stream to simplify the writing.
	CsDtFileStream socket(clientSocket);
	
	// Write the hello message.
	socket.write(std::string(clientName));
	
	// Initialize the poll file descriptors.
	struct pollfd pollfds[2];
	struct pollfd& clientSocketPoll = pollfds[0];
	struct pollfd& stdinPoll = pollfds[1];
	clientSocketPoll.fd = clientSocket;
	clientSocketPoll.events = POLLIN;
	stdinPoll.fd = fileno(stdin);
	stdinPoll.events = POLLIN;
	
	// Read data either from console or from the server.
	bool running = true;
	while(running) {
		// Poll for events.
		int nPollReady = poll(pollfds, 2, -1);
		if(nPollReady < 0) break;
		
		// Check whether client socket is ready.
		if(clientSocketPoll.revents & POLLIN) {
			clientSocketPoll.revents ^= POLLIN;
			
			// Retrieve the packetId.
			int packetId = 0;
			if(socket.read(packetId) < 0) break;
			
			// React to the packet id.
			switch(packetId) {
				case 0:	{ // Line.
					std::string line;
					if(socket.read(line) < 0) running = false;
					else std::cout << line << format() << std::endl;
				} break;
				default:
					running = false;
				break;
			}
		}
		
		// Check whether stdin is ready.
		if(stdinPoll.revents & POLLIN) {
			stdinPoll.revents ^= POLLIN;

			// Loop reading command from the command line.
			char commandBuffer[BUFSIZ + 1];
			while(true) {
				ssize_t readSize = read(fileno(stdin), commandBuffer, BUFSIZ);
				if(readSize > 0) {
					commandBuffer[readSize] = 0;
					command += commandBuffer;
				}
				else {
					if(readSize == 0) running = false;	// End of stream.
					else if(readSize < 0) {			// See what kind of error.
						if(errno != EWOULDBLOCK) running = false;
					}
					break;					// Break the reading loop.
				}
			}

			// See whether to process the command.
			size_t lineBreakPos = std::string::npos;
			while((lineBreakPos = command.find('\n')) != std::string::npos) {
				if(!processCommandLine(socket, command.substr(0, lineBreakPos))) {
					running = false; break;
				}
				command = command.substr(lineBreakPos + 1, 
					command.length() - lineBreakPos - 1);
			}

			if(!running && command.length() > 0) 
				processCommandLine(socket, command);
		}
	}
	
	// Clean up the resource.
	close(clientSocket);
	return 0;
}
