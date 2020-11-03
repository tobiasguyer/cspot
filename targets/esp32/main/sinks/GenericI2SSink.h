#ifndef GENERICI2SSINK_H
#define GENERICI2SSINK_H

#include <vector>
#include <iostream>
#include "AudioSink.h"
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_log.h"

class GenericI2SSink : public AudioSink
{
public:
    GenericI2SSink();
    ~GenericI2SSink();
    void feedPCMFrames(std::vector<uint8_t> &data);
};

#endif