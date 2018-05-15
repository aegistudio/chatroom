# The default target that outputs the fork server, poll server and client program.
all: bin bin/chatserver_fork bin/chatserver_poll bin/chatclient 

# The clean up function.
clean:
	rm bin/*

# The binary output directory.
bin:
	mkdir bin

# The chat server that based on the fork() model.
bin/chatserver_fork: chatserver_fork.cpp defaultlogic.cpp servercommon.cpp util.cpp
	g++ -std=c++11 -O3 $^ -o $@ -pthread -lrt

# The chat server that based on the poll() model.
bin/chatserver_poll: chatserver_poll.cpp defaultlogic.cpp servercommon.cpp util.cpp
	g++ -std=c++11 -O3 $^ -o $@

# The chat client used to connect to the fork() or poll() server.
bin/chatclient: chatclient.cpp util.cpp
	g++ -g -std=c++11 -O3 $^ -o $@
