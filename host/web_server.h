#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <map>
#include <mutex>
#include <functional>
#include <vector>

class WebServer {
public:
    struct HttpRequest {
        std::string method;
        std::string path;
        std::map<std::string, std::string> headers;
        std::string body;
        std::map<std::string, std::string> query_params;
        std::string client_ip;
    };
    
    struct HttpResponse {
        int status_code = 200;
        std::map<std::string, std::string> headers;
        std::string body;
        std::string content_type = "text/html";
    };
    
    using RouteHandler = std::function<HttpResponse(const HttpRequest&)>;
    
    WebServer(int port = 8081);
    ~WebServer();
    
    void start();
    void stop();
    void pause();
    void resume();
    bool isRunning() const { return running_.load(); }
    bool isPaused() const { return paused_.load(); }
    
    // Configuration
    void setPort(int port);
    int getPort() const { return port_; }
    
    // Route management
    void addRoute(const std::string& method, const std::string& path, RouteHandler handler);
    void addStaticRoute(const std::string& path, const std::string& content, const std::string& content_type);
    
    // API endpoints
    void setupApiRoutes();
    
    // WebSocket support
    void addWebSocketRoute(const std::string& path, std::function<void(int)> handler);
    void broadcastToWebSockets(const std::string& message);
    
private:
    int port_;
    std::atomic<bool> running_{false};
    std::atomic<bool> should_stop_{false};
    std::atomic<bool> paused_{false};
    std::unique_ptr<std::thread> server_thread_;
    
    // Route storage
    std::map<std::string, std::map<std::string, RouteHandler>> routes_;
    std::map<std::string, std::pair<std::string, std::string>> static_routes_;
    
    // WebSocket connections
    std::vector<int> websocket_connections_;
    std::mutex websocket_mutex_;
    
    // Rate limiting
    struct RateLimitInfo {
        std::chrono::steady_clock::time_point last_request;
        int request_count;
    };
    std::map<std::string, RateLimitInfo> rate_limit_map_;
    std::mutex rate_limit_mutex_;
    
    void serverLoop();
    void handleClient(int client_fd);
    HttpResponse processRequest(const HttpRequest& request);
    HttpRequest parseRequest(const std::string& raw_request);
    std::string serializeResponse(const HttpResponse& response);
    
    // Helper methods
    std::string urlDecode(const std::string& str);
    std::map<std::string, std::string> parseQueryString(const std::string& query);
    bool isWebSocketUpgrade(const HttpRequest& request);
    void handleWebSocket(int client_fd, const HttpRequest& request);
    bool isInCall() const;
    bool isRinging() const;
    bool isHighPriorityState() const;
    bool isAudioActive() const;
    bool checkRateLimit(const std::string& client_ip, const std::string& endpoint);
    HttpResponse createRateLimitResponse();
    
    // Default handlers
    HttpResponse handleNotFound(const HttpRequest& request);
    HttpResponse handleApiStatus(const HttpRequest& request);
    HttpResponse handleApiMetrics(const HttpRequest& request);
    HttpResponse handleApiHealth(const HttpRequest& request);
    HttpResponse handleApiConfig(const HttpRequest& request);
    HttpResponse handleApiState(const HttpRequest& request);
    HttpResponse handleApiControl(const HttpRequest& request);
    HttpResponse handleApiLogs(const HttpRequest& request);
};
