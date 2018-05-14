all: bin/chatserver_fork bin/chatserver_poll bin/chatclient 

clean:
	rm bin/*

bin/chatserver_fork: chatserver_fork.cpp defaultlogic.cpp servercommon.cpp util.cpp
	g++ -std=c++11 -O3 $^ -o $@ -pthread -lrt

bin/chatserver_poll: chatserver_poll.cpp defaultlogic.cpp servercommon.cpp util.cpp
	g++ -std=c++11 -O3 $^ -o $@

bin/chatclient: chatclient.cpp util.cpp
	g++ -std=c++11 -O3 $^ -o $@
