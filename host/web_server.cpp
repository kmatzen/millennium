#include "web_server.h"
#include "config.h"
#include "logger.h"
#include "metrics.h"
#include "health_monitor.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <regex>
#include <fstream>
#include <filesystem>
#include <iomanip>

// External references to daemon state (will be set by daemon)
extern std::unique_ptr<class DaemonState> daemon_state;
extern std::unique_ptr<class MillenniumClient> client;

// Forward declaration of DaemonState structure
struct DaemonStateInfo {
    int current_state;
    int inserted_cents;
    std::string keypad_buffer;
    std::chrono::steady_clock::time_point last_activity;
};

// Function to get daemon state info (will be implemented in daemon)
DaemonStateInfo getDaemonStateInfo();

// Function to get daemon uptime (will be implemented in daemon)
std::chrono::steady_clock::time_point getDaemonStartTime();

// Function to send control commands to daemon (will be implemented in daemon)
bool sendControlCommand(const std::string& action);

WebServer::WebServer(int port) : port_(port) {
}

WebServer::~WebServer() {
    stop();
}

void WebServer::start() {
    if (running_.load()) {
        return;
    }
    
    should_stop_ = false;
    running_ = true;
    
    // Setup API routes
    setupApiRoutes();
    
    server_thread_ = std::make_unique<std::thread>(&WebServer::serverLoop, this);
    
    MillenniumLogger::getInstance().info("WebServer", 
        "Web server started on port " + std::to_string(port_));
}

void WebServer::stop() {
    if (!running_.load()) {
        return;
    }
    
    should_stop_ = true;
    running_ = false;
    
    if (server_thread_ && server_thread_->joinable()) {
        server_thread_->join();
    }
    
    MillenniumLogger::getInstance().info("WebServer", "Web server stopped");
}

void WebServer::pause() {
    paused_ = true;
    MillenniumLogger::getInstance().info("WebServer", "Web server paused for audio protection");
}

void WebServer::resume() {
    paused_ = false;
    MillenniumLogger::getInstance().info("WebServer", "Web server resumed");
}

void WebServer::setPort(int port) {
    if (running_.load()) {
        MillenniumLogger::getInstance().warn("WebServer", 
            "Cannot change port while server is running");
        return;
    }
    port_ = port;
}

void WebServer::addRoute(const std::string& method, const std::string& path, RouteHandler handler) {
    routes_[method][path] = handler;
}

void WebServer::addStaticRoute(const std::string& path, const std::string& content, const std::string& content_type) {
    static_routes_[path] = {content, content_type};
}

void WebServer::setupApiRoutes() {
    // API routes
    addRoute("GET", "/api/status", [this](const HttpRequest& req) { return handleApiStatus(req); });
    addRoute("GET", "/api/metrics", [this](const HttpRequest& req) { return handleApiMetrics(req); });
    addRoute("GET", "/api/health", [this](const HttpRequest& req) { return handleApiHealth(req); });
    addRoute("GET", "/api/config", [this](const HttpRequest& req) { return handleApiConfig(req); });
    addRoute("GET", "/api/state", [this](const HttpRequest& req) { return handleApiState(req); });
    addRoute("POST", "/api/control", [this](const HttpRequest& req) { return handleApiControl(req); });
    addRoute("GET", "/api/logs", [this](const HttpRequest& req) { return handleApiLogs(req); });
    
    // WebSocket route for real-time updates
    addWebSocketRoute("/ws", [this](int client_fd) {
        // WebSocket connection established
        std::lock_guard<std::mutex> lock(websocket_mutex_);
        websocket_connections_.push_back(client_fd);
    });
}

void WebServer::addWebSocketRoute(const std::string& path, std::function<void(int)> handler) {
    // WebSocket routes are handled specially in the request processing
}

void WebServer::broadcastToWebSockets(const std::string& message) {
    std::lock_guard<std::mutex> lock(websocket_mutex_);
    auto it = websocket_connections_.begin();
    while (it != websocket_connections_.end()) {
        int fd = *it;
        if (send(fd, message.c_str(), message.length(), 0) < 0) {
            // Connection closed, remove it
            close(fd);
            it = websocket_connections_.erase(it);
        } else {
            ++it;
        }
    }
}

void WebServer::serverLoop() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        MillenniumLogger::getInstance().error("WebServer", 
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
        MillenniumLogger::getInstance().error("WebServer", 
            "Failed to bind to port " + std::to_string(port_));
        close(server_fd);
        return;
    }
    
    if (listen(server_fd, 10) < 0) {
        MillenniumLogger::getInstance().error("WebServer", 
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
            handleClient(client_fd);
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            // Log error if it's not just a non-blocking accept
            MillenniumLogger::getInstance().error("WebServer", 
                "Accept failed with error: " + std::to_string(errno));
        }
    }
    
    close(server_fd);
}

void WebServer::handleClient(int client_fd) {
    char buffer[4096] = {0};
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    
    if (bytes_read > 0) {
        std::string request(buffer);
        HttpRequest parsed_request = parseRequest(request);
        
        // Get client IP for rate limiting
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        if (getpeername(client_fd, (struct sockaddr*)&client_addr, &client_len) == 0) {
            parsed_request.client_ip = inet_ntoa(client_addr.sin_addr);
        } else {
            parsed_request.client_ip = "unknown";
        }
        
        HttpResponse response = processRequest(parsed_request);
        std::string response_str = serializeResponse(response);
        send(client_fd, response_str.c_str(), response_str.length(), 0);
    }
    
    close(client_fd);
}

WebServer::HttpRequest WebServer::parseRequest(const std::string& raw_request) {
    HttpRequest request;
    std::istringstream stream(raw_request);
    std::string line;
    
    // Parse request line
    if (std::getline(stream, line)) {
        std::istringstream line_stream(line);
        line_stream >> request.method >> request.path;
        
        // Remove query parameters from path
        size_t query_pos = request.path.find('?');
        if (query_pos != std::string::npos) {
            std::string query_string = request.path.substr(query_pos + 1);
            request.path = request.path.substr(0, query_pos);
            request.query_params = parseQueryString(query_string);
        }
    }
    
    // Parse headers
    while (std::getline(stream, line) && line != "\r") {
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1);
            
            // Trim whitespace
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t\r") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t\r") + 1);
            
            request.headers[key] = value;
        }
    }
    
    // Parse body (if any)
    std::ostringstream body_stream;
    while (std::getline(stream, line)) {
        body_stream << line << "\n";
    }
    request.body = body_stream.str();
    
    return request;
}

WebServer::HttpResponse WebServer::processRequest(const HttpRequest& request) {
    // Apply rate limiting for API endpoints
    if (request.path.find("/api/") == 0) {
        if (!checkRateLimit(request.client_ip, request.path)) {
            return createRateLimitResponse();
        }
    }
    
    // Check for WebSocket upgrade
    if (isWebSocketUpgrade(request)) {
        // Handle WebSocket connection
        return HttpResponse{101, {{"Upgrade", "websocket"}, {"Connection", "Upgrade"}}, "", ""};
    }
    
    // Check static routes first
    auto static_it = static_routes_.find(request.path);
    if (static_it != static_routes_.end()) {
        HttpResponse response;
        response.body = static_it->second.first;
        response.content_type = static_it->second.second;
        return response;
    }
    
    // Check dynamic routes
    auto method_it = routes_.find(request.method);
    if (method_it != routes_.end()) {
        auto path_it = method_it->second.find(request.path);
        if (path_it != method_it->second.end()) {
            return path_it->second(request);
        }
    }
    
    // Default to 404
    return handleNotFound(request);
}

std::string WebServer::serializeResponse(const HttpResponse& response) {
    std::ostringstream oss;
    
    // Status line
    oss << "HTTP/1.1 " << response.status_code << " ";
    switch (response.status_code) {
        case 200: oss << "OK"; break;
        case 404: oss << "Not Found"; break;
        case 500: oss << "Internal Server Error"; break;
        case 101: oss << "Switching Protocols"; break;
        default: oss << "Unknown"; break;
    }
    oss << "\r\n";
    
    // Headers
    for (const auto& header : response.headers) {
        oss << header.first << ": " << header.second << "\r\n";
    }
    
    if (!response.content_type.empty()) {
        oss << "Content-Type: " << response.content_type << "\r\n";
    }
    
    oss << "Content-Length: " << response.body.length() << "\r\n";
    oss << "Connection: close\r\n";
    oss << "\r\n";
    
    // Body
    oss << response.body;
    
    return oss.str();
}

std::string WebServer::urlDecode(const std::string& str) {
    std::string result;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%' && i + 2 < str.length()) {
            int value;
            std::istringstream is(str.substr(i + 1, 2));
            if (is >> std::hex >> value) {
                result += static_cast<char>(value);
                i += 2;
            } else {
                result += str[i];
            }
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    return result;
}

std::map<std::string, std::string> WebServer::parseQueryString(const std::string& query) {
    std::map<std::string, std::string> params;
    std::istringstream stream(query);
    std::string pair;
    
    while (std::getline(stream, pair, '&')) {
        size_t eq_pos = pair.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = urlDecode(pair.substr(0, eq_pos));
            std::string value = urlDecode(pair.substr(eq_pos + 1));
            params[key] = value;
        }
    }
    
    return params;
}

bool WebServer::isWebSocketUpgrade(const HttpRequest& request) {
    auto upgrade_it = request.headers.find("Upgrade");
    auto connection_it = request.headers.find("Connection");
    
    return upgrade_it != request.headers.end() && 
           upgrade_it->second == "websocket" &&
           connection_it != request.headers.end() &&
           connection_it->second.find("Upgrade") != std::string::npos;
}

WebServer::HttpResponse WebServer::handleNotFound(const HttpRequest& request) {
    HttpResponse response;
    response.status_code = 404;
    response.content_type = "text/html";
    response.body = R"(
<!DOCTYPE html>
<html>
<head>
    <title>404 - Not Found</title>
    <style>
        body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }
        h1 { color: #e74c3c; }
    </style>
</head>
<body>
    <h1>404 - Page Not Found</h1>
    <p>The requested resource was not found.</p>
    <a href="/">Return to Dashboard</a>
</body>
</html>
)";
    return response;
}

WebServer::HttpResponse WebServer::handleApiStatus(const HttpRequest& request) {
    HttpResponse response;
    response.content_type = "application/json";
    
    // Calculate actual uptime
    auto now = std::chrono::steady_clock::now();
    auto start_time = getDaemonStartTime();
    auto uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
    
    std::ostringstream json;
    json << "{";
    json << "\"status\":\"running\",";
    json << "\"uptime\":" << uptime_seconds << ",";
    json << "\"version\":\"1.0.0\",";
    json << "\"timestamp\":" << std::time(nullptr);
    json << "}";
    
    response.body = json.str();
    return response;
}

WebServer::HttpResponse WebServer::handleApiMetrics(const HttpRequest& request) {
    HttpResponse response;
    response.content_type = "application/json";
    
    Metrics& metrics = Metrics::getInstance();
    
    std::ostringstream json;
    json << "{";
    json << "\"counters\":{";
    
    auto counters = metrics.getAllCounters();
    bool first = true;
    for (const auto& pair : counters) {
        if (!first) json << ",";
        json << "\"" << pair.first << "\":" << pair.second;
        first = false;
    }
    json << "},";
    
    json << "\"gauges\":{";
    auto gauges = metrics.getAllGauges();
    first = true;
    for (const auto& pair : gauges) {
        if (!first) json << ",";
        json << "\"" << pair.first << "\":" << pair.second;
        first = false;
    }
    json << "}";
    
    json << "}";
    
    response.body = json.str();
    return response;
}

WebServer::HttpResponse WebServer::handleApiHealth(const HttpRequest& request) {
    HttpResponse response;
    response.content_type = "application/json";
    
    HealthMonitor& health_monitor = HealthMonitor::getInstance();
    auto overall_status = health_monitor.getOverallStatus();
    auto checks = health_monitor.getAllChecks();
    
    std::ostringstream json;
    json << "{";
    json << "\"overall_status\":\"" << HealthMonitor::statusToString(overall_status) << "\",";
    json << "\"checks\":{";
    
    bool first = true;
    for (const auto& pair : checks) {
        if (!first) json << ",";
        json << "\"" << pair.first << "\":{";
        json << "\"status\":\"" << HealthMonitor::statusToString(pair.second.last_status) << "\",";
        json << "\"message\":\"" << pair.second.last_message << "\",";
        json << "\"last_check\":" << std::chrono::duration_cast<std::chrono::seconds>(
            pair.second.last_check.time_since_epoch()).count();
        json << "}";
        first = false;
    }
    json << "}";
    json << "}";
    
    response.body = json.str();
    return response;
}

WebServer::HttpResponse WebServer::handleApiConfig(const HttpRequest& request) {
    HttpResponse response;
    response.content_type = "application/json";
    
    Config& config = Config::getInstance();
    
    std::ostringstream json;
    json << "{";
    json << "\"hardware\":{";
    json << "\"display_device\":\"" << config.getDisplayDevice() << "\",";
    json << "\"baud_rate\":" << config.getBaudRate();
    json << "},";
    json << "\"call\":{";
    json << "\"cost_cents\":" << config.getCallCostCents() << ",";
    json << "\"timeout_seconds\":" << config.getCallTimeoutSeconds();
    json << "},";
    json << "\"logging\":{";
    json << "\"level\":\"" << config.getLogLevel() << "\",";
    json << "\"file\":\"" << config.getLogFile() << "\",";
    json << "\"to_file\":" << (config.getLogToFile() ? "true" : "false");
    json << "},";
    json << "\"system\":{";
    json << "\"update_interval_ms\":" << config.getUpdateIntervalMs() << ",";
    json << "\"max_retries\":" << config.getMaxRetries();
    json << "},";
    json << "\"metrics_server\":{";
    json << "\"enabled\":" << (config.getMetricsServerEnabled() ? "true" : "false") << ",";
    json << "\"port\":" << config.getMetricsServerPort();
    json << "},";
    json << "\"web_server\":{";
    json << "\"enabled\":" << (config.getWebServerEnabled() ? "true" : "false") << ",";
    json << "\"port\":" << config.getWebServerPort();
    json << "}";
    json << "}";
    
    response.body = json.str();
    return response;
}

WebServer::HttpResponse WebServer::handleApiState(const HttpRequest& request) {
    HttpResponse response;
    response.content_type = "application/json";
    
    std::ostringstream json;
    json << "{";
    
    try {
        DaemonStateInfo state_info = getDaemonStateInfo();
        json << "\"current_state\":" << state_info.current_state << ",";
        json << "\"inserted_cents\":" << state_info.inserted_cents << ",";
        json << "\"keypad_buffer\":\"" << state_info.keypad_buffer << "\",";
        json << "\"last_activity\":" << std::chrono::duration_cast<std::chrono::seconds>(
            state_info.last_activity.time_since_epoch()).count();
    } catch (...) {
        json << "\"current_state\":0,";
        json << "\"inserted_cents\":0,";
        json << "\"keypad_buffer\":\"\",";
        json << "\"last_activity\":0";
    }
    
    json << "}";
    
    response.body = json.str();
    return response;
}

WebServer::HttpResponse WebServer::handleApiControl(const HttpRequest& request) {
    HttpResponse response;
    response.content_type = "application/json";
    
    std::ostringstream json;
    json << "{";
    
    try {
        // Parse JSON from request body
        std::string action = "unknown";
        std::string key = "";
        int cents = 0;
        
        // Parse action
        size_t action_pos = request.body.find("\"action\":\"");
        if (action_pos != std::string::npos) {
            size_t start = action_pos + 10; // Length of "action":"
            size_t end = request.body.find("\"", start);
            if (end != std::string::npos) {
                action = request.body.substr(start, end - start);
            }
        }
        
        // Parse key for keypad actions
        size_t key_pos = request.body.find("\"key\":\"");
        if (key_pos != std::string::npos) {
            size_t start = key_pos + 7; // Length of "key":"
            size_t end = request.body.find("\"", start);
            if (end != std::string::npos) {
                key = request.body.substr(start, end - start);
            }
        }
        
        // Parse cents for card actions
        size_t cents_pos = request.body.find("\"cents\":");
        if (cents_pos != std::string::npos) {
            size_t start = cents_pos + 8; // Length of "cents":
            size_t end = request.body.find_first_of(",}", start);
            if (end != std::string::npos) {
                std::string cents_str = request.body.substr(start, end - start);
                cents = std::stoi(cents_str);
            }
        }
        
        // Handle different actions
        bool success = false;
        std::string message;
        
        if (action == "start_call") {
            success = sendControlCommand("start_call");
            message = success ? "Call initiation requested" : "Failed to initiate call";
        } else if (action == "reset_system") {
            success = sendControlCommand("reset_system");
            message = success ? "System reset initiated" : "Failed to reset system";
        } else if (action == "emergency_stop") {
            success = sendControlCommand("emergency_stop");
            message = success ? "Emergency stop activated" : "Failed to activate emergency stop";
        } else if (action == "keypad_press") {
            success = sendControlCommand("keypad_press:" + key);
            message = success ? "Keypad key " + key + " pressed" : "Failed to simulate keypad press";
        } else if (action == "keypad_clear") {
            success = sendControlCommand("keypad_clear");
            message = success ? "Keypad cleared" : "Failed to clear keypad";
        } else if (action == "keypad_backspace") {
            success = sendControlCommand("keypad_backspace");
            message = success ? "Keypad backspace" : "Failed to simulate backspace";
        } else if (action == "coin_insert") {
            success = sendControlCommand("coin_insert:" + std::to_string(cents));
            message = success ? "Coin inserted: " + std::to_string(cents) + "¢" : "Failed to insert coin";
        } else if (action == "coin_return") {
            success = sendControlCommand("coin_return");
            message = success ? "Coins returned" : "Failed to return coins";
        } else if (action == "handset_up") {
            success = sendControlCommand("handset_up");
            message = success ? "Handset lifted" : "Failed to simulate handset up";
        } else if (action == "handset_down") {
            success = sendControlCommand("handset_down");
            message = success ? "Handset placed down" : "Failed to simulate handset down";
        } else {
            success = false;
            message = "Unknown action: " + action;
        }
        
        json << "\"success\":" << (success ? "true" : "false") << ",";
        json << "\"action\":\"" << action << "\",";
        json << "\"message\":\"" << message << "\"";
    } catch (...) {
        json << "\"success\":false,";
        json << "\"action\":\"unknown\",";
        json << "\"message\":\"Failed to parse request\"";
    }
    
    json << "}";
    response.body = json.str();
    return response;
}

WebServer::HttpResponse WebServer::handleApiLogs(const HttpRequest& request) {
    HttpResponse response;
    response.content_type = "application/json";
    
    // Get log level parameter
    std::string level = "INFO";
    auto level_it = request.query_params.find("level");
    if (level_it != request.query_params.end()) {
        level = level_it->second;
    }
    
    // Get max entries parameter (default to 20, max 50)
    int max_entries = 20;
    auto max_it = request.query_params.find("max_entries");
    if (max_it != request.query_params.end()) {
        try {
            max_entries = std::stoi(max_it->second);
            max_entries = std::min(max_entries, 50); // Cap at 50 entries
            max_entries = std::max(max_entries, 1);  // Minimum 1 entry
        } catch (const std::exception&) {
            max_entries = 20; // Use default if parsing fails
        }
    }
    
    // Get log file path from config
    Config& config = Config::getInstance();
    std::string log_file = config.getLogFile();
    
    std::ostringstream json;
    json << "{";
    json << "\"logs\":[";
    
    std::vector<std::string> log_entries;
    
    // Try to read from log file if it exists and logging to file is enabled
    if (config.getLogToFile() && !log_file.empty() && std::filesystem::exists(log_file)) {
        try {
            std::ifstream file(log_file);
            if (file.is_open()) {
                std::string line;
                while (std::getline(file, line)) {
                    if (!line.empty()) {
                        log_entries.push_back(line);
                    }
                }
                file.close();
            }
        } catch (const std::exception& e) {
            MillenniumLogger::getInstance().warn("WebServer", 
                "Failed to read log file: " + std::string(e.what()));
        }
    }
    
    // If no log file entries, return empty logs
    if (log_entries.empty()) {
        json << "],";
        json << "\"total\":0";
        json << "}";
        response.body = json.str();
        return response;
    }
    
    // Parse and format log entries
    bool first = true;
    int total_count = 0;
    const size_t MAX_RESPONSE_SIZE = 50000; // Limit response to ~50KB
    
    // Process entries in reverse order (newest first) and limit to last max_entries entries
    int start_idx = std::max(0, static_cast<int>(log_entries.size()) - max_entries);
    for (int i = static_cast<int>(log_entries.size()) - 1; i >= start_idx; --i) {
        const std::string& entry = log_entries[i];
        
        // Parse log entry format: [timestamp] [level] [category] message
        std::regex log_regex(R"(\[([^\]]+)\]\s+\[([^\]]+)\]\s+(?:\[([^\]]+)\]\s+)?(.+))");
        std::smatch matches;
        
        if (std::regex_match(entry, matches, log_regex)) {
            std::string timestamp_str = matches[1].str();
            std::string level_str = matches[2].str();
            std::string category = matches.size() > 3 ? matches[3].str() : "";
            std::string message = matches.size() > 4 ? matches[4].str() : matches[3].str();
            
            // Convert timestamp to Unix timestamp
            std::tm tm = {};
            std::istringstream ss(timestamp_str);
            ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
            if (ss.fail()) {
                // Fallback to current time if parsing fails
                auto now = std::chrono::system_clock::now();
                auto time_t_now = std::chrono::system_clock::to_time_t(now);
                tm = *std::localtime(&time_t_now);
            }
            auto time_t = std::mktime(&tm);
            
            // Filter by level if specified
            if (level != "ALL") {
                std::string upper_level = level;
                std::transform(upper_level.begin(), upper_level.end(), upper_level.begin(), ::toupper);
                std::string upper_entry_level = level_str;
                std::transform(upper_entry_level.begin(), upper_entry_level.end(), upper_entry_level.begin(), ::toupper);
                
                if (upper_level != upper_entry_level && upper_level != "INFO") {
                    continue;
                }
            }
            
            // Escape JSON special characters in message and truncate if too long
            std::string escaped_message = message;
            std::replace(escaped_message.begin(), escaped_message.end(), '"', '\'');
            std::replace(escaped_message.begin(), escaped_message.end(), '\n', ' ');
            std::replace(escaped_message.begin(), escaped_message.end(), '\r', ' ');
            
            // Truncate very long messages to prevent huge responses
            if (escaped_message.length() > 500) {
                escaped_message = escaped_message.substr(0, 497) + "...";
            }
            
            // Check if adding this entry would exceed our size limit
            std::ostringstream test_json;
            test_json << (first ? "" : ",");
            test_json << "{\"timestamp\":" << time_t << ",\"level\":\"" << level_str << "\"";
            if (!category.empty()) {
                test_json << ",\"category\":\"" << category << "\"";
            }
            test_json << ",\"message\":\"" << escaped_message << "\"}";
            
            if (json.str().length() + test_json.str().length() > MAX_RESPONSE_SIZE) {
                break; // Stop adding entries if we'd exceed size limit
            }
            
            json << (first ? "" : ",");
            json << "{";
            json << "\"timestamp\":" << time_t << ",";
            json << "\"level\":\"" << level_str << "\",";
            if (!category.empty()) {
                json << "\"category\":\"" << category << "\",";
            }
            json << "\"message\":\"" << escaped_message << "\"";
            json << "}";
            
            first = false;
            total_count++;
        }
    }
    
    json << "],";
    json << "\"total\":" << total_count;
    json << "}";
    
    response.body = json.str();
    return response;
}

bool WebServer::isInCall() const {
    try {
        DaemonStateInfo state = getDaemonStateInfo();
        // States: 0=INVALID, 1=IDLE_DOWN, 2=IDLE_UP, 3=CALL_INCOMING, 4=CALL_ACTIVE
        return state.current_state >= 3; // CALL_INCOMING or CALL_ACTIVE
    } catch (...) {
        // If we can't determine state, assume we're not in a call to be safe
        return false;
    }
}

bool WebServer::isRinging() const {
    try {
        DaemonStateInfo state = getDaemonStateInfo();
        // State 3 = CALL_INCOMING (ringing)
        return state.current_state == 3;
    } catch (...) {
        return false;
    }
}

bool WebServer::isHighPriorityState() const {
    try {
        DaemonStateInfo state = getDaemonStateInfo();
        // High priority states: ringing (3) or active call (4)
        return state.current_state >= 3;
    } catch (...) {
        return false;
    }
}

bool WebServer::isAudioActive() const {
    try {
        DaemonStateInfo state = getDaemonStateInfo();
        // Audio is active during ringing (3) or active call (4)
        // Also consider IDLE_UP (2) as potentially audio-active since handset is up
        return state.current_state >= 2;
    } catch (...) {
        return false;
    }
}

bool WebServer::checkRateLimit(const std::string& client_ip, const std::string& endpoint) {
    std::lock_guard<std::mutex> lock(rate_limit_mutex_);
    
    auto now = std::chrono::steady_clock::now();
    std::string key = client_ip + ":" + endpoint;
    
    // Clean up old entries (older than 60 seconds)
    auto it = rate_limit_map_.begin();
    while (it != rate_limit_map_.end()) {
        if (now - it->second.last_request > std::chrono::seconds(60)) {
            it = rate_limit_map_.erase(it);
        } else {
            ++it;
        }
    }
    
    // Check rate limit
    auto& info = rate_limit_map_[key];
    
    // Update counters
    info.request_count++;
    info.last_request = now;
    
    return true; // Allowed
}

WebServer::HttpResponse WebServer::createRateLimitResponse() {
    WebServer::HttpResponse response;
    response.status_code = 429; // Too Many Requests
    response.headers["Retry-After"] = "10";
    response.headers["Content-Type"] = "application/json";
    
    response.body = R"({
        "error": "Rate limit exceeded",
        "message": "Too many requests. Please slow down, especially during calls.",
        "retry_after": 10
    })";
    
    return response;
}
