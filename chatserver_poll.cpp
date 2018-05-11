#include <sys/types.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/mman.h>

#include <signal.h>
#include <poll.h>
#include <fcntl.h>
#include <sched.h>
#include <semaphore.h>
#include <stropts.h>

#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>

#include <set>
#include <map>
#include <iostream>
#include <sstream>

#include "servercommon.hpp"
#include "chatlogic.hpp"
#include "util.hpp"

// Defines the client control data.
struct CsRtPollClientService : public CsDtClientService {
	// The file descriptor of the client socket.
	int clientSocket;
	
	/// The handler for the client socket. Will be null at first.
	CsDtClientHandler* handler;
	
	/// The client's address and length.
	struct sockaddr_in clientAddress;
	
	/// Pointing to position in the handler's buffer.
	size_t readPointer = 0;
	
	/// Pointing to the server's unified name map.
	std::string clientName;
	std::map<int, CsRtPollClientService>* clientServices;
	std::set<std::string>* clientNameSet;
	
	// Initialize the control block as empty.
	CsRtPollClientService(): clientNameSet(nullptr), clientServices(nullptr),
		readPointer(0), clientSocket(-1) {}
	
	// Initialize the control block.
	CsRtPollClientService(std::map<int, CsRtPollClientService>* clientServices,
		std::set<std::string>* clientNameSet, const struct sockaddr_in& _clientAddress, 
		int clientSocket): clientNameSet(clientNameSet), clientServices(clientServices), 
		readPointer(0), clientSocket(clientSocket) {
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
			&((char*)buffer)[readPointer], size - readPointer);
		
		// Check the code while invoking read.
		if(readSize < 0) {
			if(errno == EWOULDBLOCK) return 0;	// Just nothing changed.
			else return -1;						// There's error reading.
		}
		else if(readSize == 0) return -1;		// The socket is no longer readable.
		else {									// Update the read pointer.
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
	
	// Attempt to push data to send to the client socket.
	void nextSend(const void* buffer, size_t size) {
		// Temporarily we assume we will never send data too often.
		size_t dataSent = 0;
		while(dataSent < size) {
			ssize_t newlySent = write(clientSocket, 
				&((const char*)buffer)[dataSent], size - dataSent);
			
			// Handle the newly sent size.
			if(newlySent < 0) {
				if(errno != EWOULDBLOCK) return;	// Write error.
				else continue;						// Just be buffer full.
			}
			else if(newlySent == 0) return;
			else dataSent += newlySent;
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
	std::vector<struct pollfd> polls;
	while(true) {
		polls.resize(clientServices.size() + 1);
		
		// Initialize the outer poll structure.
		polls[0].fd = serverSocket;
		polls[0].events = POLLIN;
		
		auto iter = clientServices.begin();
		for(int i = 1; i <= clientServices.size(); ++ i, ++ iter) {
			polls[i].fd = iter -> second.clientSocket;
			polls[i].events = POLLIN;
		}
		
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
			
			// Add the client control block only if the client is valid.
			if(isValidClientSocket) {
				CsRtPollClientService movedClientService(&clientServices, 
					&clientNameSet, clientAddress, clientSocket);
				clientServices[clientSocket] = {};
				clientServices[clientSocket].move(std::move(movedClientService));
				CsRtPollClientService& clientService = clientServices[clientSocket];
				clientService.handler = CsDtClientHandler::newClientHandler(&clientService);
			}
			else if(clientSocket >= 0) close(clientSocket);
		}
		
		// Respond to the read requests.
		for(int i = 1; i <= clientServices.size(); ++ i) {
			if(numAvailables <= 0) break;	// Nore more to service.
			if((polls[i].revents & POLLIN) > 0) {
				-- numAvailables;
				
				// Destroy the client socket if it is not available.
				CsRtPollClientService& clientService = clientServices[polls[i].fd];
				if(clientService.receive() < 0) {
					delete clientService.handler;
					close(clientService.clientSocket);
					if(clientService.clientName != "") 
						clientNameSet.erase(clientService.clientName);
					clientServices.erase(polls[i].fd);
				}
			}
		}
	}
}
