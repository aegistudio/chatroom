#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string>

// Defines the exit code when there's anything unexcepted occurs.
enum CsExServerErrorCode {
	// Argument errors.
	eNoServerPort = 1,
	eServerPortNotNumber,
	eListenQueueNotNumber,
	
	// Runtime errors.
	eServerSocketCreation,
	eServerSocketBinding,
	eServerSocketListen,
	
	// For those chat server based on fork.
	ePipeCreation,
	eSigaction,
	eSharedMemory,
};

/// Used to print out the usage when there's no need to launch the server.
void exitUsage(int argc, char** argv, int exitCode);

/// Used to print out the POSIX error and exit.
void exitPosix(const char* message, int exitCode);

/// Used to parse the argument of the chat server program.
void parseArguments(int argc, char** argv, int& serverPort, long& listenQueue);

/// Format the ip and port as ip:port string.
std::string ipPort(const struct sockaddr_in& address);

/// Create a server socket with given arguments.
int createServerSocket(int argc, char** argv, struct sockaddr_in& serverAddress);