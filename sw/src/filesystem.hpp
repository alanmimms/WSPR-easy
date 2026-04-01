/*
 * File System Management for WSPR-ease
 * Handles LittleFS mounting and management
 */

#pragma once

#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>

namespace wspr {

  class FileSystem {
  public:
    static FileSystem& instance();

    // Mount the LittleFS partition
    int mount();

    // Check if filesystem is mounted
    bool isMounted() const { return mounted; }

    // Get mount point
    const char* getMountPoint() const { return "/lfs"; }

  private:
    FileSystem();
    ~FileSystem() = default;

    // Prevent copying
    FileSystem(const FileSystem&) = delete;
    FileSystem& operator=(const FileSystem&) = delete;

    bool mounted = false;
    struct fs_mount_t lfsMount;
  };

} // namespace wspr
