all: server client

server: server.cpp common.h
	g++ -o server server.cpp

client: client.cpp common.h
	g++ -o client client.cpp

clean:
	rm -f server client

reset:
	rm -f player_stats.dat
