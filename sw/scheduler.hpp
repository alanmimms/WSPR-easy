/* file: src/sys/Scheduler.hpp */
#pragma once
#include "Config.hpp"
#include <optional>
#include <algorithm>

class Scheduler {
public:
  struct TransmissionPlan {
    std::string band;
    uint32_t freq;
    // WSPR symbols would be generated later
  };

  void updateSunTimes(time_t now) {
    // Implement simplified NOAA solar calc here
    // Update sunriseEpoch and sunsetEpoch based on config.lat/lon
  }

  std::optional<TransmissionPlan> getNextTransmission(const AppConfig& config) {
    time_t now = time(nullptr);
    std::vector<const BandSchedule*> candidates;

    // 1. Filter: Find all bands currently "open" based on time rules
    for (const auto& rule : config.schedules) {
      time_t start = resolveTime(rule.startTime, now);
      time_t stop  = resolveTime(rule.stopTime, now);
            
      // Handle wrap-around midnight logic if needed
      if (now >= start && now < stop) {
	// Check interval constraint (lastTxTime vs now)
	// If OK, add to candidates
	candidates.push_back(&rule);
      }
    }

    if (candidates.empty()) return std::nullopt;

    // 2. Sort: Lowest frequency first
    std::sort(candidates.begin(), candidates.end(), 
	      [](auto a, auto b) { return a->frequencyHz < b->frequencyHz; });

    // 3. Round Robin: Find next band after the one we used last
    auto it = std::find_if(candidates.begin(), candidates.end(), 
			   [this](auto rule) { return rule->frequencyHz > lastTxFreq; });

    const BandSchedule* selection = (it != candidates.end()) ? *it : candidates.front();

    lastTxFreq = selection->frequencyHz;
    return TransmissionPlan{selection->bandName, selection->frequencyHz};
  }

private:
  uint32_t lastTxFreq = 0;
  time_t sunriseEpoch;
  time_t sunsetEpoch;

  time_t resolveTime(const TimePoint& tp, time_t now) {
    if (tp.reference == TimeRef::UTC_ABSOLUTE) {
      // Convert offsetMinutes to minutes-of-day logic
      struct tm* t = gmtime(&now);
      return (now - (t->tm_min * 60 + t->tm_hour * 3600)) + (tp.offsetMinutes * 60); 
    } else if (tp.reference == TimeRef::SUNRISE) {
      return sunriseEpoch + (tp.offsetMinutes * 60);
    } else {
      return sunsetEpoch + (tp.offsetMinutes * 60);
    }
  }
};
