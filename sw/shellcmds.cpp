/* file: src/sys/ShellCmds.cpp (Extensions) */
#include "dev/GnssReceiver.hpp"
#include "sys/CalibrationManager.hpp"
#include "model/Configuration.hpp"
#include <zephyr/shell/shell.h>

extern GnssReceiver gGnss; // Global instance

static int cmd_status(const struct shell *sh, size_t argc, char **argv) {
  shell_print(sh, "=== WSPR-ease Status ===");
    
  // 1. Time
  int64_t now = k_uptime_get();
  time_t utc = time(nullptr);
  struct tm * pt = gmtime(&utc);
  shell_print(sh, "System Uptime: %lld ms", now);
  shell_print(sh, "UTC Time:      %02d:%02d:%02d", pt->tm_hour, pt->tm_min, pt->tm_sec);

  // 2. GNSS Status
  auto stats = gGnss.getStats();
  bool fix = gConfig.gnss.hasFix;
    
  shell_print(sh, "--- GNSS ---");
  shell_print(sh, "Fix Status:    %s", fix ? "LOCKED" : "SEARCHING");
  shell_print(sh, "Satellites:    %d", stats.satellitesTracked);
  shell_print(sh, "Quality:       %d", stats.fixQuality);
  shell_print(sh, "Location:      Lat %.5f, Lon %.5f", gConfig.gnss.lat, gConfig.gnss.lon);
  shell_print(sh, "Grid Square:   %s (Auto) / %s (Manual)", 
	      gConfig.gnss.detectedGrid.c_str(), 
	      gConfig.user.gridSquare.c_str());

  // 3. Calibration Status
  auto calStats = gCalibration.getStats();
  shell_print(sh, "--- Clock & RF ---");
  shell_print(sh, "FPGA PPS Count: %u", calStats.lastFpgaCount);
  shell_print(sh, "Ref Clock Err:  %.3f ppm", calStats.frequencyErrorPpm);
  shell_print(sh, "Discipline:     %s", calStats.isLocked ? "LOCKED" : "TRAINING");

  return 0;
}

// Register command
SHELL_STATIC_SUBCMD_SET_CREATE(sub_wspr,
			       SHELL_CMD(status, NULL, "Show system status", cmd_status),
			       // ... previous commands ...
			       SHELL_SUBCMD_SET_END
			       );

