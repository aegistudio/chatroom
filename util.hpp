#pragma once

/**
 * @file util.hpp
 *
 * 2018 @ Nanjing University Software Institute
 * @author Haoran Luo
 * @brief Defines the utility classes for streaming and text coloring.
 *
 * The utility classes are shared between servers and clients.
 * 
 * The streaming class abstract the input stream and output stream. So that the upper level
 * classes could use them as stream either directly operated on file or memory buffers.
 *
 * The text coloring class defines the escape sequences that could change the command line color.
 * And use interfaces instead of classes could make the code more readable.
 */

// The STL headers.
#include <string>
#include <vector>

/// Abstraction of the stream that could be read from.
class CsDtInputStream {
public:
	/// Make the destructor virtual.
	virtual ~CsDtInputStream() {}
	
	/// Read integer from the stream, and return -1 if fails to read.
	int read(int& value);
	
	/// Read string from the stream, and return -1 if fails to read.
	/// Max length equals to 0 means there's no constrain for the length.
	int read(std::string& data, size_t maxLength = 0);
	
	/// The read method to implement for the subclasses.
	virtual int read0(void* buffer, size_t size) = 0;
};

/// Abstraction of the stream that could be written to.
class CsDtOutputStream {
public:
	/// Make the destructor virtual.
	virtual ~CsDtOutputStream() {}
	
	/// Write integer to the stream, and return -1 if fails to write.
	int write(const int& value);
	
	/// Write string to the stream, and return -1 if fails to write.
	/// Max length equals to 0 means there's no constrain for the length.
	int write(const std::string& data, size_t maxLength = 0);
	
	/// Write string from the stream, and return -1 if fails to write.
	virtual int write0(const void* buffer, size_t size) = 0;
};

/// Concrete stream for reading from /writing to a blocked file descriptor.
class CsDtFileStream : public CsDtInputStream, public CsDtOutputStream {
public:
	// The internal file descriptor.
	const int fd;
	
	// Constructor for the file stream.
	CsDtFileStream(int fd): fd(fd) {}
	
	// Destructor for the file stream.
	virtual ~CsDtFileStream() {}

	/// Implementation for the read method.
	virtual int read0(void* buffer, size_t size) override;

	/// Implementation for the write method.
	virtual int write0(const void* buffer, size_t size) override;
	
	/// Flush the file stream.
	void flush();
};

/// Concrete stream for reading from a buffer.
class CsDtReadBuffer : public CsDtInputStream {
	// The internal read buffer.
	size_t size; void* buffer;
public:
	// Constructor for the read buffer.
	CsDtReadBuffer(size_t size, void* buffer):
		size(size), buffer(buffer) {}
	
	// Implementation for the read method.
	virtual int read0(void* buffer, size_t size) override;
};

/// Concrete stream for writing to a buffer.
class CsDtWriteBuffer : public CsDtOutputStream {
	// The internal write buffer.
	std::vector<char> buffer;
public:
	// Constructor for the write buffer.
	CsDtWriteBuffer(): buffer() {}
	
	// Implementation for the write method.
	virtual int write0(const void* buffer, size_t size) override;
	
	// Retrieve the content size of the buffer.
	size_t size() const { return buffer.size(); }
	
	// Retrieve the content data of the buffer.
	const char* data() const { return buffer.data(); }
	
	// Write the data in the buffer to another input stream.
	inline int writeTo(CsDtOutputStream& outputStream) const {
		return outputStream.write0(buffer.data(), size());
	}
};

/// Retrieve an integer from a data buffer.
int readInt(size_t* size, void** buffer, int& target);

/// Write an integer to a data buffer.
void writeInt(std::vector<char>& buffer, const int& value);

/// Retrieve a string from the data buffer.
int readString(size_t* size, void** buffer, std::string& target);

// Write a string to a data buffer.
void writeString(std::vector<char>& buffer, const std::string& value);

void write(std::vector<char>& buffer, const void* data, size_t dataSize);

/// The console text that has color.
enum CsDtConsoleFormat {
	cfReset = 0,
	
	// Decorations.
	cfBright = 1,
	cfUnderline = 4,
	
	// Foreground color.
	cfFgBlack = 30,
	cfFgRed, 
	cfFgGreen, 
	cfFgYellow, 
	cfFgBlue, 
	cfFgMagenta, 
	cfFgCyan, 
	cfFgWhite,
	
	// Background color.
	cfBgBlack = 40,
	cfBgRed,
	cfBgGreen,
	cfBgYellow,
	cfBgBlue,
	cfBgMagenta,
	cfBgCyan,
	cfBgWhite
};

// Retrieve a format string by provided formats.
std::string format(const std::vector<CsDtConsoleFormat>& fmts = {});