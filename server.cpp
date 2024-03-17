#include <iostream>
#include <thread>
#include <cstdlib>
#include <string>
#include <sstream>
#include <fstream>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <map>
#include <filesystem>

const std::string CRLF = "\r\n";
const std::string ST_OK = "HTTP/1.1 200 OK" + CRLF;
const std::string ST_CR = "HTTP/1.1 201 Created" + CRLF;
const std::string ST_NF = "HTTP/1.1 404 Not Found" + CRLF;

const int port_number = 4221;

bool directory_enabled = false;
std::string directory{};

struct HttpRequest {
    std::string method;
    std::string path;
    std::string version;
    std::map<std::string, std::string> headers;
    std::string body;
};

void parse_header(HttpRequest &http_request, std::string header) {
  std::istringstream iss(header);
  std::string line;

  while (std::getline(iss, line) && !line.empty()) {
    size_t pos = line.find(':');
    if (pos != std::string::npos) {
      std::string key = line.substr(0, pos);
      std::string value = line.substr(pos+1);
      // Remove leading and trailing whitespace from value
      value.erase(0, value.find_first_not_of(" \t\n\r\f\v"));
      value.erase(value.find_last_not_of(" \t\n\r\f\v") + 1);
      http_request.headers[key] = value;
    }
  }

  http_request.body = iss.str();
}

HttpRequest parse_http_request(const std::string &request) {
  HttpRequest http_request;

  // Extract the method (GET, POST, etc.) and path from the start-line
  std::istringstream iss(request);
  std::string line;

  std::getline(iss, line);
  std::istringstream start_line(line);
  start_line >> http_request.method >> http_request.path >> http_request.version;

  // Parse headers
  while (std::getline(iss, line) && !(line.compare("\r") == 0)) {
    size_t pos = line.find(':');
    if (pos != std::string::npos) {
      std::string key = line.substr(0, pos);
      std::string value = line.substr(pos+1);
      // Remove leading and trailing whitespace from value
      value.erase(0, value.find_first_not_of(" \t\n\r\f\v"));
      value.erase(value.find_last_not_of(" \t\n\r\f\v") + 1);
      http_request.headers[key] = value;
    }
  }

  // Extract the body
  http_request.body = request.substr(request.find("\r\n\r\n") + 4);

  return http_request;
}

std::string create_response(std::string content, bool is_file=false) {
  return  ST_OK +
          "Content-Type: " + (is_file ? "application/octet-stream" : "text/plain") + CRLF +
          "Content-Length: " + std::to_string(content.size()) + CRLF +
          CRLF + content + CRLF;
}

std::string handle_echo(HttpRequest &http_request) {
  size_t echo_pos = http_request.path.find("/echo/");
  std::string content = http_request.path.substr(echo_pos + 6);
  return create_response(content);
}

std::string handle_files(HttpRequest &http_request) {
  size_t file_pos = http_request.path.find("/files/");
  std::string file_name = http_request.path.substr(file_pos + 7);
  std::ifstream file(directory + file_name);
  if (!file.good() || !file.is_open()) {
    std::cerr << "Failed to open file: " << file_name << std::endl;
      return ST_NF + CRLF;
  } else {
    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();
    return create_response(buffer.str(), true);
    }
}

void handle_file_creation(HttpRequest &http_request) {
  size_t file_pos = http_request.path.find("/files/");
  std::string file_name = http_request.path.substr(file_pos + 7);
  std::ofstream file(directory + file_name);
  if (file.is_open()) {
    file << http_request.body;
    file.close();
  }
}

std::string handle_get(HttpRequest &http_request) {
  std::string data;

  if (http_request.path == "/") {
    data = ST_OK + CRLF;
  } else if (http_request.path.find("/echo/") != std::string::npos) {
    data = handle_echo(http_request);
  } else if (http_request.path == "/user-agent") {
    data = create_response(http_request.headers["User-Agent"]);
  } else if (http_request.path.find("/files/") != std::string::npos) {
    data = handle_files(http_request);
  } else {
    data = ST_NF + CRLF;
  }

  return data;
}

std::string handle_post(HttpRequest &http_request) {
  std::string data;

  if (http_request.path.find("/files/") != std::string::npos) {
    handle_file_creation(http_request);
    data = ST_CR + CRLF;
  } else {
    data = ST_NF + CRLF;
  }

  return data;
}

void handle_client(int client_fd) {
  std::string buffer;
  char temp_buffer[1024] = {0};
  int bytes_received;

  do {
    bytes_received = recv(client_fd, temp_buffer, sizeof(temp_buffer), 0);
    if (bytes_received > 0) {
      buffer.append(temp_buffer, bytes_received);
    } else if (bytes_received == 0) {
      std::cout << "Client disconnected\n";
    } else {
      std::cerr << "Error receiving data\n";
      close(client_fd);
      return;
    }
  } while (bytes_received == sizeof(temp_buffer));
  
  // buffer.resize(bytes_received); // Resize the string to the actual size of the received data
  std::cout << "Received data from client: " << buffer << std::endl;

  // Find the position of the first occurrence of "\r\n"
  size_t pos = buffer.find("\r\n\r\n");
  if (pos == std::string::npos) {
    std::cerr << "Invalid request format\n";
    close(client_fd);
    return;
  }

  std::string temp_data;

  HttpRequest http_request = parse_http_request(buffer);
  std::cout << "Body: \n" << http_request.body << std::endl;
  if (http_request.method == "GET") {
    temp_data = handle_get(http_request);
  } else if (http_request.method == "POST") {
    temp_data = handle_post(http_request);
  }

  char data[1024] = {0};
  strcpy(data, temp_data.c_str());
  write(client_fd, data, sizeof(data));

  close(client_fd);
}

int main(int argc, char **argv) {
  if (argc > 2) {
    std::string options = argv[1];
    if (options.compare("--directory") == 0) {
      directory_enabled = true;
      directory = argv[2];
    } 
  }

  // You can use print statements as follows for debugging, they'll be visible when running tests.
  std::cout << "Logs from your program will appear here!\n";

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    std::cerr << "Failed to create server socket\n";
    return 1;
  }

  // Since the tester restarts your program quite often, setting REUSE_PORT
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) < 0) {
    std::cerr << "setsockopt failed\n";
    return 1;
  }

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(port_number);

  if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
    std::cerr << "Failed to bind to port 4221\n";
    return 1;
  }

  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    std::cerr << "listen failed\n";
    return 1;
  }

  std::cout << "Waiting for a client to connect...\n";
  while (true) {
    struct sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);
    int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len);
    if (client_fd == -1) {
      std::cerr << "Error accepting connection\n";
      continue;
    }

    std::thread t(handle_client, client_fd);
    t.detach();
  }

  close(server_fd);
  return 0;
}