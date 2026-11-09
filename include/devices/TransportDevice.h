#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "core/DeviceBase.h"
#include "transport/ITransport.h"

class TransportDevice : public DeviceBase {
public:
    TransportDevice(const std::string& id, const std::string& name, DeviceType type, unsigned capabilities,
                    std::shared_ptr<ITransport> transport, int timeoutMs);

protected:
    Status doOpen() override;
    void doClose() override;
    Status doRecover() override;

    Result<std::vector<uint8_t> > readFrame(size_t frameLength);
    Result<std::string> readLine(size_t maxLength);
    Status sendBytes(const std::vector<uint8_t>& data);
    Status configureCommon(const std::string& key, const std::string& value, bool& handled);
    std::map<std::string, std::string> commonConfig() const;

    std::shared_ptr<ITransport> transport_;
    std::vector<uint8_t> rx_;
    int timeoutMs_;
    bool discardingLine_;
};
