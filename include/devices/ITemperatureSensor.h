#pragma once
#include "core/DeviceError.h"
#include "core/Measurement.h"

class ITemperatureSensor {
public:
    virtual ~ITemperatureSensor() {}
    virtual Result<TemperatureMeasurement> readTemperature() = 0;
};
