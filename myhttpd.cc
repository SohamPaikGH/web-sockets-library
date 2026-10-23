
const char * usage =
"                                                               \n"
"myhttpd:                                                \n"
"                                                               \n"
"Simple HTTP server program that can use multithreading and     \n"
"multiprocessing to host webpages                               \n"
"                                                               \n"
"To use it in one window type:                                  \n"
"                                                               \n"
"   myhttpd [-f|-[|-d] <port>                                   \n"
"                                                               \n"
"Where 1024 < port < 65536.             \n"
"                                                               \n"
"In another window type:                                       \n"
"                                                               \n"
"   telnet <host> <port>                                        \n"
"                                                               \n"
"where <host> is the name of the machine where myhttpd         \n"
"is running. <port> is the port number you used when you run   \n"
"myhttpd.                                                      \n"
"                                                               \n"
"In your browser type:                                         \n"
"    <host>:<port>                                             \n"
"                                                               \n"
"where <host> is the name of the machine where myhttpd is       \n"
"running. <port> is the port number you used when you run       \n"
"myhttpd.                                                       \n";

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <algorithm>
#include <float.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <dirent.h>
#include <time.h>
#include <string>
#include <vector>

#define FREE(p) { free(p); p = NULL; }

const char *loginInfo = "dXNlcjpzYWZlcGFzc3dvcmQ=";
const char *authorName = "Soham Paik";
int num_requests = 0;
double min_request_time = 1000.0;
double max_request_time = 0.0;
char slowest_request[256] = {0};
char fastest_request[256] = {0};
time_t start_time;

pthread_mutex_t mutex_lock;
int masterSocket;
std::vector<int> slaveSockets;
int port;

extern void zombie_killer(int sig) {
  int wstatus;
  pid_t pid;

  if (sig == SIGCHLD) {
    while (waitpid(-1, &wstatus, WNOHANG) > 0);
  }
}

extern void handle_sigint(int sig) {
  int wstatus;
  pid_t pid;

  for (int slaveSocket : slaveSockets) {
    shutdown(slaveSocket, SHUT_RDWR);
    close(slaveSocket);
  }

  shutdown(masterSocket, SHUT_RDWR);
  close(masterSocket);
  putchar('\n');
  exit(0);
}

char *get_file_extension(char *filepath) {
  char *extension = strrchr(filepath, '.');
  return (extension ? strdup(extension) : NULL);
}

int is_directory(const char *filepath) {
  struct stat file_stat;
  if (stat(filepath, &file_stat) < 0) {
    perror("stat");
    return -1;
  }

  return S_ISDIR(file_stat.st_mode) ? 1 : 0;
}

void setContentType(char *contentType, char *file_extension) {
  if (!strcmp(".html", file_extension)) {
    strcpy(contentType, "text/html");
  }
  else if (!strcmp(".gif", file_extension)) {
    strcpy(contentType, "image/gif");
  }
  else if (!strcmp(".svg", file_extension)) {
    strcpy(contentType, "image/svg+xml");
  }
  else if (!strcmp(".jpg", file_extension) || !strcmp(".jpeg", file_extension)) {
    strcpy(contentType, "image/jpeg");
  }
  else if (!strcmp(".png", file_extension)) {
    strcpy(contentType, "image/png");
  }
  else if (!strcmp(".ico", file_extension)) {
    //strcpy(contentType, "image/vnd.microsoft.icon");
    strcpy(contentType, "image/x-icon");
  }
  else if (!strcmp(".xbm", file_extension)) {
    strcpy(contentType, "image/x-xbitmap");
  }
  return;
}

void write401(int fd) {
  const char *realm = "\"myhttpd-cs252\"";
  write(fd, "HTTP/1.1 401 Unauthorized\r\n", 27);
  write(fd, "WWW-Authenticate: Basic realm=", 30);
  write(fd, realm, strlen(realm));
  write(fd, "\r\n\r\n", 4);
}

void write404(int fd, char *content_type) {
  write(fd, "HTTP/1.1 404FileNotFound\r\n", 26);
  write(fd, "Server: CS 252 Lab 5\r\n", 21);
  write(fd, "Content-type: ", 14);
  write(fd, content_type, strlen(content_type));
  write(fd, "\r\n\r\n", 4);
  write(fd, "File not found", 14);
}

void write200(int fd, char *content_type) {
  write(fd, "HTTP/1.1 200 Document follows\r\n", 31);
  write(fd, "Server: CS 252 lab 5\r\n", 22);
  write(fd, "Content-type: ", 14);
  write(fd, content_type, strlen(content_type));
  write(fd, "\r\n\r\n", 4);
}

char *search_file(const char *dir_path, const char *target_file) {
  DIR *dir;
  struct dirent *entry;
  struct stat file_stat;
  const int path_size = 1024;
  char path[path_size] = {0};

  if ((dir = opendir(dir_path)) == NULL) {
    perror("Error opening directory");
    return NULL;
  }

  // Check if target_file is a file path and just get the last part
  // Also remove the last character of target_file in case that char
  // equals '/'
  char *old_target_file = strdup(target_file);
  char *new_target_file = NULL;
  char *t;
  if ((t = strrchr(old_target_file, '/'))) {
    new_target_file = t + 1;
    printf("Target File: %s\n", new_target_file);
    *t = '\0';
    target_file = new_target_file;
  }

  while ((entry = readdir(dir)) != NULL) {
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
      continue;

    snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);

    if (stat(path, &file_stat) < 0) {
      perror("Error getting file status");
      continue;
    }

    if (S_ISREG(file_stat.st_mode) && strcmp(entry->d_name, target_file) == 0) {
      printf("Found %s at: %s\n", target_file, path);

      // If target_file contains a file path (e.g. with '/') then first check
      // if the target file matches the predetermined path.
      printf("Old Target File: %s, Target File: %s\n", old_target_file, target_file);
      if (!strcmp(old_target_file, target_file)) {
        free(old_target_file);
        closedir(dir);
        return strdup(path);
      }

      closedir(dir);
      return strdup(path);
    }

    if (S_ISDIR(file_stat.st_mode)) {
      char *file_path = NULL;

      if (!strcmp(entry->d_name, target_file)) {
        closedir(dir);
        errno = EISDIR;
        printf("EISDIR - filepath: %s\n", path);
        free(old_target_file);
        return strdup(path);
      }

      if ((file_path = search_file(path, target_file))) {
        closedir(dir);
        free(old_target_file);
        return file_path;
      }
    }

  }

  closedir(dir);
  errno = ENOENT;
  free(old_target_file);
  return NULL;
}

int get_file_size(const char *filename) {
  struct stat buffer;
  if (stat(filename, &buffer) < 0) {
    perror("stat");
    fprintf(stderr, "Could not get file size of %s\n", filename);
    return -1;
  }
  return (intptr_t) buffer.st_size;
}

time_t get_mod_time(const char *filename) {
  struct stat buffer;
  if (stat(filename, &buffer) < 0) {
    perror("stat");
    fprintf(stderr, "Could not get modification time of %s\n", filename);
    return -1;
  }

  return buffer.st_mtime;
}

bool compare_name(std::string str1, std::string str2) {
  if (str1 <= str2) {
    return true;
  }
  else {
    return false;
  }
}

bool compare_size(std::string str1, std::string str2) {
  return get_file_size(str1.c_str()) < get_file_size(str2.c_str());
}

bool compare_time(std::string str1, std::string str2) {
  return get_mod_time(str1.c_str()) < get_mod_time(str2.c_str());
}

char *get_parent_dir(const char *dir_path) {
  char *copy = strdup(dir_path);
  char *s = strrchr(copy, '/');
  *s = 0;
  if (!strcmp(copy, "./htdocs")) {
    return NULL;
  }

  return copy;
}

void handle_directory_listing(int fd, std::string *dir_path, const char *query_string) {
  printf("You are now in handle_directory_listing! directory path: %s, ", dir_path->c_str());
  printf("Query String: %s\n", query_string);
  char contentType[16];
  setContentType(contentType, ".html");
  write200(fd, contentType);

  // Add header to HTML file
  char line[1024];
  std::string html_file = "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">\n"
                          "<html>\n"
                          "<head>\n";
  snprintf(line, sizeof(line), "<title>Index of /homes/cs252/lab5-http-server/lab5-src/%s</title>\n", (dir_path->c_str()) + 2);
  html_file.append(line);
  snprintf(line, sizeof(line), "</head>\n<body>\n<h1>Index of /homes/cs252/lab5-http-server/lab5-src/%s\n<table>\n", (dir_path->c_str()) + 2);
  html_file.append(line);
  //if (info.alphabetic_order) {
  if (!query_string || !strcmp(query_string, "C=N;O=A")) {
    html_file.append("<tr><th valign=\"top\"></th><th><a href=\"?C=N;O=D\">Name</a></th>");
  }
  else {
    html_file.append("<tr><th valign=\"top\"></th><th><a href=\"?C=N;O=A\">Name</a></th>");
  }

  //if (info.latest_order) {
  if (!query_string || !strcmp(query_string, "C=M;O=A")) {
    html_file.append("<th><a href=\"?C=M;O=D\">Last modified</a></th>");
  }
  else {
    html_file.append("<th><a href=\"?C=M;O=A\">Last modified</a></th>");
  }

  //if (info.numeric_order) {
  if (!query_string || !strcmp(query_string, "C=S;O=A")) {
    html_file.append("<th><a href=\"?C=S;O=D\">Size</a></th><th>");
  }
  else {
    html_file.append("<th><a href=\"?C=S;O=A\">Size</a></th><th>");
  }
  html_file.append("<a href=\"?\"C=D;O=A\">Description</a></th></tr>"
                   "<tr><th colspan=\"5\"><hr></th></tr>");


  char *parent = get_parent_dir(dir_path->c_str());
  printf("Parent Dir: %s\n", parent);
  if (parent == NULL) {
    snprintf(line, sizeof(line), "<tr><td valign=\"top\"><img src=\"/icons/back.gif\" alt=\"[PARENTDIR]\"></td><td><a href=\"http://data.cs.purdue.edu:%d\">Parent Directory</a></td>"
                          "<td>&nbsp;</td><td align=\"right\">  - </td><td>&nbsp;</td></tr>", port);
  }
  else {
    snprintf(line, sizeof(line), "<tr><td valign=\"top\"><img src=\"/icons/back.gif\" alt=\"[PARENTDIR]\"></td><td><a href=\"http://data.cs.purdue.edu:%d%s\">Parent Directory</a></td>"
                          "<td>&nbsp;</td><td align=\"right\">  - </td><td>&nbsp;</td></tr>", port, parent + 1);
  }
  free(parent);

  html_file.append(line);

  DIR *dir;
  struct dirent *entry;
  struct stat file_stat;
  const int path_size = 1024;
  char path[path_size];
  std::vector<std::string> files;

  if ((dir = opendir(dir_path->c_str())) == NULL) {
    perror("opendir");
    html_file.append("</table>\n</body></html>\n");
    return;
  }

  while ((entry = readdir(dir)) != NULL) {
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
      continue;
    }

    std::string file_path = *dir_path;
    file_path.append("/");
    file_path.append(entry->d_name);
    if (stat(file_path.c_str(), &file_stat) < 0) {
      perror("stat");
      closedir(dir);
      html_file.append("</table>\n</body></html>\n");
      return;
    }

    //printf("Directory Entry Path: %s\n", file_path.c_str());
    char buf[1024];
    char *res = realpath(file_path.c_str(), buf);
    //printf("Directory Entry Resolved Path: %s\n", res);
    files.push_back(res);
  }

  closedir(dir);

  if (query_string) {
    if (!strncmp(query_string, "C=N", 3)) {
      if (!strcmp(query_string, "C=N;O=D")) {
        std::sort(files.rbegin(), files.rend(), compare_name);
      }
      else {
        std::sort(files.begin(), files.end(), compare_name);
      }
    }
    else if (!strncmp(query_string, "C=M", 3)) {
      if (!strcmp(query_string, "C=M;O=D")) {
        std::sort(files.rbegin(), files.rend(), compare_time);
      }
      else {
        std::sort(files.begin(), files.end(), compare_time);
      }
    }
    else if (!strncmp(query_string, "C=S", 3)) {
      if (!strcmp(query_string, "C=S;O=D")) {
        std::sort(files.rbegin(), files.rend(), compare_size);
      }
      else {
        std::sort(files.begin(), files.end(), compare_size);
      }
    }
  }
  else {
    std::sort(files.begin(), files.end(), compare_name);
  }

  // Add entries to html file
  for (auto &file : files) {
    char line[1024] = {0};
    printf("Now printing file %s\n", file.c_str());
    int file_size = get_file_size(file.c_str());

    char time_str[100] = {0};
    struct tm time_info;
    time_t last_mod_time = get_mod_time(file.c_str());
    localtime_r(&last_mod_time, &time_info);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", &time_info);
    printf("Got strftime\n");

    char *icon = NULL;
    char *file_extension = get_file_extension(const_cast<char *>(file.c_str()));
    printf("File extension: %s\n", file_extension);
    if (!file_extension) {
      if (is_directory(file.c_str())) {
        icon = "/icons/folder.png";
      }
      else {
        icon = "/icons/unknown.gif";
      }
    }
    else if (!strcmp(file_extension, ".gif")) {
      icon = "/icons/image.gif";
    }

    free(file_extension);
    printf("Got file extension\n");

    const char *filename = strrchr(file.c_str(), '/') + 1;
    //std::string href = dir_path->c_str() + 2;
    //href.append("/");
    //href.append(filename);

    snprintf(line, sizeof(line), "<tr><td valign=\"top\"><img src=\"%s\" alt=\"[   ]\"></td><td><a href=\"%s\">%s</a></td><td align=\"right\">%s  </td><td align=\"right\">  %.1fK </td><td>&nbsp;</td></tr>\n",
    icon, filename, filename, time_str, (double) file_size / 1024);
    html_file.append(line);
  }

  // Add footer to the file

  html_file.append("<tr><th colspan=\"5\"><hr></th></tr>\n</table>\n</body></html>\n");

  printf("\n%s\n", html_file.c_str());

  write(fd, html_file.c_str(), html_file.size());
}

void handle_cgi_request(int fd, char *query_string, char *fullpath) {
  char script_path[1024];
  char full_path[2048];
  struct stat file_buffer;
  int status;

  // Check if the file exists
  if (stat(fullpath, &file_buffer) < 0) {
    fprintf(stderr, "Failed to extract script from %s\n", fullpath);
    return;
  }

  // Check if file is executable
  if (!(file_buffer.st_mode & S_IXUSR)) {
    fprintf(stderr, "Failed to execute script %s\n", fullpath);
    return;
  }

  // Save stdout and stderr
  int tmpout = dup(1);
  int tmperr = dup(2);

  // Write header
  const char *message = "HTTP/1.0 200 Document follows\r\n"
                        "Server: CS 252 lab 5\r\n";

  write(fd, message, strlen(message));

  // Fork and run script in child process
  pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    return;
  }
  else if (pid == 0) {
    // Child process

    dup2(fd, 1);
    dup2(fd, 2);

    close(fd);
    close(fd);

    setenv("REQUEST_METHOD", "GET", 1);
    if (query_string) {
      setenv("QUERY_STRING", query_string, 1);
    }

    char *args[2];
    args[0] = fullpath;
    args[1] = NULL;

    execvp(args[0], args);
    perror("execvp");
    printf("Content-Type: text/plain\r\n\r\n");
    printf("500 Internal Server Error: Could not execute CGI script\r\n\r\n");

    exit(EXIT_FAILURE);
  }
  else {
    waitpid(pid, &status, 0);
  }

}

char *get_server_uptime(time_t current_time) {
  double secs = difftime(current_time, start_time);
  int hours = (int) secs / 3600;
  secs -= hours * 3600;
  int minutes = (int) secs / 60;
  secs -= minutes * 60;
  char time_string[64];
  snprintf(time_string, sizeof(time_string), "%d hours, %d minutes, %.2f seconds\n",
            hours, minutes, secs);
  return strdup(time_string);
}

void update_stats(double cpu_time_used, const char *url) {
  if (cpu_time_used < min_request_time) {
    min_request_time = cpu_time_used;
    strcpy(slowest_request, url);
  }

  if (cpu_time_used > max_request_time) {
    max_request_time = cpu_time_used;
    strcpy(fastest_request, url);
  }
}

void sendStats(int fd) {
    char contentType[16];
    setContentType(contentType, ".html");
    write200(fd, contentType);

    std::string stats_page = "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">"
                             "<html>"
                             "<head>"
                             "<title>Stats of /homes/cs252/lab5-http-server/lab5-src/http-root-dir</title>"
                             "</head>"
                             "<h1>Stats of /homes/cs252/lab5-http-server/lab5-src/http-root-dir</h1>";

    char line[1024];

    snprintf(line, sizeof(line), "<p>Name of Author: %s<p>", authorName);
    stats_page.append(line);

    time_t current_time;
    time(&current_time);
    char *formatted_time = get_server_uptime(current_time);
    snprintf(line, sizeof(line), "<p>Server Uptime: %s<p>", formatted_time);
    stats_page.append(line);

    snprintf(line, sizeof(line), "<p>Number of requests: %d<p>", num_requests);
    stats_page.append(line);

    snprintf(line, sizeof(line), "<p>URL Request that took Minimum Service Time: %s\n",
             fastest_request);
    stats_page.append(line);

    snprintf(line, sizeof(line), "<p>Minimum Service Time: %.2f seconds", min_request_time);
    stats_page.append(line);

    snprintf(line, sizeof(line), "<p>URL Request that took Maximum Service Time: %s\n",
              slowest_request);
    stats_page.append(line);

    snprintf(line, sizeof(line), "<p>Maximum Service Time: %.2f seconds", max_request_time);
    stats_page.append(line);

    stats_page.append("<tr><th colspan=\"5\"><hr></th></tr>\n</table>\n</body></html>\n");
    write(fd, stats_page.c_str(), stats_page.size());

    char log_file_info[1024];
    snprintf(log_file_info, sizeof(log_file_info),
             "Name of Author: %s\nServer Uptime: %s\nNumber of requests: %d\n"
             "URL Request that took Minimum Service Time: %s\nMinimum Service Time: %.2f\n"
             "URL Request that took Maximum Service Time: %s\nMaximum Service Time: %.2f seconds\n",
             authorName, formatted_time, num_requests, fastest_request, min_request_time,
             slowest_request, max_request_time);

    int log_fd = open("log", O_CREAT|O_WRONLY|O_TRUNC, 0644);
    if (log_fd < 0) {
      perror("open");
      close(log_fd);
    }
    else {
      write(log_fd, log_file_info, strlen(log_file_info));
      close(log_fd);
    }
    free(formatted_time);
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

  bool gotGet = false;
  bool gotAuth = false;
  bool gotBasic = false;
  bool gotPassword = false;
  bool gotCRLF = false;
  int passwordsMatch;

  // Read the HTTP request from the client
  size_t bytes_read;
  while ((bytes_read = read(fd, &newChar, sizeof(newChar))) > 0) {
    printf("%c", newChar);
    if (newChar == ' ') {
      if (!strncmp("GET", buffer, 3)) {
        gotGet = true;
        bzero(buffer, buffer_size);
        length = 0;
      }
      else if (!strncmp("Authorization", buffer, 13)) {
        gotAuth = true;
        bzero(buffer, buffer_size);
        length = 0;
      }
      else if (!strncmp("Basic", buffer, 5) && gotAuth) {
        gotBasic = true;
        bzero(buffer, buffer_size);
        length = 0;
        gotAuth = false;
      }
      else if (gotGet) {
        buffer[length] = '\0';
        strncpy(requestPath, buffer, buffer_size);
        gotGet = false;
      }
      else {
        gotAuth = false;
        gotBasic = false;
      }
    }
    else if (newChar == '\n' && lastChar == '\r') {
      if (gotBasic) {
        if (strlen(password) == 0) {
          strcpy(password, buffer);
          gotPassword = true;
        }
        gotBasic = false;
      }
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
      buffer[length] = newChar;
      length++;
    }
  }

  //num_requests++;

  // Check if the password was sent in the HTTP request
  if (!gotPassword) {
    printf("No authentication\n\n");
    write401(fd);
  }
  else {
    // Authenticate the password
    printf("Correct Login Info: %s\nPassword entered: %s\n", password, loginInfo);
    printf("Requested Path start: %s\n", requestPath);
    if (!strncmp(password, loginInfo, 23)) {
      printf("Authenticated!\n");
    }
    else {
      printf("Wrong password!\n");
      write401(fd);
      end = clock();
      cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
      update_stats(cpu_time_used, requestPath);
      return;
    }

    // Check the requested path
    char contentType[16] = {0};
    char homePage[128] = "htdocs/index.html";
    char path[1024] = {0};
    chdir("http-root-dir");
    if (!strcmp("/", requestPath) || !strcmp("/htdocs", requestPath)) {
      std::string redirect_path = "/htdocs/";
      std::string response = "HTTP/1.1 301 Moved Permanently\r\n"
                             "Location: " + redirect_path + "\r\n"
                             "Content-Length: 0\r\n"
                             "\r\n";
      write(fd, response.c_str(), response.size());
    }
    else if (!strcmp("/htdocs/", requestPath)) {
      // If the requested path is "/", then send index.html to client

      printf("Requested Path: %s\n", requestPath);
      strcpy(contentType, "text/html");
      write200(fd, contentType);

      struct stat buffer;
      if (stat(homePage, &buffer)) {
        printf("File not found\n");
        write404(fd, contentType);
        end = clock();
        cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
        update_stats(cpu_time_used, requestPath);
        return;
      }

      getcwd(filepath, sizeof(filepath));
      strcat(filepath, "/");
      strcat(filepath, homePage);
      printf("Now sending: %s\n", filepath);

      int file = open(filepath, O_RDONLY);
      if (file == -1) {
        perror("open");
        end = clock();
        cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
        update_stats(cpu_time_used, requestPath);
        return;
      }

      int sz = buffer.st_size;
      int bytes_read;
      char write_buf[sz] = {0};
      write_buf[sz] = '\0';
      while ((bytes_read = read(file, &write_buf, sz)) > 0) {
        write(fd, &write_buf, bytes_read);
      }
      putchar('\n');

      close(fd);
      close(file);
    }
    else if (!strcmp("/stats", requestPath) || !strcmp("/stats/", requestPath)) {
      // If the requested path is "/stats" then send stats page to client
      sendStats(fd);
    }
    else {
      // For any other HTML files
      // If the requested path ends with '/', remove it from the file
      // path, so the directory can be found


      printf("Requested Path ELSE: %s\n", requestPath);
      // Check for any query string and save if one exists
      char *query_string = NULL;
      char *q;
      if ((q = strchr(requestPath, '?'))) {
        query_string = q + 1;
        *q = '\0';
        //if (*(q - 1) == '/') {
        //  *(q - 1) = '\0';
        //}
        printf("Query String: %s\n", query_string);
      }

      char *file_loc;
      if (requestPath[strlen(requestPath) - 1] == '/') {
        char *requestPathCopy = strdup(requestPath);
        requestPathCopy[strlen(requestPathCopy) - 1] = '\0';
        file_loc = search_file(".", requestPathCopy + 1);
        free(requestPathCopy);
      }
      else {
        file_loc = search_file(".", requestPath + 1);
      }

      // Look for the file requested
      fprintf(stdout, "File location: %s\n", file_loc);

      // If the file was not found, send HTML 404 certificate
      if (!file_loc && errno == ENOENT) {
        char *file_extension = get_file_extension(requestPath);
        if (!file_extension) {
          strcpy(file_extension, "text/html");
        }
        printf("File extension: %s\n", file_extension);
        puts("HIT before strcpy");
        strcpy(contentType, file_extension);
        puts("HIT after strcpy");
        fprintf(stderr, "File not found\n");
        write404(fd, contentType);
        fprintf(stderr, "Now sending 404 to client\n");
        FREE(file_extension);
        FREE(file_loc);
        end = clock();
        cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
        update_stats(cpu_time_used, requestPath);
        return;
      }

      // If the file is a directory create the directory listing page
      // and use the query string if needed
      if (errno == EISDIR) {
        puts("You are about to handle directory listing");
        printf("File Loc: %s, Query String: %s, Request Path: %s\n", file_loc, query_string, requestPath);
        if (requestPath[strlen(requestPath) - 1] != '/') {
          std::string new_location = requestPath;
          new_location += "/";
          std::string response = "HTTP/1.1 301 Moved Permanently\r\n";
          response += "Location: " + new_location + "\r\n";
          response += "Content-Length: 0\r\n";
          response += "\r\n";
          write(fd, response.c_str(), response.size());
          end = clock();
          cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
          update_stats(cpu_time_used, requestPath);
          return;
        }
        handle_directory_listing(fd, new std::string(file_loc), query_string);
        end = clock();
        cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
        update_stats(cpu_time_used, requestPath);
        return;
      }

      // Create the absolute path of the file
      getcwd(filepath, sizeof(filepath));
      strcat(filepath, "/");
      strcat(filepath, file_loc + 2);
      fprintf(stdout, "Now sending: %s\n", filepath);

      // Check if it is a CGI request
      const char *cgi_prefix = "/cgi-bin/";
      if (!strncmp(requestPath, cgi_prefix, strlen(cgi_prefix))) {
        handle_cgi_request(fd, query_string, filepath);
        end = clock();
        cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
        update_stats(cpu_time_used, requestPath);
        return;
      }

      // Get the file extension of the file you will send and set content
      // type of the HTML header accordingly.
      // If the file does not have an extension, set
      // the content type of the HTML header as text/html.
      char *file_extension = get_file_extension(filepath);
      if (file_extension) {
        fprintf(stdout, "File extension: %s\n", file_extension);
        setContentType(contentType, file_extension);
      }
      else {
        fprintf(stdout, "File extension: %s\n", file_extension);
        setContentType(contentType, ".html");
      }

      // Check if the file exists
      struct stat buffer;
      if (stat(homePage, &buffer)) {
        fprintf(stderr, "File not found\n");
        write404(fd, contentType);
        end = clock();
        cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
        update_stats(cpu_time_used, requestPath);
        return;
      }

      // Write the HTTP 200 Header
      fprintf(stdout, "Content Type: %s\n", contentType);
      write200(fd, contentType);

      // Open the file
      int file = open(filepath, O_RDONLY);
      if (file == -1) {
        perror("open");
        end = clock();
        cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
        update_stats(cpu_time_used, requestPath);
        return;
      }

      // Read the file and send it to the client
      int sz = buffer.st_size;
      int bytes_read;
      char write_buffer[sz];
      while ((bytes_read = read(file, &write_buffer, sz)) > 0) {
        if ((write(fd, &write_buffer, bytes_read)) == -1) {
          perror("write");
          end = clock();
          cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
          update_stats(cpu_time_used, requestPath);
          return;
        }
      }
      putchar('\n');

      FREE(file_loc);
      FREE(file_extension);
      close(file);
      close(fd);
    }
  }
  end = clock();
  cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
  update_stats(cpu_time_used, requestPath);
  puts("Stats updated");
}

void *handle_request_thread(void *arg) {
  int fd = (intptr_t) arg;
  handleHTTPRequest(fd);
  shutdown(fd, SHUT_RDWR);
  close(fd);
  return NULL;
}

void set_server_mode(bool &process_mode, bool &thread_mode, bool &pool_mode, int &port, char *arg) {
  // If <port> is not passed, you will choose your own default number
  if (!strcmp("-h", arg)) {
    printf("%s", usage);
    exit(0);
  }
  else if (!strcmp("-f", arg)) {
    process_mode = true;
  }
  else if (!strcmp("-t", arg)) {
    thread_mode = true;
  }
  else if (!strcmp("-p", arg)) {
    pool_mode = true;
  }
  else {
    port = atoi(arg);
  }
}

void *pool_thread(void *arg) {
  int *parentSocket = (int *) arg;
  while (1) {
    struct sockaddr_in clientAddress;
    int addr_len = sizeof(clientAddress);
    pthread_mutex_lock(&mutex_lock);
    int childSocket = accept(*parentSocket, (struct sockaddr *) &clientAddress,
                            (socklen_t *) &addr_len);
    pthread_mutex_unlock(&mutex_lock);

    if (childSocket >= 0) {
      slaveSockets.push_back(childSocket);
      handleHTTPRequest(childSocket);
      shutdown(childSocket, SHUT_RDWR);
      close(childSocket);
      puts("Request complete");
    }
    else {
      perror("accept");
      puts("Line 930");
      exit(0);
    }
  }
  return NULL;
}

int
main(int argc, char **argv) {

   // Add your HTTP implementation here
  port = 5445;
  int queue_length = 5;
  bool process_mode = false;
  bool thread_mode = false;
  bool pool_mode = false;

  time(&start_time);

  if (argc == 1) {
    // Run the server in iterative server mode
  }
  else if (argc == 2) {
    set_server_mode(process_mode, thread_mode, pool_mode, port, argv[1]);
  }
  else if (argc == 3) {
    set_server_mode(process_mode, thread_mode, pool_mode, port, argv[1]);
    port = atoi( argv[2] );
  }
  else {
    fprintf(stdout, "Error: too many arguments\n");
    exit( -1 );
  }

  // Register function with signal handler
  struct sigaction sigint_action;
  sigint_action.sa_handler = handle_sigint;
  sigint_action.sa_flags = SA_RESTART;
  sigemptyset(&sigint_action.sa_mask);

  if (sigaction(SIGINT, &sigint_action, NULL) > 0) {
    perror("sigaction");
    exit(-1);
  }

  // Set the IP address and port for this server
  struct sockaddr_in serverIPAddress;
  memset( &serverIPAddress, 0, sizeof(serverIPAddress) );
  serverIPAddress.sin_family = AF_INET;
  serverIPAddress.sin_addr.s_addr = INADDR_ANY;
  serverIPAddress.sin_port = htons((u_short) port);

  // Allocate a socket
  masterSocket = socket(PF_INET, SOCK_STREAM, 0);
  if (masterSocket < 0) {
    perror("socket");
    exit( -1 );
  }

  // Set socket options to reuse port
  int optval = 1;
  int err = setsockopt(masterSocket, SOL_SOCKET, SO_REUSEADDR,
                        (char *) &optval, sizeof( int ));

  // Bind the socket to the IP address and port
  int error = bind( masterSocket, (struct sockaddr *) &serverIPAddress,
                    sizeof(serverIPAddress) );
  if ( error ) {
    perror("bind");
    exit( -1 );
  }

  // Put socket in listening mode and set
  // the size of the queue of unprocessed connections
  error = listen( masterSocket, queue_length );
  if ( error ) {
    perror("listen");
    exit( -1 );
  }

  if (process_mode) {
    // Run process mode
    puts("Process Mode Activated");
    while (1) {
      struct sockaddr_in clientAddress;
      int addr_len = sizeof(clientAddress);
      int slaveSocket = accept(masterSocket, (struct sockaddr *) &clientAddress,
                                (socklen_t *) &addr_len);
      if (slaveSocket < 0) {
        perror("accept");
        puts("Line 1020");
        exit(-1);
      }
      slaveSockets.push_back(slaveSocket);

      // Set up zombie killer signal handler
      struct sigaction sa;
      sa.sa_handler = zombie_killer;
      sa.sa_flags = SA_RESTART;
      sigemptyset(&sa.sa_mask);

      if (sigaction(SIGCHLD, &sa, NULL)) {
        perror("sigaction");
        exit(2);
      }

      if (slaveSocket < 0) {
        perror("accept");
        puts("Line 1038");
        exit(-1);
      }

      num_requests++;
      int ret = fork();
      if (ret == 0) {
        handleHTTPRequest(slaveSocket);
        close(slaveSocket);
        exit(0);
      }
      //shutdown(slaveSocket, SHUT_RDWR);
      close(slaveSocket);
    }
  }
  else if (thread_mode) {
    // Run thread mode
    puts("Thread Mode Activated");
    while (1) {
      struct sockaddr_in clientAddress;
      int addr_len = sizeof(clientAddress);
      int slaveSocket = accept(masterSocket, (struct sockaddr *) &clientAddress,
                              (socklen_t *) &addr_len);

      if (slaveSocket < 0) {
        perror("accept");
        puts("Line 1061");
        exit(-1);
      }
      slaveSockets.push_back(slaveSocket);

      pthread_t thread;
      pthread_attr_t attr;
      pthread_attr_init(&attr);
      pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

      num_requests++;
      int ret = pthread_create(&thread, &attr, &handle_request_thread, (void *) slaveSocket);
      if (ret) {
        perror("pthread_create");
        exit(-1);
      }

      pthread_detach(thread);
    }
  }
  else if (pool_mode) {
    // Run pool mode
    puts("Pool Mode Activated");

    pthread_t pool_threads[4];
    pthread_mutex_init(&mutex_lock, NULL);
    for (int i = 0; i < 4; i++) {
      printf("%d\n", i);
      num_requests++;
      pthread_create(&pool_threads[i], NULL, &pool_thread, (void *) &masterSocket);
    }
    num_requests++;
    pool_thread((void *) &masterSocket);
  }
  else {
    // Run iterative mode
    puts("Iterative Mode Activated");
    while (1) {
      struct sockaddr_in clientAddress;
      int addr_len = sizeof(clientAddress);
      int slaveSocket = accept(masterSocket, (struct sockaddr *) &clientAddress,
                              (socklen_t *) &addr_len);

      if (slaveSocket < 0) {
        perror("accept");
        puts("Line 1104");
        exit(-1);
      }
      slaveSockets.push_back(slaveSocket);

      num_requests++;
      handleHTTPRequest(slaveSocket);

      shutdown(slaveSocket, SHUT_RDWR);
    }
  }
}
