/*
 * Shell Commands for WSPR-ease
 * Provides CLI access to system status and FPGA control
 */

#include <zephyr/shell/shell.h>
#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include "wifi_manager.hpp"
#include "gnss.hpp"
#include "fpga.hpp"

namespace wspr {

static int cmd_wspr_status(const struct shell *sh, size_t argc, char **argv) {
    auto& wifi = WifiManager::instance();
    auto& gnss = Gnss::instance();
    auto& fpga = Fpga::instance();

    shell_print(sh, "=== WSPR-ease Status ===");
    shell_print(sh, "WiFi: %s (SSID: %s, RSSI: %d)", 
                wifi.is_connected() ? "Connected" : "Disconnected",
                wifi.ssid(), wifi.rssi());
    shell_print(sh, "IP:   %s", wifi.ip_address());
    
    shell_print(sh, "--- GNSS ---");
    shell_print(sh, "Fix:  %s (Sats: %d, HDOP: %.2f)", 
                gnss.has_fix() ? "YES" : "NO",
                gnss.satellites(), (double)gnss.hdop());
    shell_print(sh, "Pos:  Lat %.6f, Lon %.6f, Alt %.1f m",
                gnss.latitude(), gnss.longitude(), gnss.altitude());
    shell_print(sh, "Time: %s (Grid: %s)", gnss.time_string(), gnss.grid_locator());

    shell_print(sh, "--- FPGA ---");
    shell_print(sh, "Init: %s (Mode: %s)", 
                fpga.is_initialized() ? "YES" : "NO",
                fpga.is_transmitting() ? "TX" : "IDLE");
    shell_print(sh, "Freq: %u Hz", fpga.frequency());
    shell_print(sh, "PPS:  %u (40MHz clock count)", fpga.get_counter());

    return 0;
}

static int cmd_fpga_reset(const struct shell *sh, size_t argc, char **argv) {
    shell_print(sh, "Resetting FPGA...");
    Fpga::instance().reset();
    return 0;
}

static int cmd_fpga_flash(const struct shell *sh, size_t argc, char **argv) {
    const char* path = "/lfs/fpga.img";
    if (argc > 1) {
        path = argv[1];
    }

    shell_print(sh, "Loading FPGA bitstream from %s...", path);
    int ret = Fpga::instance().load_bitstream(path);
    if (ret == 0) {
        shell_print(sh, "FPGA flashed successfully");
    } else {
        shell_error(sh, "FPGA flash failed: %d", ret);
    }
    return ret;
}

static int cmd_fpga_counter(const struct shell *sh, size_t argc, char **argv) {
    uint32_t count = Fpga::instance().get_counter();
    shell_print(sh, "FPGA 1PPS Counter: %u (expected ~40000000)", count);
    if (count > 0) {
        double error = (double)count - 40000000.0;
        double ppm = (error / 40.0);
        shell_print(sh, "Clock Error: %.2f Hz (%.3f ppm)", error, ppm);
    }
    return 0;
}

// File system helpers
static int cmd_fs_ls(const struct shell *sh, size_t argc, char **argv) {
    const char* path = "/lfs";
    if (argc > 1) path = argv[1];

    struct fs_dir_t dir;
    fs_dir_t_init(&dir);
    
    int ret = fs_opendir(&dir, path);
    if (ret < 0) {
        shell_error(sh, "Failed to open directory %s: %d", path, ret);
        return ret;
    }

    shell_print(sh, "Directory %s:", path);
    struct fs_dirent entry;
    while (fs_readdir(&dir, &entry) == 0 && entry.name[0]) {
        if (entry.type == FS_DIR_ENTRY_DIR) {
            shell_print(sh, "  [DIR] %s", entry.name);
        } else {
            shell_print(sh, "  %-20s %u bytes", entry.name, (uint32_t)entry.size);
        }
    }
    fs_closedir(&dir);
    return 0;
}

static int cmd_fs_rm(const struct shell *sh, size_t argc, char **argv) {
    if (argc < 2) {
        shell_error(sh, "Usage: rm <filename>");
        return -EINVAL;
    }
    char path[256];
    snprintf(path, sizeof(path), "/lfs/%s", argv[1]);
    int ret = fs_unlink(path);
    if (ret == 0) {
        shell_print(sh, "Deleted %s", path);
    } else {
        shell_error(sh, "Failed to delete %s: %d", path, ret);
    }
    return ret;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_fpga,
    SHELL_CMD(reset, NULL, "Reset iCE40 FPGA", cmd_fpga_reset),
    SHELL_CMD(flash, NULL, "Load bitstream from LFS [path]", cmd_fpga_flash),
    SHELL_CMD(counter, NULL, "Read 1PPS reference counter", cmd_fpga_counter),
    SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_fs,
    SHELL_CMD(ls, NULL, "List files [path]", cmd_fs_ls),
    SHELL_CMD(rm, NULL, "Remove file from /lfs", cmd_fs_rm),
    SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_wspr,
    SHELL_CMD(status, NULL, "Show system status", cmd_wspr_status),
    SHELL_CMD(fpga, &sub_fpga, "FPGA control commands", NULL),
    SHELL_CMD(fs, &sub_fs, "FileSystem commands", NULL),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(wspr, &sub_wspr, "WSPR-ease commands", NULL);

} // namespace wspr
