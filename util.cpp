/**
 * @file util.hpp
 *
 * 2018 @ Nanjing University Software Institute
 * @author Haoran Luo
 * @brief Defines the utility classes for streaming and text coloring.
 *
 * See the header file **util.hpp** for details.
 */

// The interface header.
#include "util.hpp"

// The network headers.
#include <arpa/inet.h>
#include <sys/ioctl.h>

// The unix headers.
#include <stropts.h>
#include <unistd.h>
#include <fcntl.h>

// The C standard headers.
#include <cerrno>
#include <cstring>

// The STL headers.
#include <sstream>
#include <vector>

// Implementation for write integer.
int CsDtOutputStream::write(const int& value) {
	//int writtenValue = htonl(value);
	//return write0(&writtenValue, sizeof(writtenValue));
	return write0(&value, sizeof(value));
}

/// Read an integer from the file descriptor.
int CsDtInputStream::read(int& value) {
	/*int readValue;
	if(read0(&readValue, sizeof(value)) < 0) return -1;
	value = ntohl(value);
	return 0;*/
	return read0(&value, sizeof(value));
}

// Implementation for write string.
int CsDtOutputStream::write(const std::string& result, size_t maxLength) {
	// Write the length.
	int length = result.length();
	if(maxLength > 0 && length > maxLength) 
		{ errno = EINVAL; return -1; }
	if(write(length) < 0) return -1;
	
	// Write the string data.
	if(write0(result.c_str(), length) < 0) return -1;
	return 0;
}

// Implementation for read string.
int CsDtInputStream::read(std::string& result, size_t maxLength) {
	// Retrieve the length.
	int length = 0;
	if(read(length) < 0) return -1;
	if(length < 0) { errno = EIO; return -1; }
	if(maxLength > 0 && length > maxLength) 
		{ errno = EIO; return -1; }
	
	// Retrieve the buffer data.
	std::vector<char> buffer(length + 1);
	if(read0(buffer.data(), length) < 0) return -1;
	buffer[length] = '\0';
	result = std::string(buffer.data());
	return 0;
}

// Implementation for reading the file stream.
int CsDtFileStream::read0(void* buffer, size_t size) {
	size_t numRead = 0;
	while(numRead < size) {
		ssize_t readStatus = ::read(fd, offset(buffer, numRead), size - numRead);
		if(readStatus == 0) return -1;	// End of file.
		else if(readStatus < 0) {
			if(errno != EWOULDBLOCK) return -1;	// I/O error.
			else sched_yield();			// Don't read too quickly.
		}
		else numRead += readStatus;
	}
	return 0;
}

// Implementation for writing the file stream.
int CsDtFileStream::write0(const void* buffer, size_t size) {
	size_t numWritten = 0;
	while(numWritten  < size) {
		ssize_t writeStatus = ::write(fd, offset(buffer, numWritten), size - numWritten);
		
		if(writeStatus == 0) return -1;	// File closed.
		else if(writeStatus < 0) {
			if(errno != EWOULDBLOCK) return -1;	// I/O error.
			else sched_yield();			// Don't write too quickly.
		}
		else numWritten += writeStatus;
	}
	return 0;
}

// Implementaion for flushing the file stream.
void CsDtFileStream::flush() {
	ioctl(fd, I_FLUSH, FLUSHW);
}

// Implementation for reading the stream.
int CsDtReadBuffer::read0(void* paramBuffer, size_t paramSize) {
	// Copy out the buffer data.
	if(size < paramSize) return -1;
	memcpy(paramBuffer, buffer, paramSize);
	
	// Update the data.
	size -= paramSize;
	buffer = offset(buffer, paramSize);
	
	return 0;
}

// Implementation for writing integer to buffer.
int CsDtWriteBuffer::write0(const void* paramBuffer, size_t paramSize) {
	int bufferSize = buffer.size();
	buffer.resize(bufferSize + paramSize);
	memcpy(&buffer[bufferSize], paramBuffer, paramSize);
	return 0;
}

// Implementation for the format string.
std::string format(const std::vector<CsDtConsoleFormat>& fmts) {
	std::stringstream result;
	result << "\033[";
	
	// The format token.
	if(fmts.size() == 0) result << "0";
	else {
		result << fmts[0];
		for(int i = 1; i < fmts.size(); ++ i)
			result << ";" << fmts[i];
	}
	
	result << "m";
	return result.str();
}
