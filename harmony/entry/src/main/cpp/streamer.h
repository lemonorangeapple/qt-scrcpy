#pragma once

#include <cstdint>
#include <string>

namespace qsc {

enum class Source : uint8_t { Screen = 0, BackCamera = 1, FrontCamera = 2 };

struct Options {
    Source source = Source::Screen;
    int width = 0;      // 0 = pick a default for the source
    int height = 0;
    int frameRate = 30;
    int bitrate = 6'000'000;
    int port = 27183;
};

// Starts capturing + encoding + serving on a background thread. Returns false and fills
// `error` if something could not be set up. Only one session can run at a time.
bool Start(const Options &options, std::string &error);
void Stop();
bool IsRunning();
// Last problem seen by a background thread (empty when none).
std::string LastError();

} // namespace qsc
