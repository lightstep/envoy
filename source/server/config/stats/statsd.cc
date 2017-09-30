#include "server/config/stats/statsd.h"

#include <string>

#include "envoy/registry/registry.h"

#include "common/config/well_known_names.h"
#include "common/stats/statsd.h"

#include "api/bootstrap.pb.h"

namespace Envoy {
namespace Server {
namespace Configuration {

Stats::SinkPtr StatsdSinkFactory::createStatsSink(const Protobuf::Message& config,
                                                  Server::Instance& server) {

  const auto& statsd_sink = dynamic_cast<const envoy::api::v2::StatsdSink&>(config);
  switch (statsd_sink.statsd_specifier_case()) {
  case envoy::api::v2::StatsdSink::kAddress: {
    Network::Address::InstanceConstSharedPtr address =
        Network::Utility::fromProtoAddress(statsd_sink.address());
    ENVOY_LOG(info, "statsd UDP ip address: {}", address->asString());
    return Stats::SinkPtr(
        new Stats::Statsd::UdpStatsdSink(server.threadLocal(), std::move(address)));
    break;
  }
  case envoy::api::v2::StatsdSink::kTcpClusterName:
    ENVOY_LOG(info, "statsd TCP cluster: {}", statsd_sink.tcp_cluster_name());
    return Stats::SinkPtr(new Stats::Statsd::TcpStatsdSink(
        server.localInfo(), statsd_sink.tcp_cluster_name(), server.threadLocal(),
        server.clusterManager(), server.stats()));
    break;
  default:
    throw EnvoyException(
        fmt::format("No tcp_cluster_name or address provided for {} Stats::Sink config", name()));
  }
}

ProtobufTypes::MessagePtr StatsdSinkFactory::createEmptyConfigProto() {
  return std::unique_ptr<envoy::api::v2::StatsdSink>(new envoy::api::v2::StatsdSink());
}

std::string StatsdSinkFactory::name() { return Config::StatsSinkNames::get().STATSD; }

/**
 * Static registration for the statsd sink factory. @see RegisterFactory.
 */
static Registry::RegisterFactory<StatsdSinkFactory, StatsSinkFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
