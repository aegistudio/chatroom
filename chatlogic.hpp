#pragma once

/**
 * @file chat.hpp
 *
 * 2018 @ Nanjing University Software Institute
 * @author Haoran Luo
 * @brief Defines logic classes related to the chat room.
 *
 * In order to reuse the code of serving clients, we abstract the logic class from the server code.
 *
 * The logic code is state-machine based, the server can know how many bytes the client service is 
 * requesting, and where the data can be written to (CsDtClientHandler::next()), and can request for
 * service (CsDtClientHandler::bufferFilled()) after data is pushed into the provided buffer.
 *
 * The logic code interacts with the server model via the service class (CsDtClientService). 
 * Different server model should implement them regarding the underlying communication.
 *
 * The concrete implementation of the logic should be retrieved by calling 
 * CsDtClientHandler::newClientHandler, where the caller should manage the dynamic allocated memory.
 */
 
#include <string>
#include <set>

 /**
  * The pure virtual class defines the API that the server should
  * provides to the client.
  *
  * Please notice the server will exists in the client handling 
  * process after creation.
  */
class CsDtClientService {
public:
	virtual ~CsDtClientService() {}
	
	/// Retrieve the client address data.
	virtual std::string ipPort() = 0;
	
	/// Set the user online, so it could be found in the
	/// online users list. The result indicates whether the 
	/// client can join the server.
	virtual bool userOnline(const std::string& online) = 0;
	
	/// List all online users currently on the server.
	virtual std::set<std::string> listOnlineUsers() = 0;
	
	/// Broadcast message to the users. The users appears in the 
	/// muted user will not receive the message.
	virtual void broadcast(const std::string& message, 
		std::set<std::string> mutedUser = {}) = 0;
	
	/// Attempt to send log to the server console.
	virtual void log(const std::string& logging) = 0;
	
	/// Send message privately to the current user indicated by 
	/// the current user.
	virtual void send(const std::string& message) = 0;
};

/**
 * The pure virtual class defines the API of the client logic.
 *
 * The client should be designed as if it is a state machine.
 */
class CsDtClientHandler {
public:
	virtual ~CsDtClientHandler() {}
	
	/// Retrieve the next bytes to read. The size set to zero
	/// means the termination of the client.
	virtual void next(size_t& size, void*& buffer) = 0;
	
	/// Tells the user that the current has finished filling.
	virtual void bufferFilled() = 0;
	
	static CsDtClientHandler* newClientHandler(CsDtClientService*);
};