#pragma once
#include "core/DeviceError.h"
#include "core/Measurement.h"

class IGpsDevice {
public:
    virtual ~IGpsDevice() {}
    virtual Result<GpsMeasurement> readPosition() = 0;
};
