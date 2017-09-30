#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/optional.h"
#include "envoy/common/pure.h"
#include "envoy/http/header_map.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Grpc {

/**
 * Callbacks for an individual grpc request.
 */
class RpcChannelCallbacks {
public:
  virtual ~RpcChannelCallbacks() {}

  /**
   * Called before the channel dispatches an HTTP/2 request. This can be used to customize the
   * transport headers for the RPC.
   */
  virtual void onPreRequestCustomizeHeaders(Http::HeaderMap& headers) PURE;

  /**
   * Called when the request has succeeded and the response object is populated.
   */
  virtual void onSuccess() PURE;

  /**
   * Called when the request has failed. The response object has not been populated.
   * @param grpc_status supplies the grpc_status for the error, if available.
   * @param message supplies additional error information if available.
   */
  virtual void onFailure(const Optional<uint64_t>& grpc_status, const std::string& message) PURE;
};

/**
 * A single active grpc request arbiter. This interface derives from
 * Protobuf::RpcChannel. When mocking, CallMethod() can be overriden to accept
 * the response message and the mock constructor can accept a RequestCallbacks
 * object. An RpcChannel should be passed to the constructor of an RPC stub
 * generated via protoc using the "option cc_generic_services = true;" option.
 * It can be used for multiple service calls, but not concurrently.
 * DEPRECATED: See https://github.com/envoyproxy/envoy/issues/1102
 */
class RpcChannel : public Protobuf::RpcChannel {
public:
  virtual ~RpcChannel() {}

  /**
   * Cancel an inflight RPC. The Request can be used again to make another call if desired.
   */
  virtual void cancel() PURE;
};

typedef std::unique_ptr<RpcChannel> RpcChannelPtr;

/**
 * Interface for creating new RPC channels.
 */
class RpcChannelFactory {
public:
  virtual ~RpcChannelFactory() {}

  /**
   * Create a new RPC channel given a set of callbacks.
   */
  virtual RpcChannelPtr create(RpcChannelCallbacks& callbacks,
                               const Optional<std::chrono::milliseconds>& timeout) PURE;
};

} // namespace Grpc
} // namespace Envoy
