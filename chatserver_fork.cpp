/**
 * @file chatserver_fork.cpp
 *
 * 2018 @ Nanjing University Software Institute
 * @author Haoran Luo
 * @brief Implementation of the fork() server.
 *
 * A fork() server responds to the newly come client connections in the main thread, and create 
 * sub-processes to serve distint client connections.
 *
 * Inter-process Communication (IPC) is required as different process has segregated address space.
 * The communication (formats and techniques) will be defined in this source file. The semaphore 
 * (including creation of shared memory and semaphore struct) will also be defined in this file as 
 * other server model does not require IPC synchronization.
 *
 * When a client has joined, left or request inter-client services, the client will send UNIX 
 * signals (SIGUSR1) to interrupt the blocking of parent process. Requested service will be written
 * to a bus (pipe) so that the service order would be guaranteed.
 */

// The system headers.
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/mman.h>

// The unix headers.
#include <signal.h>
#include <fcntl.h>
#include <sched.h>
#include <semaphore.h>

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

// The user defined headers.
#include "servercommon.hpp"
#include "chatlogic.hpp"
#include "util.hpp"

// Defines error codes that could only be thrown by fork server.
enum CsExForkErrorCode {
	efPipeCreation = eMaxCommonError + 1,
	efSharedMemory
};

// Defines the semaphore used to synchronize different process.
// The logic of creating shared memory and initialize semaphore is comprised.
class CsRtSemaphore {
	bool semValid;
	sem_t* semObj;
public:
	// Initialize the semaphore instance as invalid.
	CsRtSemaphore(): semObj((sem_t*)MAP_FAILED), semValid(false) {}
	
	// Destroy the internal semaphore object if there's initialized one.
	~CsRtSemaphore() {
		if(((void*)semObj) != MAP_FAILED && semValid) {
			sem_destroy(semObj);
			munmap((void*)semObj, sizeof(sem_t));
		}
	}

	// Initialize shared memory and open the semaphore.
	int open(int init) {
		if((semObj = (sem_t*)mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, 
			MAP_ANONYMOUS | MAP_SHARED, -1, 0)) == MAP_FAILED) return -1;
		if(sem_init(semObj, 1, init) < 0) {
			semValid = true;
			return -1;
		}
		return 0;
	}
	
	// Move the semaphore instance from the right value expresion.
	void move(CsRtSemaphore&& rhs) {
		// Move valid status to the left.
		semValid = rhs.semValid;
		rhs.semValid = false;
		
		// Move internal semaphore objects.
		semObj = rhs.semObj;
		rhs.semObj = (sem_t*)MAP_FAILED;
	}
	
	// Wait for and acquire the semaphore.
	void wait() {
		sem_wait(semObj);
	}
	
	// Wait for and try to acquire the semaphore.
	// Return true if the semaphore is acquired, but false if it is not acquired.
	bool tryWait() {
		return sem_trywait(semObj) == 0;
	}
	
	// Increse the resource of the semaphore.
	void post() {
		sem_post(semObj);
	}
};

// Defines the client control block. Which records essential data for identifying 
// client status on server side.
struct CsRtForkClientControl {
	// Fields just for recording purpose.
	pid_t pid;						// The pid of the client process.
	std::string clientName;			// The name of the client process.
	
	// Actually using purpose resources.
	int respondPipe[2];
	CsRtSemaphore socketMutex;
	CsRtSemaphore respondSemaphore;
	
	// Initialize the client control block.
	CsRtForkClientControl() {
		respondPipe[0] = respondPipe[1] = -1;
	}
	
	// Destroy the client control block.
	~CsRtForkClientControl() {
		if(respondPipe[0] > 0) close(respondPipe[0]);
		if(respondPipe[1] > 0) close(respondPipe[1]);
	}
	
	// Initialize the control control block, may fails to initialize and return -1
	// if it does fail to.
	int init() {
		// Initialize pipe.
		if(pipe2(respondPipe, O_CLOEXEC) < 0) return -1;

		// Initialize semaphore.
		if(socketMutex.open(1) < 0) return -1;
		if(respondSemaphore.open(0) < 0) return -1;

		return 0;
	}
	
	// Move the control block from the right value expression.
	void move(CsRtForkClientControl&& rhs) {
		pid = rhs.pid;

		// Move the respond pipe.
		respondPipe[0] = rhs.respondPipe[0];
		rhs.respondPipe[0] = -1;
		respondPipe[1] = rhs.respondPipe[1];
		rhs.respondPipe[1] = -1;
		
		// Move the semaphore.
		socketMutex.move(std::move(rhs.socketMutex));
		respondSemaphore.move(std::move(rhs.respondSemaphore));
	}
};

// Represents the shared data between the server and the client service subprocess.
struct CsRtSharedMemory {
	CsRtSemaphore logMutex;			// Control the access to the console log.
	CsRtSemaphore pipeMutex;		// Control the access to the broadcast pipe.
	CsRtSemaphore pipeSemaphore;	// Control the access to the broadcast pipe.
};

// Defines the IPC request ids.
enum CsRtIpcRequest {
	ipcJoin = 0,
	ipcLeave,
	ipcBroadcast,
	ipcListOnline,
};

// Defines the client service.
class CsRtForkClientService : public CsDtClientService {
	pid_t parentPid;
	CsDtFileStream clientSocket, pipe;
	CsDtFileStream respondPipe;
	
	CsRtForkClientControl& ccb;
	CsRtSharedMemory& rtshm;
	struct sockaddr_in clientAddress;
public:
	CsRtForkClientService(pid_t ppid, int clientSocket, 
		int pipe, CsRtForkClientControl& ccb, CsRtSharedMemory& rtshm, 
		const struct sockaddr_in& _clientAddress):
		parentPid(ppid), clientSocket(clientSocket), ccb(ccb), 
		pipe(pipe), respondPipe(ccb.respondPipe[0]), rtshm(rtshm) {
			
			memcpy(&clientAddress, &_clientAddress, sizeof(clientAddress));
		}

	virtual std::string ipPort() {
		return ::ipPort(clientAddress);
	}
	
	void beforeRequest(int requestId) {
		// Write out the request header.
		pipe.write(clientSocket.fd);
		pipe.write(requestId);
		
		// Acquire the pipe mutex.
		rtshm.pipeMutex.wait();
	}
	
	void afterRequest() {
		// End of current request.
		pipe.flush();
		
		// Release the pipe mutex.
		rtshm.pipeMutex.post();
		
		// Increase semaphore.
		rtshm.pipeSemaphore.post();
		
		// Send signal to parent process until there's signal in respond semaphore.
		while(!ccb.respondSemaphore.tryWait()) {
			kill(parentPid, SIGUSR1);
			sched_yield();
		}
	}
	
	virtual bool userOnline(const std::string& online) {
		// Before the request.
		beforeRequest(ipcJoin);
		
		// Write the request data.
		pipe.write(online);
		
		// After the request.
		afterRequest();
		
		// Retrieve the result.
		int result;
		respondPipe.read(result);
		return result == 0;
	}
	
	/// List all online users currently on the server.
	virtual std::set<std::string> listOnlineUsers() {
		// Before the request.
		beforeRequest(ipcListOnline);
		
		// After the request.
		afterRequest();
		
		// Retrieve the user list size.
		int numUsersOnline;
		respondPipe.read(numUsersOnline);
		
		// Retrieve the user list.
		std::set<std::string> onlineUsers;
		for(int i = 0; i < numUsersOnline; ++ i) {
			std::string onlineUser;
			respondPipe.read(onlineUser);
			onlineUsers.insert(onlineUser);
		}
		return onlineUsers;
	}
	
	/// Broadcast message to the users. The users appears in the 
	/// muted user will not receive the message.
	virtual void broadcast(const std::string& message, std::set<std::string> mutedUsers) {
		// Before the request.
		beforeRequest(ipcBroadcast);
		
		// Write out the request data.
		pipe.write(message);
		int mutedSize = mutedUsers.size();
		pipe.write(mutedSize);
		for(auto& mutedUser : mutedUsers)
			pipe.write(mutedUser);
		
		// After the request.
		afterRequest();
	}
	
	virtual void log(const std::string& logging) {
		// Write the message to the server log.
		rtshm.logMutex.wait();
		std::clog << logging << std::endl;
		rtshm.logMutex.post();
	}
	
	virtual void send(const std::string& message) {
		ccb.socketMutex.wait();
		int packetId = 0;
		clientSocket.write(packetId);
		clientSocket.write(message);
		ccb.socketMutex.post();
	}
};

// The main function of the fork() server.
int main(int argc, char** argv) {
	// Create the server socket.
	struct sockaddr_in serverAddress;
	int serverSocket = createServerSocket(argc, argv, serverAddress);
	
	// Attempt to create the pipes, the pipes will be used to communicate the parent
	// process with the child process when to broadcast client message.
	int pipefd[2];
	if(pipe2(pipefd, O_CLOEXEC) < 0) exitPosix(
		"The pipe cannot be created!\n", efPipeCreation);
	CsDtFileStream pipeReadEnd = pipefd[0], pipeWriteEnd = pipefd[1];
	
	// Install the signal handler for SIGUSR1 and SIGPIPE, because the child process may 
	// send SIGUSR1 to request termination or data writing on the pipe.
	registerEmptyHandler({SIGUSR1, SIGPIPE});
		
	// Create the signal set containing SIGUSR1 for blocking and unblocking, as we 
	// need to interrupt the accept function but don't want to interrupt other I/O
	// functions.
	sigset_t signalSet;
	sigemptyset(&signalSet);
	sigaddset(&signalSet, SIGUSR1);

	// Create shared memory to be used among its child process.
	CsRtSharedMemory rtshm;
	if(rtshm.logMutex.open(1) < 0) exitPosix("Cannot create log mutex.", efSharedMemory);
	if(rtshm.pipeMutex.open(1) < 0) exitPosix("Cannot create pipe mutex.", efSharedMemory);
	if(rtshm.pipeSemaphore.open(0) < 0) exitPosix("Cannot create pipe semaphore", efSharedMemory);

	// Now the server is ready, so print out the ready message to server log.
	std::clog << format({cfFgCyan}) << "Chat room fork() server is ready at " << format({cfBright}) << 
		ipPort(serverAddress) << format() << format({cfFgCyan}) << "." << format() << std::endl;
	
	// The main loop of the chat server to receive client sockets, create child process
	// for them and check for message to write.
	std::map<int, CsRtForkClientControl> clientHandlers;
	std::set<std::string> nameSet;
	pid_t parentPid = getpid();
	while(true) {
		struct sockaddr_in clientAddress;
		socklen_t clientAddrLength = sizeof(clientAddress);
		memset(&clientAddress, 0, clientAddrLength);
		
		// Attempt to accept a new client socket. This could be interrupted by the SIGUSR1.
		sigprocmask(SIG_UNBLOCK, &signalSet, NULL);
		int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddress, &clientAddrLength);
		sigprocmask(SIG_BLOCK, &signalSet, NULL);
		
		// Reaction to the newly come client socket.
		if(clientSocket >= 0) {
			// Attempt to allocate control resource for the client.
			CsRtForkClientControl ccb;
			
			// Make sure the control resource could be allocated.
			if(ccb.init() >= 0) {
				// Attempt to fork a new process to serve the client.
				pid_t childPid = fork();
				if(childPid < 0) close(clientSocket); // Child process cannot be forked.
				else if(childPid == 0) {              // Body of child process.
					// Close useless file descriptors and clear the map.
					close(pipeReadEnd.fd);
					for(auto& clientHandler : clientHandlers) close(clientHandler.first);
					clientHandlers = {};
					
					// Attempt to create suitable service for client logic.
					CsRtForkClientService service(parentPid, clientSocket, 
						pipeWriteEnd.fd, ccb, rtshm, clientAddress);
					CsDtClientHandler* handler = CsDtClientHandler::newClientHandler(&service);
					
					// Run the reading loop.
					size_t size; void* buffer;
					while(true) {
						handler -> next(size, buffer);
						if(size <= 0) break;
						
						// Read data from the client socket.
						if(read(clientSocket, buffer, size) <= 0) break;
						handler -> bufferFilled();
					}
					delete handler;
					
					// Loop and exit.
					service.beforeRequest(ipcLeave);
					service.afterRequest();
					exit(0);
				}
				else {                                // Body of parent process.
					ccb.pid = childPid;
					clientHandlers[clientSocket] = {};
					clientHandlers[clientSocket].move(std::move(ccb));
				}
			}
			else {
				// Print out the message that the client could not be created.
				rtshm.logMutex.wait();
				std::clog << format({cfFgRed}) << "Client handler for " << format({cfBright}) <<
					ipPort(clientAddress) << format() << format({cfFgRed}) << 
					" could not be created." << std::endl;
				perror("");
				std::clog << format();
				rtshm.logMutex.post();
			}
		} else {		
			// Attempt to check the status of each buffer object.
			while(rtshm.pipeSemaphore.tryWait()) {
				// Respond to the request.
				int requestConnection;
				pipeReadEnd.read(requestConnection);
				CsRtForkClientControl& ccb = clientHandlers[requestConnection];
				ccb.respondSemaphore.post();
				
				// Construct the respond pipe.
				CsDtFileStream respondPipe(ccb.respondPipe[1]);
				
				// Parse the request.
				int requestId = -1;
				pipeReadEnd.read(requestId);
				switch(requestId) {
					// Broadcast message request.
					case ipcBroadcast: {
						// Retrieve the message.
						std::string message;
						pipeReadEnd.read(message);
						
						// Retrieve the ignored set.
						std::string user;
						std::set<std::string> ignored;
						int mutedSize;
						pipeReadEnd.read(mutedSize);
						for(int i = 0; i < mutedSize; ++ i) {
							pipeReadEnd.read(user);
							ignored.insert(user);
						}
					
						// Broadcast the message.
						for(auto& broadcastHandler : clientHandlers) {
							if(ignored.count(broadcastHandler.second.clientName)) continue;
							CsDtFileStream broadcastSocket(broadcastHandler.first);
							broadcastHandler.second.socketMutex.wait();
							broadcastSocket.write(0);
							broadcastSocket.write(message);
							broadcastHandler.second.socketMutex.post();
						}
					} break;
					
					// User online request.
					case ipcJoin: {
						// Retrieve the requested name.
						std::string requestedName;
						pipeReadEnd.read(requestedName);
						
						// Judge whether the name is available.
						int returnResult = 0;	// SUCCESS.
						if(nameSet.count(requestedName) > 0) 
							returnResult = 1;	// FAILED.
						else {
							nameSet.insert(requestedName);
							ccb.clientName = requestedName;
						}
						
						// Write back the result.
						respondPipe.write(returnResult);
					} break;
					
					// List online users request.
					case ipcListOnline: {
						// Write the number of clients on the server.
						respondPipe.write(nameSet.size());
						
						// Write the online client list.
						for(auto& name : nameSet) respondPipe.write(name);
					} break;
					
					// User offline request.
					case ipcLeave: {
						// Wait for the forked process to exit.
						int returnStatus = 0;
						waitpid(ccb.pid, &returnStatus, 0);
						
						// Erase the client name and client control block.
						if(nameSet.count(ccb.clientName) > 0)
							nameSet.erase(ccb.clientName);
						clientHandlers.erase(requestConnection);
						
						// Close the client connection on server side.
						close(requestConnection);
					} break;
					
					default: break;
				}
			}
		}
	}
}
