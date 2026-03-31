#ifndef __BAND_H__
#define __BAND_H__

// Singleton Band metadata
class Band {
public:
  Band(const Band&) = delete;
  Band& operator=(const Band&) = delete;

  static Band& get() {
    static Band inst;
    return inst;
  }

  inline static const struct {
    const char *name;
    unsigned hz;
  } metadata[] = {
    {"160m", 1836600},
    {"80m", 3568600},
    {"40m", 7038600},
    {"30m", 10138700},
    {"20m", 14095600},
    {"17m", 18104600},
    {"15m", 21094600},
    {"12m", 24924600},
    {"10m", 28124600},
  };

  static constexpr int nBands = sizeof(metadata) / sizeof(metadata[0]);

  const char *nameForHz(unsigned hz) {

    for (int k = 0; k < nBands; ++k) {
      if (metadata[k].hz == hz) return metadata[k].name;
    }

    return NULL;		// Not found.
  }

  const unsigned hzForName(const char *key) {

    for (int k = 0; k < nBands; ++k) {
      if (strcmp(metadata[k].name, key) == 0) return metadata[k].hz;
    }

    return 0;			// Not found.
  }

private:
  Band() = default;
};

#endif
