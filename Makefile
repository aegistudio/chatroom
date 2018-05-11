all: bin/chatserver_fork bin/chatserver_poll bin/chatclient

clean:
	rm bin/*

bin/%.o: %.cpp
	g++ -O3 -c $^ -o $@ -std=c++11

bin/chatserver_fork: bin/chatserver_fork.o bin/defaultlogic.o bin/servercommon.o bin/util.o
	g++ -O3 $^ -o $@ -pthread -lrt

bin/chatserver_poll: bin/chatserver_poll.o bin/defaultlogic.o bin/servercommon.o bin/util.o
	g++ -O3 $^ -o $@
	
bin/chatclient: bin/chatclient.o bin/util.o
	g++ -O3 $^ -o $@