#include "TestHelper.h"
#include "devices/Parsers.h"

TEST(temperature_frame_round_trip) {
    double values[] = {25.0, -10.5, 0.0, 99.99, -40.0, 125.0};
    for (size_t i = 0; i < 6; i++) {
        Result<TemperatureMeasurement> r = parseTemperatureFrame(buildTemperatureFrame(values[i]), 1000);
        CHECK(r.ok());
        CHECK_NEAR(r.value.celsius, values[i], 0.006);
        CHECK(r.value.valid);
        CHECK_EQ(r.value.timestampMs, 1000LL);
    }
}

TEST(temperature_frame_rejects_bad_input) {
    std::vector<uint8_t> frame = buildTemperatureFrame(20.0);

    std::vector<uint8_t> badSum = frame;
    badSum[3] ^= 0x01;
    CHECK_CODE(parseTemperatureFrame(badSum, 0).status, ErrorCode::InvalidData);

    std::vector<uint8_t> noSync = frame;
    noSync[0] = 0x00;
    CHECK_CODE(parseTemperatureFrame(noSync, 0).status, ErrorCode::InvalidData);

    std::vector<uint8_t> shortFrame(frame.begin(), frame.begin() + 3);
    CHECK_CODE(parseTemperatureFrame(shortFrame, 0).status, ErrorCode::InvalidData);

    CHECK_CODE(parseTemperatureFrame(buildTemperatureFrame(200.0), 0).status, ErrorCode::InvalidData);
    CHECK_CODE(parseTemperatureFrame(buildTemperatureFrame(-60.0), 0).status, ErrorCode::InvalidData);
}

TEST(accel_frame_scaling_for_every_range) {
    int ranges[] = {2, 4, 8, 16};
    for (int i = 0; i < 4; i++) {
        Result<AccelMeasurement> r = parseAccelFrame(buildAccelFrame(0.5, -0.25, 1.0, ranges[i]), ranges[i], 5);
        CHECK(r.ok());
        CHECK_NEAR(r.value.x, 0.5, 0.001 * ranges[i]);
        CHECK_NEAR(r.value.y, -0.25, 0.001 * ranges[i]);
        CHECK_NEAR(r.value.z, 1.0, 0.001 * ranges[i]);
        CHECK(r.value.valid);
        CHECK_NEAR(r.value.rangeG, ranges[i], 0.0001);
    }
}

TEST(accel_frame_using_wrong_range_gives_wrong_scale) {
    Result<AccelMeasurement> r = parseAccelFrame(buildAccelFrame(1.0, 0, 0, 2), 4, 0);
    CHECK(r.ok());
    CHECK_NEAR(r.value.x, 2.0, 0.01);
}

TEST(accel_frame_rejects_bad_input) {
    std::vector<uint8_t> frame = buildAccelFrame(0, 0, 1, 2);
    std::vector<uint8_t> badSum = frame;
    badSum[7] ^= 0x10;
    CHECK_CODE(parseAccelFrame(badSum, 2, 0).status, ErrorCode::InvalidData);
    CHECK_CODE(parseAccelFrame(frame, 3, 0).status, ErrorCode::InvalidArgument);
    std::vector<uint8_t> shortFrame(frame.begin(), frame.begin() + 5);
    CHECK_CODE(parseAccelFrame(shortFrame, 2, 0).status, ErrorCode::InvalidData);
}

TEST(accel_saturated_reading_is_flagged) {
    Result<AccelMeasurement> r = parseAccelFrame(buildAccelFrame(5.0, 0, 0, 2), 2, 0);
    CHECK(r.ok());
    CHECK(!r.value.valid);
}

TEST(nmea_builder_matches_known_checksum) {
    std::string body = "GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,";
    CHECK_EQ(buildNmea(body), "$" + body + "*47\r\n");
}

TEST(nmea_gga_is_parsed) {
    Result<GpsMeasurement> r = parseNmea("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47", 9);
    CHECK(r.ok());
    CHECK(r.value.fixValid);
    CHECK_NEAR(r.value.latitude, 48.1173, 0.0001);
    CHECK_NEAR(r.value.longitude, 11.5167, 0.0001);
    CHECK(r.value.hasAltitude);
    CHECK_NEAR(r.value.altitude, 545.4, 0.0001);
    CHECK_EQ(r.value.utcTime, std::string("123519"));
}

TEST(nmea_rmc_is_parsed) {
    Result<GpsMeasurement> r = parseNmea(
        "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A", 0);
    CHECK(r.ok());
    CHECK(r.value.fixValid);
    CHECK(r.value.hasSpeed);
    CHECK_NEAR(r.value.speedMps, 22.4 * 0.514444, 0.001);
}

TEST(nmea_southern_and_western_hemispheres) {
    std::string line = buildNmea("GPGGA,010203,3352.000,S,15112.000,W,1,06,1.0,10.0,M,0.0,M,,");
    Result<GpsMeasurement> r = parseNmea(line.substr(0, line.size() - 2), 0);
    CHECK(r.ok());
    CHECK(r.value.latitude < 0);
    CHECK(r.value.longitude < 0);
}

TEST(nmea_without_fix_is_not_valid) {
    std::string gga = buildNmea("GPGGA,123519,,,,,0,00,99.9,,M,,M,,");
    Result<GpsMeasurement> a = parseNmea(gga.substr(0, gga.size() - 2), 0);
    CHECK(a.ok());
    CHECK(!a.value.fixValid);

    std::string rmc = buildNmea("GPRMC,123519,V,,,,,,,230394,,,N");
    Result<GpsMeasurement> b = parseNmea(rmc.substr(0, rmc.size() - 2), 0);
    CHECK(b.ok());
    CHECK(!b.value.fixValid);
}

TEST(nmea_rejects_malformed_sentences) {
    CHECK_CODE(parseNmea("", 0).status, ErrorCode::InvalidData);
    CHECK_CODE(parseNmea("GPGGA,1,2,3*00", 0).status, ErrorCode::InvalidData);
    CHECK_CODE(parseNmea("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*48", 0).status,
               ErrorCode::InvalidData);
    CHECK_CODE(parseNmea("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,", 0).status,
               ErrorCode::InvalidData);
    CHECK_CODE(parseNmea("$GPGGA,123519,4807.038,N,0113*47", 0).status, ErrorCode::InvalidData);
    CHECK_CODE(parseNmea("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*ZZ", 0).status,
               ErrorCode::InvalidData);
}

TEST(nmea_rejects_bad_coordinates) {
    std::string badLat = buildNmea("GPGGA,123519,9507.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
    CHECK_CODE(parseNmea(badLat.substr(0, badLat.size() - 2), 0).status, ErrorCode::InvalidData);
    std::string badMinutes = buildNmea("GPGGA,123519,4877.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
    CHECK_CODE(parseNmea(badMinutes.substr(0, badMinutes.size() - 2), 0).status, ErrorCode::InvalidData);
    std::string badHemisphere = buildNmea("GPGGA,123519,4807.038,X,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
    CHECK_CODE(parseNmea(badHemisphere.substr(0, badHemisphere.size() - 2), 0).status, ErrorCode::InvalidData);
}

TEST(nmea_unknown_sentence_is_unsupported) {
    std::string line = buildNmea("GPGSV,3,1,11,03,03,111,00,04,15,270,00");
    CHECK_CODE(parseNmea(line.substr(0, line.size() - 2), 0).status, ErrorCode::Unsupported);
}

TEST_MAIN("unit: parsers")
