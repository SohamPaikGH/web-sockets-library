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
    char* cipher = malloc(strlen((char *) plain) * 4 / 3 + 4);
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
  char server_ws_key[128] = {0};
  strcpy(server_ws_key, client_ws_key);
  strcat(server_ws_key, GUID);

  /* Generate SHA-1 hash of server web socket key */
  unsigned char hash[SHA_DIGEST_LENGTH];
  SHA1((const unsigned char *) server_ws_key, strlen(server_ws_key), hash);

  char *encoded = base64_encode((unsigned char *) hash);

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
  "HTTP/1.1 101 Switching Protocols"
  "Host: server.example.com\r\n"
  "Upgrade: websocket\r\n"
  "Connection: Upgrade\r\n"
  "Sec-WebSocket-Accept: %s\r\n"
  "\r\n\r\n",
  server_ws_key);

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
  char buffer[BUFFER_SIZE + 1] = {0};
  int length = 0;
  int j = 0;
  unsigned int payload_len = 0;

  char client_ws_key[25] = {0};
  char payload_buf[256] = {0};

  unsigned char newChar;
  unsigned char lastChar = 0;
  char masking_key[4] = {0};

  bool gotGet = false, gotHost = false, gotUpgrade = false,
       gotConnection = false, gotKey = false, gotOrigin = false,
       gotProtocol = false, gotVersion = false, isWebSocket = false,
       gotCloseFrameHeader = false, gotPayloadLen = false, gotMaskingKey = false,
       gotNormalClosure = false;

  // Read the HTTP request from the client
  size_t bytes_read;
  while ((bytes_read = read(fd, &newChar, sizeof(newChar))) > 0) {
    // printf("%c", newChar);
    printf("Buffer: %s\n\n", buffer);
    if (newChar == ' ') {
      if (!strcmp("GET", buffer)) {
        gotGet = true;
        bzero(buffer, BUFFER_SIZE);
        length = 0;
        // fprintf(stdout, "\ngotGet\n");
      }
      // else if (!strncmp("Host:", buffer, 5)) {
      else if (strstr(buffer, "Host:")) {
        gotHost = true;
        bzero(buffer, BUFFER_SIZE);
        length = 0;
        // fprintf(stdout, "gotHost\n");
      }
      // else if (!strncmp("Upgrade:", buffer, 8)) {
      else if (strstr(buffer, "Upgrade:")) {
        gotUpgrade = true;
        bzero(buffer, BUFFER_SIZE);
        length = 0;
        // fprintf(stdout, "\ngotUpgrade\n");
      }
      // else if (!strncmp("Connection:", buffer, 11)) {
      else if (strstr(buffer, "Connection:")) {
        gotConnection = true;
        bzero(buffer, BUFFER_SIZE);
        length = 0;
        // fprintf(stdout, "\ngotConnection\n");
      }
      // else if (!strncmp("Sec-WebSocket-Key:", buffer, 18)) {
      else if (strstr(buffer, "Sec-WebSocket-Key:")) {
        gotKey = true;
        bzero(buffer, BUFFER_SIZE);
        length = 0;
        // fprintf(stdout, "\ngotKey\n");
      }
      // else if (!strncmp("Origin:", buffer, 7) & gotKey) {
      else if (strstr(buffer, "Origin:") && gotKey) {
        gotOrigin = true;
        // fprintf(stdout, "\ngotOrigin\n");
      }
      else if (!strncmp("Sec-WebSocket-Protocol:", buffer, 23)) {
        gotProtocol = true;
        // fprintf(stdout, "\ngotProtocol\n");
      }
      else if (!strncmp("Sec-WebSocket-Version:", buffer, 22)) {
        gotVersion = true;
        // fprintf(stdout, "\ngotVersion\n");
      }
    }
    else if (newChar == (0x80 | WS_OPCODE_CLOSE)) {
      gotCloseFrameHeader = true;
    }
    else if (gotCloseFrameHeader && (newChar & 0x80)) {
      payload_len = newChar & 0x7F;
      gotPayloadLen = true;

      if (payload_len == 0) {
        gotCloseFrameHeader = false;
        gotPayloadLen = false;
        gotMaskingKey = false;
        payload_len = 0;
        bzero(masking_key, 4);
      }
      else if (payload_len == 1) {
        fprintf(stderr, "Web Socket Close Frame Protocol Error detected: Close frame payload length must be at least 2 bytes\n");
        gotCloseFrameHeader = false;
        gotPayloadLen = false;
        gotMaskingKey = false;
        payload_len = 0;
        bzero(masking_key, 4);
      }
    }
    else if (gotCloseFrameHeader && gotPayloadLen) {
      if (j == 4) {
        strncpy(masking_key, buffer, 4);
        gotMaskingKey = true;
        j = 0;
        bzero(buffer, BUFFER_SIZE);
      }
      else {
        j++;
      }
    }
    else if (gotCloseFrameHeader && gotPayloadLen && gotMaskingKey) {
      for (size_t a = 0; a < payload_len; a++) {
        payload_buf[a] = payload_buf[a] ^ masking_key[a % 4];
      }
    }
    else if (gotCloseFrameHeader && gotPayloadLen && gotMaskingKey && payload_buf[0] != 0) {
      for (int a = 0; a < payload_len; a++) {
        payload_buf[a] = htons(payload_buf[a]);
      }

      uint16_t value_be = (payload_buf[0] << 8) | payload_buf[1];
      if (value_be != NORMAL_CLOSURE) {
        fprintf(stderr, "Web Socket Close Frame Protocol Error detected: Payload header is not status code 1000\n");
        gotCloseFrameHeader = false;
        gotPayloadLen = false;
        gotMaskingKey = false;
        payload_len = 0;
        bzero(masking_key, 4);
        bzero(payload_buf, payload_len);
      }
      gotNormalClosure = true;
    }
    else if (gotCloseFrameHeader && gotPayloadLen && gotMaskingKey && gotNormalClosure) {
      fprintf(stdout, "Sending Close Frame...\n");
      send_close_frame(fd);
      return;
    }
    else {
      if (gotKey) {
        if (newChar == '\r') {
          strncpy(client_ws_key, buffer, 24);
          bzero(buffer, BUFFER_SIZE);
          j = 0;
          break;
        }
        client_ws_key[j++] = newChar;
      }
      else if (gotUpgrade && newChar == '\r') {
        if (!strncmp(buffer, "websocket", 9)) {
          isWebSocket = true;
        }
        else {
          gotUpgrade = false;
        }
      }
      else if (gotGet && gotHost && gotUpgrade && gotConnection && gotKey && gotVersion) {
        fprintf(stdout, "Responding to client with opening handshake...\n");
        server_side_handshake(client_ws_key, fd);
        gotGet = false;
        gotUpgrade = false;
        gotConnection = false;
        gotKey = false;
        gotVersion = false;
      }
      else {
        lastChar = newChar;
        buffer[length] = newChar;
        length++;
      }
    }
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
