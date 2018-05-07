#include "chatlogic.hpp"
#include "util.hpp"
#include <vector>
#include <map>
#include <sstream>
#include <arpa/inet.h>

enum CsDtClientStatus {
	stTerminated = 0,
	
	// Client bootstrap.
	stNameSize,
	stNameBuffer,
	
	// Client default working.
	stPacketSize,
	stPacketData,
};

// The format color string.
static std::string fmtRed       = format() + format({cfFgRed});
static std::string fmtBrightRed = format() + format({cfBright, cfFgRed});
static std::string fmtYellow    = format() + format({cfFgYellow, cfBright});
static std::string fmtMagenta   = format() + format({cfBright, cfFgMagenta});
static std::string fmtPurple    = format() + format({cfFgMagenta});

class CsDtDefaultHandler : public CsDtClientHandler {
	CsDtClientService* server;
	CsDtClientStatus status;

	int dataSize;
	int packetId;
	std::vector<char> dataBuffer;
	
	bool hasJoinedServer;
	std::string clientName;
public:
	CsDtDefaultHandler(CsDtClientService* server): server(server), 
		status(stNameSize), hasJoinedServer(false) {}

	virtual ~CsDtDefaultHandler() {
		if(hasJoinedServer) {
			std::stringstream leaveMessage;
			leaveMessage << fmtYellow << "User " << 
				fmtMagenta << clientName << fmtPurple << " (" << 
				server -> ipPort() << ")" << format() << 
				fmtYellow << " has left the chat.";
			broadcastOtherAndLog(leaveMessage.str());
		}
	}

	// Attempt to broadcast the message to other users.
	void broadcastOtherAndLog(const std::string& message) {
		std::set<std::string> ignoreSet;
		ignoreSet.insert(clientName);
		server -> log(message);
		server -> broadcast(message, ignoreSet);
	}

	// Retrieve next data to read.
	virtual void next(size_t& size, void*& buffer) override {
		switch(status) {
			// The packet size state.
			case stPacketSize:
			case stNameSize: 
				size = sizeof(dataSize); 
				buffer = &dataSize;
			break;
				
			// The packet data state.
			case stPacketData:
			case stNameBuffer: 
				size = dataSize;
				buffer = dataBuffer.data();
			break;

			// Invalid state.
			default: 
				size = 0;
				buffer = NULL;
			break;
		}
	}

	// Process the packet which has already been read.
	virtual int processPacket(size_t size, void* buffer) {
		CsDtReadBuffer packet(size, buffer);
		int packetId = 0;
		if(packet.read(packetId) < 0) return -1;
		switch(packetId) {
			case 0:	{ // Client send chat.
				// Parse the chat packet.
				std::string chat;
				if(packet.read(chat) < 0) return -1;

				// Send chat globally.
				std::stringstream chatMessage;
				chatMessage << "[" << fmtMagenta << clientName 
					<< format() << "] " << chat;
				server -> broadcast(chatMessage.str());
			} break;
			
			case 1: { // Client send command.
				// Parse the command packet.
				std::string command;
				if(packet.read(command) < 0) return -1;

				// Execute command.
				std::vector<std::string> arguments;
				size_t position;
				while((position = command.find(" ")) != std::string::npos) {
					if(position > 0) {
						arguments.push_back(command.substr(0, position));
						command = command.substr(position + 1, command.length());
					}
				}
				if(command.length() > 0) arguments.push_back(command);
				if(arguments.size() > 0) processCommand(arguments);
			} break;
			
			default: {
				return -1;
			} break;
		}
		
		return 0;
	}

	// Execute the command by provided arguments.
	virtual void processCommand(const std::vector<std::string>& args) {
		if(args[0] == "online") {
			// List the users online.
			std::set<std::string> users = server -> listOnlineUsers();
			
			// Construct the online user message.
			std::stringstream onlineUserMessage;
			onlineUserMessage << fmtYellow << "There " << 
				((users.size() > 1)? "are" : "is") 
				<< " "<< users.size() << " user";
			if(users.size() > 1) onlineUserMessage << "s";
			onlineUserMessage << " online: ";
			bool firstUser = true;
			for(auto& user : users) {
				// Output the separator.
				if(firstUser) firstUser = false;
				else onlineUserMessage << fmtYellow << ", ";
				
				// Output the user name.
				onlineUserMessage << fmtMagenta << user;
			}
			onlineUserMessage << fmtYellow << ".";
			
			// Send back the online message.
			server -> send(onlineUserMessage.str());
		} else if(args[0] == "help") {
			// Construct the help list.
			std::map<std::string, std::string> helpList;
			helpList["online"] = "list online users in this chatroom.";
			helpList["help"]   = "show available commands.";
			
			// List available commands.
			std::stringstream commandMessage;
			commandMessage << fmtYellow << "List of available commands: ";
			for(auto& helpEntry : helpList) {
				commandMessage << std::endl << fmtYellow << "/" << 
					helpEntry.first << format() << ": " << helpEntry.second;
			}
				
			// Send the help message back.
			server -> send(commandMessage.str());
		} else {
			// Send the command not found message.
			server -> send(fmtRed + "Unknown command " + fmtBrightRed + "/" + args[0] + fmtRed 
				+ ". Issue " + fmtBrightRed + "/help" + fmtRed + " for the list of commands.");
		}
	}

	/// Tells the user that the current has finished filling.
	virtual void bufferFilled() override {
		switch(status) {
			case stNameSize:
				// Make sure the name length is not greater than 64 bytes.
				if(dataSize >= 64) status = stTerminated;
				else {
					status = stNameBuffer;
					dataBuffer.resize(dataSize);
				}
			break;
			
			case stPacketSize: {
				// Just receive the client packet.
				status = stPacketData;
				dataBuffer.resize(dataSize);
			} break;
			
			case stPacketData: {
				// Reaction to the packet.
				if(processPacket((size_t)dataSize, dataBuffer.data()) < 0)
					status = stTerminated;
				else status = stPacketSize;
				dataBuffer.resize(0);
			} break;
			
			case stNameBuffer: {
				// Create the client name and attempt to join the server.
				clientName = std::string(dataBuffer.data());
				dataBuffer.resize(0);
				
				// Attempt to join the server.
				if(server -> userOnline(clientName)) {
					hasJoinedServer = true;
					
					// Congratulate the user for joining the server.
					server -> send(fmtYellow + "Welcome to the chat room, " + 
						fmtMagenta + clientName + fmtYellow + ".");
					
					// Construct the message to broadcast for other users.
					std::stringstream joinMessage;
					joinMessage << fmtYellow << "New user " << 
						fmtMagenta << clientName << fmtPurple << " (" << 
						server -> ipPort() << ")" << format() << 
						fmtYellow << " has joined the chat room.";
					
					// Attempt to broadcast the message to other users.
					broadcastOtherAndLog(joinMessage.str());
					
					// Update client status.
					status = stPacketSize;
				} else {
					// Tells the user that it cannot join the server because of
					// the duplicated name.
					server -> send(fmtRed + "Sorry but " + fmtMagenta
						+ clientName + fmtRed + (" is already "
						"online, why not choose another name?"));
					status = stTerminated;
				}
			} break;
			
			default:
				status = stTerminated;
			break;
		}
	}
};

CsDtClientHandler* CsDtClientHandler::newClientHandler(CsDtClientService* svc) {
	return new CsDtDefaultHandler(svc);
}