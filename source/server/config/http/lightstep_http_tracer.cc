#include "server/config/http/lightstep_http_tracer.h"

#include <string>

#include "envoy/registry/registry.h"

#include "common/common/utility.h"
#include "common/config/well_known_names.h"
#include "common/tracing/http_tracer_impl.h"
#include "common/tracing/lightstep_tracer_impl.h"

#include "lightstep/tracer.h"

namespace Envoy {
namespace Server {
namespace Configuration {

Tracing::HttpTracerPtr
LightstepHttpTracerFactory::createHttpTracer(const Json::Object& json_config,
                                             Server::Instance& server,
                                             Upstream::ClusterManager& cluster_manager) {

  std::unique_ptr<lightstep::LightStepTracerOptions> opts(new lightstep::LightStepTracerOptions());
  opts->access_token = server.api().fileReadToEnd(json_config.getString("access_token_file"));
  StringUtil::rtrim(opts->access_token);
  opts->component_name = server.localInfo().clusterName();

  Tracing::DriverPtr lightstep_driver(
      new Tracing::LightStepDriver(json_config, cluster_manager, server.stats(),
                                   server.threadLocal(), server.runtime(), std::move(opts)));
  return Tracing::HttpTracerPtr(
      new Tracing::HttpTracerImpl(std::move(lightstep_driver), server.localInfo()));
}

std::string LightstepHttpTracerFactory::name() { return Config::HttpTracerNames::get().LIGHTSTEP; }

/**
 * Static registration for the lightstep http tracer. @see RegisterFactory.
 */
static Registry::RegisterFactory<LightstepHttpTracerFactory, HttpTracerFactory> register_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
