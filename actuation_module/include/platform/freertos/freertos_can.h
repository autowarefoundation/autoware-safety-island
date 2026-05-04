#ifndef PLATFORM__FREERTOS__CAN_H_
#define PLATFORM__FREERTOS__CAN_H_

#include <array>
#include <cstddef>
#include <pthread.h>

#include "common/can/can_frame.hpp"
#include "common/logger/logger.hpp"

namespace common::can::platform
{

constexpr std::size_t kRecordedCanFrameCapacity = 256U;

inline std::array<CanFrame, kRecordedCanFrameCapacity> recorded_can_frames{};
inline std::size_t recorded_can_frames_size{0U};
inline pthread_mutex_t recorded_can_frames_mutex = PTHREAD_MUTEX_INITIALIZER;

class RecordedCanFramesLock
{
public:
  RecordedCanFramesLock()
  {
    pthread_mutex_lock(&recorded_can_frames_mutex);
  }

  ~RecordedCanFramesLock()
  {
    pthread_mutex_unlock(&recorded_can_frames_mutex);
  }

  RecordedCanFramesLock(const RecordedCanFramesLock &) = delete;
  RecordedCanFramesLock & operator=(const RecordedCanFramesLock &) = delete;
};

inline void reset_recorded_can_frames()
{
  RecordedCanFramesLock lock;
  recorded_can_frames = {};
  recorded_can_frames_size = 0U;
}

inline std::size_t recorded_can_frame_count()
{
  RecordedCanFramesLock lock;
  return recorded_can_frames_size;
}

inline CanFrame recorded_can_frame_at(const std::size_t index)
{
  RecordedCanFramesLock lock;
  return recorded_can_frames[index];
}

inline bool can_init()
{
  reset_recorded_can_frames();
  common::logger::log_info("FreeRTOS CAN mock initialized");
  return true;
}

inline bool can_send(const CanFrame & frame)
{
  RecordedCanFramesLock lock;
  if (recorded_can_frames_size >= recorded_can_frames.size()) {
    common::logger::log_error("FreeRTOS CAN mock frame buffer full");
    return false;
  }

  recorded_can_frames[recorded_can_frames_size] = frame;
  ++recorded_can_frames_size;

  common::logger::log_debug(
    "FreeRTOS CAN mock sent frame id=0x%03x dlc=%u",
    static_cast<unsigned int>(frame.id),
    static_cast<unsigned int>(frame.dlc));
  return true;
}

}  // namespace common::can::platform

#endif  // PLATFORM__FREERTOS__CAN_H_
