#pragma once

#include <vector>
#include <mutex>
#include <atomic>

// Start the receiver in a new thread. streamPort is UDP port to bind to.
// The receiver will fill imgBuf under imgMutex with packed BGRA pixels for fixed 240x160 frames
// (mgba::STREAM_WIDTH x mgba::STREAM_HEIGHT) and set newFrame to true when a new frame is available.
// For audio, it will fill audioBuf under audioMutex with S16 stereo samples
// and set newAudio to true when new audio samples are available.
void start_video_receiver(int localPort, std::mutex& imgMutex, std::vector<unsigned char>& imgBuf, std::atomic<bool>& newFrame, std::atomic<bool>& runningFlag, std::mutex& audioMutex, std::vector<int16_t>& audioBuf, std::atomic<int>& audioChannels, std::atomic<int>& audioSampleRate, std::atomic<bool>& newAudio);

void stop_video_receiver();
