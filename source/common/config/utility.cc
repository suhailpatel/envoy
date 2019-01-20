#include "common/config/utility.h"

#include <unordered_set>

#include "envoy/config/metrics/v2/stats.pb.h"
#include "envoy/stats/scope.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/hex.h"
#include "common/common/utility.h"
#include "common/config/json_utility.h"
#include "common/config/resources.h"
#include "common/config/well_known_names.h"
#include "common/filesystem/filesystem_impl.h"
#include "common/json/config_schemas.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"
#include "common/stats/histogram_options_impl.h"
#include "common/stats/stats_matcher_impl.h"
#include "common/stats/tag_producer_impl.h"

namespace Envoy {
namespace Config {

void Utility::translateApiConfigSource(const std::string& cluster, uint32_t refresh_delay_ms,
                                       const std::string& api_type,
                                       envoy::api::v2::core::ApiConfigSource& api_config_source) {
  // TODO(junr03): document the option to chose an api type once we have created
  // stronger constraints around v2.
  if (api_type == ApiType::get().Grpc) {
    api_config_source.set_api_type(envoy::api::v2::core::ApiConfigSource::GRPC);
    envoy::api::v2::core::GrpcService* grpc_service = api_config_source.add_grpc_services();
    grpc_service->mutable_envoy_grpc()->set_cluster_name(cluster);
  } else {
    if (api_type == ApiType::get().UnsupportedRestLegacy) {
      api_config_source.set_api_type(
          envoy::api::v2::core::ApiConfigSource::UNSUPPORTED_REST_LEGACY);
    } else if (api_type == ApiType::get().Rest) {
      api_config_source.set_api_type(envoy::api::v2::core::ApiConfigSource::REST);
    }
    api_config_source.add_cluster_names(cluster);
  }

  api_config_source.mutable_refresh_delay()->CopyFrom(
      Protobuf::util::TimeUtil::MillisecondsToDuration(refresh_delay_ms));
}

void Utility::checkCluster(const std::string& error_prefix, const std::string& cluster_name,
                           Upstream::ClusterManager& cm) {
  Upstream::ThreadLocalCluster* cluster = cm.get(cluster_name);
  if (cluster == nullptr) {
    throw EnvoyException(fmt::format("{}: unknown cluster '{}'", error_prefix, cluster_name));
  }

  if (cluster->info()->addedViaApi()) {
    throw EnvoyException(fmt::format("{}: invalid cluster '{}': currently only "
                                     "static (non-CDS) clusters are supported",
                                     error_prefix, cluster_name));
  }
}

void Utility::checkClusterAndLocalInfo(const std::string& error_prefix,
                                       const std::string& cluster_name,
                                       Upstream::ClusterManager& cm,
                                       const LocalInfo::LocalInfo& local_info) {
  checkCluster(error_prefix, cluster_name, cm);
  checkLocalInfo(error_prefix, local_info);
}

void Utility::checkLocalInfo(const std::string& error_prefix,
                             const LocalInfo::LocalInfo& local_info) {
  if (local_info.clusterName().empty() || local_info.nodeName().empty()) {
    throw EnvoyException(
        fmt::format("{}: node 'id' and 'cluster' are required. Set it either in 'node' config or "
                    "via --service-node and --service-cluster options.",
                    error_prefix, local_info.node().DebugString()));
  }
}

void Utility::checkFilesystemSubscriptionBackingPath(const std::string& path) {
  // TODO(junr03): the file might be deleted between this check and the
  // watch addition.
  if (!Filesystem::fileExists(path)) {
    throw EnvoyException(fmt::format(
        "envoy::api::v2::Path must refer to an existing path in the system: '{}' does not exist",
        path));
  }
}

void Utility::checkApiConfigSourceNames(
    const envoy::api::v2::core::ApiConfigSource& api_config_source) {
  const bool is_grpc =
      (api_config_source.api_type() == envoy::api::v2::core::ApiConfigSource::GRPC);

  if (api_config_source.cluster_names().empty() && api_config_source.grpc_services().empty()) {
    throw EnvoyException(
        fmt::format("API configs must have either a gRPC service or a cluster name defined: {}",
                    api_config_source.DebugString()));
  }

  if (is_grpc) {
    if (!api_config_source.cluster_names().empty()) {
      throw EnvoyException(fmt::format(
          "envoy::api::v2::core::ConfigSource::GRPC must not have a cluster name specified: {}",
          api_config_source.DebugString()));
    }
    if (api_config_source.grpc_services().size() > 1) {
      throw EnvoyException(fmt::format(
          "envoy::api::v2::core::ConfigSource::GRPC must have a single gRPC service specified: {}",
          api_config_source.DebugString()));
    }
  } else {
    if (!api_config_source.grpc_services().empty()) {
      throw EnvoyException(
          fmt::format("envoy::api::v2::core::ConfigSource, if not of type gRPC, must not have "
                      "a gRPC service specified: {}",
                      api_config_source.DebugString()));
    }
    if (api_config_source.cluster_names().size() != 1) {
      throw EnvoyException(fmt::format(
          "envoy::api::v2::core::ConfigSource must have a singleton cluster name specified: {}",
          api_config_source.DebugString()));
    }
  }
}

void Utility::validateClusterName(const Upstream::ClusterManager::ClusterInfoMap& clusters,
                                  const std::string& cluster_name) {
  const auto& it = clusters.find(cluster_name);
  if (it == clusters.end() || it->second.get().info()->addedViaApi() ||
      it->second.get().info()->type() == envoy::api::v2::Cluster::EDS) {
    throw EnvoyException(fmt::format(
        "envoy::api::v2::core::ConfigSource must have a statically "
        "defined non-EDS cluster: '{}' does not exist, was added via api, or is an EDS cluster",
        cluster_name));
  }
}

void Utility::checkApiConfigSourceSubscriptionBackingCluster(
    const Upstream::ClusterManager::ClusterInfoMap& clusters,
    const envoy::api::v2::core::ApiConfigSource& api_config_source) {
  Utility::checkApiConfigSourceNames(api_config_source);

  const bool is_grpc =
      (api_config_source.api_type() == envoy::api::v2::core::ApiConfigSource::GRPC);

  if (!api_config_source.cluster_names().empty()) {
    // All API configs of type REST and UNSUPPORTED_REST_LEGACY should have cluster names.
    // Additionally, some gRPC API configs might have a cluster name set instead
    // of an envoy gRPC.
    Utility::validateClusterName(clusters, api_config_source.cluster_names()[0]);
  } else if (is_grpc) {
    // Some ApiConfigSources of type GRPC won't have a cluster name, such as if
    // they've been configured with google_grpc.
    if (api_config_source.grpc_services()[0].has_envoy_grpc()) {
      // If an Envoy gRPC exists, we take its cluster name.
      Utility::validateClusterName(
          clusters, api_config_source.grpc_services()[0].envoy_grpc().cluster_name());
    }
  }
  // Otherwise, there is no cluster name to validate.
}

std::chrono::milliseconds Utility::apiConfigSourceRefreshDelay(
    const envoy::api::v2::core::ApiConfigSource& api_config_source) {
  if (!api_config_source.has_refresh_delay()) {
    throw EnvoyException("refresh_delay is required for REST API configuration sources");
  }

  return std::chrono::milliseconds(
      DurationUtil::durationToMilliseconds(api_config_source.refresh_delay()));
}

std::chrono::milliseconds Utility::apiConfigSourceRequestTimeout(
    const envoy::api::v2::core::ApiConfigSource& api_config_source) {
  return std::chrono::milliseconds(
      PROTOBUF_GET_MS_OR_DEFAULT(api_config_source, request_timeout, 1000));
}

void Utility::translateCdsConfig(const Json::Object& json_config,
                                 envoy::api::v2::core::ConfigSource& cds_config) {
  translateApiConfigSource(json_config.getObject("cluster")->getString("name"),
                           json_config.getInteger("refresh_delay_ms", 30000),
                           json_config.getString("api_type", ApiType::get().UnsupportedRestLegacy),
                           *cds_config.mutable_api_config_source());
}

void Utility::translateRdsConfig(
    const Json::Object& json_rds,
    envoy::config::filter::network::http_connection_manager::v2::Rds& rds,
    const Stats::StatsOptions& stats_options) {
  json_rds.validateSchema(Json::Schema::RDS_CONFIGURATION_SCHEMA);

  const std::string name = json_rds.getString("route_config_name", "");
  checkObjNameLength("Invalid route_config name", name, stats_options);
  rds.set_route_config_name(name);

  translateApiConfigSource(json_rds.getString("cluster"),
                           json_rds.getInteger("refresh_delay_ms", 30000),
                           json_rds.getString("api_type", ApiType::get().UnsupportedRestLegacy),
                           *rds.mutable_config_source()->mutable_api_config_source());
}

void Utility::translateLdsConfig(const Json::Object& json_lds,
                                 envoy::api::v2::core::ConfigSource& lds_config) {
  json_lds.validateSchema(Json::Schema::LDS_CONFIG_SCHEMA);
  translateApiConfigSource(json_lds.getString("cluster"),
                           json_lds.getInteger("refresh_delay_ms", 30000),
                           json_lds.getString("api_type", ApiType::get().UnsupportedRestLegacy),
                           *lds_config.mutable_api_config_source());
}

RateLimitSettings
Utility::parseRateLimitSettings(const envoy::api::v2::core::ApiConfigSource& api_config_source) {
  RateLimitSettings rate_limit_settings;
  if (api_config_source.has_rate_limit_settings()) {
    rate_limit_settings.enabled_ = true;
    rate_limit_settings.max_tokens_ =
        PROTOBUF_GET_WRAPPED_OR_DEFAULT(api_config_source.rate_limit_settings(), max_tokens,
                                        Envoy::Config::RateLimitSettings::DefaultMaxTokens);
    rate_limit_settings.fill_rate_ =
        PROTOBUF_GET_WRAPPED_OR_DEFAULT(api_config_source.rate_limit_settings(), fill_rate,
                                        Envoy::Config::RateLimitSettings::DefaultFillRate);
  }
  return rate_limit_settings;
}

Stats::TagProducerPtr
Utility::createTagProducer(const envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
  return std::make_unique<Stats::TagProducerImpl>(bootstrap.stats_config());
}

Stats::StatsMatcherPtr
Utility::createStatsMatcher(const envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
  return std::make_unique<Stats::StatsMatcherImpl>(bootstrap.stats_config());
}

Stats::HistogramOptionsPtr
Utility::createHistogramOptions(const envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
  const auto config_buckets = bootstrap.stats_config().histogram_buckets();
  std::vector<double> buckets;
  buckets.reserve(config_buckets.size());
  for (const auto item : config_buckets) {
    buckets.emplace_back(item);
  }

  const auto config_quantiles = bootstrap.stats_config().histogram_quantiles();
  std::vector<double> quantiles;
  quantiles.reserve(config_quantiles.size());
  for (const auto item : config_quantiles) {
    quantiles.emplace_back(item);
  }

  return std::make_unique<Stats::HistogramOptionsImpl>(buckets, quantiles);
}

void Utility::checkObjNameLength(const std::string& error_prefix, const std::string& name,
                                 const Stats::StatsOptions& stats_options) {
  if (name.length() > stats_options.maxNameLength()) {
    throw EnvoyException(fmt::format("{}: Length of {} ({}) exceeds allowed maximum length ({})",
                                     error_prefix, name, name.length(),
                                     stats_options.maxNameLength()));
  }
}

Grpc::AsyncClientFactoryPtr Utility::factoryForGrpcApiConfigSource(
    Grpc::AsyncClientManager& async_client_manager,
    const envoy::api::v2::core::ApiConfigSource& api_config_source, Stats::Scope& scope) {
  Utility::checkApiConfigSourceNames(api_config_source);

  if (api_config_source.api_type() != envoy::api::v2::core::ApiConfigSource::GRPC) {
    throw EnvoyException(fmt::format("envoy::api::v2::core::ConfigSource type must be GRPC: {}",
                                     api_config_source.DebugString()));
  }

  envoy::api::v2::core::GrpcService grpc_service;
  grpc_service.MergeFrom(api_config_source.grpc_services(0));

  return async_client_manager.factoryForGrpcService(grpc_service, scope, false);
}

envoy::api::v2::ClusterLoadAssignment Utility::translateClusterHosts(
    const Protobuf::RepeatedPtrField<envoy::api::v2::core::Address>& hosts) {
  envoy::api::v2::ClusterLoadAssignment load_assignment;
  envoy::api::v2::endpoint::LocalityLbEndpoints* locality_lb_endpoints =
      load_assignment.add_endpoints();
  // Since this LocalityLbEndpoints is built from hosts list, set the default weight to 1.
  locality_lb_endpoints->mutable_load_balancing_weight()->set_value(1);
  for (const envoy::api::v2::core::Address& host : hosts) {
    envoy::api::v2::endpoint::LbEndpoint* lb_endpoint = locality_lb_endpoints->add_lb_endpoints();
    lb_endpoint->mutable_endpoint()->mutable_address()->MergeFrom(host);
    lb_endpoint->mutable_load_balancing_weight()->set_value(1);
  }
  return load_assignment;
}

void Utility::translateOpaqueConfig(const ProtobufWkt::Any& typed_config,
                                    const ProtobufWkt::Struct& config,
                                    Protobuf::Message& out_proto) {
  static const std::string& struct_type =
      ProtobufWkt::Struct::default_instance().GetDescriptor()->full_name();

  if (!typed_config.value().empty()) {

    // Unpack methods will only use the fully qualified type name after the last '/'.
    // https://github.com/protocolbuffers/protobuf/blob/3.6.x/src/google/protobuf/any.proto#L87
    absl::string_view type = typed_config.type_url();
    size_t pos = type.find_last_of('/');
    if (pos != absl::string_view::npos) {
      type = type.substr(pos + 1);
    }

    // out_proto is expecting Struct, unpack directly
    if (type != struct_type || out_proto.GetDescriptor()->full_name() == struct_type) {
      typed_config.UnpackTo(&out_proto);
    } else {
      ProtobufWkt::Struct struct_config;
      typed_config.UnpackTo(&struct_config);
      MessageUtil::jsonConvert(struct_config, out_proto);
    }
  }

  if (!config.fields().empty()) {
    MessageUtil::jsonConvert(config, out_proto);
  }
}

} // namespace Config
} // namespace Envoy
