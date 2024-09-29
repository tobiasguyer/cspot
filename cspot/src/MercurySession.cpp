#include "MercurySession.h"

#include <string.h>     // for memcpy
#include <memory>       // for shared_ptr
#include <mutex>        // for scoped_lock
#include <stdexcept>    // for runtime_error
#include <type_traits>  // for remove_extent_t, __underlying_type_impl<>:...
#include <utility>      // for pair
#ifndef _WIN32
#include <arpa/inet.h>  // for htons, ntohs, htonl, ntohl
#endif
#include "BellLogger.h"         // for AbstractLogger
#include "BellTask.h"           // for Task
#include "BellUtils.h"          // for BELL_SLEEP_MS
#include "Logger.h"             // for CSPOT_LOG
#include "NanoPBHelper.h"       // for pbPutString, pbDecode, pbEncode
#include "PlainConnection.h"    // for PlainConnection
#include "ShannonConnection.h"  // for ShannonConnection
#include "TimeProvider.h"       // for TimeProvider
#include "Utils.h"              // for extract, pack, hton64

using namespace cspot;

MercurySession::MercurySession(std::shared_ptr<TimeProvider> timeProvider)
    : bell::Task("mercury_dispatcher", 32 * 1024, 3, 1) {
  this->timeProvider = timeProvider;
}

MercurySession::~MercurySession() {
  std::scoped_lock lock(this->isRunningMutex);
}

void MercurySession::runTask() {
  isRunning = true;
  std::scoped_lock lock(this->isRunningMutex);

  this->executeEstabilishedCallback = true;
  while (isRunning) {
    cspot::Packet packet = {};
    try {
      packet = shanConn->recvPacket();
      CSPOT_LOG(info, "Received packet, command: %d", packet.command);

      if (static_cast<RequestType>(packet.command) == RequestType::PING) {
        timeProvider->syncWithPingPacket(packet.data);

        this->lastPingTimestamp = timeProvider->getSyncedTimestamp();
        this->shanConn->sendPacket(0x49, packet.data);
      } else {
        this->packetQueue.push(packet);
      }
    } catch (const std::runtime_error& e) {
      CSPOT_LOG(error, "Error while receiving packet: %s", e.what());
      connection_lost = true;
      failAllPending();

      if (!isRunning)
        return;

      reconnect();
      connection_lost = false;
      continue;
    }
  }
}

void MercurySession::reconnect() {
  isReconnecting = true;

  try {
    this->conn = nullptr;
    this->shanConn = nullptr;
    this->partials.clear();

    this->connectWithRandomAp();
    this->authenticate(this->authBlob);

    CSPOT_LOG(info, "Reconnection successful");

    BELL_SLEEP_MS(100);

    lastPingTimestamp = timeProvider->getSyncedTimestamp();
    isReconnecting = false;

    this->executeEstabilishedCallback = true;
  } catch (...) {
    CSPOT_LOG(error, "Cannot reconnect, will retry in 5s");
    BELL_SLEEP_MS(5000);

    if (isRunning) {
      return reconnect();
    }
  }
}

void MercurySession::setConnectedHandler(
    ConnectionEstabilishedCallback callback) {
  this->connectionReadyCallback = callback;
}

bool MercurySession::triggerTimeout() {
  if (!isRunning)
    return true;
  auto currentTimestamp = timeProvider->getSyncedTimestamp();

  if (currentTimestamp - this->lastPingTimestamp > PING_TIMEOUT_MS) {
    CSPOT_LOG(debug, "Reconnection required, no ping received");
    return true;
  }

  return false;
}

void MercurySession::unregister(uint64_t sequenceId) {
  auto callback = this->callbacks.find(sequenceId);

  if (callback != this->callbacks.end()) {
    this->callbacks.erase(callback);
  }
}

void MercurySession::unregisterAudioKey(uint32_t sequenceId) {
  auto callback = this->audioKeyCallbacks.find(sequenceId);

  if (callback != this->audioKeyCallbacks.end()) {
    this->audioKeyCallbacks.erase(callback);
  }
}

void MercurySession::disconnect() {
  CSPOT_LOG(info, "Disconnecting mercury session");
  this->isRunning = false;
  conn->close();
  std::scoped_lock lock(this->isRunningMutex);
}

std::string MercurySession::getCountryCode() {
  return this->countryCode;
}

void MercurySession::handlePacket() {
  Packet packet = {};
  if (connection_lost)
    return;
  this->packetQueue.wtpop(packet, 200);

  if (executeEstabilishedCallback && this->connectionReadyCallback != nullptr) {
    executeEstabilishedCallback = false;
    this->connectionReadyCallback();
  }

  switch (static_cast<RequestType>(packet.command)) {
    case RequestType::COUNTRY_CODE_RESPONSE: {
      this->countryCode = std::string();
      this->countryCode.resize(2);
      memcpy(this->countryCode.data(), packet.data.data(), 2);
      CSPOT_LOG(debug, "Received country code %s", this->countryCode.c_str());
      break;
    }
    case RequestType::AUDIO_KEY_FAILURE_RESPONSE:
    case RequestType::AUDIO_KEY_SUCCESS_RESPONSE: {
      // this->lastRequestTimestamp = -1;

      // First four bytes mark the sequence id
      auto seqId = ntohl(extract<uint32_t>(packet.data, 0));

      if (this->audioKeyCallbacks.count(seqId) > 0) {
        auto success = static_cast<RequestType>(packet.command) ==
                       RequestType::AUDIO_KEY_SUCCESS_RESPONSE;
        this->audioKeyCallbacks[seqId](success, packet.data);
      }

      break;
    }
    case RequestType::SEND:
    case RequestType::SUB:
    case RequestType::UNSUB: {
      CSPOT_LOG(debug, "Received mercury packet");
      auto response = this->decodeResponse(packet.data);
      if (!response.fail) {
        if (response.sequenceId >= 0) {
          if (this->callbacks.count(response.sequenceId)) {
            this->callbacks[response.sequenceId](response);
            this->callbacks.erase(this->callbacks.find(response.sequenceId));
          }
        }
        pb_release(Header_fields, &response.mercuryHeader);
      }
      break;
    }
    case RequestType::SUBRES: {
      auto response = decodeResponse(packet.data);
      if (!response.fail) {
        std::string uri(response.mercuryHeader.uri);
        for (auto& it : this->subscriptions) {
          if (uri.find(it.first) != std::string::npos) {
            it.second(response);
            break;  // Exit loop once subscription is found
          }
        }
        pb_release(Header_fields, &response.mercuryHeader);
      }
      break;
    }
    default:
      break;
  }
}

void MercurySession::failAllPending() {
  Response response = {};
  response.fail = true;

  // Fail all callbacks
  for (auto& it : this->callbacks) {
    it.second(response);
  }

  // Fail all subscriptions
  for (auto& it : this->subscriptions) {
    it.second(response);
  }

  // Remove references
  this->subscriptions = {};
  this->callbacks = {};
}

MercurySession::Response MercurySession::decodeResponse(
    const std::vector<uint8_t>& data) {
  auto sequenceLength = ntohs(extract<uint16_t>(data, 0));
  int64_t sequenceId;
  uint8_t flag;
  Response resp;
  if (sequenceLength == 2)
    sequenceId = ntohs(extract<int16_t>(data, 2));
  else if (sequenceLength == 4)
    sequenceId = ntohl(extract<int32_t>(data, 2));
  else if (sequenceLength == 8)
    sequenceId = hton64(extract<int64_t>(data, 2));
  else
    return resp;

  size_t pos = 2 + sequenceLength;
  flag = (uint8_t)data[pos];
  pos++;
  uint16_t parts = ntohs(extract<uint16_t>(data, pos));
  pos += 2;
  auto partial = partials.begin();
  while (partial != partials.end() && partial->sequenceId != sequenceId)
    partial++;  // if(partial.first == sequenceId)
  if (partial == partials.end()) {
    CSPOT_LOG(debug,
              "Creating new Mercury Response, seq: %lli, flags: %i, parts: %i",
              sequenceId, flag, parts);
    this->partials.push_back(Response());
    partial = partials.end() - 1;
    partial->parts = {};
    partial->sequenceId = sequenceId;
  } else
    CSPOT_LOG(debug,
              "Adding to Mercury Response, seq: %lli, flags: %i, parts: %i",
              sequenceId, flag, parts);
  uint8_t index = 0;
  while (parts) {
    if (data.size() <= pos)
      break;
    auto partSize = ntohs(extract<uint16_t>(data, pos));
    pos += 2;
    if (partial->mercuryHeader.uri == NULL) {
      auto headerBytes = std::vector<uint8_t>(data.begin() + pos,
                                              data.begin() + pos + partSize);
      pbDecode(partial->mercuryHeader, Header_fields, headerBytes);
    } else {
      if (index >= partial->parts.size())
        partial->parts.push_back(std::vector<uint8_t>{});
      partial->parts[index].insert(partial->parts[index].end(),
                                   data.begin() + pos,
                                   data.begin() + pos + partSize);
      index++;
    }
    pos += partSize;
    parts--;
  }
  if (flag == static_cast<uint8_t>(ResponseFlag::FINAL)) {
    resp = *partial;
    partials.erase(partial);
    resp.fail = false;
  }
  return resp;
}

void MercurySession::addSubscriptionListener(const std::string& uri,
                                             ResponseCallback subscription) {
  this->subscriptions.insert({uri, subscription});
}

void MercurySession::addSubscriptionListener(const std::string& uri,
                                             ResponseCallback subscription) {
  this->subscriptions.insert({uri, subscription});
}

uint64_t MercurySession::executeSubscription(RequestType method,
                                             const std::string& uri,
                                             ResponseCallback callback,
                                             ResponseCallback subscription,
                                             DataParts& payload) {
  CSPOT_LOG(debug, "Executing Mercury Request, type %s",
            RequestTypeMap[method].c_str());

  // Encode header
  pb_release(Header_fields, &tempMercuryHeader);
  tempMercuryHeader.uri = strdup(uri.c_str());
  tempMercuryHeader.method = strdup(RequestTypeMap[method].c_str());

  // GET and SEND are actually the same. Therefore the override
  // The difference between them is only in header's method
  if (method == RequestType::GET || method == RequestType::POST ||
      method == RequestType::PUT) {
    method = RequestType::SEND;
  }

  if (method == RequestType::SUB) {
    this->subscriptions.insert({uri, subscription});
  }

  auto headerBytes = pbEncode(Header_fields, &tempMercuryHeader);
  pb_release(Header_fields, &tempMercuryHeader);

  if (callback != nullptr)
    this->callbacks.insert({sequenceId, callback});

  // Structure: [Sequence size] [SequenceId] [0x1] [Payloads number]
  // [Header size] [Header] [Payloads (size + data)]

  // Pack sequenceId
  auto sequenceIdBytes = pack<uint64_t>(hton64(this->sequenceId));
  auto sequenceSizeBytes = pack<uint16_t>(htons(sequenceIdBytes.size()));

  sequenceIdBytes.insert(sequenceIdBytes.begin(), sequenceSizeBytes.begin(),
                         sequenceSizeBytes.end());
  sequenceIdBytes.push_back(0x01);

  auto payloadNum = pack<uint16_t>(htons(payload.size() + 1));
  sequenceIdBytes.insert(sequenceIdBytes.end(), payloadNum.begin(),
                         payloadNum.end());

  auto headerSizePayload = pack<uint16_t>(htons(headerBytes.size()));
  sequenceIdBytes.insert(sequenceIdBytes.end(), headerSizePayload.begin(),
                         headerSizePayload.end());
  sequenceIdBytes.insert(sequenceIdBytes.end(), headerBytes.begin(),
                         headerBytes.end());

  // Encode all the payload parts
  for (int x = 0; x < payload.size(); x++) {
    headerSizePayload = pack<uint16_t>(htons(payload[x].size()));
    sequenceIdBytes.insert(sequenceIdBytes.end(), headerSizePayload.begin(),
                           headerSizePayload.end());
    sequenceIdBytes.insert(sequenceIdBytes.end(), payload[x].begin(),
                           payload[x].end());
  }

  // Bump sequence id
  this->sequenceId += 1;

  try {
    this->shanConn->sendPacket(
        static_cast<std::underlying_type<RequestType>::type>(method),
        sequenceIdBytes);
  } catch (...) {
    // @TODO: handle disconnect
  }

  return this->sequenceId - 1;
}

uint32_t MercurySession::requestAudioKey(const std::vector<uint8_t>& trackId,
                                         const std::vector<uint8_t>& fileId,
                                         AudioKeyCallback audioCallback) {
  auto buffer = fileId;

  // Store callback
  this->audioKeyCallbacks.insert({this->audioKeySequence, audioCallback});

  // Structure: [FILEID] [TRACKID] [4 BYTES SEQUENCE ID] [0x00, 0x00]
  buffer.insert(buffer.end(), trackId.begin(), trackId.end());
  auto audioKeySequenceBuffer = pack<uint32_t>(htonl(this->audioKeySequence));
  buffer.insert(buffer.end(), audioKeySequenceBuffer.begin(),
                audioKeySequenceBuffer.end());
  auto suffix = std::vector<uint8_t>({0x00, 0x00});
  buffer.insert(buffer.end(), suffix.begin(), suffix.end());

  // Bump audio key sequence
  this->audioKeySequence += 1;

  // Used for broken connection detection
  // this->lastRequestTimestamp = timeProvider->getSyncedTimestamp();
  try {
    this->shanConn->sendPacket(
        static_cast<uint8_t>(RequestType::AUDIO_KEY_REQUEST_COMMAND), buffer);
  } catch (...) {
    // @TODO: Handle disconnect
  }
  return audioKeySequence - 1;
}