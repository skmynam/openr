/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <string>
#include <unordered_map>

#include <boost/serialization/strong_typedef.hpp>
#include <fbzmq/zmq/Zmq.h>
#include <folly/IPAddress.h>
#include <folly/Optional.h>
#include <folly/futures/Future.h>

#include <openr/common/AsyncThrottle.h>
#include <openr/common/OpenrEventBase.h>
#include <openr/common/Util.h>
#include <openr/config-store/PersistentStore.h>
#include <openr/config/Config.h>
#include <openr/if/gen-cpp2/Lsdb_types.h>
#include <openr/if/gen-cpp2/Network_types.h>
#include <openr/if/gen-cpp2/PrefixManager_types.h>
#include <openr/kvstore/KvStoreClientInternal.h>
#include <openr/messaging/Queue.h>

namespace openr {

class PrefixManager final : public OpenrEventBase {
 public:
  PrefixManager(
      messaging::RQueue<thrift::PrefixUpdateRequest> prefixUpdateRequestQueue,
      std::shared_ptr<const Config> config,
      PersistentStore* configStore,
      KvStore* kvStore,
      // enable convergence performance measurement for Adjacencies update
      bool enablePerfMeasurement,
      const std::chrono::seconds& initialDumpTime,
      bool perPrefixKeys = true);

  ~PrefixManager();

  /**
   * Override stop method of OpenrEventBase
   */
  void stop() override;

  // disable copying
  PrefixManager(PrefixManager const&) = delete;
  PrefixManager& operator=(PrefixManager const&) = delete;

  /*
   * Public API for PrefixManager operations:
   *
   * Write APIs - will schedule syncKvStoreThrottled_ to update kvstore,
   * @return true if there are changes else false
   *  - add prefixes
   *  - withdraw prefixes
   *  - withdraw prefixes by type
   *  - sync prefixes by type: replace all prefixes of @type w/ @prefixes
   *
   *
   * Read APIs - dump internal prefixDb
   *  - dump all prefixes
   *  - dump all prefixes by type
   */
  folly::SemiFuture<bool> advertisePrefixes(
      std::vector<thrift::PrefixEntry> prefixes);

  folly::SemiFuture<bool> withdrawPrefixes(
      std::vector<thrift::PrefixEntry> prefixes);

  folly::SemiFuture<bool> withdrawPrefixesByType(thrift::PrefixType prefixType);

  folly::SemiFuture<bool> syncPrefixesByType(
      thrift::PrefixType prefixType, std::vector<thrift::PrefixEntry> prefixes);

  folly::SemiFuture<std::unique_ptr<std::vector<thrift::PrefixEntry>>>
  getPrefixes();

  folly::SemiFuture<std::unique_ptr<std::vector<thrift::PrefixEntry>>>
  getPrefixesByType(thrift::PrefixType prefixType);

 private:
  /*
   * Private helpers to update prefixMap_ and send prefixes to KvStore
   *
   * Called upon:
   * - public write APIs
   * - request from PrefixUpdateRequest
   *
   * modify prefix db and schedule syncKvStoreThrottled_ to update kvstore
   * @return true if the db is modified
   */
  bool advertisePrefixesImpl(const std::vector<thrift::PrefixEntry>& prefixes);
  bool withdrawPrefixesImpl(const std::vector<thrift::PrefixEntry>& prefixes);
  bool withdrawPrefixesByTypeImpl(thrift::PrefixType type);
  bool syncPrefixesByTypeImpl(
      thrift::PrefixType type,
      const std::vector<thrift::PrefixEntry>& prefixes);

  // Update kvstore with both ephemeral and non-ephemeral prefixes
  void syncKvStore();

  // add prefix entry in kvstore, return per prefix key name
  std::string updateKvStorePrefixEntry(thrift::PrefixEntry& prefixEntry);

  // Update persistent store with non-ephemeral prefix entries
  void persistPrefixDb();

  // add event named updateEvent to perfEvents if it has value and the last
  // element is not already updateEvent
  void addPerfEventIfNotExist(
      thrift::PerfEvents& perfEvents, std::string const& updateEvent);

  // this node name
  const std::string nodeId_;

  // module ptr to interact with ConfigStore
  PersistentStore* configStore_{nullptr};

  // module ptr to interact with KvStore
  KvStore* kvStore_{nullptr};

  // keep track of prefixDB on disk
  thrift::PrefixDatabase diskState_;

  bool perPrefixKeys_{true};

  // enable convergence performance measurement for Adjacencies update
  const bool enablePerfMeasurement_{false};

  // Throttled version of syncKvStore. It batches up multiple calls and
  // send them in one go!
  std::unique_ptr<AsyncThrottle> syncKvStoreThrottled_;
  std::unique_ptr<folly::AsyncTimeout> initialSyncKvStoreTimer_;

  // TTL for a key in the key value store
  const std::chrono::milliseconds ttlKeyInKvStore_;

  // kvStoreClient for persisting our prefix db
  std::unique_ptr<KvStoreClientInternal> kvStoreClient_{nullptr};

  // The current prefix db this node is advertising. In-case if multiple entries
  // exists for a given prefix, lowest prefix-type is preferred. This is to
  // bring deterministic behavior for advertising routes.
  // IMP: Ordered
  std::map<
      thrift::PrefixType,
      std::unordered_map<thrift::IpPrefix, thrift::PrefixEntry>>
      prefixMap_;

  // the serializer/deserializer helper we'll be using
  apache::thrift::CompactSerializer serializer_;

  // track any prefix keys for this node that we see to make sure we withdraw
  // anything we no longer wish to advertise
  std::unordered_set<std::string> keysToClear_;

  // perfEvents related to a given prefisEntry
  std::unordered_map<
      thrift::PrefixType,
      std::unordered_map<thrift::IpPrefix, thrift::PerfEvents>>
      addingEvents_;

  // area Id
  const std::unordered_set<std::string> areas_{};
}; // PrefixManager

} // namespace openr
