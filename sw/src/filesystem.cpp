/*
 * File System Management Implementation for WSPR-ease
 */

#include "filesystem.hpp"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/drivers/flash.h>

#include "logmanager.hpp"

LOG_MODULE_REGISTER(filesystem, LOG_LEVEL_INF);

namespace wspr {

// Register subsystem with LogManager
static Logger& logger = LogManager::instance().registerSubsystem("fs", 
    {"mount", "ops"});

// LittleFS partition definition from devicetree
#define LFS_PARTITION_ID FIXED_PARTITION_ID(lfs_partition)

// LittleFS storage configuration with custom parameters to ensure stability
// Args: name, alignment, read_sz, prog_sz, cache_sz, lookahead_sz
FS_LITTLEFS_DECLARE_CUSTOM_CONFIG(lfsStorage, 4096, 16, 16, 64, 32);

FileSystem& FileSystem::instance() {
  static FileSystem inst;
  return inst;
}

FileSystem::FileSystem() {
  // Initialize mount configuration
  lfsMount.type = FS_LITTLEFS;
  lfsMount.mnt_point = "/lfs";
  lfsMount.fs_data = &lfsStorage;
  lfsMount.storage_dev = (void *)LFS_PARTITION_ID;
  lfsMount.flags = 0;
}

int FileSystem::mount() {
  if (mounted) {
    return 0; // Already mounted
  }

  logger.inf("mount", "Mounting LittleFS at %s...", lfsMount.mnt_point);
  
  int ret = fs_mount(&lfsMount);
  if (ret < 0) {
    logger.err("mount", "LittleFS mount failed: %d", ret);
    mounted = false;
    return ret;
  }

  logger.inf("mount", "LittleFS mounted successfully");
  mounted = true;

  // List root directory for debugging
  struct fs_dir_t dir;
  fs_dir_t_init(&dir);
  
  ret = fs_opendir(&dir, lfsMount.mnt_point);
  if (ret == 0) {
    logger.inf("ops", "Files in %s:", lfsMount.mnt_point);
    struct fs_dirent entry;
    while (fs_readdir(&dir, &entry) == 0 && entry.name[0] != '\0') {
      if (entry.type == FS_DIR_ENTRY_FILE) {
        logger.inf("ops", "  [F] %s (%zu bytes)", entry.name, (size_t)entry.size);
      } else {
        logger.inf("ops", "  [D] %s", entry.name);
      }
    }
    fs_closedir(&dir);
  }

  return 0;
}

} // namespace wspr
