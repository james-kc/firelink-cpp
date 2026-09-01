#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <string>
#include <functional>
#include <atomic>
#include <thread>

#include "config.h"

// Minimal dependency-free HTTP server for the pre-flight configuration page.
//
// Routes:
//   GET  /                 embedded control page
//   GET  /api/status       live status JSON (state, sensors, GPS, disk)
//   GET  /api/config       full config as JSON
//   POST /api/config       urlencoded key=value pairs; persists to disk
//   POST /api/arm          arm the flight computer (force=true overrides checks)
//   POST /api/disarm       return to pre-flight / post-flight idle
//   POST /api/recalibrate  re-measure pad pressure
//   POST /api/restart      restart the process (systemd relaunches it)
//   GET  /api/data         list recorded session files
//   GET  /data/...         download a recorded file
//
// The flight-specific behaviour is injected via Handlers so this class stays
// a pure transport/HTTP layer.
class WebServer {
public:
    struct Handlers {
        std::function<std::string()> getStatusJson;
        std::function<std::string(bool force)> arm;        // returns result JSON
        std::function<std::string()> disarm;               // returns result JSON
        std::function<std::string()> recalibrate;          // returns result JSON
        std::function<std::string(const std::string &relativePath)> readDataFile; // empty = not found
        std::function<std::string()> listDataJson;
    };

    WebServer(int port, Config &config, Handlers handlers,
              const std::string &data_dir);
    ~WebServer();

    bool start();
    void stop();

private:
    int port_;
    Config &config_;
    Handlers handlers_;
    std::string data_dir_;

    std::atomic<bool> running_{false};
    int listen_fd_ = -1;
    std::thread accept_thread_;

    void acceptLoop();
    void handleConnection(int client_fd);
    void sendResponse(int fd, int code, const std::string &content_type,
                      const std::string &body);
};

#endif // WEB_SERVER_H
