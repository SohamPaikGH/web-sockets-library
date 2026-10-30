CC=gcc
FLAGS=-I/opt/homebrew/opt/openssl@3/include -L/opt/homebrew/opt/openssl@3/lib -lssl -lcrypto -g

SERVER=ws_server

all: $(SERVER)

$(SERVER): ws_server.c
	$(CC) $(FLAGS) -o $(SERVER) ws_server.c

run:
	./ws_server

clean:
	rm -rf $(SERVER)
