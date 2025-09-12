#include "metrics_server.h"
#include "metrics.h"
#include "logger.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <sstream>

MetricsServer::MetricsServer(int port) : port_(port) {
}

MetricsServer::~MetricsServer() {
    stop();
}

void MetricsServer::start() {
    if (running_.load()) {
        return;
    }
    
    should_stop_ = false;
    running_ = true;
    
    server_thread_ = std::make_unique<std::thread>(&MetricsServer::serverLoop, this);
    
    MillenniumLogger::getInstance().info("MetricsServer", 
        "Metrics server started on port " + std::to_string(port_));
}

void MetricsServer::stop() {
    if (!running_.load()) {
        return;
    }
    
    should_stop_ = true;
    running_ = false;
    
    if (server_thread_ && server_thread_->joinable()) {
        server_thread_->join();
    }
    
    MillenniumLogger::getInstance().info("MetricsServer", "Metrics server stopped");
}

void MetricsServer::setPort(int port) {
    if (running_.load()) {
        MillenniumLogger::getInstance().warn("MetricsServer", 
            "Cannot change port while server is running");
        return;
    }
    port_ = port;
}

void MetricsServer::serverLoop() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        MillenniumLogger::getInstance().error("MetricsServer", 
            "Failed to create socket");
        return;
    }
    
    // Set socket options for reuse
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);
    
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        MillenniumLogger::getInstance().error("MetricsServer", 
            "Failed to bind to port " + std::to_string(port_));
        close(server_fd);
        return;
    }
    
    if (listen(server_fd, 3) < 0) {
        MillenniumLogger::getInstance().error("MetricsServer", 
            "Failed to listen on socket");
        close(server_fd);
        return;
    }
    
    // Set socket to non-blocking
    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);
    
    while (!should_stop_.load()) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_fd >= 0) {
            // Handle request in a simple way (no threading to save resources)
            char buffer[1024] = {0};
            ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
            
            if (bytes_read > 0) {
                std::string request(buffer);
                std::string response = handleRequest(request);
                sendResponse(client_fd, response);
            }
            
            close(client_fd);
        }
        
        // Small delay to prevent busy waiting
        usleep(10000); // 10ms
    }
    
    close(server_fd);
}

std::string MetricsServer::handleRequest(const std::string& request) {
    // Simple request parsing
    if (request.find("GET /metrics") != std::string::npos) {
        return generateMetricsResponse();
    } else if (request.find("GET /health") != std::string::npos) {
        return generateHealthResponse();
    } else if (request.find("GET /") != std::string::npos) {
        // Default to metrics for any GET request
        return generateMetricsResponse();
    }
    
    // Default response
    return "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
}

std::string MetricsServer::generateMetricsResponse() {
    Metrics& metrics = Metrics::getInstance();
    std::string prometheus_data = metrics.exportPrometheus();
    
    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: text/plain; version=0.0.4; charset=utf-8\r\n";
    response << "Content-Length: " << prometheus_data.length() << "\r\n";
    response << "Connection: close\r\n";
    response << "\r\n";
    response << prometheus_data;
    
    return response.str();
}

std::string MetricsServer::generateHealthResponse() {
    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: application/json\r\n";
    response << "Content-Length: 15\r\n";
    response << "Connection: close\r\n";
    response << "\r\n";
    response << "{\"status\":\"ok\"}";
    
    return response.str();
}

void MetricsServer::sendResponse(int client_fd, const std::string& response) {
    send(client_fd, response.c_str(), response.length(), 0);
}
