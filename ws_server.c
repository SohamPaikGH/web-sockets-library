#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <float.h>
#include <openssl/sha.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>


#define FREE(p) { free(p); p = NULL; }

/* Size of Buffer Array */
#define BUFFER_SIZE 1024

/* Globally Unique ID used by WebSocket protocol, as defined in RFC 6455 */
#define GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/* Opcodes used by WebSockets protocol, as defined in RFC 6455 */
#define FIN 0x80
#define WS_OPCODE_CF 0x00
#define WS_OPCODE_TXT 0x01
#define WS_OPCODE_BIN 0x02
#define WS_OPCODE_CLOSE 0x8
#define WS_OPCODE_PING 0x9
#define WS_OPCODE_PONG 0xA
#define WS_FIN 0x80

/* Status Codes used by WebSocket protocol, as defined in RFC 6455 */
#define NORMAL_CLOSURE 0x03E8
#define ENDPOINT_GOING_AWAY 0x03E9
#define PROTOCOL_ERROR 0x03EA
#define BAD_DATA 0x03EB

char base46_map[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
                     'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
                     'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
                     'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'};

char* base64_encode(unsigned char* plain) {

    char counts = 0;
    char buffer[3];
    char* cipher = (char *) malloc(strlen((char *) plain) * 4 / 3 + 4);
    int i = 0, c = 0;

    for(i = 0; plain[i] != '\0'; i++) {
        buffer[counts++] = plain[i];
        if(counts == 3) {
            cipher[c++] = base46_map[buffer[0] >> 2];
            cipher[c++] = base46_map[((buffer[0] & 0x03) << 4) + (buffer[1] >> 4)];
            cipher[c++] = base46_map[((buffer[1] & 0x0f) << 2) + (buffer[2] >> 6)];
            cipher[c++] = base46_map[buffer[2] & 0x3f];
            counts = 0;
        }
    }

    if(counts > 0) {
        cipher[c++] = base46_map[buffer[0] >> 2];
        if(counts == 1) {
            cipher[c++] = base46_map[(buffer[0] & 0x03) << 4];
            cipher[c++] = '=';
        } else {                      // if counts == 2
            cipher[c++] = base46_map[((buffer[0] & 0x03) << 4) + (buffer[1] >> 4)];
            cipher[c++] = base46_map[(buffer[1] & 0x0f) << 2];
        }
        cipher[c++] = '=';
    }

    cipher[c] = '\0';   /* string padding character */
    return cipher;
}

void write400(int fd, const char *content_type) {
  write(fd, "HTTP/1.1 400 Bad Request\r\n", 26);
  write(fd, "Server: Durbhash\r\n", 18);
  write(fd, "Content-type: ", 14);
  write(fd, content_type, strlen(content_type));
  write(fd, "\r\n\r\n", 4);
}

void print_dec(uint8_t num) {
  for (int i = 7; i >= 0; i--) {
    printf("%d", (num >> i) & 1);
  }
  putchar('\n');
}

char *generate_server_ws_key(const char *client_ws_key) {
  //fprintf(stdout, "Hashing %s\n", client_ws_key);
  char server_ws_key[128] = {0};
  strcpy(server_ws_key, client_ws_key);
  strcat(server_ws_key, GUID);

  /* Generate SHA-1 hash of server web socket key */
  unsigned char hash[SHA_DIGEST_LENGTH];
  SHA1((const unsigned char *) server_ws_key, strlen(server_ws_key), hash);

  char *encoded = base64_encode((unsigned char *) hash);
  //fprintf(stdout, "Encoded: %s\n", encoded);

  return encoded;
}

void server_side_handshake(const char *client_ws_key, int fd) {
  char *server_ws_key = generate_server_ws_key(client_ws_key);
  if (!server_ws_key) {
    write400(fd, "text");
    return;
  }
  char server_opening_handshake[256] = {0};

  snprintf(server_opening_handshake, sizeof(server_opening_handshake), 
  "HTTP/1.1 101 Switching Protocols\r\n"
  "Host: server.example.com\r\n"
  "Upgrade: websocket\r\n"
  "Connection: Upgrade\r\n"
  "Sec-WebSocket-Accept: %s\r\n"
  "\r\n\r\n",
  server_ws_key);

  //fprintf(stdout, "%s\n", server_opening_handshake);
  send(fd, server_opening_handshake, strlen(server_opening_handshake), MSG_DONTWAIT);
  free(server_ws_key);
}

void send_close_frame(int fd) {
  unsigned char frame[4] = {0};
  size_t payload_len = 2;

  frame[0] = 0x80 | WS_OPCODE_CLOSE;
  frame[1] = payload_len;
  frame[2] = (NORMAL_CLOSURE >> 8) & 0xFF;
  frame[3] = NORMAL_CLOSURE & 0xFF;

  send(fd, frame, sizeof(frame), MSG_DONTWAIT);
}

void handleHTTPRequest(int fd) {
  const int buffer_size = 2400;
  char buffer[buffer_size + 1] = {0};
  int length = 0;

  const int path_size = 1024;
  char filepath[path_size + 1] = {0};
  char requestPath[path_size + 1] = {0};

  const int password_size = 1024;
  char password[password_size + 1] = {0};
  char newPassword[password_size + 1] = {0};

  clock_t start, end;
  double cpu_time_used;

  start = clock();

  // Characters

  unsigned char newChar;
  unsigned char lastChar = 0;

  char client_key[64] = {0};

  bool gotGet = false;
  bool gotHost = false;
  bool gotUpgrade = false;
  bool gotConnection = false;
  bool gotKey = false;
  bool gotVersion = false;
  bool gotExtensions = false;
  bool gotCRLF = false;

  // Read the HTTP request from the client
  size_t bytes_read;
  size_t i = 0;
  while ((bytes_read = read(fd, &newChar, sizeof(newChar))) > 0) {
    if (gotKey && !gotVersion && !gotExtensions)
      client_key[i++] = newChar;

    fprintf(stdout, "%c", newChar);
    if (newChar == ' ') {
      if (!strncmp("GET", buffer, 3)) {
        gotGet = true;
        bzero(buffer, buffer_size);
        length = 0;
      }
      else if (!strncmp("Host:", buffer, 13)) {
        gotHost = true;
        bzero(buffer, buffer_size);
        length = 0;
      }
      else if (!strncmp("Upgrade:", buffer, 5)) {
        gotUpgrade = true;
        bzero(buffer, buffer_size);
        length = 0;
      }
      else if (!strncmp("Connection:", buffer, 11)) {
        gotConnection = true;
        bzero(buffer, buffer_size);
        length = 0;
      }
      else if (!strncmp("Sec-WebSocket-Key:", buffer, 18)) {
        gotKey = true;
        bzero(buffer, buffer_size);
      }
      else if (!strncmp("Sec-WebSocket-Version:", buffer, 22)) {
        gotVersion = true;
        bzero(buffer, buffer_size);
      }
      else if (!strncmp("Sec-WebSocket-Extensions:", buffer, 25)) {
        gotExtensions = true;
        bzero(buffer, buffer_size);
      }
    }
    else if (newChar == '\n' && lastChar == '\r') {
      bzero(buffer, buffer_size);
      length = 0;
      if (gotCRLF) {
        break;
      }
      gotCRLF = true;
    }
    else {
      lastChar = newChar;
      if (newChar != '\r') {
        gotCRLF = false;
      }
      buffer[length++] = newChar;
    }
  }

  // puts("END REACHED");
  if (gotHost && gotUpgrade && gotConnection && gotKey && gotVersion && gotExtensions) {
    /* client_key contains the Sec-WebSocket-Version field, so we need to remove it */
    fprintf(stdout, "\nClient key: %s\n", client_key);
    // fprintf(stdout, "HTTP PACKET RECEIVED\nSENDING RESPONSE BACK\n");
    server_side_handshake(client_key, fd);
  }
}

int main() {

  int port = 1025;
  int queue_length = 5;
  struct sockaddr_in serverIPAddress;
  memset(&serverIPAddress, 0, sizeof(serverIPAddress));
  serverIPAddress.sin_family = AF_INET;
  serverIPAddress.sin_addr.s_addr = INADDR_ANY;
  serverIPAddress.sin_port = htons((u_short) port);

  int masterSocket = socket(PF_INET, SOCK_STREAM, 0);
  if (masterSocket < 0) {
    perror("socket");
    exit(-1);
  }

  int optval = 1;
  int err = setsockopt(masterSocket, SOL_SOCKET, SO_REUSEADDR,
                       (char *) &optval, sizeof(int));

  int error = bind(masterSocket, (struct sockaddr *) &serverIPAddress,
                   sizeof(serverIPAddress));
  if (error) {
    perror("bind");
    exit(-1);
  }

  error = listen(masterSocket, queue_length);
  if (error) {
    perror("listen");
    exit(-1);
  }

  while (1) {
    struct sockaddr_in clientAddress;
    int addr_len = sizeof(clientAddress);
    int slaveSocket = accept(masterSocket, (struct sockaddr *) &clientAddress,
                             (socklen_t *) &addr_len);

    if (slaveSocket < 0) {
      perror("accept");
      exit(-1);
    }

    handleHTTPRequest(slaveSocket);

    shutdown(slaveSocket, SHUT_RDWR);
    close(slaveSocket);
  }

  return 0;
}
