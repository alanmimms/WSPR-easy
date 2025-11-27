/* file: src/model/Configuration.cpp */
#include "Configuration.hpp"
#include <cmath>
#include <algorithm>

std::string Configuration::latLonToGrid(double lat, double lon) {
  // Normalize Longitude to -180..180
  // Maidenhead starts at -180, -90
  double adjLon = lon + 180.0;
  double adjLat = lat + 90.0;

  char grid[5];
  grid[4] = '\0';

  // Field 1 (A-R) - 20 degrees each
  grid[0] = 'A' + (int)(adjLon / 20.0);
  grid[1] = 'A' + (int)(adjLat / 10.0);

  // Remainder
  double remLon = fmod(adjLon, 20.0);
  double remLat = fmod(adjLat, 10.0);

  // Field 2 (0-9) - 2 degrees lon, 1 degree lat
  grid[2] = '0' + (int)(remLon / 2.0);
  grid[3] = '0' + (int)(remLat / 1.0);
    
  // (Optional: Add subsquares if 6-char precision needed)

  return std::string(grid);
}
