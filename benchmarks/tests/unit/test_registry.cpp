#include "Rigs.h"
#include "TestHelper.h"
#include "manager/DeviceRegistry.h"

TEST(id_validation) {
    CHECK(DeviceRegistry::isValidId("temp-virtual_01"));
    CHECK(!DeviceRegistry::isValidId(""));
    CHECK(!DeviceRegistry::isValidId("has space"));
    CHECK(!DeviceRegistry::isValidId("slash/id"));
    CHECK(!DeviceRegistry::isValidId(std::string(65, 'a')));
    CHECK(DeviceRegistry::isValidId(std::string(64, 'a')));
}

TEST(add_find_list_remove) {
    DeviceRegistry registry;
    TempRig a("a");
    TempRig b("b");
    CHECK_OK(registry.add(a.device));
    CHECK_OK(registry.add(b.device));
    CHECK_EQ(registry.size(), static_cast<size_t>(2));
    CHECK(registry.find("a") != nullptr);
    CHECK(registry.find("zzz") == nullptr);
    CHECK_EQ(registry.list().size(), static_cast<size_t>(2));
    CHECK(registry.remove("a"));
    CHECK(!registry.remove("a"));
    CHECK(registry.find("a") == nullptr);
    CHECK_EQ(registry.size(), static_cast<size_t>(1));
}

TEST(duplicate_and_invalid_ids_are_rejected) {
    DeviceRegistry registry;
    TempRig a("same");
    TempRig b("same");
    CHECK_OK(registry.add(a.device));
    CHECK_CODE(registry.add(b.device), ErrorCode::AlreadyExists);
    TempRig bad("bad id");
    CHECK_CODE(registry.add(bad.device), ErrorCode::InvalidArgument);
    CHECK_CODE(registry.add(std::shared_ptr<IDevice>()), ErrorCode::InvalidArgument);
    CHECK_EQ(registry.size(), static_cast<size_t>(1));
}

TEST(removed_device_stays_alive_for_holders) {
    DeviceRegistry registry;
    TempRig a("a");
    registry.add(a.device);
    std::shared_ptr<IDevice> held = registry.find("a");
    registry.remove("a");
    CHECK_EQ(held->info().id, std::string("a"));
}

TEST_MAIN("unit: registry")
