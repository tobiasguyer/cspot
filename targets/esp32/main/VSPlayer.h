#pragma once

#include <stddef.h>  // for size_t
#include <stdint.h>  // for uint8_t
#include <atomic>    // for atomic
#include <memory>    // for shared_ptr, unique_ptr
#include <mutex>     // for mutex
#include <string>    // for string

#include "DeviceStateHandler.h"  // for DeviceStateHandler, DeviceStateHandler::CommandType
#include "VS1053.h"
namespace cspot {
class DeviceStateHandler;
}  // namespace cspot

class VSPlayer {
 public:
  VSPlayer(std::shared_ptr<cspot::DeviceStateHandler> handler,
           std::shared_ptr<VS1053_SINK> vsSink = NULL);
  void disconnect();
  size_t volume = 0;

 private:
  size_t trackId = 0;
  std::string currentTrackId;
  std::shared_ptr<VS1053_SINK> vsSink;
  std::shared_ptr<cspot::DeviceStateHandler> handler;
  std::shared_ptr<VS1053_TRACK> track = nullptr;
  std::shared_ptr<cspot::QueuedTrack> futureTrack = nullptr,
                                      currentTrack = nullptr;
  void state_callback(uint8_t state);

  std::atomic<bool> pauseRequested = false;
  std::atomic<bool> isPaused = true;
  std::atomic<bool> isRunning = true;
  std::mutex runningMutex;
  std::atomic<bool> playlistEnd = false;
};
