#pragma once

#include "fs_hal.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <optional>
#include <iostream>

namespace fs = std::filesystem;

/**
 * @brief Mock filesystem implementation for Linux testing
 *
 * Maps virtual paths to a real directory on the Linux host.
 * Provides safety checks to prevent access outside the root.
 */
class MockFilesystem : public HAL::IFilesystem {
public:
  /**
   * @brief Construct mock filesystem with root directory
   *
   * @param rootPath Root directory for filesystem (e.g., "./webui_data")
   */
  MockFilesystem(const std::string& rootPath) : root(fs::weakly_canonical(fs::absolute(rootPath))) {
    // Create root if it doesn't exist
    if (!fs::exists(root)) {
      fs::create_directories(root);
    }
  }

  bool listDirectory(const std::string& path, std::vector<HAL::FileInfo>& files) override {
    auto realPath = getRealPath(path);

    // Debug logging
    std::cerr << "[MockFS] listDirectory('" << path << "')" << std::endl;
    if (!realPath) {
      std::cerr << "[MockFS]   getRealPath returned nullopt" << std::endl;
      return false;
    }
    std::cerr << "[MockFS]   realPath: " << *realPath << std::endl;
    std::cerr << "[MockFS]   exists: " << fs::exists(*realPath) << std::endl;
    std::cerr << "[MockFS]   is_directory: " << fs::is_directory(*realPath) << std::endl;

    if (!fs::is_directory(*realPath)) {
      return false;
    }

    files.clear();
    try {
      for (const auto& entry : fs::directory_iterator(*realPath)) {
        HAL::FileInfo info;
        info.name = entry.path().filename().string();
        info.isDirectory = entry.is_directory();
        info.size = entry.is_directory() ? 0 : fs::file_size(entry.path());

        auto ftime = fs::last_write_time(entry.path());
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
          ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
        );
        info.modifiedTime = std::chrono::system_clock::to_time_t(sctp);

        files.push_back(info);
      }
      return true;
    } catch (const fs::filesystem_error& e) {
      return false;
    }
  }

  bool readFile(const std::string& path, std::string& content) override {
    auto realPath = getRealPath(path);
    if (!realPath || !fs::is_regular_file(*realPath)) {
      return false;
    }

    std::ifstream file(*realPath, std::ios::binary);
    if (!file) return false;

    std::ostringstream ss;
    ss << file.rdbuf();
    content = ss.str();
    return true;
  }

  bool writeFile(const std::string& path, const std::string& content) override {
    auto realPath = getRealPath(path);
    if (!realPath) return false;

    // Create parent directories if needed
    auto parent = realPath->parent_path();
    if (!fs::exists(parent)) {
      fs::create_directories(parent);
    }

    std::ofstream file(*realPath, std::ios::binary);
    if (!file) return false;

    file << content;
    return file.good();
  }

  bool deleteFile(const std::string& path) override {
    auto realPath = getRealPath(path);
    if (!realPath || !fs::exists(*realPath)) {
      return false;
    }

    try {
      return fs::remove(*realPath);
    } catch (const fs::filesystem_error& e) {
      return false;
    }
  }

  bool exists(const std::string& path) override {
    auto realPath = getRealPath(path);
    return realPath && fs::exists(*realPath);
  }

  bool getFileInfo(const std::string& path, HAL::FileInfo& info) override {
    auto realPath = getRealPath(path);
    if (!realPath || !fs::exists(*realPath)) {
      return false;
    }

    try {
      info.name = realPath->filename().string();
      info.isDirectory = fs::is_directory(*realPath);
      info.size = info.isDirectory ? 0 : fs::file_size(*realPath);

      auto ftime = fs::last_write_time(*realPath);
      auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
      );
      info.modifiedTime = std::chrono::system_clock::to_time_t(sctp);

      return true;
    } catch (const fs::filesystem_error& e) {
      return false;
    }
  }

  bool createDirectory(const std::string& path) override {
    auto realPath = getRealPath(path);
    if (!realPath) return false;

    try {
      return fs::create_directories(*realPath);
    } catch (const fs::filesystem_error& e) {
      return false;
    }
  }

private:
  fs::path root;

  /**
   * @brief Convert virtual path to real filesystem path
   *
   * Validates path to prevent directory traversal attacks.
   * Returns nullopt if path is invalid or escapes root.
   */
  std::optional<fs::path> getRealPath(const std::string& virtualPath) {
    try {
      // Clean up the path and resolve any .. or .
      fs::path vpath = virtualPath;

      // Remove leading slash if present
      std::string pathStr = vpath.string();
      if (!pathStr.empty() && pathStr[0] == '/') {
        pathStr = pathStr.substr(1);
      }

      fs::path realPath = root / pathStr;
      realPath = fs::weakly_canonical(realPath);

      // Security check: ensure path is within root
      auto rootStr = root.string();
      auto realStr = realPath.string();

      std::cerr << "[MockFS::getRealPath] vpath='" << virtualPath
                << "' root='" << rootStr << "' real='" << realStr << "'" << std::endl;

      if (realStr.find(rootStr) != 0) {
        std::cerr << "[MockFS::getRealPath] SECURITY: Path escapes root!" << std::endl;
        return std::nullopt;  // Path escapes root
      }

      return realPath;
    } catch (const fs::filesystem_error& e) {
      std::cerr << "[MockFS::getRealPath] EXCEPTION: " << e.what() << std::endl;
      return std::nullopt;
    }
  }
};
