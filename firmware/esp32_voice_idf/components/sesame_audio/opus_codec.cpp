#include "sesame_audio/opus_codec.h"

#ifdef ESP_PLATFORM

#include "decoder/esp_audio_dec.h"
#include "decoder/impl/esp_opus_dec.h"
#include "encoder/esp_audio_enc.h"
#include "encoder/impl/esp_opus_enc.h"

namespace sesame::audio {
namespace {

esp_err_t codec_error(esp_audio_err_t result) {
  return result == ESP_AUDIO_ERR_OK ? ESP_OK : ESP_FAIL;
}

}  // namespace

OpusCodec::~OpusCodec() { shutdown(); }

esp_err_t OpusCodec::initialize() {
  if (initialized()) {
    return ESP_OK;
  }
  shutdown();

  esp_opus_enc_config_t encoder_config = ESP_OPUS_ENC_CONFIG_DEFAULT();
  encoder_config.sample_rate = kSampleRateHz;
  encoder_config.channel = 1;
  encoder_config.bits_per_sample = 16;
  encoder_config.bitrate = 24000;
  encoder_config.frame_duration = ESP_OPUS_ENC_FRAME_DURATION_20_MS;
  encoder_config.application_mode = ESP_OPUS_ENC_APPLICATION_VOIP;
  encoder_config.complexity = 1;
  encoder_config.enable_fec = false;
  encoder_config.enable_dtx = false;
  encoder_config.enable_vbr = true;

  esp_audio_err_t result = esp_opus_enc_open(
      &encoder_config, sizeof(encoder_config), &encoder_);
  if (result != ESP_AUDIO_ERR_OK) {
    shutdown();
    return codec_error(result);
  }
  result = esp_opus_enc_get_frame_size(
      encoder_, &encoder_input_bytes_, &encoder_output_bytes_);
  if (result != ESP_AUDIO_ERR_OK ||
      encoder_input_bytes_ != static_cast<int>(kPcmBytesPerPacket) ||
      encoder_output_bytes_ <= 0 ||
      encoder_output_bytes_ > static_cast<int>(kMaxPacketBytes)) {
    shutdown();
    return ESP_ERR_INVALID_SIZE;
  }

  esp_opus_dec_cfg_t decoder_config = ESP_OPUS_DEC_CONFIG_DEFAULT();
  decoder_config.sample_rate = kSampleRateHz;
  decoder_config.channel = 1;
  decoder_config.frame_duration = ESP_OPUS_DEC_FRAME_DURATION_20_MS;
  decoder_config.self_delimited = false;
  result = esp_opus_dec_open(
      &decoder_config, sizeof(decoder_config), &decoder_);
  if (result != ESP_AUDIO_ERR_OK) {
    shutdown();
    return codec_error(result);
  }
  return ESP_OK;
}

void OpusCodec::shutdown() {
  if (encoder_ != nullptr) {
    esp_opus_enc_close(encoder_);
    encoder_ = nullptr;
  }
  if (decoder_ != nullptr) {
    esp_opus_dec_close(decoder_);
    decoder_ = nullptr;
  }
  encoder_input_bytes_ = 0;
  encoder_output_bytes_ = 0;
}

esp_err_t OpusCodec::reset() {
  if (!initialized()) {
    return ESP_ERR_INVALID_STATE;
  }
  if (esp_opus_enc_reset(encoder_) != ESP_AUDIO_ERR_OK ||
      esp_opus_dec_reset(decoder_) != ESP_AUDIO_ERR_OK) {
    return ESP_FAIL;
  }
  return ESP_OK;
}

esp_err_t OpusCodec::encode(const int16_t* pcm, size_t samples,
                            uint8_t* packet, size_t packet_capacity,
                            size_t* packet_size) {
  if (packet_size != nullptr) {
    *packet_size = 0;
  }
  if (!initialized()) {
    return ESP_ERR_INVALID_STATE;
  }
  if (pcm == nullptr || packet == nullptr || packet_size == nullptr ||
      samples != kPcmSamplesPerPacket ||
      packet_capacity < static_cast<size_t>(encoder_output_bytes_)) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_audio_enc_in_frame_t input{
      .buffer = reinterpret_cast<uint8_t*>(const_cast<int16_t*>(pcm)),
      .len = static_cast<uint32_t>(kPcmBytesPerPacket),
  };
  esp_audio_enc_out_frame_t output{
      .buffer = packet,
      .len = static_cast<uint32_t>(packet_capacity),
      .encoded_bytes = 0,
      .pts = 0,
  };
  const esp_audio_err_t result =
      esp_opus_enc_process(encoder_, &input, &output);
  if (result != ESP_AUDIO_ERR_OK) {
    return codec_error(result);
  }
  if (!is_valid_packet_size(output.encoded_bytes)) {
    return ESP_ERR_INVALID_SIZE;
  }
  *packet_size = output.encoded_bytes;
  return ESP_OK;
}

esp_err_t OpusCodec::decode(const uint8_t* packet, size_t packet_size,
                            int16_t* pcm, size_t pcm_capacity_samples,
                            size_t* decoded_samples) {
  if (decoded_samples != nullptr) {
    *decoded_samples = 0;
  }
  if (!initialized()) {
    return ESP_ERR_INVALID_STATE;
  }
  if (packet == nullptr || pcm == nullptr || decoded_samples == nullptr ||
      !is_valid_packet_size(packet_size) ||
      pcm_capacity_samples < kPcmSamplesPerPacket) {
    return ESP_ERR_INVALID_ARG;
  }

  esp_audio_dec_in_raw_t input{
      .buffer = const_cast<uint8_t*>(packet),
      .len = static_cast<uint32_t>(packet_size),
      .consumed = 0,
      .frame_recover = ESP_AUDIO_DEC_RECOVERY_NONE,
  };
  esp_audio_dec_out_frame_t output{
      .buffer = reinterpret_cast<uint8_t*>(pcm),
      .len = static_cast<uint32_t>(pcm_capacity_samples * sizeof(int16_t)),
      .needed_size = 0,
      .decoded_size = 0,
  };
  esp_audio_dec_info_t info{};
  const esp_audio_err_t result =
      esp_opus_dec_decode(decoder_, &input, &output, &info);
  if (result != ESP_AUDIO_ERR_OK) {
    return codec_error(result);
  }
  if (output.decoded_size != kPcmBytesPerPacket) {
    return ESP_ERR_INVALID_SIZE;
  }
  *decoded_samples = output.decoded_size / sizeof(int16_t);
  return ESP_OK;
}

}  // namespace sesame::audio

#endif
