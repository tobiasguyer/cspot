#ifndef AUDIOCHUNKMANAGER_H
#define AUDIOCHUNKMANAGER_H

#include <memory>
#include "Utils.h"
#include "AudioChunk.h"
#include "WrappedSemaphore.h"
#include "Task.h"

#define DATA_SIZE_HEADER 24
#define DATA_SIZE_FOOTER 2

class AudioChunkManager : public Task {
    std::vector<std::shared_ptr<AudioChunk>> chunks;
    std::unique_ptr<WrappedSemaphore> queueSemaphore;
    std::vector<std::pair<std::vector<uint8_t>, bool>> queue;
    void runTask();
public:
    AudioChunkManager();
    std::shared_ptr<AudioChunk> registerNewChunk(uint16_t seqId, std::vector<uint8_t> &audioKey, uint32_t startPos, uint32_t endPos);
    void handleChunkData(std::vector<uint8_t> &data, bool failed = false);
};

#endif