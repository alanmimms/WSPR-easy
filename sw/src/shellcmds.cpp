/*
 * Shell Commands for WSPR-ease
 * Provides CLI access to system status and FPGA control
 */

#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>
#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <stdlib.h>
#include <string>
#include "wifiManager.hpp"
#include "gnss.hpp"
#include "fpga.hpp"
#include "logmanager.hpp"

namespace wspr {

static struct k_thread sweepThreadData;
static K_THREAD_STACK_DEFINE(sweepThreadStack, 2048);
static bool sweepRunning = false;
static bool sweepContinuous = false;
static uint32_t sweepDurationSec = 10;

static void sweepThreadEntry(void *p1, void *p2, void *p3) {
    auto& fpga = FPGA::instance();
    const struct shell *sh = (const struct shell *)p1;

    uint32_t startFreq = 1000000; // 1 MHz
    uint32_t endFreq = 30000000; // 30 MHz
    uint32_t steps = 1000;
    uint32_t stepDelayMs = (sweepDurationSec * 1000) / steps;
    
    do {
        shell_print(sh, "Starting sweep: %u to %u MHz over %u s", 
                    startFreq/1000000, endFreq/1000000, sweepDurationSec);
        
        for (uint32_t i = 0; i <= steps && sweepRunning; i++) {
            uint32_t freq = startFreq + ((uint64_t)(endFreq - startFreq) * i) / steps;
            fpga.setFrequency(freq);
            k_msleep(stepDelayMs);
        }
    } while (sweepRunning && sweepContinuous);

    sweepRunning = false;
    fpga.stopTX();
    shell_print(sh, "Sweep stopped.");
}

static int cmd_wspr_status(const struct shell *sh, size_t argc, char **argv) {
    auto& wifi = WifiManager::instance();
    auto& gnss = GNSS::instance();
    auto& fpga = FPGA::instance();

    shell_print(sh, "=== WSPR-ease Status ===");
    shell_print(sh, "WiFi: %s (SSID: %s, RSSI: %d)", 
                wifi.isConnected() ? "Connected" : "Disconnected",
                wifi.getSSID(), wifi.getRSSI());
    shell_print(sh, "IP:   %s", wifi.getIPAddress());
    
    shell_print(sh, "--- GNSS ---");
    shell_print(sh, "Fix:  %s (Sats: %d, HDOP: %.2f)", 
                gnss.hasFix() ? "YES" : "NO",
                gnss.satellites(), (double)gnss.getHDOP());
    shell_print(sh, "Pos:  Lat %.6f, Lon %.6f, Alt %.1f m",
                gnss.latitude(), gnss.longitude(), gnss.altitude());
    shell_print(sh, "Time: %s (Grid: %s)", gnss.timeString(), gnss.gridLocator());

    shell_print(sh, "--- FPGA ---");
    shell_print(sh, "Init: %s (Mode: %s)", 
                fpga.isInitialized() ? "YES" : "NO",
                fpga.isTransmitting() ? "TX" : "IDLE");
    shell_print(sh, "Freq: %u Hz", fpga.frequency());
    shell_print(sh, "PPS:  %u", fpga.getCounter());

    return 0;
}

static int cmd_tx_start(const struct shell *sh, size_t argc, char **argv) {
    if (argc < 2) {
        shell_error(sh, "Usage: tx_start <freq_hz> [power_0_255]");
        return -EINVAL;
    }
    uint32_t freq = strtoul(argv[1], NULL, 10);
    uint8_t pwr = 255;
    if (argc >= 3) {
        pwr = (uint8_t)strtoul(argv[2], NULL, 10);
    }

    auto& fpga = FPGA::instance();
    fpga.setFrequency(freq);
    fpga.setPowerLevel(pwr);
    int ret = fpga.startTX();
    if (ret == 0) {
        shell_print(sh, "TX started at %u Hz, Power %u (Ignored by current RTL)", freq, pwr);
    } else {
        shell_error(sh, "Failed to start TX: %d", ret);
    }
    return ret;
}

static int cmd_tx_stop(const struct shell *sh, size_t argc, char **argv) {
    sweepRunning = false;
    FPGA::instance().stopTX();
    shell_print(sh, "TX stopped");
    return 0;
}

static int cmd_tx_sweep(const struct shell *sh, size_t argc, char **argv) {
    if (sweepRunning) {
        shell_error(sh, "Sweep already running. Use tx_stop first.");
        return -EBUSY;
    }

    sweepDurationSec = (argc > 1) ? strtoul(argv[1], NULL, 10) : 10;
    sweepContinuous = (argc > 2 && strcmp(argv[2], "continuous") == 0);
    
    auto& fpga = FPGA::instance();
    fpga.setPowerLevel(255); // Full power for sweep
    fpga.startTX();

    sweepRunning = true;
    k_thread_create(&sweepThreadData, sweepThreadStack,
                    K_THREAD_STACK_SIZEOF(sweepThreadStack),
                    sweepThreadEntry, (void*)sh, NULL, NULL,
                    7, 0, K_NO_WAIT);

    return 0;
}

static int cmd_fpga_reset(const struct shell *sh, size_t argc, char **argv) {
    shell_print(sh, "Resetting FPGA...");
    FPGA::instance().reset();
    return 0;
}

static int cmd_fpga_flash(const struct shell *sh, size_t argc, char **argv) {
    const char* path = "/lfs/fpga.img";
    if (argc > 1) {
        path = argv[1];
    }

    shell_print(sh, "Loading FPGA bitstream from %s...", path);
    int ret = FPGA::instance().loadBitstream(path);
    if (ret == 0) {
        shell_print(sh, "FPGA flashed successfully");
    } else {
        shell_error(sh, "FPGA flash failed: %d", ret);
    }
    return ret;
}

static int cmd_fpga_counter(const struct shell *sh, size_t argc, char **argv) {
    auto& fpga = wspr::FPGA::instance();
    uint32_t r1, r2, r1_check, r2_check;
    
    // Read registers in a loop until stable to avoid hardware update race
    int attempts = 0;
    do {
        fpga.readRegister(0x09, &r1);
        fpga.readRegister(0x0A, &r2);
        fpga.readRegister(0x09, &r1_check);
        fpga.readRegister(0x0A, &r2_check);
        attempts++;
    } while ((r1 != r1_check || r2 != r2_check) && attempts < 100);

    if (attempts >= 100) {
        shell_error(sh, "Failed to get stable PPS readings from FPGA!");
        return -EIO;
    }

    // Handle 32-bit rollover to find the most recent sample
    uint32_t diff1 = r1 - r2;
    uint32_t diff2 = r2 - r1;
    
    uint32_t latest = (diff1 < diff2) ? r1 : r2;
    uint32_t previous = (diff1 < diff2) ? r2 : r1;
    uint32_t delta_f = latest - previous;

    uint32_t status, tuning, pwr, edges, sig;
    fpga.readRegister(0x00, &status);
    fpga.readRegister(0x01, &tuning);
    fpga.readRegister(0x04, &pwr);
    fpga.readRegister(0x07, &edges);
    fpga.readRegister(0x0B, &sig);

    bool tx_active = (status & 0x01);
    bool pll_lock  = (status & 0x02);
    bool pps_val   = (status & 0x04);
    bool hb        = (status & 0x08);
    uint8_t step   = (status >> 4) & 0x07;

    shell_print(sh, "=== FPGA PPS Diagnostics ===");
    shell_print(sh, "FPGA Signature:    0x%04X %s", sig, (sig == 0x600D) ? "(OK)" : "(FAIL)");
    shell_print(sh, "Status:            TX=%s, PLL=%s, PPS=%s, HB=%s, Step=%d",
                tx_active ? "ON" : "OFF", pll_lock ? "LOCKED" : "NO_LOCK",
                pps_val ? "HIGH" : "LOW", hb ? "1" : "0", step);
    shell_print(sh, "Total Edge Count:  %u", edges);
    shell_print(sh, "Sample 1 (Rising): %u", latest);
    shell_print(sh, "Sample 2 (Rising): %u", previous);
    shell_print(sh, "Tuning Word:       0x%08x", tuning);
    shell_print(sh, "Power Thresh:      0x%08x", pwr);
    
    // We are measuring exactly 1 second between two rising edges
    if (delta_f > 0) {
        double freq = (double)delta_f;
        double ppm = (freq - 120000000.0) / 120.0;
        shell_print(sh, "----------------------------");
        shell_print(sh, "Measured Frequency: %.3f Hz", freq);
        shell_print(sh, "Clock Error:        %.3f ppm", ppm);
    } else {
        shell_warn(sh, "WARNING: PPS delta is zero. Clock not running or PPS missing?");
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
    fs_opendir(&dir, path); // Reset to beginning for next readdir if needed or just close
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

static int cmd_gnss_raw(const struct shell *sh, size_t argc, char **argv) {
    char buf[128];
    if (GNSS::instance().getRawNmea(buf, sizeof(buf)) > 0) {
        shell_print(sh, "Most recent GNSS UART sentence:");
        shell_print(sh, "%s", buf);
    } else {
        shell_warn(sh, "No GNSS data received yet.");
    }
    return 0;
}

static int cmd_gnss_reset(const struct shell *sh, size_t argc, char **argv) {
    shell_print(sh, "Resetting GNSS chip via IO15...");
    GNSS::instance().reset();
    return 0;
}

static int cmd_gnss_monitor(const struct shell *sh, size_t argc, char **argv) {
    auto& gnss = GNSS::instance();
    struct k_msgq* q = gnss.getMonitorQueue();
    char buf[256];

    // Flush any pending input (like the newline from the command itself)
    if (sh && sh->iface && sh->iface->api && sh->iface->api->read) {
        uint8_t dummy;
        size_t nread;
        while (sh->iface->api->read(sh->iface, &dummy, 1, &nread) == 0 && nread > 0) {
            // consume existing characters
        }
    }

    shell_print(sh, "Entering GNSS monitor mode. Press Ctrl+C to exit.");
    gnss.setMonitor(true);

    while (true) {
        // Wait for a message with timeout so we can check for user input
        if (k_msgq_get(q, buf, K_MSEC(10)) == 0) {
            shell_print(sh, "%s", buf);
        }

        // Check if user pressed Ctrl+C (0x03)
        if (sh && sh->iface && sh->iface->api && sh->iface->api->read) {
            uint8_t c;
            size_t nread = 0;
            if (sh->iface->api->read(sh->iface, &c, 1, &nread) == 0 && nread > 0) {
                if (c == 0x03) { // Ctrl+C
                    break;
                }
            }
        }
    }

    gnss.setMonitor(false);
    // Flush remaining messages
    while (k_msgq_get(q, buf, K_NO_WAIT) == 0) {}
    
    shell_print(sh, "Exited GNSS monitor mode.");
    return 0;
}

static int cmd_wspr_log(const struct shell *sh, size_t argc, char **argv) {
    auto& lm = LogManager::instance();

    if (argc < 2) {
        lm.listSubsystems(sh);
        return 0;
    }

    std::string arg = argv[1];
    if (arg == "quiet") {
        lm.setAll(false);
        shell_print(sh, "All logging disabled.");
    } else if (arg == "full") {
        lm.setAll(true);
        shell_print(sh, "All logging enabled.");
    } else if (arg[0] == '+') {
        std::string sub = arg.substr(1);
        size_t colon = sub.find(':');
        if (colon != std::string::npos) {
            std::string type = sub.substr(colon + 1);
            sub = sub.substr(0, colon);
            lm.setSubtype(sub, type, true);
            shell_print(sh, "Subsystem %s subtype %s enabled.", sub.c_str(), type.c_str());
        } else {
            lm.setSubsystem(sub, true);
            shell_print(sh, "Subsystem %s enabled.", sub.c_str());
        }
    } else if (arg[0] == '-') {
        std::string sub = arg.substr(1);
        size_t colon = sub.find(':');
        if (colon != std::string::npos) {
            std::string type = sub.substr(colon + 1);
            sub = sub.substr(0, colon);
            lm.setSubtype(sub, type, false);
            shell_print(sh, "Subsystem %s subtype %s disabled.", sub.c_str(), type.c_str());
        } else {
            lm.setSubsystem(sub, false);
            shell_print(sh, "Subsystem %s disabled.", sub.c_str());
        }
    } else {
        lm.listSubtypes(sh, arg);
    }

    return 0;
}

static int cmd_fpga_status(const struct shell *sh, size_t argc, char **argv) {
    uint32_t ctrl = 0, tuning = 0, pwr = 0, inc = 0;
    auto& fpga = FPGA::instance();
    
    fpga.readRegister(0x00, &ctrl);
    fpga.readRegister(0x01, &tuning);
    fpga.readRegister(0x04, &pwr);
    fpga.readRegister(0x08, &inc);

    shell_print(sh, "=== FPGA Hardware Status ===");
    shell_print(sh, "TX Enable:       %s", (ctrl & 0x01) ? "ON" : "OFF");
    shell_print(sh, "PLL Locked:      %s", (ctrl & 0x02) ? "YES" : "NO");
    shell_print(sh, "Heartbeat:       %s", (ctrl & 0x08) ? "1" : "0");
    shell_print(sh, "Step Index:      %u", (ctrl >> 4) & 0x07);
    shell_print(sh, "--- Registers ---");
    shell_print(sh, "Tuning Word:     0x%08X", tuning);
    shell_print(sh, "Phase Inc (Act): 0x%08X", inc);
    shell_print(sh, "Power Thresh:    0x%08X", pwr);
    
    if (!(ctrl & 0x02)) {
        shell_warn(sh, "WARNING: FPGA PLL is not locked.");
    }
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_fpga,
    SHELL_CMD(status, NULL, "Show FPGA hardware register status", cmd_fpga_status),
    SHELL_CMD(reset, NULL, "Reset iCE40 FPGA", cmd_fpga_reset),
    SHELL_CMD(flash, NULL, "Load bitstream from LFS [path]", cmd_fpga_flash),
    SHELL_CMD(counter, NULL, "Read 1PPS reference counter (Falling edge)", cmd_fpga_counter),
    SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_fs,
    SHELL_CMD(ls, NULL, "List files [path]", cmd_fs_ls),
    SHELL_CMD(rm, NULL, "Remove file from /lfs", cmd_fs_rm),
    SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_tx,
    SHELL_CMD(start, NULL, "Start TX <freq_hz> [pwr_0_255]", cmd_tx_start),
    SHELL_CMD(stop, NULL, "Stop TX / Sweep", cmd_tx_stop),
    SHELL_CMD(sweep, NULL, "Sweep 1-30MHz <duration_sec> [single|continuous]", cmd_tx_sweep),
    SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_gnss,
    SHELL_CMD(raw, NULL, "Show most recent raw NMEA string", cmd_gnss_raw),
    SHELL_CMD(reset, NULL, "Manual GNSS chip reset (IO15)", cmd_gnss_reset),
    SHELL_CMD(monitor, NULL, "Continuously monitor GNSS UART data", cmd_gnss_monitor),
    SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_wspr,
    SHELL_CMD(status, NULL, "Show system status", cmd_wspr_status),
    SHELL_CMD(log, NULL, "Control subsystem logging", cmd_wspr_log),
    SHELL_CMD(fpga, &sub_fpga, "FPGA control commands", NULL),
    SHELL_CMD(gnss, &sub_gnss, "GNSS control commands", NULL),
    SHELL_CMD(fs, &sub_fs, "FileSystem commands", NULL),
    SHELL_CMD(tx, &sub_tx, "Transmitter test commands", NULL),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(wspr, &sub_wspr, "WSPR-ease commands", NULL);

} // namespace wspr
