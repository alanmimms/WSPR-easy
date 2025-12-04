#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace HAL {

/**
 * @brief File information structure
 */
struct FileInfo {
  std::string name;
  size_t size;
  bool isDirectory;
  uint64_t modifiedTime;  // Unix timestamp
};

/**
 * @brief Interface for filesystem operations
 *
 * Abstracts filesystem access for both mock (Linux) and real (ESP32) environments.
 */
class IFilesystem {
public:
  virtual ~IFilesystem() = default;

  /**
   * @brief List files in a directory
   *
   * @param path Directory path
   * @param files Output vector of file info
   * @return true on success
   */
  virtual bool listDirectory(const std::string& path, std::vector<FileInfo>& files) = 0;

  /**
   * @brief Read entire file into string
   *
   * @param path File path
   * @param content Output content
   * @return true on success
   */
  virtual bool readFile(const std::string& path, std::string& content) = 0;

  /**
   * @brief Write string to file
   *
   * @param path File path
   * @param content Content to write
   * @return true on success
   */
  virtual bool writeFile(const std::string& path, const std::string& content) = 0;

  /**
   * @brief Delete a file
   *
   * @param path File path
   * @return true on success
   */
  virtual bool deleteFile(const std::string& path) = 0;

  /**
   * @brief Check if file exists
   *
   * @param path File path
   * @return true if exists
   */
  virtual bool exists(const std::string& path) = 0;

  /**
   * @brief Get file info
   *
   * @param path File path
   * @param info Output file info
   * @return true on success
   */
  virtual bool getFileInfo(const std::string& path, FileInfo& info) = 0;

  /**
   * @brief Create directory (recursive)
   *
   * @param path Directory path
   * @return true on success
   */
  virtual bool createDirectory(const std::string& path) = 0;
};

} // namespace HAL
