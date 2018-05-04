#include "util.hpp"

#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <stropts.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>

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
	return ::read(fd, buffer, size);
}

// Implementation for writing the file stream.
int CsDtFileStream::write0(const void* buffer, size_t size) {
	return ::write(fd, buffer, size);
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
	buffer = &((char*)buffer)[paramSize];
	
	return 0;
}

// Implementation for writing integer to buffer.
int CsDtWriteBuffer::write0(const void* paramBuffer, size_t paramSize) {
	int bufferSize = buffer.size();
	buffer.resize(bufferSize + paramSize);
	memcpy(&(buffer[bufferSize]), paramBuffer, paramSize);
	return 0;
}

// Implementation for read integer from buffer.
int readInt(size_t* size, void** buffer, int& target) {
	if(*size < sizeof(int)) return -1;
	int* intBuffer = (int*)(*buffer);
	*buffer = (void*)(intBuffer + 1);
	target = *intBuffer;
	return 0;
}

// Implementation for read string from buffer.
int readString(size_t* size, void** buffer, std::string& target) {
	// Retrieve length.
	int length;
	if(readInt(size, buffer, length) < 0) return -1;
	
	// Retrieve string.
	std::vector<char> stringBuffer(length + 1);
	stringBuffer[length] = '\0';
	memcpy(stringBuffer.data(), *buffer, length);
	*buffer = &(((char*)buffer)[length]);
	target = std::string(stringBuffer.data());
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