// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "media/base/audio_decoder_config.h"
#include "media/base/decoder_buffer.h"
#include "media/base/mock_callback.h"
#include "media/base/mock_demuxer_host.h"
#include "media/base/test_data_util.h"
#include "media/filters/chunk_demuxer.h"
#include "media/filters/chunk_demuxer_client.h"
#include "media/webm/cluster_builder.h"
#include "media/webm/webm_constants.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::AnyNumber;
using ::testing::InSequence;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgumentPointee;
using ::testing::_;

namespace media {

static const uint8 kTracksHeader[] = {
  0x16, 0x54, 0xAE, 0x6B,  // Tracks ID
  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // tracks(size = 0)
};

static const int kTracksHeaderSize = sizeof(kTracksHeader);
static const int kTracksSizeOffset = 4;

// The size of TrackEntry element in test file "webm_vp8_track_entry" starts at
// index 1 and spans 8 bytes.
static const int kVideoTrackSizeOffset = 1;
static const int kVideoTrackSizeWidth = 8;
static const int kVideoTrackEntryHeaderSize = kVideoTrackSizeOffset +
                                              kVideoTrackSizeWidth;

static const int kVideoTrackNum = 1;
static const int kAudioTrackNum = 2;

static const int kAudioBlockDuration = 23;
static const int kVideoBlockDuration = 33;

static const char* kSourceId = "SourceId";

base::TimeDelta kDefaultDuration() {
  return base::TimeDelta::FromMilliseconds(201224);
}

// Write an integer into buffer in the form of vint that spans 8 bytes.
// The data pointed by |buffer| should be at least 8 bytes long.
// |number| should be in the range 0 <= number < 0x00FFFFFFFFFFFFFF.
static void WriteInt64(uint8* buffer, int64 number) {
  DCHECK(number >= 0 && number < GG_LONGLONG(0x00FFFFFFFFFFFFFF));
  buffer[0] = 0x01;
  int64 tmp = number;
  for (int i = 7; i > 0; i--) {
    buffer[i] = tmp & 0xff;
    tmp >>= 8;
  }
}

MATCHER_P(HasTimestamp, timestamp_in_ms, "") {
  return arg && !arg->IsEndOfStream() &&
      arg->GetTimestamp().InMilliseconds() == timestamp_in_ms;
}

static void OnReadDone(const base::TimeDelta& expected_time,
                       bool* called,
                       const scoped_refptr<DecoderBuffer>& buffer) {
  EXPECT_EQ(expected_time, buffer->GetTimestamp());
  *called = true;
}

class MockChunkDemuxerClient : public ChunkDemuxerClient {
 public:
  MockChunkDemuxerClient() {}
  virtual ~MockChunkDemuxerClient() {}

  MOCK_METHOD1(DemuxerOpened, void(ChunkDemuxer* demuxer));
  MOCK_METHOD0(DemuxerClosed, void());
  // TODO(xhwang): This is a workaround of the issue that move-only parameters
  // are not supported in mocked methods. Remove this when the issue is fixed.
  // See http://code.google.com/p/googletest/issues/detail?id=395
  MOCK_METHOD2(KeyNeededMock, void(const uint8* init_data, int init_data_size));
  void KeyNeeded(scoped_array<uint8> init_data, int init_data_size) {
    KeyNeededMock(init_data.get(), init_data_size);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(MockChunkDemuxerClient);
};

class ChunkDemuxerTest : public testing::Test {
 protected:
  enum CodecsIndex {
    AUDIO,
    VIDEO,
    MAX_CODECS_INDEX
  };

  ChunkDemuxerTest()
      : buffered_bytes_(0),
        client_(new MockChunkDemuxerClient()),
        demuxer_(new ChunkDemuxer(client_.get())) {
  }

  virtual ~ChunkDemuxerTest() {
    ShutdownDemuxer();
  }

  void CreateInfoTracks(bool has_audio, bool has_video,
                        bool video_content_encoded, scoped_array<uint8>* buffer,
                        int* size) {
    scoped_refptr<DecoderBuffer> info;
    scoped_refptr<DecoderBuffer> audio_track_entry;
    scoped_refptr<DecoderBuffer> video_track_entry;
    scoped_refptr<DecoderBuffer> video_content_encodings;

    info = ReadTestDataFile("webm_info_element");

    int tracks_element_size = 0;

    if (has_audio) {
      audio_track_entry = ReadTestDataFile("webm_vorbis_track_entry");
      tracks_element_size += audio_track_entry->GetDataSize();
    }

    if (has_video) {
      video_track_entry = ReadTestDataFile("webm_vp8_track_entry");
      tracks_element_size += video_track_entry->GetDataSize();
      if (video_content_encoded) {
        video_content_encodings = ReadTestDataFile("webm_content_encodings");
        tracks_element_size += video_content_encodings->GetDataSize();
      }
    }

    *size = info->GetDataSize() + kTracksHeaderSize + tracks_element_size;

    buffer->reset(new uint8[*size]);

    uint8* buf = buffer->get();
    memcpy(buf, info->GetData(), info->GetDataSize());
    buf += info->GetDataSize();

    memcpy(buf, kTracksHeader, kTracksHeaderSize);
    WriteInt64(buf + kTracksSizeOffset, tracks_element_size);
    buf += kTracksHeaderSize;

    if (has_audio) {
      memcpy(buf, audio_track_entry->GetData(),
             audio_track_entry->GetDataSize());
      buf += audio_track_entry->GetDataSize();
    }

    if (has_video) {
      memcpy(buf, video_track_entry->GetData(),
             video_track_entry->GetDataSize());
      if (video_content_encoded) {
        memcpy(buf + video_track_entry->GetDataSize(),
               video_content_encodings->GetData(),
               video_content_encodings->GetDataSize());
        WriteInt64(buf + kVideoTrackSizeOffset,
                   video_track_entry->GetDataSize() +
                   video_content_encodings->GetDataSize() -
                   kVideoTrackEntryHeaderSize);
        buf += video_content_encodings->GetDataSize();
      }
      buf += video_track_entry->GetDataSize();
    }
  }

  ChunkDemuxer::Status AddId() {
    std::vector<std::string> codecs(2);
    codecs[0] = "vp8";
    codecs[1] = "vorbis";
    return demuxer_->AddId(kSourceId, "video/webm", codecs);
  }

  bool AppendData(const uint8* data, size_t length) {
    CHECK(length);
    EXPECT_CALL(host_, AddBufferedByteRange(_, _)).Times(AnyNumber())
        .WillRepeatedly(SaveArg<1>(&buffered_bytes_));
    EXPECT_CALL(host_, SetNetworkActivity(true))
        .Times(AnyNumber());
    return demuxer_->AppendData(kSourceId, data, length);
  }

  bool AppendDataInPieces(const uint8* data, size_t length) {
    return AppendDataInPieces(data, length, 7);
  }

  bool AppendDataInPieces(const uint8* data, size_t length, size_t piece_size) {
    const uint8* start = data;
    const uint8* end = data + length;
    while (start < end) {
      int64 old_buffered_bytes = buffered_bytes_;
      size_t append_size = std::min(piece_size,
                                    static_cast<size_t>(end - start));
      if (!AppendData(start, append_size))
        return false;
      start += append_size;

      EXPECT_GT(buffered_bytes_, old_buffered_bytes);
    }
    return true;
  }

  bool AppendInfoTracks(bool has_audio, bool has_video,
                        bool video_content_encoded) {
    scoped_array<uint8> info_tracks;
    int info_tracks_size = 0;
    CreateInfoTracks(has_audio, has_video, video_content_encoded,
                     &info_tracks, &info_tracks_size);
    return AppendData(info_tracks.get(), info_tracks_size);
  }

  void InitDoneCalled(PipelineStatus expected_status,
                      PipelineStatus status) {
    EXPECT_EQ(status, expected_status);
  }

  PipelineStatusCB CreateInitDoneCB(const base::TimeDelta& expected_duration,
                                    PipelineStatus expected_status) {
    if (expected_status == PIPELINE_OK)
      EXPECT_CALL(host_, SetDuration(expected_duration));

    return base::Bind(&ChunkDemuxerTest::InitDoneCalled,
                      base::Unretained(this),
                      expected_status);
  }

  bool InitDemuxer(bool has_audio, bool has_video,
                   bool video_content_encoded) {
    PipelineStatus expected_status =
        (has_audio || has_video) ? PIPELINE_OK : DEMUXER_ERROR_COULD_NOT_OPEN;

    EXPECT_CALL(*client_, DemuxerOpened(_));
    demuxer_->Initialize(
        &host_, CreateInitDoneCB(kDefaultDuration(), expected_status));

    if (AddId() != ChunkDemuxer::kOk)
      return false;

    return AppendInfoTracks(has_audio, has_video, video_content_encoded);
  }

  void ShutdownDemuxer() {
    if (demuxer_) {
      EXPECT_CALL(*client_, DemuxerClosed());
      demuxer_->Shutdown();
    }
  }

  void AddSimpleBlock(ClusterBuilder* cb, int track_num, int64 timecode) {
    uint8 data[] = { 0x00 };
    cb->AddSimpleBlock(track_num, timecode, 0, data, sizeof(data));
  }

  scoped_ptr<Cluster> GenerateCluster(int timecode, int block_count) {
    return GenerateCluster(timecode, timecode, block_count);
  }

  scoped_ptr<Cluster> GenerateCluster(int audio_timecode, int video_timecode,
                                      int block_count) {
    CHECK_GT(block_count, 0);

    int size = 10;
    scoped_array<uint8> data(new uint8[size]);

    ClusterBuilder cb;
    cb.SetClusterTimecode(std::min(audio_timecode, video_timecode));

    if (block_count == 1) {
      cb.AddBlockGroup(kAudioTrackNum, audio_timecode, kAudioBlockDuration,
                       kWebMFlagKeyframe, data.get(), size);
      return cb.Finish();
    }

    // Create simple blocks for everything except the last 2 blocks.
    // The first video frame must be a keyframe.
    uint8 video_flag = kWebMFlagKeyframe;
    for (int i = 0; i < block_count - 2; i++) {
      if (audio_timecode <= video_timecode) {
        cb.AddSimpleBlock(kAudioTrackNum, audio_timecode, kWebMFlagKeyframe,
                          data.get(), size);
        audio_timecode += kAudioBlockDuration;
        continue;
      }

      cb.AddSimpleBlock(kVideoTrackNum, video_timecode, video_flag, data.get(),
                        size);
      video_timecode += kVideoBlockDuration;
      video_flag = 0;
    }

    // Make the last 2 blocks BlockGroups so that they don't get delayed by the
    // block duration calculation logic.
    if (audio_timecode <= video_timecode) {
      cb.AddBlockGroup(kAudioTrackNum, audio_timecode, kAudioBlockDuration,
                       kWebMFlagKeyframe, data.get(), size);
      cb.AddBlockGroup(kVideoTrackNum, video_timecode, kVideoBlockDuration,
                       video_flag, data.get(), size);
    } else {
      cb.AddBlockGroup(kVideoTrackNum, video_timecode, kVideoBlockDuration,
                       video_flag, data.get(), size);
      cb.AddBlockGroup(kAudioTrackNum, audio_timecode, kAudioBlockDuration,
                       kWebMFlagKeyframe, data.get(), size);
    }

    return cb.Finish();
  }

  void GenerateExpectedReads(int timecode, int block_count,
                             DemuxerStream* audio,
                             DemuxerStream* video) {
    CHECK_GT(block_count, 0);
    int audio_timecode = timecode;
    int video_timecode = timecode;

    if (block_count == 1) {
      ExpectRead(audio, audio_timecode);
      return;
    }

    for (int i = 0; i < block_count; i++) {
      if (audio_timecode <= video_timecode) {
        ExpectRead(audio, audio_timecode);
        audio_timecode += kAudioBlockDuration;
        continue;
      }

      ExpectRead(video, video_timecode);
      video_timecode += kVideoBlockDuration;
    }
  }

  MOCK_METHOD1(ReadDone, void(const scoped_refptr<DecoderBuffer>&));

  void ExpectRead(DemuxerStream* stream, int64 timestamp_in_ms) {
    EXPECT_CALL(*this, ReadDone(HasTimestamp(timestamp_in_ms)));
    stream->Read(base::Bind(&ChunkDemuxerTest::ReadDone,
                            base::Unretained(this)));
  }

  MOCK_METHOD1(Checkpoint, void(int id));

  struct BufferTimestamps {
    int video_time_ms;
    int audio_time_ms;
  };
  static const int kSkip = -1;

  // Test parsing a WebM file.
  // |filename| - The name of the file in media/test/data to parse.
  // |timestamps| - The expected timestamps on the parsed buffers.
  //    a timestamp of kSkip indicates that a Read() call for that stream
  //    shouldn't be made on that iteration of the loop. If both streams have
  //    a kSkip then the loop will terminate.
  bool ParseWebMFile(const std::string& filename,
                     const BufferTimestamps* timestamps,
                     const base::TimeDelta& duration) {
    EXPECT_CALL(*client_, DemuxerOpened(_));
    demuxer_->Initialize(
        &host_, CreateInitDoneCB(duration, PIPELINE_OK));

    if (AddId() != ChunkDemuxer::kOk)
      return false;

    // Read a WebM file into memory and send the data to the demuxer.
    scoped_refptr<DecoderBuffer> buffer = ReadTestDataFile(filename);
    if (!AppendDataInPieces(buffer->GetData(), buffer->GetDataSize(), 512))
      return false;

    scoped_refptr<DemuxerStream> audio =
        demuxer_->GetStream(DemuxerStream::AUDIO);
    scoped_refptr<DemuxerStream> video =
        demuxer_->GetStream(DemuxerStream::VIDEO);

    // Verify that the timestamps on the first few packets match what we
    // expect.
    for (size_t i = 0;
         (timestamps[i].audio_time_ms != kSkip ||
          timestamps[i].video_time_ms != kSkip);
         i++) {
      bool audio_read_done = false;
      bool video_read_done = false;

      if (timestamps[i].audio_time_ms != kSkip) {
        DCHECK(audio);
        audio->Read(base::Bind(&OnReadDone,
                               base::TimeDelta::FromMilliseconds(
                                   timestamps[i].audio_time_ms),
                               &audio_read_done));
        EXPECT_TRUE(audio_read_done);
      }

      if (timestamps[i].video_time_ms != kSkip) {
        DCHECK(video);
        video->Read(base::Bind(&OnReadDone,
                               base::TimeDelta::FromMilliseconds(
                                   timestamps[i].video_time_ms),
                               &video_read_done));

        EXPECT_TRUE(video_read_done);
      }
    }

    return true;
  }

  MockDemuxerHost host_;
  int64 buffered_bytes_;

  scoped_ptr<MockChunkDemuxerClient> client_;
  scoped_refptr<ChunkDemuxer> demuxer_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ChunkDemuxerTest);
};

TEST_F(ChunkDemuxerTest, TestInit) {
  // Test no streams, audio-only, video-only, and audio & video scenarios,
  // with video content encoded or not.
  for (int i = 0; i < 8; i++) {
    bool has_audio = (i & 0x1) != 0;
    bool has_video = (i & 0x2) != 0;
    bool video_content_encoded = (i & 0x4) != 0;

    // No test on invalid combination.
    if (!has_video && video_content_encoded)
      continue;

    client_.reset(new MockChunkDemuxerClient());
    demuxer_ = new ChunkDemuxer(client_.get());
    if (has_video && video_content_encoded)
      EXPECT_CALL(*client_, KeyNeededMock(NotNull(), 16));

    ASSERT_TRUE(InitDemuxer(has_audio, has_video, video_content_encoded));

    scoped_refptr<DemuxerStream> audio_stream =
        demuxer_->GetStream(DemuxerStream::AUDIO);
    if (has_audio) {
      ASSERT_TRUE(audio_stream);

      const AudioDecoderConfig& config = audio_stream->audio_decoder_config();
      EXPECT_EQ(kCodecVorbis, config.codec());
      EXPECT_EQ(16, config.bits_per_channel());
      EXPECT_EQ(CHANNEL_LAYOUT_STEREO, config.channel_layout());
      EXPECT_EQ(44100, config.samples_per_second());
      EXPECT_TRUE(config.extra_data());
      EXPECT_GT(config.extra_data_size(), 0u);
    } else {
      EXPECT_FALSE(audio_stream);
    }

    scoped_refptr<DemuxerStream> video_stream =
        demuxer_->GetStream(DemuxerStream::VIDEO);
    if (has_video) {
      EXPECT_TRUE(video_stream);
    } else {
      EXPECT_FALSE(video_stream);
    }

    ShutdownDemuxer();
    demuxer_ = NULL;
  }
}

// Makes sure that Seek() reports an error if Shutdown()
// is called before the first cluster is passed to the demuxer.
TEST_F(ChunkDemuxerTest, TestShutdownBeforeFirstSeekCompletes) {
  ASSERT_TRUE(InitDemuxer(true, true, false));

  demuxer_->Seek(base::TimeDelta::FromSeconds(0),
                 NewExpectedStatusCB(PIPELINE_ERROR_ABORT));
}

// Test that Seek() completes successfully when the first cluster
// arrives.
TEST_F(ChunkDemuxerTest, TestAppendDataAfterSeek) {
  ASSERT_TRUE(InitDemuxer(true, true, false));

  InSequence s;

  EXPECT_CALL(*this, Checkpoint(1));

  demuxer_->Seek(base::TimeDelta::FromSeconds(0),
                 NewExpectedStatusCB(PIPELINE_OK));

  EXPECT_CALL(*this, Checkpoint(2));

  scoped_ptr<Cluster> cluster(GenerateCluster(0, 4));

  Checkpoint(1);

  ASSERT_TRUE(AppendData(cluster->data(), cluster->size()));

  Checkpoint(2);
}

// Test the case where a Seek() is requested while the parser
// is in the middle of cluster. This is to verify that the parser
// resets itself on seek and is in the right state when data from
// the new seek point arrives.
TEST_F(ChunkDemuxerTest, TestSeekWhileParsingCluster) {
  ASSERT_TRUE(InitDemuxer(true, true, false));

  scoped_refptr<DemuxerStream> audio =
      demuxer_->GetStream(DemuxerStream::AUDIO);
  scoped_refptr<DemuxerStream> video =
      demuxer_->GetStream(DemuxerStream::VIDEO);

  InSequence s;

  scoped_ptr<Cluster> cluster_a(GenerateCluster(0, 6));
  scoped_ptr<Cluster> cluster_b(GenerateCluster(5000, 6));

  // Append all but the last byte so that everything but
  // the last block can be parsed.
  ASSERT_TRUE(AppendData(cluster_a->data(), cluster_a->size() - 1));

  ExpectRead(audio, 0);
  ExpectRead(video, 0);
  ExpectRead(audio, kAudioBlockDuration);
  // Note: We skip trying to read a video buffer here because computing
  // the duration for this block relies on successfully parsing the last block
  // in the cluster the cluster.
  ExpectRead(audio, 2 * kAudioBlockDuration);

  demuxer_->StartWaitingForSeek();
  demuxer_->Seek(base::TimeDelta::FromSeconds(5),
                 NewExpectedStatusCB(PIPELINE_OK));


  // Append the new cluster and verify that only the blocks
  // in the new cluster are returned.
  ASSERT_TRUE(AppendData(cluster_b->data(), cluster_b->size()));
  GenerateExpectedReads(5000, 6, audio, video);
}

// Test the case where AppendData() is called before Init().
TEST_F(ChunkDemuxerTest, TestAppendDataBeforeInit) {
  scoped_array<uint8> info_tracks;
  int info_tracks_size = 0;
  CreateInfoTracks(true, true, false, &info_tracks, &info_tracks_size);

  EXPECT_FALSE(demuxer_->AppendData(kSourceId, info_tracks.get(),
                                    info_tracks_size));
}

// Make sure Read() callbacks are dispatched with the proper data.
TEST_F(ChunkDemuxerTest, TestRead) {
  ASSERT_TRUE(InitDemuxer(true, true, false));

  scoped_refptr<DemuxerStream> audio =
      demuxer_->GetStream(DemuxerStream::AUDIO);
  scoped_refptr<DemuxerStream> video =
      demuxer_->GetStream(DemuxerStream::VIDEO);

  bool audio_read_done = false;
  bool video_read_done = false;
  audio->Read(base::Bind(&OnReadDone,
                         base::TimeDelta::FromMilliseconds(0),
                         &audio_read_done));

  video->Read(base::Bind(&OnReadDone,
                         base::TimeDelta::FromMilliseconds(0),
                         &video_read_done));

  scoped_ptr<Cluster> cluster(GenerateCluster(0, 4));

  ASSERT_TRUE(AppendData(cluster->data(), cluster->size()));

  EXPECT_TRUE(audio_read_done);
  EXPECT_TRUE(video_read_done);
}

TEST_F(ChunkDemuxerTest, TestOutOfOrderClusters) {
  ASSERT_TRUE(InitDemuxer(true, true, false));

  scoped_ptr<Cluster> cluster_a(GenerateCluster(10, 4));

  ASSERT_TRUE(AppendData(cluster_a->data(), cluster_a->size()));

  // Cluster B starts before cluster_a and has data
  // that overlaps.
  scoped_ptr<Cluster> cluster_b(GenerateCluster(5, 4));

  // Make sure that AppendData() does not fail.
  ASSERT_TRUE(AppendData(cluster_b->data(), cluster_b->size()));

  // Verify that AppendData() can still accept more data.
  scoped_ptr<Cluster> cluster_c(GenerateCluster(45, 2));
  ASSERT_TRUE(demuxer_->AppendData(kSourceId, cluster_c->data(),
                                   cluster_c->size()));
}

TEST_F(ChunkDemuxerTest, TestNonMonotonicButAboveClusterTimecode) {
  ASSERT_TRUE(InitDemuxer(true, true, false));

  ClusterBuilder cb;

  // Test the case where block timecodes are not monotonically
  // increasing but stay above the cluster timecode.
  cb.SetClusterTimecode(5);
  AddSimpleBlock(&cb, kAudioTrackNum, 5);
  AddSimpleBlock(&cb, kVideoTrackNum, 10);
  AddSimpleBlock(&cb, kAudioTrackNum, 7);
  AddSimpleBlock(&cb, kVideoTrackNum, 15);
  scoped_ptr<Cluster> cluster_a(cb.Finish());

  EXPECT_CALL(host_, OnDemuxerError(PIPELINE_ERROR_DECODE));
  ASSERT_TRUE(AppendData(cluster_a->data(), cluster_a->size()));

  // Verify that AppendData() doesn't accept more data now.
  scoped_ptr<Cluster> cluster_b(GenerateCluster(20, 2));
  EXPECT_FALSE(demuxer_->AppendData(kSourceId, cluster_b->data(),
                                    cluster_b->size()));
}

TEST_F(ChunkDemuxerTest, TestBackwardsAndBeforeClusterTimecode) {
  ASSERT_TRUE(InitDemuxer(true, true, false));

  ClusterBuilder cb;

  // Test timecodes going backwards and including values less than the cluster
  // timecode.
  cb.SetClusterTimecode(5);
  AddSimpleBlock(&cb, kAudioTrackNum, 5);
  AddSimpleBlock(&cb, kVideoTrackNum, 5);
  AddSimpleBlock(&cb, kAudioTrackNum, 3);
  AddSimpleBlock(&cb, kVideoTrackNum, 3);
  scoped_ptr<Cluster> cluster_a(cb.Finish());

  EXPECT_CALL(host_, OnDemuxerError(PIPELINE_ERROR_DECODE));
  ASSERT_TRUE(AppendData(cluster_a->data(), cluster_a->size()));

  // Verify that AppendData() doesn't accept more data now.
  scoped_ptr<Cluster> cluster_b(GenerateCluster(6, 2));
  EXPECT_FALSE(demuxer_->AppendData(kSourceId, cluster_b->data(),
                                    cluster_b->size()));
}


TEST_F(ChunkDemuxerTest, TestPerStreamMonotonicallyIncreasingTimestamps) {
  ASSERT_TRUE(InitDemuxer(true, true, false));

  ClusterBuilder cb;

  // Test strict monotonic increasing timestamps on a per stream
  // basis.
  cb.SetClusterTimecode(5);
  AddSimpleBlock(&cb, kAudioTrackNum, 5);
  AddSimpleBlock(&cb, kVideoTrackNum, 5);
  AddSimpleBlock(&cb, kAudioTrackNum, 5);
  AddSimpleBlock(&cb, kVideoTrackNum, 7);
  scoped_ptr<Cluster> cluster(cb.Finish());

  EXPECT_CALL(host_, OnDemuxerError(PIPELINE_ERROR_DECODE));
  ASSERT_TRUE(AppendData(cluster->data(), cluster->size()));
}

TEST_F(ChunkDemuxerTest, TestMonotonicallyIncreasingTimestampsAcrossClusters) {
  ASSERT_TRUE(InitDemuxer(true, true, false));

  ClusterBuilder cb;

  // Test strict monotonic increasing timestamps on a per stream
  // basis across clusters.
  cb.SetClusterTimecode(5);
  AddSimpleBlock(&cb, kAudioTrackNum, 5);
  AddSimpleBlock(&cb, kVideoTrackNum, 5);
  scoped_ptr<Cluster> cluster_a(cb.Finish());

  ASSERT_TRUE(AppendData(cluster_a->data(), cluster_a->size()));

  cb.SetClusterTimecode(5);
  AddSimpleBlock(&cb, kAudioTrackNum, 5);
  AddSimpleBlock(&cb, kVideoTrackNum, 7);
  scoped_ptr<Cluster> cluster_b(cb.Finish());

  EXPECT_CALL(host_, OnDemuxerError(PIPELINE_ERROR_DECODE));
  ASSERT_TRUE(AppendData(cluster_b->data(), cluster_b->size()));

  // Verify that AppendData() doesn't accept more data now.
  scoped_ptr<Cluster> cluster_c(GenerateCluster(10, 2));
  EXPECT_FALSE(demuxer_->AppendData(kSourceId, cluster_c->data(),
                                    cluster_c->size()));
}

// Test the case where a cluster is passed to AppendData() before
// INFO & TRACKS data.
TEST_F(ChunkDemuxerTest, TestClusterBeforeInfoTracks) {
  EXPECT_CALL(*client_, DemuxerOpened(_));
  demuxer_->Initialize(
      &host_, NewExpectedStatusCB(DEMUXER_ERROR_COULD_NOT_OPEN));

  ASSERT_EQ(AddId(), ChunkDemuxer::kOk);

  scoped_ptr<Cluster> cluster(GenerateCluster(0, 1));

  ASSERT_TRUE(AppendData(cluster->data(), cluster->size()));
}

// Test cases where we get an EndOfStream() call during initialization.
TEST_F(ChunkDemuxerTest, TestEOSDuringInit) {
  EXPECT_CALL(*client_, DemuxerOpened(_));
  demuxer_->Initialize(
      &host_, NewExpectedStatusCB(DEMUXER_ERROR_COULD_NOT_OPEN));
  demuxer_->EndOfStream(PIPELINE_OK);
}

TEST_F(ChunkDemuxerTest, TestDecodeErrorEndOfStream) {
  ASSERT_TRUE(InitDemuxer(true, true, false));

  scoped_ptr<Cluster> cluster(GenerateCluster(0, 4));
  ASSERT_TRUE(AppendData(cluster->data(), cluster->size()));

  EXPECT_CALL(host_, OnDemuxerError(PIPELINE_ERROR_DECODE));
  demuxer_->EndOfStream(PIPELINE_ERROR_DECODE);
}

TEST_F(ChunkDemuxerTest, TestNetworkErrorEndOfStream) {
  ASSERT_TRUE(InitDemuxer(true, true, false));

  scoped_ptr<Cluster> cluster(GenerateCluster(0, 4));
  ASSERT_TRUE(AppendData(cluster->data(), cluster->size()));

  EXPECT_CALL(host_, OnDemuxerError(PIPELINE_ERROR_NETWORK));
  demuxer_->EndOfStream(PIPELINE_ERROR_NETWORK);
}

// Helper class to reduce duplicate code when testing end of stream
// Read() behavior.
class EndOfStreamHelper {
 public:
  EndOfStreamHelper(const scoped_refptr<Demuxer> demuxer)
      : demuxer_(demuxer),
        audio_read_done_(false),
        video_read_done_(false) {
  }

  // Request a read on the audio and video streams.
  void RequestReads() {
    EXPECT_FALSE(audio_read_done_);
    EXPECT_FALSE(video_read_done_);

    scoped_refptr<DemuxerStream> audio =
        demuxer_->GetStream(DemuxerStream::AUDIO);
    scoped_refptr<DemuxerStream> video =
        demuxer_->GetStream(DemuxerStream::VIDEO);

    audio->Read(base::Bind(&OnEndOfStreamReadDone,
                           &audio_read_done_));

    video->Read(base::Bind(&OnEndOfStreamReadDone,
                           &video_read_done_));
  }

  // Check to see if |audio_read_done_| and |video_read_done_| variables
  // match |expected|.
  void CheckIfReadDonesWereCalled(bool expected) {
    EXPECT_EQ(expected, audio_read_done_);
    EXPECT_EQ(expected, video_read_done_);
  }

 private:
  static void OnEndOfStreamReadDone(
      bool* called, const scoped_refptr<DecoderBuffer>& buffer) {
    EXPECT_TRUE(buffer->IsEndOfStream());
    *called = true;
  }

  scoped_refptr<Demuxer> demuxer_;
  bool audio_read_done_;
  bool video_read_done_;

  DISALLOW_COPY_AND_ASSIGN(EndOfStreamHelper);
};

// Make sure that all pending reads that we don't have media data for get an
// "end of stream" buffer when EndOfStream() is called.
TEST_F(ChunkDemuxerTest, TestEndOfStreamWithPendingReads) {
  ASSERT_TRUE(InitDemuxer(true, true, false));

  scoped_refptr<DemuxerStream> audio =
      demuxer_->GetStream(DemuxerStream::AUDIO);
  scoped_refptr<DemuxerStream> video =
      demuxer_->GetStream(DemuxerStream::VIDEO);

  bool audio_read_done_1 = false;
  bool video_read_done_1 = false;
  EndOfStreamHelper end_of_stream_helper_1(demuxer_);
  EndOfStreamHelper end_of_stream_helper_2(demuxer_);

  audio->Read(base::Bind(&OnReadDone,
                         base::TimeDelta::FromMilliseconds(0),
                         &audio_read_done_1));

  video->Read(base::Bind(&OnReadDone,
                         base::TimeDelta::FromMilliseconds(0),
                         &video_read_done_1));

  end_of_stream_helper_1.RequestReads();
  end_of_stream_helper_2.RequestReads();

  scoped_ptr<Cluster> cluster(GenerateCluster(0, 2));

  ASSERT_TRUE(AppendData(cluster->data(), cluster->size()));

  EXPECT_TRUE(audio_read_done_1);
  EXPECT_TRUE(video_read_done_1);
  end_of_stream_helper_1.CheckIfReadDonesWereCalled(false);
  end_of_stream_helper_2.CheckIfReadDonesWereCalled(false);

  demuxer_->EndOfStream(PIPELINE_OK);

  end_of_stream_helper_1.CheckIfReadDonesWereCalled(true);
  end_of_stream_helper_2.CheckIfReadDonesWereCalled(true);
}

// Make sure that all Read() calls after we get an EndOfStream()
// call return an "end of stream" buffer.
TEST_F(ChunkDemuxerTest, TestReadsAfterEndOfStream) {
  ASSERT_TRUE(InitDemuxer(true, true, false));

  scoped_refptr<DemuxerStream> audio =
      demuxer_->GetStream(DemuxerStream::AUDIO);
  scoped_refptr<DemuxerStream> video =
      demuxer_->GetStream(DemuxerStream::VIDEO);

  bool audio_read_done_1 = false;
  bool video_read_done_1 = false;
  EndOfStreamHelper end_of_stream_helper_1(demuxer_);
  EndOfStreamHelper end_of_stream_helper_2(demuxer_);
  EndOfStreamHelper end_of_stream_helper_3(demuxer_);

  audio->Read(base::Bind(&OnReadDone,
                         base::TimeDelta::FromMilliseconds(0),
                         &audio_read_done_1));

  video->Read(base::Bind(&OnReadDone,
                         base::TimeDelta::FromMilliseconds(0),
                         &video_read_done_1));

  end_of_stream_helper_1.RequestReads();

  scoped_ptr<Cluster> cluster(GenerateCluster(0, 2));

  ASSERT_TRUE(AppendData(cluster->data(), cluster->size()));

  EXPECT_TRUE(audio_read_done_1);
  EXPECT_TRUE(video_read_done_1);
  end_of_stream_helper_1.CheckIfReadDonesWereCalled(false);

  EXPECT_TRUE(demuxer_->EndOfStream(PIPELINE_OK));

  end_of_stream_helper_1.CheckIfReadDonesWereCalled(true);

  // Request a few more reads and make sure we immediately get
  // end of stream buffers.
  end_of_stream_helper_2.RequestReads();
  end_of_stream_helper_2.CheckIfReadDonesWereCalled(true);

  end_of_stream_helper_3.RequestReads();
  end_of_stream_helper_3.CheckIfReadDonesWereCalled(true);
}

// Make sure AppendData() will accept elements that span multiple calls.
TEST_F(ChunkDemuxerTest, TestAppendingInPieces) {

  EXPECT_CALL(*client_, DemuxerOpened(_));
  demuxer_->Initialize(
      &host_, CreateInitDoneCB(kDefaultDuration(), PIPELINE_OK));

  ASSERT_EQ(AddId(), ChunkDemuxer::kOk);

  scoped_array<uint8> info_tracks;
  int info_tracks_size = 0;
  CreateInfoTracks(true, true, false, &info_tracks, &info_tracks_size);

  scoped_ptr<Cluster> cluster_a(GenerateCluster(0, 4));
  scoped_ptr<Cluster> cluster_b(GenerateCluster(46, 66, 5));

  size_t buffer_size = info_tracks_size + cluster_a->size() + cluster_b->size();
  scoped_array<uint8> buffer(new uint8[buffer_size]);
  uint8* dst = buffer.get();
  memcpy(dst, info_tracks.get(), info_tracks_size);
  dst += info_tracks_size;

  memcpy(dst, cluster_a->data(), cluster_a->size());
  dst += cluster_a->size();

  memcpy(dst, cluster_b->data(), cluster_b->size());
  dst += cluster_b->size();

  ASSERT_TRUE(AppendDataInPieces(buffer.get(), buffer_size));

  scoped_refptr<DemuxerStream> audio =
      demuxer_->GetStream(DemuxerStream::AUDIO);
  scoped_refptr<DemuxerStream> video =
      demuxer_->GetStream(DemuxerStream::VIDEO);

  ASSERT_TRUE(audio);
  ASSERT_TRUE(video);

  GenerateExpectedReads(0, 9, audio, video);
}

TEST_F(ChunkDemuxerTest, TestWebMFile_AudioAndVideo) {
  struct BufferTimestamps buffer_timestamps[] = {
    {0, 0},
    {33, 3},
    {67, 6},
    {100, 9},
    {133, 12},
    {kSkip, kSkip},
  };

  ASSERT_TRUE(ParseWebMFile("bear-320x240.webm", buffer_timestamps,
                            base::TimeDelta::FromMilliseconds(2744)));
}

TEST_F(ChunkDemuxerTest, TestWebMFile_LiveAudioAndVideo) {
  struct BufferTimestamps buffer_timestamps[] = {
    {0, 0},
    {33, 3},
    {67, 6},
    {100, 9},
    {133, 12},
    {kSkip, kSkip},
  };

  ASSERT_TRUE(ParseWebMFile("bear-320x240-live.webm", buffer_timestamps,
                            kInfiniteDuration()));
}

TEST_F(ChunkDemuxerTest, TestWebMFile_AudioOnly) {
  struct BufferTimestamps buffer_timestamps[] = {
    {kSkip, 0},
    {kSkip, 3},
    {kSkip, 6},
    {kSkip, 9},
    {kSkip, 12},
    {kSkip, kSkip},
  };

  ASSERT_TRUE(ParseWebMFile("bear-320x240-audio-only.webm", buffer_timestamps,
                            base::TimeDelta::FromMilliseconds(2744)));
}

TEST_F(ChunkDemuxerTest, TestWebMFile_VideoOnly) {
  struct BufferTimestamps buffer_timestamps[] = {
    {0, kSkip},
    {33, kSkip},
    {67, kSkip},
    {100, kSkip},
    {133, kSkip},
    {kSkip, kSkip},
  };

  ASSERT_TRUE(ParseWebMFile("bear-320x240-video-only.webm", buffer_timestamps,
                            base::TimeDelta::FromMilliseconds(2703)));
}

// Verify that we output buffers before the entire cluster has been parsed.
TEST_F(ChunkDemuxerTest, TestIncrementalClusterParsing) {
  ASSERT_TRUE(InitDemuxer(true, true, false));

  scoped_ptr<Cluster> cluster(GenerateCluster(0, 6));
  scoped_refptr<DemuxerStream> audio =
      demuxer_->GetStream(DemuxerStream::AUDIO);
  scoped_refptr<DemuxerStream> video =
      demuxer_->GetStream(DemuxerStream::VIDEO);

  bool audio_read_done = false;
  bool video_read_done = false;
  audio->Read(base::Bind(&OnReadDone,
                         base::TimeDelta::FromMilliseconds(0),
                         &audio_read_done));

  video->Read(base::Bind(&OnReadDone,
                         base::TimeDelta::FromMilliseconds(0),
                         &video_read_done));

  // Make sure the reads haven't completed yet.
  EXPECT_FALSE(audio_read_done);
  EXPECT_FALSE(video_read_done);

  // Append data one byte at a time until the audio read completes.
  int i = 0;
  for (; i < cluster->size() && !audio_read_done; ++i) {
    ASSERT_TRUE(AppendData(cluster->data() + i, 1));
  }

  EXPECT_TRUE(audio_read_done);
  EXPECT_FALSE(video_read_done);
  EXPECT_GT(i, 0);
  EXPECT_LT(i, cluster->size());

  // Append data one byte at a time until the video read completes.
  for (; i < cluster->size() && !video_read_done; ++i) {
    ASSERT_TRUE(AppendData(cluster->data() + i, 1));
  }

  EXPECT_TRUE(video_read_done);
  EXPECT_LT(i, cluster->size());

  audio_read_done = false;
  video_read_done = false;
  audio->Read(base::Bind(&OnReadDone,
                         base::TimeDelta::FromMilliseconds(23),
                         &audio_read_done));

  video->Read(base::Bind(&OnReadDone,
                         base::TimeDelta::FromMilliseconds(33),
                         &video_read_done));

  // Make sure the reads haven't completed yet.
  EXPECT_FALSE(audio_read_done);
  EXPECT_FALSE(video_read_done);

  // Append the remaining data.
  ASSERT_LT(i, cluster->size());
  ASSERT_TRUE(AppendData(cluster->data() + i, cluster->size() - i));

  EXPECT_TRUE(audio_read_done);
  EXPECT_TRUE(video_read_done);
}


TEST_F(ChunkDemuxerTest, TestParseErrorDuringInit) {
  EXPECT_CALL(host_, OnDemuxerError(PIPELINE_ERROR_DECODE));

  EXPECT_CALL(*client_, DemuxerOpened(_));
  demuxer_->Initialize(
      &host_, CreateInitDoneCB(kDefaultDuration(), PIPELINE_OK));

  ASSERT_EQ(AddId(), ChunkDemuxer::kOk);

  ASSERT_TRUE(AppendInfoTracks(true, true, false));

  uint8 tmp = 0;
  ASSERT_TRUE(demuxer_->AppendData(kSourceId, &tmp, 1));
}

TEST_F(ChunkDemuxerTest, TestAVHeadersWithAudioOnlyType) {
  EXPECT_CALL(*client_, DemuxerOpened(_));
  demuxer_->Initialize(
      &host_, CreateInitDoneCB(kDefaultDuration(),
                               DEMUXER_ERROR_COULD_NOT_OPEN));

  std::vector<std::string> codecs(1);
  codecs[0] = "vorbis";
  ASSERT_EQ(demuxer_->AddId(kSourceId, "audio/webm", codecs),
            ChunkDemuxer::kOk);

  ASSERT_TRUE(AppendInfoTracks(true, true, false));
}

TEST_F(ChunkDemuxerTest, TestAVHeadersWithVideoOnlyType) {
  EXPECT_CALL(*client_, DemuxerOpened(_));
  demuxer_->Initialize(
      &host_, CreateInitDoneCB(kDefaultDuration(),
                               DEMUXER_ERROR_COULD_NOT_OPEN));

  std::vector<std::string> codecs(1);
  codecs[0] = "vp8";
  ASSERT_EQ(demuxer_->AddId(kSourceId, "video/webm", codecs),
            ChunkDemuxer::kOk);

  ASSERT_TRUE(AppendInfoTracks(true, true, false));
}

}  // namespace media
