#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <memory>

class MetricsServer {
public:
    MetricsServer(int port = 8080);
    ~MetricsServer();
    
    void start();
    void stop();
    bool isRunning() const { return running_.load(); }
    
    // Configuration
    void setPort(int port);
    int getPort() const { return port_; }
    
private:
    int port_;
    std::atomic<bool> running_{false};
    std::atomic<bool> should_stop_{false};
    std::unique_ptr<std::thread> server_thread_;
    
    void serverLoop();
    std::string handleRequest(const std::string& request);
    std::string generateMetricsResponse();
    std::string generateHealthResponse();
    void sendResponse(int client_fd, const std::string& response);
};
