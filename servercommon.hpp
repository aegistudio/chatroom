#pragma once

/**
 * @file servercommon.hpp
 *
 * 2018 @ Nanjing University Software Institute
 * @author Haoran Luo
 * @brief The classes and routines shared between servers.
 *
 * There could be duplicated code of processing console command line and creating a server socket.
 * The header defines classes and routines so that different server models could reuses them easily.
 */
 
// The system headers.
#include <sys/types.h>
#include <sys/socket.h>

// The network headers.
#include <netinet/in.h>
#include <arpa/inet.h>

// The unix headers.
#include <unistd.h>

// The STL headers.
#include <string>

// Defines the exit code when there's anything unexcepted occurs on server side.
enum CsExServerErrorCode {
	// Argument errors.
	eNoServerPort = 1,
	eServerPortNotNumber,
	eListenQueueNotNumber,
	
	// Runtime errors.
	eServerSocketCreation,
	eServerSocketBinding,
	eServerSocketListen,
	eSigaction,
	
	// Warning: Should not return this code directly.
	eMaxCommonError
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