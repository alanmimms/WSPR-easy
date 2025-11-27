/* file: src/net/WebServer.hpp */
#include <zephyr/net/http/server.h>
#include <string>
#include <map>

class WebServer {
public:
  void start();

private:
  // Map of template keys {{KEY}} to generator functions
  std::map<std::string, std::function<std::string()>> templateHooks;

  void setupHooks() {
    templateHooks["STATUS_TX_COUNT"] = []() { return std::to_string(stats.txCount); };
    templateHooks["CONFIG_CALLSIGN"] = []() { return currentConfig.callsign; };
    // ... add others
  }

  // This function reads the file from LittleFS in chunks
  // It scans for "{{", buffers, resolves the key, and sends the replacement
  static int dynamicHandler(struct http_client_ctx *ctx, enum http_data_status status,
			    uint8_t *buffer, size_t len, void *user_data) {
    // Implementation of stream processing:
    // 1. Read block from FS
    // 2. Search for {{
    // 3. Send data before {{
    // 4. Extract KEY, lookup value, send value
    // 5. Continue
  }
};
