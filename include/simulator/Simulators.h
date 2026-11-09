#pragma once
#include "simulator/SimulatedSource.h"

class TemperatureSimulator : public SimulatedSource {
public:
    explicit TemperatureSimulator(std::shared_ptr<VirtualTransport> transport);
    ~TemperatureSimulator() override;

    void setFixedValue(double celsius);
    void useWave();

protected:
    std::vector<uint8_t> produce() override;

private:
    unsigned long step_;
    bool fixed_;
    double fixedValue_;
};

class AccelerometerSimulator : public SimulatedSource {
public:
    explicit AccelerometerSimulator(std::shared_ptr<VirtualTransport> transport);
    ~AccelerometerSimulator() override;

    void setFixedValue(double x, double y, double z);
    void useMotion();
    int range() const;

protected:
    std::vector<uint8_t> produce() override;
    void handleWrite(const std::vector<uint8_t>& data) override;

private:
    unsigned long step_;
    bool fixed_;
    double fixedX_;
    double fixedY_;
    double fixedZ_;
    int rangeG_;
};

class GpsSimulator : public SimulatedSource {
public:
    explicit GpsSimulator(std::shared_ptr<VirtualTransport> transport);
    ~GpsSimulator() override;

    void setFix(bool hasFix);

protected:
    std::vector<uint8_t> produce() override;
    std::vector<uint8_t> corrupt(const std::vector<uint8_t>& frame, int variant) override;

private:
    unsigned long step_;
    bool hasFix_;
    double latitude_;
    double longitude_;
};

class VirtualDeviceSimulator : public SimulatedSource {
public:
    explicit VirtualDeviceSimulator(std::shared_ptr<VirtualTransport> transport);
    ~VirtualDeviceSimulator() override;

protected:
    std::vector<uint8_t> produce() override;
    std::vector<uint8_t> corrupt(const std::vector<uint8_t>& frame, int variant) override;
    void handleWrite(const std::vector<uint8_t>& data) override;

private:
    unsigned int sequence_;
};
