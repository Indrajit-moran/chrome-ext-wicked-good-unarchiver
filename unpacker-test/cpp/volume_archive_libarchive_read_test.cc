// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "volume_archive_libarchive.h"

#include "fake_lib_archive.h"
#include "fake_volume_reader.h"
#include "gtest/gtest.h"

namespace {

// The request id for which the tested VolumeArchiveLibarchive is created.
const char kRequestId[] = "1";

// Fake archive data used for testing.
const char kArchiveData[] =
    "Fake data contained by the archive. Content is "
    "not important and it is used strictly for testing.";

}  // namespace

// Class used by TEST_F macro to initialize the environment for testing
// VolumeArchiveLibarchive Read method.
class VolumeArchiveLibarchiveReadTest : public testing::Test {
 protected:
  VolumeArchiveLibarchiveReadTest() : volume_archive(NULL) {}

  virtual void SetUp() {
    fake_lib_archive_config::ResetVariables();
    // Pass FakeVolumeReader ownership to VolumeArchiveLibarchive.
    volume_archive = new VolumeArchiveLibarchive(std::string(kRequestId),
                                                 new FakeVolumeReader());

    // Prepare for read.
    volume_archive->Init();
    const char* path_name = NULL;
    int64_t size = 0;
    bool is_directory = false;
    time_t modification_time = 0;
    volume_archive->GetNextHeader(
        &path_name, &size, &is_directory, &modification_time);
  }

  virtual void TearDown() {
    volume_archive->Cleanup();
    delete volume_archive;
    volume_archive = NULL;
  }

  VolumeArchiveLibarchive* volume_archive;
};

// This test is used to test VolumeArchive::ReadData for correct reads with
// different offsets, lengths and a buffer that has different characters inside.
// The idea of the test is to make consecutive calls to VolumeArchive::ReadData
// in order to coverage all special cases that can appear:
//     - read data from offset 0 with length equal to archive data
//     - make 2 small consecutive reads with first starting from offset 0 and
//     second starting with offset as the length of the first read
//     - read data with offset different from 0 but less than the length of the
//     previous read
// Tests lengths < volume_archive_constants::kMininumDataChunkSize.
// VolumeArchive::ReadData should not be affected by this constant.
TEST_F(VolumeArchiveLibarchiveReadTest, ReadSuccessForSmallLengths) {
  fake_lib_archive_config::archive_data = kArchiveData;
  fake_lib_archive_config::archive_data_size = sizeof(kArchiveData);
  size_t archive_data_size = fake_lib_archive_config::archive_data_size;

  // Test successful ReadData with length equal to data size.
  {
    size_t length = archive_data_size;
    char buffer[length];
    memset(buffer, 0, length);
    EXPECT_TRUE(volume_archive->ReadData(0, length, buffer));
    EXPECT_EQ(0, memcmp(buffer, kArchiveData, length));
  }

  // Test successful read with offset less than VolumeArchiveLibarchive current
  // offset (due to last read) and length equal to half of the data size.
  {
    size_t length = archive_data_size / 2;
    char buffer[length];
    memset(buffer, 0, length);
    EXPECT_TRUE(volume_archive->ReadData(0, length, buffer));
    EXPECT_EQ(0, memcmp(buffer, kArchiveData, length));
  }

  // Test successful read for the other half of the data.
  {
    int64_t offset = archive_data_size / 2;
    int length = archive_data_size - offset;
    char buffer[length];
    memset(buffer, 0, length);
    EXPECT_TRUE(volume_archive->ReadData(offset, length, buffer));
    EXPECT_EQ(0, memcmp(buffer, kArchiveData + offset, length));
  }

  // Test successful read with offset less than last read but greater than 0.
  // This should trigger the execution of all the code inside ReadData.
  {
    int64_t offset = archive_data_size / 3;
    size_t length = archive_data_size - offset;
    char buffer[length];
    memset(buffer, 0, length);
    EXPECT_TRUE(volume_archive->ReadData(offset, length, buffer));
    EXPECT_EQ(0, memcmp(buffer, kArchiveData + offset, length));
  }
}

// Test read with length greater than data size.
TEST_F(VolumeArchiveLibarchiveReadTest,
       ReadSuccessForSmallLengthGreaterThanArchiveDataSize) {
  fake_lib_archive_config::archive_data = kArchiveData;
  fake_lib_archive_config::archive_data_size = sizeof(kArchiveData);
  size_t archive_data_size = fake_lib_archive_config::archive_data_size;

  size_t length = archive_data_size * 2;
  char buffer[length];
  memset(buffer, 0, length);
  EXPECT_TRUE(volume_archive->ReadData(0, length, buffer));
  EXPECT_EQ(0, memcmp(buffer, kArchiveData, archive_data_size));

  // Only archive_data_size should be read and written to buffer.
  size_t left_length = length - archive_data_size;
  char zero_buffer[left_length];
  memset(zero_buffer, 0, left_length);
  EXPECT_EQ(0,  // The rest of the bytes from buffer shouldn't be modified.
            memcmp(buffer + archive_data_size, zero_buffer, left_length));
}

// Test Read with length between volume_archive_constants::kMinimumDataChunkSize
// and volume_archive_constants::kMaximumDataChunkSize.
// VolumeArchive::ReadData should not be affected by this constant.
TEST_F(VolumeArchiveLibarchiveReadTest, ReadSuccessForMediumLength) {
  size_t buffer_length = volume_archive_constants::kMinimumDataChunkSize * 2;
  ASSERT_LT(buffer_length, volume_archive_constants::kMaximumDataChunkSize);

  char* expected_buffer = new char[buffer_length];  // Stack is small for tests.
  memset(
      expected_buffer,
      1 /* Different from below memset for testing if VolumeArchive::ReadData
              was correct. */,
      buffer_length);

  fake_lib_archive_config::archive_data = expected_buffer;
  fake_lib_archive_config::archive_data_size = buffer_length;

  char* buffer = new char[buffer_length];
  memset(expected_buffer,
         0 /* Different from above as the contents should be
                               changed by VolumeArchive::ReadData. */,
         buffer_length);
  fake_lib_archive_config::archive_data = buffer;
  EXPECT_TRUE(volume_archive->ReadData(0, buffer_length, buffer));
  EXPECT_EQ(0, memcmp(buffer, expected_buffer, buffer_length));

  delete expected_buffer;
  delete buffer;
}

// Test Read with length > volume_archive_constants::kMaximumDataChunkSize.
// VolumeArchive::ReadData should not be affected by this constant.
TEST_F(VolumeArchiveLibarchiveReadTest, ReadSuccessForLargeLength) {
  size_t buffer_length = volume_archive_constants::kMaximumDataChunkSize * 2;
  char* expected_buffer = new char[buffer_length];  // Stack is small for tests.
  memset(
      expected_buffer,
      1 /* Different from below memset for testing if VolumeArchive::ReadData
              was correct. */,
      buffer_length);

  fake_lib_archive_config::archive_data = expected_buffer;
  fake_lib_archive_config::archive_data_size = buffer_length;

  char* buffer = new char[buffer_length];
  memset(expected_buffer,
         0 /* Different from above as the contents should be
                               changed by VolumeArchive::ReadData. */,
         buffer_length);
  fake_lib_archive_config::archive_data = buffer;
  EXPECT_TRUE(volume_archive->ReadData(0, buffer_length, buffer));
  EXPECT_EQ(0, memcmp(buffer, expected_buffer, buffer_length));

  delete expected_buffer;
  delete buffer;
}

TEST_F(VolumeArchiveLibarchiveReadTest, ReadFailureForOffsetEqualToZero) {
  fake_lib_archive_config::archive_data = NULL;
  char buffer[10];
  EXPECT_FALSE(volume_archive->ReadData(0, 10, buffer));

  std::string read_data_error =
      std::string(volume_archive_constants::kArchiveReadDataErrorPrefix) +
      fake_lib_archive_config::kArchiveError;
  EXPECT_EQ(read_data_error, volume_archive->error_message());
}

TEST_F(VolumeArchiveLibarchiveReadTest, ReadFailureForOffsetGreaterThanZero) {
  fake_lib_archive_config::archive_data = NULL;
  char buffer[10];
  EXPECT_FALSE(volume_archive->ReadData(10, 10, buffer));

  std::string read_data_error =
      std::string(volume_archive_constants::kArchiveReadDataErrorPrefix) +
      fake_lib_archive_config::kArchiveError;
  EXPECT_EQ(read_data_error, volume_archive->error_message());
}
