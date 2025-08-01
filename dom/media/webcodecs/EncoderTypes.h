/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_EncoderTypes_h
#define mozilla_dom_EncoderTypes_h

#include "mozilla/Maybe.h"
#include "mozilla/dom/EncodedVideoChunk.h"
#include "mozilla/dom/MediaRecorderBinding.h"
#include "mozilla/dom/VideoEncoderBinding.h"
#include "mozilla/dom/AudioEncoderBinding.h"
#include "mozilla/dom/VideoFrame.h"
#include "mozilla/dom/VideoFrameBinding.h"
#include "nsStringFwd.h"
#include "nsTLiteralString.h"
#include "VideoDecoder.h"

namespace mozilla {

class TrackInfo;
class MediaByteBuffer;

namespace dom {

class AudioEncoderConfigInternal {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(AudioEncoderConfigInternal);
  explicit AudioEncoderConfigInternal(const AudioEncoderConfig& aConfig);
  explicit AudioEncoderConfigInternal(
      const AudioEncoderConfigInternal& aConfig);

  void SetSpecific(const EncoderConfig::CodecSpecific& aSpecific);

  nsCString ToString() const;

  bool Equals(const AudioEncoderConfigInternal& aOther) const;
  bool CanReconfigure(const AudioEncoderConfigInternal& aOther) const;

  // Returns an EncoderConfig struct with as many filled members as
  // possible.
  EncoderConfig ToEncoderConfig() const;

  already_AddRefed<WebCodecsConfigurationChangeList> Diff(
      const AudioEncoderConfigInternal& aOther) const;

  nsString mCodec;
  Maybe<uint32_t> mSampleRate;
  Maybe<uint32_t> mNumberOfChannels;
  Maybe<uint32_t> mBitrate;
  BitrateMode mBitrateMode;
  EncoderConfig::CodecSpecific mSpecific{void_t{}};

 private:
  AudioEncoderConfigInternal(const nsAString& aCodec,
                             Maybe<uint32_t> aSampleRate,
                             Maybe<uint32_t> aNumberOfChannels,
                             Maybe<uint32_t> aBitRate,
                             BitrateMode aBitratemode);
  ~AudioEncoderConfigInternal() = default;
};

class AudioEncoderTraits {
 public:
  static constexpr nsLiteralCString Name = "AudioEncoder"_ns;
  using ConfigType = AudioEncoderConfig;
  using ConfigTypeInternal = AudioEncoderConfigInternal;
  using InputType = dom::AudioData;
  using OutputConfigType = mozilla::dom::AudioDecoderConfig;
  using InputTypeInternal = mozilla::AudioData;
  using OutputType = EncodedAudioChunk;
  using OutputCallbackType = EncodedAudioChunkOutputCallback;
  using MetadataType = EncodedAudioChunkMetadata;

  static bool IsSupported(const ConfigTypeInternal& aConfig);
  static Result<UniquePtr<TrackInfo>, nsresult> CreateTrackInfo(
      const ConfigTypeInternal& aConfig);
  static bool Validate(const ConfigType& aConfig, nsCString& aErrorMessage);
  static RefPtr<ConfigTypeInternal> CreateConfigInternal(
      const ConfigType& aConfig);
  static RefPtr<InputTypeInternal> CreateInputInternal(
      const InputType& aInput, const VideoEncoderEncodeOptions& aOptions);
  static already_AddRefed<OutputConfigType> EncoderConfigToDecoderConfig(
      nsIGlobalObject* aGlobal, const RefPtr<MediaRawData>& aData,
      const ConfigTypeInternal& mOutputConfig);
};

class VideoEncoderConfigInternal {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(VideoEncoderConfigInternal);
  explicit VideoEncoderConfigInternal(const VideoEncoderConfig& aConfig);
  explicit VideoEncoderConfigInternal(
      const VideoEncoderConfigInternal& aConfig);

  // Returns an EncoderConfig struct with as many filled members as
  // possible.
  // TODO: handle codec specific things
  EncoderConfig ToEncoderConfig() const;

  bool Equals(const VideoEncoderConfigInternal& aOther) const;
  bool CanReconfigure(const VideoEncoderConfigInternal& aOther) const;
  already_AddRefed<WebCodecsConfigurationChangeList> Diff(
      const VideoEncoderConfigInternal& aOther) const;
  nsCString ToString() const;

  nsString mCodec;
  uint32_t mWidth;
  uint32_t mHeight;
  Maybe<uint32_t> mDisplayWidth;
  Maybe<uint32_t> mDisplayHeight;
  Maybe<uint32_t> mBitrate;
  Maybe<double> mFramerate;
  HardwareAcceleration mHardwareAcceleration;
  AlphaOption mAlpha;
  Maybe<nsString> mScalabilityMode;
  VideoEncoderBitrateMode mBitrateMode;
  LatencyMode mLatencyMode;
  Maybe<nsString> mContentHint;
  Maybe<AvcEncoderConfig> mAvc;

 private:
  VideoEncoderConfigInternal(
      const nsAString& aCodec, uint32_t aWidth, uint32_t aHeight,
      Maybe<uint32_t>&& aDisplayWidth, Maybe<uint32_t>&& aDisplayHeight,
      Maybe<uint32_t>&& aBitrate, Maybe<double>&& aFramerate,
      const HardwareAcceleration& aHardwareAcceleration,
      const AlphaOption& aAlpha, Maybe<nsString>&& aScalabilityMode,
      const VideoEncoderBitrateMode& aBitrateMode,
      const LatencyMode& aLatencyMode, Maybe<nsString>&& aContentHint);

  ~VideoEncoderConfigInternal() = default;
};

class VideoEncoderTraits {
 public:
  static constexpr nsLiteralCString Name = "VideoEncoder"_ns;
  using ConfigType = VideoEncoderConfig;
  using ConfigTypeInternal = VideoEncoderConfigInternal;
  using InputType = dom::VideoFrame;
  using InputTypeInternal = mozilla::VideoData;
  using OutputConfigType = mozilla::dom::VideoDecoderConfig;
  using OutputType = EncodedVideoChunk;
  using OutputCallbackType = EncodedVideoChunkOutputCallback;
  using MetadataType = EncodedVideoChunkMetadata;

  static bool IsSupported(const ConfigTypeInternal& aConfig);
  static bool CanEncodeVideo(const ConfigTypeInternal& aConfig);
  static bool Validate(const ConfigType& aConfig, nsCString& aErrorMessage);
  static RefPtr<ConfigTypeInternal> CreateConfigInternal(
      const ConfigType& aConfig);
  static RefPtr<InputTypeInternal> CreateInputInternal(
      const InputType& aInput, const VideoEncoderEncodeOptions& aOptions);
};

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_EncoderTypes_h
