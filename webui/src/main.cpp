#include "mock_fs.hpp"
#include "config_manager.hpp"
#include "api_server.hpp"
#include <iostream>
#include <csignal>

// We'll use cpp-httplib for the HTTP server
// Header-only library: https://github.com/yhirose/cpp-httplib
#include "httplib.h"

static httplib::Server* g_server = nullptr;

void signalHandler(int) {
  std::cout << "\nShutting down server..." << std::endl;
  if (g_server) {
    g_server->stop();
  }
}

int main() {
  std::cout << "WSPR-ease Web UI Mock Server" << std::endl;
  std::cout << "==============================" << std::endl;

  // Setup signal handler
  std::signal(SIGINT, signalHandler);
  std::signal(SIGTERM, signalHandler);

  // Create mock filesystem
  MockFilesystem mockFs("./webui_data");
  std::cout << "Mock filesystem root: ./webui_data" << std::endl;

  // Create config manager
  ConfigManager configMgr(&mockFs);
  if (configMgr.load()) {
    std::cout << "Loaded configuration from filesystem" << std::endl;
  } else {
    std::cout << "Using default configuration" << std::endl;
    configMgr.save();  // Save defaults
  }

  // Create API server
  ApiServer apiServer(&mockFs, &configMgr);
  apiServer.registerRoutes();

  // Create HTTP server
  httplib::Server svr;
  g_server = &svr;

  // Serve static files from /www directory
  svr.set_mount_point("/", "./webui_data/www");

  // API endpoints - File operations
  svr.Get("/api/files", [&](const httplib::Request& req, httplib::Response& res) {
    HttpRequest apiReq;
    apiReq.method = "GET";
    apiReq.path = "/api/files";

    // Pass query parameters
    if (req.has_param("path")) {
      apiReq.params["path"] = req.get_param_value("path");
    } else {
      apiReq.params["path"] = "/";  // Default to root
    }

    HttpResponse apiRes;
    apiServer.handleRequest(apiReq, apiRes);

    std::string contentType = "application/json";
    if (apiRes.headers.count("Content-Type")) {
      contentType = apiRes.headers["Content-Type"];
    }
    res.set_content(apiRes.body, contentType.c_str());
    res.status = apiRes.status;
  });

  svr.Get(R"(/api/files/(.*))", [&](const httplib::Request& req, httplib::Response& res) {
    HttpRequest apiReq;
    apiReq.method = "GET";
    apiReq.path = req.path;

    HttpResponse apiRes;
    apiServer.handleRequest(apiReq, apiRes);

    res.set_content(apiRes.body, apiRes.headers["Content-Type"].c_str());
    res.status = apiRes.status;

    for (const auto& [key, value] : apiRes.headers) {
      res.set_header(key.c_str(), value.c_str());
    }
  });

  svr.Put(R"(/api/files/(.*))", [&](const httplib::Request& req, httplib::Response& res) {
    HttpRequest apiReq;
    apiReq.method = "PUT";
    apiReq.path = req.path;
    apiReq.body = req.body;

    HttpResponse apiRes;
    apiServer.handleRequest(apiReq, apiRes);
    res.set_content(apiRes.body, "application/json");
    res.status = apiRes.status;
  });

  svr.Delete(R"(/api/files/(.*))", [&](const httplib::Request& req, httplib::Response& res) {
    HttpRequest apiReq;
    apiReq.method = "DELETE";
    apiReq.path = req.path;

    HttpResponse apiRes;
    apiServer.handleRequest(apiReq, apiRes);
    res.set_content(apiRes.body, "application/json");
    res.status = apiRes.status;
  });

  // Configuration endpoints
  svr.Get("/api/config", [&](const httplib::Request& req, httplib::Response& res) {
    HttpRequest apiReq;
    apiReq.method = "GET";
    apiReq.path = "/api/config";

    HttpResponse apiRes;
    apiServer.handleRequest(apiReq, apiRes);
    res.set_content(apiRes.body, "application/json");
  });

  svr.Put("/api/config", [&](const httplib::Request& req, httplib::Response& res) {
    HttpRequest apiReq;
    apiReq.method = "PUT";
    apiReq.path = "/api/config";
    apiReq.body = req.body;

    HttpResponse apiRes;
    apiServer.handleRequest(apiReq, apiRes);
    res.set_content(apiRes.body, "application/json");
    res.status = apiRes.status;
  });

  svr.Get("/api/config/export", [&](const httplib::Request& req, httplib::Response& res) {
    HttpRequest apiReq;
    apiReq.method = "GET";
    apiReq.path = "/api/config/export";

    HttpResponse apiRes;
    apiServer.handleRequest(apiReq, apiRes);
    res.set_content(apiRes.body, "application/json");

    for (const auto& [key, value] : apiRes.headers) {
      res.set_header(key.c_str(), value.c_str());
    }
  });

  svr.Post("/api/config/reset", [&](const httplib::Request& req, httplib::Response& res) {
    HttpRequest apiReq;
    apiReq.method = "POST";
    apiReq.path = "/api/config/reset";

    HttpResponse apiRes;
    apiServer.handleRequest(apiReq, apiRes);
    res.set_content(apiRes.body, "application/json");
  });

  // Status endpoint
  svr.Get("/api/status", [&](const httplib::Request& req, httplib::Response& res) {
    HttpRequest apiReq;
    apiReq.method = "GET";
    apiReq.path = "/api/status";

    HttpResponse apiRes;
    apiServer.handleRequest(apiReq, apiRes);
    res.set_content(apiRes.body, "application/json");
  });

  // Transmission control
  svr.Post("/api/tx/trigger", [&](const httplib::Request& req, httplib::Response& res) {
    HttpRequest apiReq;
    apiReq.method = "POST";
    apiReq.path = "/api/tx/trigger";

    HttpResponse apiRes;
    apiServer.handleRequest(apiReq, apiRes);
    res.set_content(apiRes.body, "application/json");
  });

  // Start server
  const char* host = "0.0.0.0";
  int port = 8080;

  std::cout << "\nServer starting on http://" << host << ":" << port << std::endl;
  std::cout << "Static files: ./webui_data/www" << std::endl;
  std::cout << "Press Ctrl+C to stop\n" << std::endl;

  // Run server in non-blocking mode
  svr.listen(host, port);

  return 0;
}
