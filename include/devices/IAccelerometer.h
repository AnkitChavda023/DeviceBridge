#pragma once
#include "core/DeviceError.h"
#include "core/Measurement.h"

class IAccelerometer {
public:
    virtual ~IAccelerometer() {}
    virtual Result<AccelMeasurement> readAcceleration() = 0;
};
