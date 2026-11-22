// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2019 ESPHome
// Modified by Brett, 2026-11-01. Changes from ESPHome 2026.10.1 are listed in components/README.md.

#include <algorithm>
#include <array>
#include <memory>

#include "pn532.h"
#include "esphome/core/log.h"

namespace esphome::pn532 {

static const char *const TAG = "pn532.mifare_ultralight";

std::unique_ptr<nfc::NfcTag> PN532::read_mifare_ultralight_tag_(nfc::NfcTagUid &uid) {
  UltralightReadBuffer data;
  // pages 3 to 6 contain various info we are interested in -- do one read to grab it all
  this->retry_uid_ = &uid;
  if (!this->read_mifare_ultralight_bytes_(3, nfc::MIFARE_ULTRALIGHT_PAGE_SIZE * nfc::MIFARE_ULTRALIGHT_READ_SIZE,
                                           data)) {
    ESP_LOGW(TAG, "Error reading pages 3-6");
    this->last_read_failed_ = true;
    this->retry_uid_ = nullptr;
    return make_unique<nfc::NfcTag>(uid, nfc::NFC_FORUM_TYPE_2);
  }

  this->retry_uid_ = nullptr;  // re-armed below for the NDEF body read
  if (!this->is_mifare_ultralight_formatted_(data)) {
    ESP_LOGW(TAG, "Not NDEF formatted");
    return make_unique<nfc::NfcTag>(uid, nfc::NFC_FORUM_TYPE_2);
  }

  uint8_t message_length;
  uint8_t message_start_index;
  if (!this->find_mifare_ultralight_ndef_(data, message_length, message_start_index)) {
    ESP_LOGW(TAG, "Couldn't find NDEF message");
    return make_unique<nfc::NfcTag>(uid, nfc::NFC_FORUM_TYPE_2);
  }
  ESP_LOGVV(TAG, "NDEF message length: %u, start: %u", message_length, message_start_index);

  if (message_length == 0) {
    return make_unique<nfc::NfcTag>(uid, nfc::NFC_FORUM_TYPE_2);
  }
  // we already read pages 3-6 earlier -- pick up where we left off so we're not re-reading pages
  const uint8_t read_length = message_length + message_start_index > 12 ? message_length + message_start_index - 12 : 0;
  if (read_length) {
    this->retry_uid_ = &uid;
    const bool ok = read_mifare_ultralight_bytes_(nfc::MIFARE_ULTRALIGHT_DATA_START_PAGE + 3, read_length, data);
    this->retry_uid_ = nullptr;
    if (!ok) {
      ESP_LOGW(TAG, "Error reading tag data (NDEF length %u)", message_length);
      this->last_read_failed_ = true;
      return make_unique<nfc::NfcTag>(uid, nfc::NFC_FORUM_TYPE_2);
    }
  }
  // skip page 3 as well as any bytes ahead of message_start_index
  const size_t skip = message_start_index + nfc::MIFARE_ULTRALIGHT_PAGE_SIZE;
  if (skip >= data.size()) {
    return make_unique<nfc::NfcTag>(uid, nfc::NFC_FORUM_TYPE_2);
  }

  return make_unique<nfc::NfcTag>(uid, nfc::NFC_FORUM_TYPE_2,
                                  make_unique<nfc::NdefMessage>(std::span<const uint8_t>(data).subspan(skip)));
}

bool PN532::read_mifare_ultralight_bytes_(uint8_t start_page, uint16_t num_bytes, UltralightReadBuffer &data) {
  MifareReadData chunk;

  for (uint8_t i = 0; i * MIFARE_READ_SIZE < num_bytes; i++) {
    // a READ returns 4 pages (16 bytes)
    const uint8_t page = uint8_t(i * nfc::MIFARE_ULTRALIGHT_READ_SIZE + start_page);
    bool ok = this->mifare_read_(page, chunk);
    // After an RF error the tag drops back to IDLE and ignores further READs, so re-select it before retrying.
    for (uint8_t r = 0; !ok && this->retry_uid_ != nullptr && r < this->page_retries_; r++) {
      this->stat_page_retries_++;
      const bool reselected = this->reactivate_(*this->retry_uid_);
      ESP_LOGD(TAG, "READ page 0x%02X failed; retry %u/%u (re-select %s)", page, r + 1, this->page_retries_,
               reselected ? "ok" : "failed");
      if (reselected)
        ok = this->mifare_read_(page, chunk);
    }
    if (!ok) {
      return false;
    }
    // keep only the bytes still wanted from this read
    const uint16_t remaining = num_bytes - i * MIFARE_READ_SIZE;
    const size_t count = std::min<size_t>(MIFARE_READ_SIZE, remaining);
    for (const uint8_t byte : std::span<const uint8_t>(chunk).subspan(0, count)) {
      data.push_back(byte);
    }
  }

  char data_buf[nfc::FORMAT_BYTES_BUFFER_SIZE];
  ESP_LOGVV(TAG, "Data read: %s", nfc::format_bytes_to(data_buf, data));

  return true;
}

bool PN532::is_mifare_ultralight_formatted_(const std::span<const uint8_t> page_3_to_6) {
  const uint8_t p4_offset = nfc::MIFARE_ULTRALIGHT_PAGE_SIZE;  // page 4 will begin 4 bytes into the vector

  return (page_3_to_6.size() > p4_offset + 3) &&
         ((page_3_to_6[p4_offset + 0] != 0xFF) || (page_3_to_6[p4_offset + 1] != 0xFF) ||
          (page_3_to_6[p4_offset + 2] != 0xFF) || (page_3_to_6[p4_offset + 3] != 0xFF));
}

uint16_t PN532::read_mifare_ultralight_capacity_() {
  UltralightReadBuffer data;
  if (this->read_mifare_ultralight_bytes_(3, nfc::MIFARE_ULTRALIGHT_PAGE_SIZE, data) && data.size() > 2) {
    ESP_LOGV(TAG, "Tag capacity is %u bytes", data[2] * 8U);
    return data[2] * 8U;
  }
  return 0;
}

bool PN532::find_mifare_ultralight_ndef_(const std::span<const uint8_t> page_3_to_6, uint8_t &message_length,
                                         uint8_t &message_start_index) {
  const uint8_t p4_offset = nfc::MIFARE_ULTRALIGHT_PAGE_SIZE;  // page 4 will begin 4 bytes into the vector

  if (!(page_3_to_6.size() > p4_offset + 6)) {
    return false;
  }

  if (page_3_to_6[p4_offset + 0] == 0x03) {
    message_length = page_3_to_6[p4_offset + 1];
    message_start_index = 2;
    return true;
  } else if (page_3_to_6[p4_offset + 5] == 0x03) {
    message_length = page_3_to_6[p4_offset + 6];
    message_start_index = 7;
    return true;
  }
  return false;
}

bool PN532::write_mifare_ultralight_tag_(nfc::NfcTagUid &uid, nfc::NdefMessage *message) {
  uint32_t capacity = this->read_mifare_ultralight_capacity_();

  const auto encoded = message->encode();
  const uint32_t buffer_length = nfc::get_mifare_ultralight_buffer_size(encoded.size());

  if (buffer_length > capacity) {
    ESP_LOGE(TAG, "Message length exceeds tag capacity %" PRIu32 " > %" PRIu32, buffer_length, capacity);
    return false;
  }

  FixedVector<uint8_t> buffer;
  nfc::fill_ndef_tlv(encoded, buffer_length, buffer);

  uint32_t index = 0;
  uint8_t current_page = nfc::MIFARE_ULTRALIGHT_DATA_START_PAGE;

  while (index < buffer_length) {
    if (!this->write_mifare_ultralight_page_(
            current_page, std::span<const uint8_t>(&buffer[index], nfc::MIFARE_ULTRALIGHT_PAGE_SIZE))) {
      return false;
    }
    index += nfc::MIFARE_ULTRALIGHT_PAGE_SIZE;
    current_page++;
  }
  return true;
}

bool PN532::clean_mifare_ultralight_() {
  uint32_t capacity = this->read_mifare_ultralight_capacity_();
  uint8_t pages = (capacity / nfc::MIFARE_ULTRALIGHT_PAGE_SIZE) + nfc::MIFARE_ULTRALIGHT_DATA_START_PAGE;

  static constexpr std::array<uint8_t, nfc::MIFARE_ULTRALIGHT_PAGE_SIZE> BLANK_DATA = {0x00, 0x00, 0x00, 0x00};

  for (int i = nfc::MIFARE_ULTRALIGHT_DATA_START_PAGE; i < pages; i++) {
    if (!this->write_mifare_ultralight_page_(i, BLANK_DATA)) {
      return false;
    }
  }
  return true;
}

bool PN532::write_mifare_ultralight_page_(uint8_t page_num, const std::span<const uint8_t> write_data) {
  StaticVector<uint8_t, 4 + nfc::MIFARE_ULTRALIGHT_PAGE_SIZE> cmd = {
      PN532_COMMAND_INDATAEXCHANGE,
      0x01,  // One card
      nfc::MIFARE_CMD_WRITE_ULTRALIGHT,
      page_num,
  };
  for (const uint8_t byte : write_data) {
    cmd.push_back(byte);
  }

  PN532Frame response;
  if (!this->in_data_exchange_(cmd, response)) {
    ESP_LOGE(TAG, "Error writing page %u", page_num);
    return false;
  }

  return true;
}

}  // namespace esphome::pn532
