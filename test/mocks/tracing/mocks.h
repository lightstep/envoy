#pragma once

#include <string>
#include <vector>

#include "envoy/tracing/context.h"
#include "envoy/tracing/http_tracer.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Tracing {

inline bool operator==(const TransportContext& lhs, const TransportContext& rhs) {
  return lhs.request_id_ == rhs.request_id_ && lhs.span_context_ == rhs.span_context_;
}

class MockConfig : public Config {
public:
  MockConfig();
  ~MockConfig();

  MOCK_CONST_METHOD0(operationName, OperationName());
  MOCK_CONST_METHOD0(requestHeadersForTags, const std::vector<Http::LowerCaseString>&());

  OperationName operation_name_{OperationName::Ingress};
  std::vector<Http::LowerCaseString> headers_;
};

class MockSpan : public Span {
public:
  MockSpan();
  ~MockSpan();

  MOCK_METHOD1(setOperation, void(const std::string& operation));
  MOCK_METHOD2(setTag, void(const std::string& name, const std::string& value));
  MOCK_METHOD1(finishSpan, void(SpanFinalizer& finalizer));
  MOCK_METHOD1(injectContext, void(Http::HeaderMap& request_headers));

  SpanPtr spawnChild(const Config& config, const std::string& name,
                     SystemTime start_time) override {
    return SpanPtr{spawnChild_(config, name, start_time)};
  }

  MOCK_METHOD3(spawnChild_,
               Span*(const Config& config, const std::string& name, SystemTime start_time));
};

class MockFinalizer : public SpanFinalizer {
public:
  MockFinalizer();
  ~MockFinalizer();

  MOCK_METHOD1(finalize, void(Span& span));
};

class MockHttpTracer : public HttpTracer {
public:
  MockHttpTracer();
  ~MockHttpTracer();

  SpanPtr startSpan(const Config& config, Http::HeaderMap& request_headers,
                    const Http::AccessLog::RequestInfo& request_info) override {
    return SpanPtr{startSpan_(config, request_headers, request_info)};
  }

  MOCK_METHOD3(startSpan_, Span*(const Config& config, Http::HeaderMap& request_headers,
                                 const Http::AccessLog::RequestInfo& request_info));
};

class MockDriver : public Driver {
public:
  MockDriver();
  ~MockDriver();

  SpanPtr startSpan(const Config& config, Http::HeaderMap& request_headers,
                    const std::string& operation_name, SystemTime start_time) override {
    return SpanPtr{startSpan_(config, request_headers, operation_name, start_time)};
  }

  MOCK_METHOD4(startSpan_, Span*(const Config& config, Http::HeaderMap& request_headers,
                                 const std::string& operation_name, SystemTime start_time));
};

} // namespace Tracing
} // namespace Envoy
