/* file: src/main.cpp */
#include "sys/Logger.hpp"
#include "sys/Scheduler.hpp"
#include "net/WebServer.hpp"

// Global instances
AppConfig gConfig;
Scheduler gScheduler;
GNSSReceiver gGNSS;


static void checkGNSSStatus() {
  static int64_t lastCheckMS;
  int64_t now = k_uptime_get();

  if (now - lastCheck < 5*1000) return;

  auto gnss = gGNSS.getLatestData();

  if (gnss.isStale(10*1000)) {
    LOG_WRN("GNSS signal lost! Last update %lldms ago", now - gnss.lastMS);
  }

  if (gnss.hasFix) {
    LOG_INF("Location: %s (%.4f, %.4f, %.4f) ",
	    gnss.gridSquare.c_str(),
	    gnss.lat, gnss.lon, gnss.altitude);
  } else {
    LOG_INF("Searching for satellites... (%d visible)", gnss.satellites);
  }
}


void main(void) {
  // 1. Init Logger (1MB)
  RingBufferLogger::instance().init(1024 * 1024);
  RingBufferLogger::instance().log("Booting WSPR-ease...");

  // 2. Mount Filesystem & Load Config
  // loadConfigFromFS(gConfig);
  RingBufferLogger::instance().log("Config Loaded. Callsign: " + gConfig.callsign);

  // 3. Start Subsytems
  // startUsbConsole();
  // startGnss();
    
  // 4. Start Web Server
  // WebServer::start();

  // 5. Main Loop
  while (1) {
    checkGNSSStatus();

    // Update Time & Sun
    // gScheduler.updateSunTimes(now);
        
    auto plan = gScheduler.getNextTransmission(gConfig);
    if (plan) {
      RingBufferLogger::instance().log("Starting TX on " + plan->band);
            
      // Execute WSPR sequence (control FPGA)
      // ...
            
      RingBufferLogger::instance().log("TX Complete");
    } else {
      // Sleep / Idle
    }

    k_sleep(K_SECONDS(1));
  }
}
