#ifndef CSPOT_CONFIGURATION_H
#define CSPOT_CONFIGURATION_H

/**
 * @brief Defines prefered bitrate.
 */
enum class PreferredBitrate
{
    BITRATE_96,
    BITRATE_160,
    BITRATE_320
};

class CSpotConfiguration
{

public:
    CSpotConfiguration();
    
    /**
     * @brief CSpot will try select an audiofile matching preferred quality.
     */
    PreferredBitrate preferredBitrate = PreferredBitrate::BITRATE_160;
};

#endif