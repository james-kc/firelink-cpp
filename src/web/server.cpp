#include "web/server.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <cctype>

// The control page lives in page.cpp as a raw string literal.
extern const char *kFirelinkPage;
// The post-flight data viewer (self-contained canvas charts) lives in
// flight_page.cpp, served at /flight?session=<name>.
extern const char *kFirelinkFlightPage;

namespace {

std::string urlDecode(const std::string &s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '%' && i + 2 < s.size() && std::isxdigit(s[i + 1]) && std::isxdigit(s[i + 2])) {
            auto hex = [](char h) -> int {
                if (h >= '0' && h <= '9') return h - '0';
                if (h >= 'a' && h <= 'f') return h - 'a' + 10;
                return h - 'A' + 10;
            };
            out += (char)(hex(s[i + 1]) * 16 + hex(s[i + 2]));
            i += 2;
        } else if (c == '+') {
            out += ' ';
        } else {
            out += c;
        }
    }
    return out;
}

std::string jsonEscapeLocal(const std::string &s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;
        }
    }
    return out;
}

} // namespace

WebServer::WebServer(int port, Config &config, Handlers handlers,
                     const std::string &data_dir)
    : port_(port), config_(config), handlers_(std::move(handlers)),
      data_dir_(data_dir) {}

WebServer::~WebServer() { stop(); }

bool WebServer::start() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        perror("Web: socket failed");
        return false;
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(listen_fd_, (sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Web: bind failed");
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    if (listen(listen_fd_, 8) < 0) {
        perror("Web: listen failed");
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    running_ = true;
    accept_thread_ = std::thread(&WebServer::acceptLoop, this);
    std::cout << "Web server listening on port " << port_ << std::endl;
    return true;
}

void WebServer::stop() {
    running_ = false;
    if (listen_fd_ >= 0) {
        shutdown(listen_fd_, SHUT_RDWR);
        close(listen_fd_);
        listen_fd_ = -1;
    }
    if (accept_thread_.joinable()) accept_thread_.join();
}

void WebServer::acceptLoop() {
    while (running_) {
        int client = accept(listen_fd_, nullptr, nullptr);
        if (client < 0) {
            if (running_) perror("Web: accept failed");
            break;
        }
        std::thread(&WebServer::handleConnection, this, client).detach();
    }
}

void WebServer::sendResponse(int fd, int code, const std::string &content_type,
                             const std::string &body) {
    const char *reason =
        code == 200 ? "OK" :
        code == 400 ? "Bad Request" :
        code == 404 ? "Not Found" :
        code == 405 ? "Method Not Allowed" : "Internal Server Error";

    std::ostringstream head;
    head << "HTTP/1.1 " << code << " " << reason << "\r\n"
         << "Content-Type: " << content_type << "\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "Access-Control-Allow-Origin: *\r\n"
         << "Connection: close\r\n\r\n";

    std::string header = head.str();
    send(fd, header.c_str(), header.size(), MSG_NOSIGNAL);
    if (!body.empty()) send(fd, body.c_str(), body.size(), MSG_NOSIGNAL);
}

void WebServer::handleConnection(int client_fd) {
    // Read the request (headers + optional body), one request per connection.
    std::string req;
    req.reserve(2048);
    char buf[2048];
    size_t content_length = 0;
    size_t header_end = std::string::npos;

    while (req.size() < 65536) {
        ssize_t n = recv(client_fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        req.append(buf, (size_t)n);
        if (header_end == std::string::npos) {
            header_end = req.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                std::string headers = req.substr(0, header_end);
                std::istringstream hs(headers);
                std::string line;
                while (std::getline(hs, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (line.rfind("Content-Length:", 0) == 0) {
                        content_length = (size_t)std::atoi(line.c_str() + 15);
                    }
                }
            }
        }
        if (header_end != std::string::npos &&
            req.size() >= header_end + 4 + content_length) {
            break;
        }
    }

    if (req.empty()) { close(client_fd); return; }

    // Parse request line.
    std::istringstream rl(req.substr(0, req.find("\r\n")));
    std::string method, target, version;
    rl >> method >> target >> version;

    std::string body;
    if (header_end != std::string::npos && content_length > 0)
        body = req.substr(header_end + 4, content_length);

    // Split path / query string.
    std::string path = target;
    auto q = path.find('?');
    if (q != std::string::npos) path = path.substr(0, q);
    path = urlDecode(path);

    auto json = [&](const std::string &b, int code = 200) {
        sendResponse(client_fd, code, "application/json", b);
    };

    if (method == "GET" && (path == "/" || path == "/index.html")) {
        sendResponse(client_fd, 200, "text/html", kFirelinkPage);
    } else if (method == "GET" && path == "/flight") {
        sendResponse(client_fd, 200, "text/html", kFirelinkFlightPage);
    } else if (method == "GET" && path == "/api/status") {
        json(handlers_.getStatusJson ? handlers_.getStatusJson() : "{}");
    } else if (method == "GET" && path == "/api/config") {
        json(config_.toJson());
    } else if (method == "POST" && path == "/api/config") {
        // urlencoded key=value&key=value body.
        bool restart_required = false;
        std::istringstream bs(body);
        std::string pair;
        while (std::getline(bs, pair, '&')) {
            auto eq = pair.find('=');
            if (eq == std::string::npos || eq == 0) continue;
            std::string key = urlDecode(pair.substr(0, eq));
            std::string value = urlDecode(pair.substr(eq + 1));
            std::string old = config_.getString(key, "\x01");
            config_.set(key, value);
            if (old != value && Config::isRestartRequiredKey(key))
                restart_required = true;
        }
        config_.save("");
        std::ostringstream resp;
        resp << "{\"ok\":true,\"restart_required\":"
             << (restart_required ? "true" : "false") << "}";
        json(resp.str());
    } else if (method == "GET" && path == "/api/armcode") {
        if (handlers_.getArmCode) json(handlers_.getArmCode());
        else json("{\"ok\":false,\"error\":\"no armcode handler\"}", 500);
    } else if (method == "POST" && path == "/api/arm") {
        // urlencoded body: code=1234[&force=true]
        bool force = false;
        std::string code;
        std::istringstream bs(body);
        std::string pair;
        while (std::getline(bs, pair, '&')) {
            auto eq = pair.find('=');
            if (eq == std::string::npos || eq == 0) continue;
            std::string key = urlDecode(pair.substr(0, eq));
            std::string value = urlDecode(pair.substr(eq + 1));
            if (key == "force" && value == "true") force = true;
            else if (key == "code") code = value;
        }
        if (handlers_.arm) json(handlers_.arm(code, force));
        else json("{\"ok\":false,\"error\":\"no arm handler\"}", 500);
    } else if (method == "POST" && path == "/api/disarm") {
        if (handlers_.disarm) json(handlers_.disarm());
        else json("{\"ok\":false,\"error\":\"no disarm handler\"}", 500);
    } else if (method == "POST" && path == "/api/recalibrate") {
        if (handlers_.recalibrate) json(handlers_.recalibrate());
        else json("{\"ok\":false,\"error\":\"no recalibrate handler\"}", 500);
    } else if (method == "POST" && path == "/api/restart") {
        json("{\"ok\":true}");
        close(client_fd);
        // systemd (Restart=always) relaunches the process, which picks up
        // the persisted config from disk.
        std::thread([] {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            std::exit(0);
        }).detach();
        return;
    } else if (method == "GET" && path == "/api/data") {
        if (handlers_.listDataJson) json(handlers_.listDataJson());
        else json("{\"files\":[]}");
    } else if (method == "GET" && path.rfind("/data/", 0) == 0) {
        std::string rel = path.substr(6);
        // Reject path traversal before handing to the data handler.
        if (rel.find("..") != std::string::npos || rel.empty() ||
            rel[0] == '/') {
            sendResponse(client_fd, 400, "text/plain", "bad path");
        } else if (handlers_.readDataFile) {
            std::string content = handlers_.readDataFile(rel);
            if (content.empty())
                sendResponse(client_fd, 404, "text/plain", "not found");
            else
                sendResponse(client_fd, 200, "text/csv", content);
        } else {
            sendResponse(client_fd, 404, "text/plain", "not found");
        }
    } else {
        std::ostringstream notfound;
        notfound << "{\"ok\":false,\"error\":\"unknown route: "
                 << jsonEscapeLocal(method + " " + path) << "\"}";
        json(notfound.str(), 404);
    }

    close(client_fd);
}
