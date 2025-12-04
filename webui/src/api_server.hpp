#pragma once

#include "fs_hal.hpp"
#include "config_manager.hpp"
#include <string>
#include <functional>
#include <map>
#include <memory>

/**
 * @brief HTTP Request structure
 */
struct HttpRequest {
  std::string method;  // GET, POST, PUT, DELETE
  std::string path;
  std::map<std::string, std::string> headers;
  std::map<std::string, std::string> params;
  std::string body;
};

/**
 * @brief HTTP Response structure
 */
struct HttpResponse {
  int status = 200;
  std::map<std::string, std::string> headers;
  std::string body;

  void setJson(const std::string& json) {
    headers["Content-Type"] = "application/json";
    body = json;
  }

  void setHtml(const std::string& html) {
    headers["Content-Type"] = "text/html";
    body = html;
  }

  void setError(int code, const std::string& message) {
    status = code;
    setJson("{\"error\": \"" + message + "\"}");
  }
};

/**
 * @brief REST API Server
 *
 * Implements all REST endpoints for WSPR-ease web UI.
 * Backend-agnostic - can use httplib, crow, or ESP32 web server.
 */
class ApiServer {
public:
  using Handler = std::function<void(const HttpRequest&, HttpResponse&)>;

  ApiServer(HAL::IFilesystem* fs, ConfigManager* configMgr)
    : filesystem(fs), config(configMgr) {}

  /**
   * @brief Register all API routes
   */
  void registerRoutes() {
    // File operations
    routes["GET:/api/files"] = [this](const HttpRequest& req, HttpResponse& res) {
      handleListFiles(req, res);
    };

    routes["GET:/api/files/*"] = [this](const HttpRequest& req, HttpResponse& res) {
      handleDownloadFile(req, res);
    };

    routes["PUT:/api/files/*"] = [this](const HttpRequest& req, HttpResponse& res) {
      handleUploadFile(req, res);
    };

    routes["DELETE:/api/files/*"] = [this](const HttpRequest& req, HttpResponse& res) {
      handleDeleteFile(req, res);
    };

    // Configuration
    routes["GET:/api/config"] = [this](const HttpRequest& req, HttpResponse& res) {
      handleGetConfig(req, res);
    };

    routes["PUT:/api/config"] = [this](const HttpRequest& req, HttpResponse& res) {
      handlePutConfig(req, res);
    };

    routes["GET:/api/config/export"] = [this](const HttpRequest& req, HttpResponse& res) {
      handleExportConfig(req, res);
    };

    routes["POST:/api/config/import"] = [this](const HttpRequest& req, HttpResponse& res) {
      handleImportConfig(req, res);
    };

    routes["POST:/api/config/reset"] = [this](const HttpRequest& req, HttpResponse& res) {
      config->reset();
      config->save();
      res.setJson("{\"success\": true}");
    };

    // Status (runtime, not persisted)
    routes["GET:/api/status"] = [this](const HttpRequest& req, HttpResponse& res) {
      handleGetStatus(req, res);
    };

    // Transmission control
    routes["POST:/api/tx/trigger"] = [this](const HttpRequest& req, HttpResponse& res) {
      handleTriggerTx(req, res);
    };
  }

  /**
   * @brief Dispatch request to appropriate handler
   */
  void handleRequest(const HttpRequest& req, HttpResponse& res) {
    std::string routeKey = req.method + ":" + req.path;

    // Try exact match first
    auto it = routes.find(routeKey);
    if (it != routes.end()) {
      it->second(req, res);
      return;
    }

    // Try wildcard match
    for (const auto& [pattern, handler] : routes) {
      if (matchRoute(pattern, routeKey)) {
        handler(req, res);
        return;
      }
    }

    res.setError(404, "Not found");
  }

private:
  HAL::IFilesystem* filesystem;
  ConfigManager* config;
  std::map<std::string, Handler> routes;

  bool matchRoute(const std::string& pattern, const std::string& path) {
    if (pattern.find('*') == std::string::npos) {
      return pattern == path;
    }
    // Simple wildcard matching
    size_t starPos = pattern.find('*');
    return path.substr(0, starPos) == pattern.substr(0, starPos);
  }

  void handleListFiles(const HttpRequest& req, HttpResponse& res) {
    std::string path = req.params.count("path") ? req.params.at("path") : "/";
    std::vector<HAL::FileInfo> files;

    if (!filesystem->listDirectory(path, files)) {
      res.setError(404, "Directory not found");
      return;
    }

    std::ostringstream json;
    json << "{\"path\": \"" << path << "\", \"files\": [\n";
    for (size_t i = 0; i < files.size(); i++) {
      json << "  {\n";
      json << "    \"name\": \"" << files[i].name << "\",\n";
      json << "    \"size\": " << files[i].size << ",\n";
      json << "    \"isDirectory\": " << (files[i].isDirectory ? "true" : "false") << ",\n";
      json << "    \"modified\": " << files[i].modifiedTime << "\n";
      json << "  }" << (i < files.size() - 1 ? "," : "") << "\n";
    }
    json << "]}";
    res.setJson(json.str());
  }

  void handleDownloadFile(const HttpRequest& req, HttpResponse& res) {
    std::string path = extractFilePath(req.path);
    std::string content;

    if (!filesystem->readFile(path, content)) {
      res.setError(404, "File not found");
      return;
    }

    res.body = content;
    res.headers["Content-Type"] = getMimeType(path);
  }

  void handleUploadFile(const HttpRequest& req, HttpResponse& res) {
    std::string path = extractFilePath(req.path);

    if (!filesystem->writeFile(path, req.body)) {
      res.setError(500, "Failed to write file");
      return;
    }

    res.setJson("{\"success\": true}");
  }

  void handleDeleteFile(const HttpRequest& req, HttpResponse& res) {
    std::string path = extractFilePath(req.path);

    if (!filesystem->deleteFile(path)) {
      res.setError(500, "Failed to delete file");
      return;
    }

    res.setJson("{\"success\": true}");
  }

  void handleGetConfig(const HttpRequest& req, HttpResponse& res) {
    res.setJson(config->toJson());
  }

  void handlePutConfig(const HttpRequest& req, HttpResponse& res) {
    if (!config->fromJson(req.body)) {
      res.setError(400, "Invalid configuration JSON");
      return;
    }

    if (!config->save()) {
      res.setError(500, "Failed to save configuration");
      return;
    }

    res.setJson("{\"success\": true}");
  }

  void handleExportConfig(const HttpRequest& req, HttpResponse& res) {
    res.body = config->toJson();
    res.headers["Content-Type"] = "application/json";
    res.headers["Content-Disposition"] = "attachment; filename=\"wspr-config.json\"";
  }

  void handleImportConfig(const HttpRequest& req, HttpResponse& res) {
    handlePutConfig(req, res);
  }

  void handleGetStatus(const HttpRequest& req, HttpResponse& res) {
    // Mock status for now
    std::ostringstream json;
    json << "{\n";
    json << "  \"uptime\": 12345,\n";
    json << "  \"gnss\": {\n";
    json << "    \"locked\": false,\n";
    json << "    \"satellites\": 0\n";
    json << "  },\n";
    json << "  \"clock\": {\n";
    json << "    \"source\": \"tcxo\",\n";
    json << "    \"accuracyPpb\": 500\n";
    json << "  },\n";
    json << "  \"pa\": {\n";
    json << "    \"tempC\": 25,\n";
    json << "    \"voltageV\": 5.0\n";
    json << "  },\n";
    json << "  \"tx\": {\n";
    json << "    \"active\": false,\n";
    json << "    \"band\": \"\",\n";
    json << "    \"nextTxSec\": 120\n";
    json << "  }\n";
    json << "}";
    res.setJson(json.str());
  }

  void handleTriggerTx(const HttpRequest& req, HttpResponse& res) {
    // Mock trigger - just return success
    res.setJson("{\"success\": true, \"message\": \"Transmission triggered (mock)\"}");
  }

  std::string extractFilePath(const std::string& requestPath) {
    // Extract file path from /api/files/path/to/file.txt
    const std::string prefix = "/api/files/";
    if (requestPath.find(prefix) == 0) {
      return requestPath.substr(prefix.length() - 1);  // Keep leading /
    }
    return "/";
  }

  std::string getMimeType(const std::string& path) {
    if (path.ends_with(".html")) return "text/html";
    if (path.ends_with(".css")) return "text/css";
    if (path.ends_with(".js")) return "application/javascript";
    if (path.ends_with(".json")) return "application/json";
    if (path.ends_with(".png")) return "image/png";
    if (path.ends_with(".jpg") || path.ends_with(".jpeg")) return "image/jpeg";
    if (path.ends_with(".svg")) return "image/svg+xml";
    return "application/octet-stream";
  }
};
