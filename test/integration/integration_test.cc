#include "test/integration/integration_test.h"

#include <string>

#include "common/http/header_map_impl.h"
#include "common/protobuf/utility.h"

#include "test/integration/utility.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

INSTANTIATE_TEST_CASE_P(IpVersions, IntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(IntegrationTest, RouterNotFound) { testRouterNotFound(); }

TEST_P(IntegrationTest, RouterNotFoundBodyNoBuffer) { testRouterNotFoundWithBody(); }

TEST_P(IntegrationTest, RouterNotFoundBodyBuffer) {
  config_helper_.addFilter(ConfigHelper::DEFAULT_BUFFER_FILTER);
  testRouterNotFoundWithBody();
}

TEST_P(IntegrationTest, RouterRedirect) { testRouterRedirect(); }

TEST_P(IntegrationTest, DrainClose) { testDrainClose(); }

TEST_P(IntegrationTest, ConnectionClose) {
  config_helper_.addFilter(ConfigHelper::DEFAULT_HEALTH_CHECK_FILTER);
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  codec_client_->makeHeaderOnlyRequest(Http::TestHeaderMapImpl{{":method", "GET"},
                                                               {":path", "/healthcheck"},
                                                               {":authority", "host"},
                                                               {"connection", "close"}},
                                       *response_);
  response_->waitForEndStream();
  codec_client_->waitForDisconnect();

  EXPECT_TRUE(response_->complete());
  EXPECT_STREQ("200", response_->headers().Status()->value().c_str());
}

TEST_P(IntegrationTest, RouterRequestAndResponseWithBodyNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, false);
}

TEST_P(IntegrationTest, RouterRequestAndResponseWithBodyBuffer) {
  config_helper_.addFilter(ConfigHelper::DEFAULT_BUFFER_FILTER);
  testRouterRequestAndResponseWithBody(1024, 512, false);
}

TEST_P(IntegrationTest, RouterRequestAndResponseWithGiantBodyBuffer) {
  config_helper_.addFilter(ConfigHelper::DEFAULT_BUFFER_FILTER);
  testRouterRequestAndResponseWithBody(4 * 1024 * 1024, 4 * 1024 * 1024, false);
}

TEST_P(IntegrationTest, FlowControlOnAndGiantBody) {
  config_helper_.setBufferLimits(1024, 1024);
  testRouterRequestAndResponseWithBody(1024 * 1024, 1024 * 1024, false);
}

TEST_P(IntegrationTest, RouterRequestAndResponseLargeHeaderNoBuffer) {
  testRouterRequestAndResponseWithBody(1024, 512, true);
}

TEST_P(IntegrationTest, RouterHeaderOnlyRequestAndResponseNoBuffer) {
  testRouterHeaderOnlyRequestAndResponse(true);
}

TEST_P(IntegrationTest, RouterHeaderOnlyRequestAndResponseBuffer) {
  config_helper_.addFilter(ConfigHelper::DEFAULT_BUFFER_FILTER);
  testRouterHeaderOnlyRequestAndResponse(true);
}

TEST_P(IntegrationTest, ShutdownWithActiveConnPoolConnections) {
  testRouterHeaderOnlyRequestAndResponse(false);
}

TEST_P(IntegrationTest, RouterUpstreamDisconnectBeforeRequestcomplete) {
  testRouterUpstreamDisconnectBeforeRequestComplete();
}

TEST_P(IntegrationTest, RouterUpstreamDisconnectBeforeResponseComplete) {
  testRouterUpstreamDisconnectBeforeResponseComplete();
}

TEST_P(IntegrationTest, RouterDownstreamDisconnectBeforeRequestComplete) {
  testRouterDownstreamDisconnectBeforeRequestComplete();
}

TEST_P(IntegrationTest, RouterDownstreamDisconnectBeforeResponseComplete) {
  testRouterDownstreamDisconnectBeforeResponseComplete();
}

TEST_P(IntegrationTest, RouterUpstreamResponseBeforeRequestComplete) {
  testRouterUpstreamResponseBeforeRequestComplete();
}

TEST_P(IntegrationTest, Retry) { testRetry(); }

TEST_P(IntegrationTest, TwoRequests) { testTwoRequests(); }

TEST_P(IntegrationTest, RetryHittingBufferLimit) { testRetryHittingBufferLimit(); }

TEST_P(IntegrationTest, HittingDecoderFilterLimit) { testHittingDecoderFilterLimit(); }

// Test hitting the bridge filter with too many response bytes to buffer.  Given
// the headers are not proxied, the connection manager will send a 500.
TEST_P(IntegrationTest, HittingEncoderFilterLimitBufferingHeaders) {
  config_helper_.addFilter("{ name: envoy.grpc_http1_bridge, config: { deprecated_v1: true } }");
  config_helper_.setBufferLimits(1024, 1024);

  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  codec_client_->makeHeaderOnlyRequest(
      Http::TestHeaderMapImpl{{":method", "POST"},
                              {":path", "/test/long/url"},
                              {":scheme", "http"},
                              {":authority", "host"},
                              {"content-type", "application/grpc"},
                              {"x-envoy-retry-grpc-on", "cancelled"}},
      *response_);
  waitForNextUpstreamRequest();

  // Send the overly large response.  Because the grpc_http1_bridge filter buffers and buffer
  // limits are sent, this will be translated into a 500 from Envoy.
  upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(1024 * 65, false);

  response_->waitForEndStream();
  EXPECT_TRUE(response_->complete());
  EXPECT_STREQ("500", response_->headers().Status()->value().c_str());
}

TEST_P(IntegrationTest, HittingEncoderFilterLimit) { testHittingEncoderFilterLimit(); }

TEST_P(IntegrationTest, BadFirstline) { testBadFirstline(); }

TEST_P(IntegrationTest, MissingDelimiter) { testMissingDelimiter(); }

TEST_P(IntegrationTest, InvalidCharacterInFirstline) { testInvalidCharacterInFirstline(); }

TEST_P(IntegrationTest, LowVersion) { testLowVersion(); }

TEST_P(IntegrationTest, Http10Request) { testHttp10Request(); }

TEST_P(IntegrationTest, NoHost) { testNoHost(); }

TEST_P(IntegrationTest, BadPath) { testBadPath(); }

TEST_P(IntegrationTest, AbsolutePath) { testAbsolutePath(); }

TEST_P(IntegrationTest, AbsolutePathWithPort) { testAbsolutePathWithPort(); }

TEST_P(IntegrationTest, AbsolutePathWithoutPort) { testAbsolutePathWithoutPort(); }

TEST_P(IntegrationTest, Connect) { testConnect(); }

TEST_P(IntegrationTest, ValidZeroLengthContent) { testValidZeroLengthContent(); }

TEST_P(IntegrationTest, InvalidContentLength) { testInvalidContentLength(); }

TEST_P(IntegrationTest, MultipleContentLengths) { testMultipleContentLengths(); }

TEST_P(IntegrationTest, OverlyLongHeaders) { testOverlyLongHeaders(); }

TEST_P(IntegrationTest, UpstreamProtocolError) { testUpstreamProtocolError(); }

void setRouteUsingWebsocket(envoy::api::v2::filter::http::HttpConnectionManager& hcm) {
  auto route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->add_routes();
  route->mutable_match()->set_prefix("/websocket/test");
  route->mutable_route()->set_prefix_rewrite("/websocket");
  route->mutable_route()->set_cluster("cluster_0");
  route->mutable_route()->mutable_use_websocket()->set_value(true);
};

TEST_P(IntegrationTest, WebSocketConnectionDownstreamDisconnect) {
  // Set a less permissive default route so it does not pick up the /websocket query.
  config_helper_.setDefaultHostAndRoute("*", "/asd");
  // Enable websockets for the path /websocket/test
  config_helper_.addConfigModifier(&setRouteUsingWebsocket);
  initialize();

  // WebSocket upgrade, send some data and disconnect downstream
  IntegrationTcpClientPtr tcp_client;
  FakeRawConnectionPtr fake_upstream_connection;
  const std::string upgrade_req_str = "GET /websocket/test HTTP/1.1\r\nHost: host\r\nConnection: "
                                      "Upgrade\r\nUpgrade: websocket\r\n\r\n";
  const std::string upgrade_resp_str =
      "HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n\r\n";

  tcp_client = makeTcpConnection(lookupPort("http"));
  // Send websocket upgrade request
  // The request path gets rewritten from /websocket/test to /websocket.
  // The size of headers received by the destination is 225 bytes.
  tcp_client->write(upgrade_req_str);
  fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection();
  fake_upstream_connection->waitForData(225);
  // Accept websocket upgrade request
  fake_upstream_connection->write(upgrade_resp_str);
  tcp_client->waitForData(upgrade_resp_str);
  // Standard TCP proxy semantics post upgrade
  tcp_client->write("hello");
  // datalen = 225 + strlen(hello)
  fake_upstream_connection->waitForData(230);
  fake_upstream_connection->write("world");
  tcp_client->waitForData(upgrade_resp_str + "world");
  tcp_client->write("bye!");
  // downstream disconnect
  tcp_client->close();
  // datalen = 225 + strlen(hello) + strlen(bye!)
  fake_upstream_connection->waitForData(234);
  fake_upstream_connection->waitForDisconnect();
}

TEST_P(IntegrationTest, WebSocketConnectionUpstreamDisconnect) {
  config_helper_.setDefaultHostAndRoute("*", "/asd");
  config_helper_.addConfigModifier(&setRouteUsingWebsocket);
  initialize();

  // WebSocket upgrade, send some data and disconnect upstream
  IntegrationTcpClientPtr tcp_client;
  FakeRawConnectionPtr fake_upstream_connection;
  const std::string upgrade_req_str = "GET /websocket/test HTTP/1.1\r\nHost: host\r\nConnection: "
                                      "Upgrade\r\nUpgrade: websocket\r\n\r\n";
  const std::string upgrade_resp_str =
      "HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n\r\n";
  tcp_client = makeTcpConnection(lookupPort("http"));
  // Send websocket upgrade request
  tcp_client->write(upgrade_req_str);
  fake_upstream_connection = fake_upstreams_[0]->waitForRawConnection();
  // The request path gets rewritten from /websocket/test to /websocket.
  // The size of headers received by the destination is 225 bytes.
  fake_upstream_connection->waitForData(225);
  // Accept websocket upgrade request
  fake_upstream_connection->write(upgrade_resp_str);
  tcp_client->waitForData(upgrade_resp_str);
  // Standard TCP proxy semantics post upgrade
  tcp_client->write("hello");
  // datalen = 225 + strlen(hello)
  fake_upstream_connection->waitForData(230);
  fake_upstream_connection->write("world");
  // upstream disconnect
  fake_upstream_connection->close();
  fake_upstream_connection->waitForDisconnect();
  tcp_client->waitForDisconnect();

  EXPECT_EQ(upgrade_resp_str + "world", tcp_client->data());
}

} // namespace Envoy
