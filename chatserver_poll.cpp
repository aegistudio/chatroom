/**
 * @file chatserver_poll.cpp
 *
 * 2018 @ Nanjing University Software Institute
 * @author Haoran Luo
 * @brief Implementation of the poll() server.
 *
 * A poll() server responds to the newly come client connections and messages from different
 * clients, and respond to them just in the main loop, so is called single thread.
 *
 * The clients are non-blocking. If a single invocation to write() could not send all data to 
 * the client, the remaining data will be buffered (in a structure of client control block), 
 * and the POLLOUT event will be monitored.
 *
 * The main loop will continue attempting to send remaining data and remove the registered 
 * monitoring of POLLOUT if all data is sent. Write error will not directly close the client
 * connection as there may be remained data in the input buffer in our implementation.
 */

// The system headers.
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/mman.h>

// The unix headers.
#include <signal.h>
#include <poll.h>
#include <fcntl.h>
#include <sched.h>
#include <semaphore.h>
#include <stropts.h>

// The C standard headers.
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>

// The C++ STL headers.
#include <set>
#include <map>
#include <iostream>
#include <sstream>
#include <queue>

// The user defined headers.
#include "servercommon.hpp"
#include "chatlogic.hpp"
#include "util.hpp"

// Defines the client control data.
struct CsRtPollClientService : public CsDtClientService {
	// The file descriptor of the client socket.
	int clientSocket;
	
	// The poll structure.
	unsigned pollIndex;

	/// The handler for the client socket. Will be null at first.
	CsDtClientHandler* handler;
	
	/// The client's address and length.
	struct sockaddr_in clientAddress;
	
	/// Pointing to position in the handler's buffer.
	size_t readPointer;
	
	/// Pointing to the server's unified name map.
	std::string clientName;
	std::map<int, CsRtPollClientService>* clientServices;
	std::set<std::string>* clientNameSet;
	std::vector<struct pollfd>* pollStructs;
	
	/// The sending buffers.
	std::queue<char*> outputBuffers;
	std::queue<size_t> outputSizes;
	size_t writePointer;

	// Initialize the control block as empty.
	CsRtPollClientService(): clientNameSet(nullptr), clientServices(nullptr), pollStructs(nullptr),
		readPointer(0), writePointer(0),  clientSocket(-1), pollIndex(0) {}
	
	// Initialize the control block.
	CsRtPollClientService(std::map<int, CsRtPollClientService>* clientServices,
		std::set<std::string>* clientNameSet, std::vector<struct pollfd>* pollStructs, 
		const struct sockaddr_in& _clientAddress, int clientSocket, int pollIndex): 
		clientNameSet(clientNameSet), clientServices(clientServices), readPointer(0), writePointer(0), 
		clientSocket(clientSocket), pollStructs(pollStructs), pollIndex(pollIndex) {
			memcpy(&clientAddress, &_clientAddress, sizeof(sockaddr_in));
		}
	
	// Move the control block.
	void move(CsRtPollClientService&& rhs) {
		// Move flat data.
		clientSocket = rhs.clientSocket;
		rhs.clientSocket = -1;
		clientNameSet = rhs.clientNameSet;
		rhs.clientNameSet = nullptr;
		clientServices = rhs.clientServices;
		rhs.clientServices = nullptr;
		readPointer = rhs.readPointer;
		rhs.readPointer = 0;
		writePointer = rhs.writePointer;
		rhs.writePointer = 0;
		pollStructs = rhs.pollStructs;
		rhs.pollStructs = nullptr;
		pollIndex = rhs.pollIndex;
		rhs.pollIndex = 0;
		
		// Move the buffers.
		outputBuffers = rhs.outputBuffers;
		outputSizes = rhs.outputSizes;

		// Move the socket address.
		memcpy(&clientAddress, &rhs.clientAddress, sizeof(sockaddr_in));
	}
	
	// Attempt to receive a indicated by the handler the client socket.
	// Return -1 means end of stream or pipe error. In such case the client should be closed.
	// Return  0 means data has been successfully read from this client socket.
	int receive() {
		// Retrieve the next size and buffer.
		size_t size; void* buffer;
		handler -> next(size, buffer);
		
		// Attempt to read from the client socket.
		ssize_t readSize = read(clientSocket, 
			offset(buffer, readPointer), size - readPointer);
		
		// Check the code while invoking read.
		if(readSize < 0) {
			if(errno == EWOULDBLOCK) return 0;	// Just nothing changed.
			else return -1;				// There's error reading.
		}
		else if(readSize == 0) return -1;		// The socket is no longer readable.
		else {						// Update the read pointer.
			// Current strip has finished its reading.
			if((readPointer + readSize) == size) {
				handler -> bufferFilled();
				readPointer = 0;
			}
			
			// Current strip has not finished its reading.
			else readPointer = readPointer + readSize;
		}
		
		// Make sure after the reading the next size will not be zero.
		handler -> next(size, buffer);
		if(size == 0) return -1;
		else return 0;
	}
	
	// Perform sending in the main loop.
	// Can only be invoked when the POLLOUT event occurs. And will modify the poll struct's flag.
	// Return -1 means pipe error occurs while sending.
	// Return  0 means the sending in the mainly has ended without error.
	int transfer() {
		errno = 0;

		// Loop sending data.
		while(!outputBuffers.empty()) {
			// Send the current data.
			while(writePointer < outputSizes.front()) {
				ssize_t newlySent = write(clientSocket, 
					offset(outputBuffers.front(), writePointer), 
					outputSizes.front() - writePointer);

				// Handle the newly sent size.
				if(newlySent < 0) {
					if(errno != EWOULDBLOCK) return -1;	// Write error.
					else break;				// Write buffer full.
				}
				else if(newlySent == 0) return -1;		// Write error.
				else writePointer += newlySent;
			}

			// See whether the transfer has finished.
			if(writePointer < outputSizes.front()) break;
			else {
				delete[] outputBuffers.front();
				outputBuffers.pop();
				outputSizes.pop();
				writePointer = 0;
			}
		}

		// Remove poll flag if ends writing.
		if(outputBuffers.empty()) {
			(*pollStructs)[pollIndex].events |= POLLOUT;
			(*pollStructs)[pollIndex].events ^= POLLOUT;
		}

		return 0;
	}
	

	// Attempt to push data to send to the client socket.
	// Will modify the poll struct's flag.
	void nextSend(const void* buffer, size_t size) {
		if(outputBuffers.size() > 0) {
			// Directly clone and push the buffer.
			char* cloned = new char[size];
			memcpy(cloned, buffer, size);
			outputBuffers.push(cloned);
			outputSizes.push(size);
		}
		else {
			// We should attempt to send some data first.
			size_t dataSent = 0;
			while(dataSent < size) {
				ssize_t newlySent = write(clientSocket, 
					offset(buffer, dataSent), size - dataSent);
				
				// Handle the newly sent size.
				if(newlySent < 0) {
					if(errno != EWOULDBLOCK) break;	// Write error.
					else continue;			// Just be buffer full.
				}
				else if(newlySent == 0) return;
				else dataSent += newlySent;
			}

			// We cannot send the data, so we should clone.
			if(dataSent < size) {
				char* cloned = new char[size - dataSent];
				memcpy(cloned, offset(buffer, dataSent), size - dataSent);
				outputBuffers.push(cloned);
				outputSizes.push(size);
				writePointer = dataSent;
				(*pollStructs)[pollIndex].events |= POLLOUT;
			}
		}
	}

	virtual std::string ipPort() {
		return ::ipPort(clientAddress);
	}
	
	virtual bool userOnline(const std::string& online) {
		if(clientNameSet -> count(online) > 0) return false;
		clientNameSet -> insert(clientName = online);
		return true;
	}
	
	virtual std::set<std::string> listOnlineUsers() {
		return *clientNameSet;
	}
	
	virtual void broadcast(const std::string& message, std::set<std::string> mutedUsers) {
		for(auto& clientService : *clientServices) 
				if((clientService.second.clientName != "") 
				&& (mutedUsers.count(clientService.second.clientName) == 0)) 
						clientService.second.send(message);
	}
	
	virtual void log(const std::string& logging) {
		// Obviously no synchronization is required in single thread.
		std::clog << logging << std::endl;
	}
	
	virtual void send(const std::string& message) {
		// Construct the client sending packet.
		CsDtWriteBuffer buffer;
		buffer.write(0);
		buffer.write(message);
		nextSend(buffer.data(), buffer.size());
	}
};

// The main function of the poll() server.
int main(int argc, char** argv) {
	// Create the server socket.
	struct sockaddr_in serverAddress;
	int serverSocket = createServerSocket(argc, argv, serverAddress);
	
	// Just shield the SIGPIPE signal for our program.
	struct sigaction sa;
	sa.sa_handler = [] (int) -> void {};
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	if(sigaction(SIGPIPE, &sa, NULL) < 0) exitPosix(
		"Cannot register sigaction handler.\n", eSigaction);

	// Now the server is ready, so print out the ready message to server log.
	std::clog << format({cfFgCyan}) << "Chat room server is ready at " << format({cfBright}) << 
		ipPort(serverAddress) << format() << format({cfFgCyan}) << "." << format() << std::endl;
	
	// The main loop of the chat server to receive client sockets, create child process
	// for them and check for message to write.
	std::set<std::string> clientNameSet;
	std::map<int, CsRtPollClientService> clientServices;
	std::vector<struct pollfd> polls(1);

	// The first entry should be the server's passive socket entry.
	polls[0].fd = serverSocket;
	polls[0].events = POLLIN;

	// Run the server loop.
	while(true) {
		// Perform polling.
		int numAvailables = poll(polls.data(), clientServices.size() + 1, -1);
		
		// Respond the accept() requests.
		if(numAvailables > 0 && ((polls[0].revents & POLLIN) > 0)) {
			-- numAvailables;
			
			// The temporary client address and socket length.
			struct sockaddr_in clientAddress;
			socklen_t clientAddrLength = sizeof(clientAddress);
			memset(&clientAddress, 0, clientAddrLength);
			
			// Accept in the client.
			int clientSocket = accept(serverSocket, 
				(struct sockaddr*)&clientAddress, &clientAddrLength);

			// Initialize client socket as non-blocking.
			bool isValidClientSocket = false;
			if(clientSocket >= 0) {
				int clientFlag = fcntl(clientSocket, F_GETFL, 0);
				if(clientFlag >= 0) {
					clientFlag |= O_NONBLOCK;
					if(fcntl(clientSocket, F_SETFL, clientFlag) == 0)
						isValidClientSocket = true;
				}
			}
			
			// Only valid client socket will be served later.
			if(isValidClientSocket) {
				// Create new pollfd in the poll list.
				int socketSize = polls.size();
				polls.resize(socketSize + 1);
				polls[socketSize].fd = clientSocket;
				polls[socketSize].events = POLLIN;

				// Add the client control block.
				CsRtPollClientService movedClientService(&clientServices, &clientNameSet, 
					&polls, clientAddress, clientSocket, socketSize);
				clientServices[clientSocket] = {};
				clientServices[clientSocket].move(std::move(movedClientService));
				CsRtPollClientService& clientService = clientServices[clientSocket];
				clientService.handler = CsDtClientHandler::newClientHandler(&clientService);

			}
			else if(clientSocket >= 0) close(clientSocket);
		}
		
		// When a client socket is readable or writable.
		// Notice that we are scheduling client sockets equally.
		std::set<int> killedService;
		for(int i = 1; i < polls.size(); ++ i) {
			if(numAvailables <= 0) break;	// Nore more to service.
			if((polls[i].revents & polls[i].events) == 0) continue;

			-- numAvailables;
			CsRtPollClientService& clientService = clientServices[polls[i].fd];

			if((polls[i].revents & POLLIN) > 0) {
				// Destroy the client socket if it is not available.
				if(clientService.receive() < 0) killedService.insert(polls[i].fd);
			}

			if((polls[i].revents & POLLOUT) > 0) {
				// Destroy the client socket if it is not available.
				// We are scheduling all clients so that they will be served equally. And if we just 
				// kill the service, we are likely to lose data already in the tranferred buffer.
				if(clientService.transfer() < 0) {
					polls[i].events |= POLLOUT;
					polls[i].events ^= POLLOUT;
				}
			}
		}

		// Remove killed requests and update poll struct reference.
		if(killedService.size() > 0) {
			for(int i = 1; i < polls.size();) {
				if(killedService.count(polls[i].fd)) {
					CsRtPollClientService& clientService = clientServices[polls[i].fd];

					// Remove the service entry.
					delete clientService.handler;
					close(clientService.clientSocket);
					while(!clientService.outputBuffers.empty()) {
						delete[] clientService.outputBuffers.front();
						clientService.outputBuffers.pop();
					}
					if(clientService.clientName != "") 
						clientNameSet.erase(clientService.clientName);
					clientServices.erase(polls[i].fd);

					// Remove the poll entry.
					if(i < polls.size() - 1) {
						polls[i].fd     = polls[polls.size() - 1].fd;
						polls[i].events = polls[polls.size() - 1].events;
						clientServices[polls[i].fd].pollIndex = i;
					}
					polls.resize(polls.size() - 1);
				}
				else ++ i;
			}
		}
	}
}
