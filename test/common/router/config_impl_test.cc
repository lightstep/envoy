#include <chrono>
#include <list>
#include <map>
#include <memory>
#include <string>

#include "common/config/rds_json.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/json/json_loader.h"
#include "common/network/address_impl.h"
#include "common/router/config_impl.h"

#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ContainerEq;
using testing::ElementsAreArray;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::StrNe;
using testing::_;

namespace Envoy {
namespace Router {
namespace {

Http::TestHeaderMapImpl genHeaders(const std::string& host, const std::string& path,
                                   const std::string& method) {
  return Http::TestHeaderMapImpl{{":authority", host}, {":path", path}, {":method", method}};
}

envoy::api::v2::RouteConfiguration parseRouteConfigurationFromJson(const std::string& json_string) {
  envoy::api::v2::RouteConfiguration route_config;
  auto json_object_ptr = Json::Factory::loadFromString(json_string);
  Envoy::Config::RdsJson::translateRouteConfiguration(*json_object_ptr, route_config);
  return route_config;
}

TEST(RouteMatcherTest, TestRoutes) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["lyft.com", "www.lyft.com", "w.lyft.com", "ww.lyft.com", "wwww.lyft.com"],
      "routes": [
        {
          "prefix": "/new_endpoint",
          "prefix_rewrite": "/api/new_endpoint",
          "cluster": "www2"
        },
        {
          "path": "/",
          "cluster": "root_www2"
        },
        {
          "prefix": "/",
          "cluster": "www2"
        }
      ]
    },
    {
      "name": "www2_staging",
      "domains": ["www-staging.lyft.net", "www-staging-orca.lyft.com"],
      "routes": [
        {
          "prefix": "/",
          "cluster": "www2_staging"
        }
      ]
    },
    {
      "name": "wildcard",
      "domains": ["*.foo.com", "*-bar.baz.com"],
      "routes": [
        {
          "prefix": "/",
          "cluster": "wildcard"
        }
      ]
    },
    {
      "name": "wildcard2",
      "domains": ["*.baz.com"],
      "routes": [
        {
          "prefix": "/",
          "cluster": "wildcard2"
        }
      ]
    },
    {
      "name": "regex",
      "domains": ["bat.com"],
      "routes": [
        {
          "regex": "/t[io]c",
          "cluster": "clock"
        },
        {
          "regex": "/baa+",
          "cluster": "sheep"
        },
        {
          "regex": ".*/\\d{3}$",
          "cluster": "three_numbers",
          "prefix_rewrite": "/rewrote"
        },
        {
          "regex": ".*",
          "cluster": "regex_default"
        }
      ]
    },
    {
      "name": "regex2",
      "domains": ["bat2.com"],
      "routes": [
        {
          "regex": "",
          "cluster": "nothingness"
        },
        {
          "regex": ".*",
          "cluster": "regex_default"
        }
      ]
    },
    {
      "name": "default",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/api/application_data",
          "cluster": "ats"
        },
        {
          "path": "/api/locations",
          "cluster": "locations",
          "prefix_rewrite": "/rewrote",
          "case_sensitive": false
        },
        {
          "prefix": "/api/leads/me",
          "cluster": "ats"
        },
        {
          "prefix": "/host/rewrite/me",
          "cluster": "ats",
          "host_rewrite": "new_host"
        },
        {
          "prefix": "/oldhost/rewrite/me",
          "cluster": "ats",
          "host_rewrite": "new_oldhost"
        },
        {
          "path": "/foo",
          "prefix_rewrite": "/bar",
          "cluster": "instant-server",
          "case_sensitive": true
        },
        {
          "path": "/tar",
          "prefix_rewrite": "/car",
          "cluster": "instant-server",
          "case_sensitive": false
        },
        {
          "prefix": "/newhost/rewrite/me",
          "cluster": "ats",
          "host_rewrite": "new_host",
          "case_sensitive": false
        },
        {
          "path": "/FOOD",
          "prefix_rewrite": "/cAndy",
          "cluster": "ats",
          "case_sensitive":false
        },
        {
          "path": "/ApplEs",
          "prefix_rewrite": "/oranGES",
          "cluster": "instant-server",
          "case_sensitive": true
        },
        {
          "prefix": "/",
          "cluster": "instant-server",
          "timeout_ms": 30000
        }],
      "virtual_clusters": [
        {"pattern": "^/rides$", "method": "POST", "name": "ride_request"},
        {"pattern": "^/rides/\\d+$", "method": "PUT", "name": "update_ride"},
        {"pattern": "^/users/\\d+/chargeaccounts$", "method": "POST", "name": "cc_add"},
        {"pattern": "^/users/\\d+/chargeaccounts/(?!validate)\\w+$", "method": "PUT",
         "name": "cc_add"},
        {"pattern": "^/users$", "method": "POST", "name": "create_user_login"},
        {"pattern": "^/users/\\d+$", "method": "PUT", "name": "update_user"},
        {"pattern": "^/users/\\d+/location$", "method": "POST", "name": "ulu"}]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  NiceMock<Envoy::Http::AccessLog::MockRequestInfo> request_info;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  EXPECT_FALSE(config.usesRuntime());

  // Base routing testing.
  EXPECT_EQ("instant-server",
            config.route(genHeaders("api.lyft.com", "/", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("ats", config.route(genHeaders("api.lyft.com", "/api/leads/me", "GET"), 0)
                       ->routeEntry()
                       ->clusterName());
  EXPECT_EQ("ats", config.route(genHeaders("api.lyft.com", "/api/application_data", "GET"), 0)
                       ->routeEntry()
                       ->clusterName());

  EXPECT_EQ("locations",
            config.route(genHeaders("api.lyft.com", "/api/locations?works=true", "GET"), 0)
                ->routeEntry()
                ->clusterName());
  EXPECT_EQ("locations", config.route(genHeaders("api.lyft.com", "/api/locations", "GET"), 0)
                             ->routeEntry()
                             ->clusterName());
  EXPECT_EQ("www2",
            config.route(genHeaders("lyft.com", "/foo", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("root_www2",
            config.route(genHeaders("wwww.lyft.com", "/", "GET"), 0)->routeEntry()->clusterName());

  // Wildcards
  EXPECT_EQ("wildcard",
            config.route(genHeaders("www.foo.com", "/", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ(
      "wildcard",
      config.route(genHeaders("foo-bar.baz.com", "/", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("wildcard2",
            config.route(genHeaders("-bar.baz.com", "/", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("wildcard2",
            config.route(genHeaders("bar.baz.com", "/", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("instant-server",
            config.route(genHeaders(".foo.com", "/", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("instant-server",
            config.route(genHeaders("foo.com", "/", "GET"), 0)->routeEntry()->clusterName());

  // Regular Expression matching
  EXPECT_EQ("clock",
            config.route(genHeaders("bat.com", "/tic", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("clock",
            config.route(genHeaders("bat.com", "/toc", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("regex_default",
            config.route(genHeaders("bat.com", "/tac", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("regex_default",
            config.route(genHeaders("bat.com", "", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("regex_default",
            config.route(genHeaders("bat.com", "/tick", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("regex_default",
            config.route(genHeaders("bat.com", "/tic/toc", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("sheep",
            config.route(genHeaders("bat.com", "/baa", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ(
      "sheep",
      config.route(genHeaders("bat.com", "/baaaaaaaaaaaa", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("regex_default",
            config.route(genHeaders("bat.com", "/ba", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("nothingness",
            config.route(genHeaders("bat2.com", "", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("regex_default",
            config.route(genHeaders("bat2.com", "/foo", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ("regex_default",
            config.route(genHeaders("bat2.com", " ", "GET"), 0)->routeEntry()->clusterName());

  // Regular Expression matching with query string params
  EXPECT_EQ(
      "clock",
      config.route(genHeaders("bat.com", "/tic?tac=true", "GET"), 0)->routeEntry()->clusterName());
  EXPECT_EQ(
      "regex_default",
      config.route(genHeaders("bat.com", "/tac?tic=true", "GET"), 0)->routeEntry()->clusterName());

  // Timeout testing.
  EXPECT_EQ(std::chrono::milliseconds(30000),
            config.route(genHeaders("api.lyft.com", "/", "GET"), 0)->routeEntry()->timeout());
  EXPECT_EQ(
      std::chrono::milliseconds(15000),
      config.route(genHeaders("api.lyft.com", "/api/leads/me", "GET"), 0)->routeEntry()->timeout());

  // Prefix rewrite testing.
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/new_endpoint/foo", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    EXPECT_EQ("www2", route->clusterName());
    EXPECT_EQ("www2", route->virtualHost().name());
    route->finalizeRequestHeaders(headers, request_info);
    EXPECT_EQ("/api/new_endpoint/foo", headers.get_(Http::Headers::get().Path));
  }

  // Prefix rewrite on path match with query string params
  {
    Http::TestHeaderMapImpl headers =
        genHeaders("api.lyft.com", "/api/locations?works=true", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, request_info);
    EXPECT_EQ("/rewrote?works=true", headers.get_(Http::Headers::get().Path));
  }

  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/foo", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, request_info);
    EXPECT_EQ("/bar", headers.get_(Http::Headers::get().Path));
  }

  // Host rewrite testing.
  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/host/rewrite/me", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, request_info);
    EXPECT_EQ("new_host", headers.get_(Http::Headers::get().Host));
  }

  // Case sensitive rewrite matching test.
  {
    Http::TestHeaderMapImpl headers =
        genHeaders("api.lyft.com", "/API/locations?works=true", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, request_info);
    EXPECT_EQ("/rewrote?works=true", headers.get_(Http::Headers::get().Path));
  }

  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/fooD", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, request_info);
    EXPECT_EQ("/cAndy", headers.get_(Http::Headers::get().Path));
  }

  // Case sensitive is set to true and will not rewrite
  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/FOO", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, request_info);
    EXPECT_EQ("/FOO", headers.get_(Http::Headers::get().Path));
  }

  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/ApPles", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, request_info);
    EXPECT_EQ("/ApPles", headers.get_(Http::Headers::get().Path));
  }

  // Case insensitive set to false so there is no rewrite
  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/oLDhost/rewrite/me", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, request_info);
    EXPECT_EQ("api.lyft.com", headers.get_(Http::Headers::get().Host));
  }

  // Case sensitive is set to false and will not rewrite
  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/Tart", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, request_info);
    EXPECT_EQ("/Tart", headers.get_(Http::Headers::get().Path));
  }

  // Case sensitive is set to false and will not rewrite
  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/newhost/rewrite/me", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, request_info);
    EXPECT_EQ("new_host", headers.get_(Http::Headers::get().Host));
  }

  // Prefix rewrite for regular expression matching
  {
    Http::TestHeaderMapImpl headers = genHeaders("bat.com", "/647", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, request_info);
    EXPECT_EQ("/rewrote", headers.get_(Http::Headers::get().Path));
  }

  // Prefix rewrite for regular expression matching with query string
  {
    Http::TestHeaderMapImpl headers = genHeaders("bat.com", "/970?foo=true", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, request_info);
    EXPECT_EQ("/rewrote?foo=true", headers.get_(Http::Headers::get().Path));
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("bat.com", "/foo/bar/238?bar=true", "GET");
    const RouteEntry* route = config.route(headers, 0)->routeEntry();
    route->finalizeRequestHeaders(headers, request_info);
    EXPECT_EQ("/rewrote?bar=true", headers.get_(Http::Headers::get().Path));
  }

  // Virtual cluster testing.
  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/rides", "GET");
    EXPECT_EQ("other", config.route(headers, 0)->routeEntry()->virtualCluster(headers)->name());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/rides/blah", "POST");
    EXPECT_EQ("other", config.route(headers, 0)->routeEntry()->virtualCluster(headers)->name());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/rides", "POST");
    EXPECT_EQ("ride_request",
              config.route(headers, 0)->routeEntry()->virtualCluster(headers)->name());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/rides/123", "PUT");
    EXPECT_EQ("update_ride",
              config.route(headers, 0)->routeEntry()->virtualCluster(headers)->name());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/rides/123/456", "POST");
    EXPECT_EQ("other", config.route(headers, 0)->routeEntry()->virtualCluster(headers)->name());
  }
  {
    Http::TestHeaderMapImpl headers =
        genHeaders("api.lyft.com", "/users/123/chargeaccounts", "POST");
    EXPECT_EQ("cc_add", config.route(headers, 0)->routeEntry()->virtualCluster(headers)->name());
  }
  {
    Http::TestHeaderMapImpl headers =
        genHeaders("api.lyft.com", "/users/123/chargeaccounts/hello123", "PUT");
    EXPECT_EQ("cc_add", config.route(headers, 0)->routeEntry()->virtualCluster(headers)->name());
  }
  {
    Http::TestHeaderMapImpl headers =
        genHeaders("api.lyft.com", "/users/123/chargeaccounts/validate", "PUT");
    EXPECT_EQ("other", config.route(headers, 0)->routeEntry()->virtualCluster(headers)->name());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/foo/bar", "PUT");
    EXPECT_EQ("other", config.route(headers, 0)->routeEntry()->virtualCluster(headers)->name());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/users", "POST");
    EXPECT_EQ("create_user_login",
              config.route(headers, 0)->routeEntry()->virtualCluster(headers)->name());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/users/123", "PUT");
    EXPECT_EQ("update_user",
              config.route(headers, 0)->routeEntry()->virtualCluster(headers)->name());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/users/123/location", "POST");
    EXPECT_EQ("ulu", config.route(headers, 0)->routeEntry()->virtualCluster(headers)->name());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/something/else", "GET");
    EXPECT_EQ("other", config.route(headers, 0)->routeEntry()->virtualCluster(headers)->name());
  }
}

TEST(RouteMatcherTest, TestAddRemoveReqRespHeaders) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["lyft.com", "www.lyft.com", "w.lyft.com", "ww.lyft.com", "wwww.lyft.com"],
      "request_headers_to_add": [
          {"key": "x-global-header1", "value": "vhost-override"},
          {"key": "x-vhost-header1", "value": "vhost1-www2"}
      ],
      "routes": [
        {
          "prefix": "/new_endpoint",
          "prefix_rewrite": "/api/new_endpoint",
          "cluster": "www2",
          "request_headers_to_add": [
             {"key": "x-global-header1", "value": "route-override"},
             {"key": "x-vhost-header1", "value": "route-override"},
             {"key": "x-route-header", "value": "route-new_endpoint"}
          ]
        },
        {
          "path": "/",
          "cluster": "root_www2",
          "request_headers_to_add": [
             {"key": "x-route-header", "value": "route-allpath"}
          ]
        },
        {
          "prefix": "/",
          "cluster": "www2"
        }
      ]
    },
    {
      "name": "www2_staging",
      "domains": ["www-staging.lyft.net", "www-staging-orca.lyft.com"],
      "request_headers_to_add": [
          {"key": "x-vhost-header1", "value": "vhost1-www2_staging"}
      ],
      "routes": [
        {
          "prefix": "/",
          "cluster": "www2_staging",
          "request_headers_to_add": [
             {"key": "x-route-header", "value": "route-allprefix"}
          ]
        }
      ]
    },
    {
      "name": "default",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/",
          "cluster": "instant-server",
          "timeout_ms": 30000
        }
      ]
    }
  ],

  "internal_only_headers": [
    "x-lyft-user-id"
  ],

  "response_headers_to_add": [
    {"key": "x-envoy-upstream-canary", "value": "true"}
  ],

  "response_headers_to_remove": [
    "x-envoy-upstream-canary",
    "x-envoy-virtual-cluster"
  ],

  "request_headers_to_add": [
    {"key": "x-global-header1", "value": "global1"}
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  NiceMock<Envoy::Http::AccessLog::MockRequestInfo> request_info;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  // Request header manipulation testing.
  {
    {
      Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/new_endpoint/foo", "GET");
      const RouteEntry* route = config.route(headers, 0)->routeEntry();
      route->finalizeRequestHeaders(headers, request_info);
      EXPECT_EQ("route-override", headers.get_("x-global-header1"));
      EXPECT_EQ("route-override", headers.get_("x-vhost-header1"));
      EXPECT_EQ("route-new_endpoint", headers.get_("x-route-header"));
    }

    // Multiple routes can have same route-level headers with different values.
    {
      Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
      const RouteEntry* route = config.route(headers, 0)->routeEntry();
      route->finalizeRequestHeaders(headers, request_info);
      EXPECT_EQ("vhost-override", headers.get_("x-global-header1"));
      EXPECT_EQ("vhost1-www2", headers.get_("x-vhost-header1"));
      EXPECT_EQ("route-allpath", headers.get_("x-route-header"));
    }

    // Multiple virtual hosts can have same virtual host level headers with different values.
    {
      Http::TestHeaderMapImpl headers = genHeaders("www-staging.lyft.net", "/foo", "GET");
      const RouteEntry* route = config.route(headers, 0)->routeEntry();
      route->finalizeRequestHeaders(headers, request_info);
      EXPECT_EQ("global1", headers.get_("x-global-header1"));
      EXPECT_EQ("vhost1-www2_staging", headers.get_("x-vhost-header1"));
      EXPECT_EQ("route-allprefix", headers.get_("x-route-header"));
    }

    // Global headers.
    {
      Http::TestHeaderMapImpl headers = genHeaders("api.lyft.com", "/", "GET");
      const RouteEntry* route = config.route(headers, 0)->routeEntry();
      route->finalizeRequestHeaders(headers, request_info);
      EXPECT_EQ("global1", headers.get_("x-global-header1"));
    }
  }

  // Response header manipulation testing.
  EXPECT_THAT(std::list<Http::LowerCaseString>{Http::LowerCaseString("x-lyft-user-id")},
              ContainerEq(config.internalOnlyHeaders()));
  EXPECT_THAT((std::list<std::pair<Http::LowerCaseString, std::string>>(
                  {{Http::LowerCaseString("x-envoy-upstream-canary"), "true"}})),
              ContainerEq(config.responseHeadersToAdd()));
  EXPECT_THAT(std::list<Http::LowerCaseString>({Http::LowerCaseString("x-envoy-upstream-canary"),
                                                Http::LowerCaseString("x-envoy-virtual-cluster")}),
              ContainerEq(config.responseHeadersToRemove()));
}

TEST(RouteMatcherTest, Priority) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "local_service",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/foo",
          "cluster": "local_service_grpc",
          "priority": "high"
        },
        {
          "prefix": "/bar",
          "cluster": "local_service_grpc"
        }
      ],
      "virtual_clusters": [
        {"pattern": "^/bar$", "method": "POST", "name": "foo"}]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  EXPECT_FALSE(config.usesRuntime());

  EXPECT_EQ(Upstream::ResourcePriority::High,
            config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)->routeEntry()->priority());
  EXPECT_EQ(Upstream::ResourcePriority::Default,
            config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)->routeEntry()->priority());
}

TEST(RouteMatcherTest, NoHostRewriteAndAutoRewrite) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "local_service",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/",
          "cluster": "local_service",
          "host_rewrite": "foo",
          "auto_host_rewrite" : true
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_THROW(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

TEST(RouteMatcherTest, NoRedirectAndWebSocket) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "local_service",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/foo",
          "host_redirect": "new.lyft.com",
          "use_websocket": true
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_THROW(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

TEST(RouteMatcherTest, HeaderMatchedRouting) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "local_service",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/",
          "cluster": "local_service_with_headers",
          "headers" : [
            {"name": "test_header", "value": "test"}
          ]
        },
        {
          "prefix": "/",
          "cluster": "local_service_with_multiple_headers",
          "headers" : [
            {"name": "test_header_multiple1", "value": "test1"},
            {"name": "test_header_multiple2", "value": "test2"}
          ]
        },
        {
          "prefix": "/",
          "cluster": "local_service_with_empty_headers",
          "headers" : [
            {"name": "test_header_presence"}
          ]
        },
        {
          "prefix": "/",
          "cluster": "local_service_with_header_pattern_set_regex",
          "headers" : [
            {"name": "test_header_pattern", "value": "^user=test-\\d+$", "regex": true}
          ]
        },
        {
          "prefix": "/",
          "cluster": "local_service_with_header_pattern_unset_regex",
          "headers" : [
            {"name": "test_header_pattern", "value": "^customer=test-\\d+$"}
          ]
        },
        {
          "prefix": "/",
          "cluster": "local_service_without_headers"
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  EXPECT_FALSE(config.usesRuntime());

  {
    EXPECT_EQ("local_service_without_headers",
              config.route(genHeaders("www.lyft.com", "/", "GET"), 0)->routeEntry()->clusterName());
  }

  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("test_header", "test");
    EXPECT_EQ("local_service_with_headers", config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("test_header_multiple1", "test1");
    headers.addCopy("test_header_multiple2", "test2");
    EXPECT_EQ("local_service_with_multiple_headers",
              config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("non_existent_header", "foo");
    EXPECT_EQ("local_service_without_headers",
              config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("test_header_presence", "test");
    EXPECT_EQ("local_service_with_empty_headers",
              config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("test_header_pattern", "user=test-1223");
    EXPECT_EQ("local_service_with_header_pattern_set_regex",
              config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("test_header_pattern", "customer=test-1223");
    EXPECT_EQ("local_service_without_headers",
              config.route(headers, 0)->routeEntry()->clusterName());
  }
}

class RouterMatcherHashPolicyTest : public testing::Test {
public:
  RouterMatcherHashPolicyTest() {
    std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "local_service",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/foo",
          "cluster": "foo"
        },
        {
          "prefix": "/bar",
          "cluster": "bar"
        }
      ]
    }
  ]
}
  )EOF";
    route_config_ = parseRouteConfigurationFromJson(json);
  }

  envoy::api::v2::RouteConfiguration route_config_;
};

TEST_F(RouterMatcherHashPolicyTest, HashHeaders) {
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  route_config_.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->add_hash_policy()
      ->mutable_header()
      ->set_header_name("foo_header");
  ConfigImpl config(route_config_, runtime, cm, true);

  EXPECT_FALSE(config.usesRuntime());

  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config.route(headers, 0);
    EXPECT_FALSE(route->routeEntry()->hashPolicy()->generateHash("", headers).valid());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    headers.addCopy("foo_header", "bar");
    Router::RouteConstSharedPtr route = config.route(headers, 0);
    EXPECT_TRUE(route->routeEntry()->hashPolicy()->generateHash("", headers).valid());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/bar", "GET");
    Router::RouteConstSharedPtr route = config.route(headers, 0);
    EXPECT_EQ(nullptr, route->routeEntry()->hashPolicy());
  }
}

TEST_F(RouterMatcherHashPolicyTest, HashIp) {
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  route_config_.mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_route()
      ->add_hash_policy()
      ->mutable_connection_properties()
      ->set_source_ip(true);
  ConfigImpl config(route_config_, runtime, cm, true);

  EXPECT_FALSE(config.usesRuntime());

  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config.route(headers, 0);
    EXPECT_FALSE(route->routeEntry()->hashPolicy()->generateHash("", headers).valid());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config.route(headers, 0);
    EXPECT_TRUE(route->routeEntry()->hashPolicy()->generateHash("1.2.3.4", headers).valid());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    uint64_t old_hash = config.route(headers, 0)
                            ->routeEntry()
                            ->hashPolicy()
                            ->generateHash("1.2.3.4", headers)
                            .value();
    headers.addCopy("foo_header", "bar");
    EXPECT_EQ(old_hash, config.route(headers, 0)
                            ->routeEntry()
                            ->hashPolicy()
                            ->generateHash("1.2.3.4", headers)
                            .value());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    const auto hash_policy = config.route(headers, 0)->routeEntry()->hashPolicy();
    const uint64_t hash_1 = hash_policy->generateHash("1.2.3.4", headers).value();
    const uint64_t hash_2 = hash_policy->generateHash("4.3.2.1", headers).value();
    EXPECT_NE(hash_1, hash_2);
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/bar", "GET");
    Router::RouteConstSharedPtr route = config.route(headers, 0);
    EXPECT_EQ(nullptr, route->routeEntry()->hashPolicy());
  }
}

TEST_F(RouterMatcherHashPolicyTest, HashMultiple) {
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  auto route = route_config_.mutable_virtual_hosts(0)->mutable_routes(0)->mutable_route();
  route->add_hash_policy()->mutable_header()->set_header_name("foo_header");
  route->add_hash_policy()->mutable_connection_properties()->set_source_ip(true);
  ConfigImpl config(route_config_, runtime, cm, true);

  EXPECT_FALSE(config.usesRuntime());

  uint64_t hash_h, hash_ip, hash_both;
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config.route(headers, 0);
    EXPECT_FALSE(route->routeEntry()->hashPolicy()->generateHash("", headers).valid());
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    headers.addCopy("foo_header", "bar");
    Router::RouteConstSharedPtr route = config.route(headers, 0);
    hash_h = route->routeEntry()->hashPolicy()->generateHash("", headers).value();
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config.route(headers, 0);
    hash_ip = route->routeEntry()->hashPolicy()->generateHash("4.2.1.3", headers).value();
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config.route(headers, 0);
    headers.addCopy("foo_header", "bar");
    hash_both = route->routeEntry()->hashPolicy()->generateHash("4.2.1.3", headers).value();
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config.route(headers, 0);
    headers.addCopy("foo_header", "bar");
    // stability
    EXPECT_EQ(hash_both,
              route->routeEntry()->hashPolicy()->generateHash("4.2.1.3", headers).value());
  }
  EXPECT_NE(hash_ip, hash_h);
  EXPECT_NE(hash_ip, hash_both);
  EXPECT_NE(hash_h, hash_both);
}

TEST_F(RouterMatcherHashPolicyTest, InvalidHashPolicies) {
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  {
    auto hash_policy = route_config_.mutable_virtual_hosts(0)
                           ->mutable_routes(0)
                           ->mutable_route()
                           ->add_hash_policy();
    EXPECT_EQ(envoy::api::v2::RouteAction::HashPolicy::POLICY_SPECIFIER_NOT_SET,
              hash_policy->policy_specifier_case());
    EXPECT_THROW({ ConfigImpl config(route_config_, runtime, cm, true); }, EnvoyException);
  }
  {
    auto route = route_config_.mutable_virtual_hosts(0)->mutable_routes(0)->mutable_route();
    route->add_hash_policy()->mutable_header()->set_header_name("foo_header");
    route->add_hash_policy()->mutable_connection_properties()->set_source_ip(true);
    auto hash_policy = route->add_hash_policy();
    EXPECT_EQ(envoy::api::v2::RouteAction::HashPolicy::POLICY_SPECIFIER_NOT_SET,
              hash_policy->policy_specifier_case());
    EXPECT_THROW({ ConfigImpl config(route_config_, runtime, cm, true); }, EnvoyException);
  }
}

TEST(RouteMatcherTest, ClusterHeader) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "local_service",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/foo",
          "cluster_header": ":authority"
        },
        {
          "prefix": "/bar",
          "cluster_header": "some_header",
          "timeout_ms": 0
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  NiceMock<Envoy::Http::AccessLog::MockRequestInfo> request_info;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  EXPECT_FALSE(config.usesRuntime());

  EXPECT_EQ(
      "some_cluster",
      config.route(genHeaders("some_cluster", "/foo", "GET"), 0)->routeEntry()->clusterName());

  EXPECT_EQ(
      "", config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)->routeEntry()->clusterName());

  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/bar", "GET");
    headers.addCopy("some_header", "some_cluster");
    Router::RouteConstSharedPtr route = config.route(headers, 0);
    EXPECT_EQ("some_cluster", route->routeEntry()->clusterName());

    // Make sure things forward and don't crash.
    EXPECT_EQ(std::chrono::milliseconds(0), route->routeEntry()->timeout());
    route->routeEntry()->finalizeRequestHeaders(headers, request_info);
    route->routeEntry()->priority();
    route->routeEntry()->rateLimitPolicy();
    route->routeEntry()->retryPolicy();
    route->routeEntry()->shadowPolicy();
    route->routeEntry()->virtualCluster(headers);
    route->routeEntry()->virtualHost();
    route->routeEntry()->virtualHost().rateLimitPolicy();
  }
}

TEST(RouteMatcherTest, ContentType) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "local_service",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/",
          "cluster": "local_service_grpc",
          "headers" : [
            {"name": "content-type", "value": "application/grpc"}
          ]
        },
        {
          "prefix": "/",
          "cluster": "local_service"
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  EXPECT_FALSE(config.usesRuntime());

  {
    EXPECT_EQ("local_service",
              config.route(genHeaders("www.lyft.com", "/", "GET"), 0)->routeEntry()->clusterName());
  }

  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("content-type", "application/grpc");
    EXPECT_EQ("local_service_grpc", config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/", "GET");
    headers.addCopy("content-type", "foo");
    EXPECT_EQ("local_service", config.route(headers, 0)->routeEntry()->clusterName());
  }
}

TEST(RouteMatcherTest, Runtime) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/",
          "cluster": "something_else",
          "runtime": {
            "key": "some_key",
            "default": 50
          }
        },
        {
          "prefix": "/",
          "cluster": "www2"
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  Runtime::MockSnapshot snapshot;

  ON_CALL(runtime, snapshot()).WillByDefault(ReturnRef(snapshot));

  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  EXPECT_TRUE(config.usesRuntime());

  EXPECT_CALL(snapshot, featureEnabled("some_key", 50, 10)).WillOnce(Return(true));
  EXPECT_EQ("something_else",
            config.route(genHeaders("www.lyft.com", "/", "GET"), 10)->routeEntry()->clusterName());

  EXPECT_CALL(snapshot, featureEnabled("some_key", 50, 20)).WillOnce(Return(false));
  EXPECT_EQ("www2",
            config.route(genHeaders("www.lyft.com", "/", "GET"), 20)->routeEntry()->clusterName());
}

TEST(RouteMatcherTest, ShadowClusterNotFound) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/foo",
          "shadow": {
            "cluster": "some_cluster"
          },
          "cluster": "www2"
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_CALL(cm, get("www2")).WillRepeatedly(Return(&cm.thread_local_cluster_));
  EXPECT_CALL(cm, get("some_cluster")).WillRepeatedly(Return(nullptr));

  EXPECT_THROW(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

TEST(RouteMatcherTest, ClusterNotFound) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/foo",
          "cluster": "www2"
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_CALL(cm, get("www2")).WillRepeatedly(Return(nullptr));

  EXPECT_THROW(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

TEST(RouteMatcherTest, ClusterNotFoundNotChecking) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/foo",
          "cluster": "www2"
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_CALL(cm, get("www2")).WillRepeatedly(Return(nullptr));

  ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, false);
}

TEST(RouteMatcherTest, ClusterNotFoundNotCheckingViaConfig) {
  std::string json = R"EOF(
{
  "validate_clusters": false,
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/foo",
          "cluster": "www2"
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_CALL(cm, get("www2")).WillRepeatedly(Return(nullptr));

  ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true);
}

TEST(RouteMatcherTest, Shadow) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/foo",
          "shadow": {
            "cluster": "some_cluster"
          },
          "cluster": "www2"
        },
        {
          "prefix": "/bar",
          "shadow": {
            "cluster": "some_cluster2",
            "runtime_key": "foo"
          },
          "cluster": "www2"
        },
        {
          "prefix": "/baz",
          "cluster": "www2"
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  EXPECT_TRUE(config.usesRuntime());

  EXPECT_EQ("some_cluster", config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                                ->routeEntry()
                                ->shadowPolicy()
                                .cluster());
  EXPECT_EQ("", config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                    ->routeEntry()
                    ->shadowPolicy()
                    .runtimeKey());

  EXPECT_EQ("some_cluster2", config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                                 ->routeEntry()
                                 ->shadowPolicy()
                                 .cluster());
  EXPECT_EQ("foo", config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                       ->routeEntry()
                       ->shadowPolicy()
                       .runtimeKey());

  EXPECT_EQ("", config.route(genHeaders("www.lyft.com", "/baz", "GET"), 0)
                    ->routeEntry()
                    ->shadowPolicy()
                    .cluster());
  EXPECT_EQ("", config.route(genHeaders("www.lyft.com", "/baz", "GET"), 0)
                    ->routeEntry()
                    ->shadowPolicy()
                    .runtimeKey());
}

TEST(RouteMatcherTest, Retry) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/foo",
          "cluster": "www2",
          "retry_policy": {
            "retry_on": "connect-failure"
          }
        },
        {
          "prefix": "/bar",
          "cluster": "www2"
        },
        {
          "prefix": "/",
          "cluster": "www2",
          "retry_policy": {
            "per_try_timeout_ms" : 1000,
            "num_retries": 3,
            "retry_on": "5xx,connect-failure"
          }
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  EXPECT_FALSE(config.usesRuntime());

  EXPECT_EQ(std::chrono::milliseconds(0),
            config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .perTryTimeout());
  EXPECT_EQ(1U, config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                    ->routeEntry()
                    ->retryPolicy()
                    .numRetries());
  EXPECT_EQ(RetryPolicy::RETRY_ON_CONNECT_FAILURE,
            config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .retryOn());

  EXPECT_EQ(std::chrono::milliseconds(0),
            config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .perTryTimeout());
  EXPECT_EQ(0U, config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                    ->routeEntry()
                    ->retryPolicy()
                    .numRetries());
  EXPECT_EQ(0U, config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                    ->routeEntry()
                    ->retryPolicy()
                    .retryOn());

  EXPECT_EQ(std::chrono::milliseconds(1000),
            config.route(genHeaders("www.lyft.com", "/", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .perTryTimeout());
  EXPECT_EQ(3U, config.route(genHeaders("www.lyft.com", "/", "GET"), 0)
                    ->routeEntry()
                    ->retryPolicy()
                    .numRetries());
  EXPECT_EQ(RetryPolicy::RETRY_ON_CONNECT_FAILURE | RetryPolicy::RETRY_ON_5XX,
            config.route(genHeaders("www.lyft.com", "/", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .retryOn());
}

TEST(RouteMatcherTest, GrpcRetry) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/foo",
          "cluster": "www2",
          "retry_policy": {
            "retry_on": "connect-failure"
          }
        },
        {
          "prefix": "/bar",
          "cluster": "www2"
        },
        {
          "prefix": "/",
          "cluster": "www2",
          "retry_policy": {
            "per_try_timeout_ms" : 1000,
            "num_retries": 3,
            "retry_on": "5xx,deadline-exceeded,resource-exhausted"
          }
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  EXPECT_FALSE(config.usesRuntime());

  EXPECT_EQ(std::chrono::milliseconds(0),
            config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .perTryTimeout());
  EXPECT_EQ(1U, config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                    ->routeEntry()
                    ->retryPolicy()
                    .numRetries());
  EXPECT_EQ(RetryPolicy::RETRY_ON_CONNECT_FAILURE,
            config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .retryOn());

  EXPECT_EQ(std::chrono::milliseconds(0),
            config.route(genHeaders("www.lyft.com", "/foo", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .perTryTimeout());
  EXPECT_EQ(0U, config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                    ->routeEntry()
                    ->retryPolicy()
                    .numRetries());
  EXPECT_EQ(0U, config.route(genHeaders("www.lyft.com", "/bar", "GET"), 0)
                    ->routeEntry()
                    ->retryPolicy()
                    .retryOn());

  EXPECT_EQ(std::chrono::milliseconds(1000),
            config.route(genHeaders("www.lyft.com", "/", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .perTryTimeout());
  EXPECT_EQ(3U, config.route(genHeaders("www.lyft.com", "/", "GET"), 0)
                    ->routeEntry()
                    ->retryPolicy()
                    .numRetries());
  EXPECT_EQ(RetryPolicy::RETRY_ON_5XX | RetryPolicy::RETRY_ON_GRPC_DEADLINE_EXCEEDED |
                RetryPolicy::RETRY_ON_GRPC_RESOURCE_EXHAUSTED,
            config.route(genHeaders("www.lyft.com", "/", "GET"), 0)
                ->routeEntry()
                ->retryPolicy()
                .retryOn());
}

TEST(RouteMatcherTest, TestBadDefaultConfig) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/",
          "cluster": "www2"
        }
      ]
    },
    {
      "name": "www2_staging",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/",
          "cluster": "www2_staging"
        }
      ]
    }
  ],

  "internal_only_headers": [
    "x-lyft-user-id"
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_THROW(ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

TEST(RouteMatcherTest, TestDuplicateDomainConfig) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/",
          "cluster": "www2"
        }
      ]
    },
    {
      "name": "www2_staging",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/",
          "cluster": "www2_staging"
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_THROW(ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

static Http::TestHeaderMapImpl genRedirectHeaders(const std::string& host, const std::string& path,
                                                  bool ssl, bool internal) {
  Http::TestHeaderMapImpl headers{
      {":authority", host}, {":path", path}, {"x-forwarded-proto", ssl ? "https" : "http"}};
  if (internal) {
    headers.addCopy("x-envoy-internal", "true");
  }

  return headers;
}

TEST(RouteMatcherTest, Redirect) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "require_ssl": "all",
      "routes": [
        {
          "prefix": "/",
          "cluster": "www2"
        }
      ]
    },
    {
      "name": "api",
      "domains": ["api.lyft.com"],
      "require_ssl": "external_only",
      "routes": [
        {
          "prefix": "/",
          "cluster": "www2"
        }
      ]
    },
    {
      "name": "redirect",
      "domains": ["redirect.lyft.com"],
      "routes": [
        {
          "prefix": "/foo",
          "host_redirect": "new.lyft.com"
        },
        {
          "prefix": "/bar",
          "path_redirect": "/new_bar"
        },
        {
          "prefix": "/baz",
          "host_redirect": "new.lyft.com",
          "path_redirect": "/new_baz"
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  EXPECT_FALSE(config.usesRuntime());

  EXPECT_EQ(nullptr, config.route(genRedirectHeaders("www.foo.com", "/foo", true, true), 0));

  {
    Http::TestHeaderMapImpl headers = genRedirectHeaders("www.lyft.com", "/foo", true, true);
    EXPECT_EQ(nullptr, config.route(headers, 0)->redirectEntry());
  }
  {
    Http::TestHeaderMapImpl headers = genRedirectHeaders("www.lyft.com", "/foo", false, false);
    EXPECT_EQ("https://www.lyft.com/foo",
              config.route(headers, 0)->redirectEntry()->newPath(headers));
  }
  {
    Http::TestHeaderMapImpl headers = genRedirectHeaders("api.lyft.com", "/foo", false, true);
    EXPECT_EQ(nullptr, config.route(headers, 0)->redirectEntry());
  }
  {
    Http::TestHeaderMapImpl headers = genRedirectHeaders("api.lyft.com", "/foo", false, false);
    EXPECT_EQ("https://api.lyft.com/foo",
              config.route(headers, 0)->redirectEntry()->newPath(headers));
  }
  {
    Http::TestHeaderMapImpl headers = genRedirectHeaders("redirect.lyft.com", "/foo", false, false);
    EXPECT_EQ("http://new.lyft.com/foo",
              config.route(headers, 0)->redirectEntry()->newPath(headers));
  }
  {
    Http::TestHeaderMapImpl headers = genRedirectHeaders("redirect.lyft.com", "/bar", true, false);
    EXPECT_EQ("https://redirect.lyft.com/new_bar",
              config.route(headers, 0)->redirectEntry()->newPath(headers));
  }
  {
    Http::TestHeaderMapImpl headers = genRedirectHeaders("redirect.lyft.com", "/baz", true, false);
    EXPECT_EQ("https://new.lyft.com/new_baz",
              config.route(headers, 0)->redirectEntry()->newPath(headers));
  }
}

TEST(RouteMatcherTest, ExclusiveRouteEntryOrRedirectEntry) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/",
          "cluster": "www2"
        }
      ]
    },
    {
      "name": "redirect",
      "domains": ["redirect.lyft.com"],
      "routes": [
        {
          "prefix": "/foo",
          "host_redirect": "new.lyft.com"
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  {
    Http::TestHeaderMapImpl headers = genRedirectHeaders("www.lyft.com", "/foo", true, true);
    EXPECT_EQ(nullptr, config.route(headers, 0)->redirectEntry());
    EXPECT_EQ("www2", config.route(headers, 0)->routeEntry()->clusterName());
  }
  {
    Http::TestHeaderMapImpl headers = genRedirectHeaders("redirect.lyft.com", "/foo", false, false);
    EXPECT_EQ("http://new.lyft.com/foo",
              config.route(headers, 0)->redirectEntry()->newPath(headers));
    EXPECT_EQ(nullptr, config.route(headers, 0)->routeEntry());
  }
}

TEST(RouteMatcherTest, ExclusiveWeightedClustersEntryOrRedirectEntry) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/",
          "weighted_clusters": {
           "clusters" : [{ "name" : "www2", "weight" : 100 }]
          }
        }
      ]
    },
    {
      "name": "redirect",
      "domains": ["redirect.lyft.com"],
      "routes": [
        {
          "prefix": "/foo",
          "host_redirect": "new.lyft.com"
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  {
    Http::TestHeaderMapImpl headers = genRedirectHeaders("www.lyft.com", "/foo", true, true);
    EXPECT_EQ(nullptr, config.route(headers, 0)->redirectEntry());
    EXPECT_EQ("www2", config.route(headers, 0)->routeEntry()->clusterName());
  }

  {
    Http::TestHeaderMapImpl headers = genRedirectHeaders("redirect.lyft.com", "/foo", false, false);
    EXPECT_EQ("http://new.lyft.com/foo",
              config.route(headers, 0)->redirectEntry()->newPath(headers));
    EXPECT_EQ(nullptr, config.route(headers, 0)->routeEntry());
  }
}

TEST(RouteMatcherTest, WeightedClusters) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www1",
      "domains": ["www1.lyft.com"],
      "routes": [
        {
          "prefix": "/",
          "weighted_clusters": {
            "clusters" : [
              { "name" : "cluster1", "weight" : 30 },
              { "name" : "cluster2", "weight" : 30 },
              { "name" : "cluster3", "weight" : 40 }
            ]
          }
        }
      ]
    },
    {
      "name": "www2",
      "domains": ["www2.lyft.com"],
      "routes": [
        {
          "prefix": "/",
          "weighted_clusters": {
            "runtime_key_prefix" : "www2_weights",
            "clusters" : [
              { "name" : "cluster1", "weight" : 30 },
              { "name" : "cluster2", "weight" : 30 },
              { "name" : "cluster3", "weight" : 40 }
            ]
          }
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  {
    Http::TestHeaderMapImpl headers = genRedirectHeaders("www1.lyft.com", "/foo", true, true);
    EXPECT_EQ(nullptr, config.route(headers, 0)->redirectEntry());
  }

  // Weighted Cluster with no runtime
  {
    Http::TestHeaderMapImpl headers = genHeaders("www1.lyft.com", "/foo", "GET");
    EXPECT_EQ("cluster1", config.route(headers, 115)->routeEntry()->clusterName());
    EXPECT_EQ("cluster2", config.route(headers, 445)->routeEntry()->clusterName());
    EXPECT_EQ("cluster3", config.route(headers, 560)->routeEntry()->clusterName());
  }

  // Make sure weighted cluster entries call through to the parent when needed.
  {
    Http::TestHeaderMapImpl headers = genHeaders("www1.lyft.com", "/foo", "GET");
    const RouteEntry* route = config.route(headers, 115)->routeEntry();
    EXPECT_EQ(nullptr, route->hashPolicy());
    EXPECT_TRUE(route->opaqueConfig().empty());
    EXPECT_FALSE(route->autoHostRewrite());
    EXPECT_FALSE(route->useWebSocket());
    EXPECT_TRUE(route->includeVirtualHostRateLimits());
  }

  // Weighted Cluster with valid runtime values
  {
    Http::TestHeaderMapImpl headers = genHeaders("www2.lyft.com", "/foo", "GET");
    EXPECT_CALL(runtime.snapshot_, featureEnabled("www2", 100, _)).WillRepeatedly(Return(true));
    EXPECT_CALL(runtime.snapshot_, getInteger("www2_weights.cluster1", 30))
        .WillRepeatedly(Return(80));
    EXPECT_CALL(runtime.snapshot_, getInteger("www2_weights.cluster2", 30))
        .WillRepeatedly(Return(10));
    EXPECT_CALL(runtime.snapshot_, getInteger("www2_weights.cluster3", 40))
        .WillRepeatedly(Return(10));

    EXPECT_EQ("cluster1", config.route(headers, 45)->routeEntry()->clusterName());
    EXPECT_EQ("cluster2", config.route(headers, 82)->routeEntry()->clusterName());
    EXPECT_EQ("cluster3", config.route(headers, 92)->routeEntry()->clusterName());
  }

  // Weighted Cluster with invalid runtime values
  {
    Http::TestHeaderMapImpl headers = genHeaders("www2.lyft.com", "/foo", "GET");
    EXPECT_CALL(runtime.snapshot_, featureEnabled("www2", 100, _)).WillRepeatedly(Return(true));
    EXPECT_CALL(runtime.snapshot_, getInteger("www2_weights.cluster1", 30))
        .WillRepeatedly(Return(10));

    // We return an invalid value here, one that is greater than 100
    // Expect any random value > 10 to always land in cluster2.
    EXPECT_CALL(runtime.snapshot_, getInteger("www2_weights.cluster2", 30))
        .WillRepeatedly(Return(120));
    EXPECT_CALL(runtime.snapshot_, getInteger("www2_weights.cluster3", 40))
        .WillRepeatedly(Return(10));

    EXPECT_EQ("cluster1", config.route(headers, 1005)->routeEntry()->clusterName());
    EXPECT_EQ("cluster2", config.route(headers, 82)->routeEntry()->clusterName());
    EXPECT_EQ("cluster2", config.route(headers, 92)->routeEntry()->clusterName());
  }
}

TEST(RouteMatcherTest, ExclusiveWeightedClustersOrClusterConfig) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/",
          "weighted_clusters": {
            "clusters" : [
              { "name" : "cluster1", "weight" : 30 },
              { "name" : "cluster2", "weight" : 30 },
              { "name" : "cluster3", "weight" : 40 }
            ]
          },
          "cluster" : "www2"
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_THROW(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

TEST(RouteMatcherTest, WeightedClustersMissingClusterList) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/",
          "weighted_clusters": {
            "runtime_key_prefix" : "www2"
          }
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_THROW(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

TEST(RouteMatcherTest, WeightedClustersEmptyClustersList) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/",
          "weighted_clusters": {
            "runtime_key_prefix" : "www2",
            "clusters" : []
          }
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_THROW(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

TEST(RouteMatcherTest, WeightedClustersSumOFWeightsNotEqualToMax) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/",
          "weighted_clusters": {
            "clusters" : [
              { "name" : "cluster1", "weight" : 3 },
              { "name" : "cluster2", "weight" : 3 },
              { "name" : "cluster3", "weight" : 3 }
            ]
          }
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_THROW(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

TEST(RouteMatcherTest, TestWeightedClusterWithMissingWeights) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/",
          "weighted_clusters": {
            "clusters" : [
              { "name" : "cluster1", "weight" : 50 },
              { "name" : "cluster2", "weight" : 50 },
              { "name" : "cluster3"}
            ]
          }
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_THROW(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

TEST(RouteMatcherTest, TestWeightedClusterInvalidClusterName) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "www2",
      "domains": ["www.lyft.com"],
      "routes": [
        {
          "prefix": "/foo",
          "weighted_clusters": {
            "clusters" : [
              { "name" : "cluster1", "weight" : 33 },
              { "name" : "cluster2", "weight" : 33 },
              { "name" : "cluster3-invalid", "weight": 34}
            ]
          }
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  EXPECT_CALL(cm, get("cluster1")).WillRepeatedly(Return(&cm.thread_local_cluster_));
  EXPECT_CALL(cm, get("cluster2")).WillRepeatedly(Return(&cm.thread_local_cluster_));
  EXPECT_CALL(cm, get("cluster3-invalid")).WillRepeatedly(Return(nullptr));

  EXPECT_THROW(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

TEST(NullConfigImplTest, All) {
  NullConfigImpl config;
  Http::TestHeaderMapImpl headers = genRedirectHeaders("redirect.lyft.com", "/baz", true, false);
  EXPECT_EQ(nullptr, config.route(headers, 0));
  EXPECT_EQ(0UL, config.internalOnlyHeaders().size());
  EXPECT_EQ(0UL, config.responseHeadersToAdd().size());
  EXPECT_EQ(0UL, config.responseHeadersToRemove().size());
  EXPECT_FALSE(config.usesRuntime());
}

TEST(BadHttpRouteConfigurationsTest, BadRouteConfig) {
  std::string json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": ["*"],
        "routes": [
          {
            "prefix": "/",
            "cluster": "www2"
          }
        ]
      }
    ],
    "fake_entry" : "fake_type"
  }
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;

  EXPECT_THROW(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

TEST(BadHttpRouteConfigurationsTest, BadVirtualHostConfig) {
  std::string json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": ["*"],
        "router" : {
          "cluster" : "my_cluster"
        },
        "routes": [
          {
            "prefix": "/",
            "cluster": "www2"
          }
        ]
      }
    ]
  }
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;

  EXPECT_THROW(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

TEST(BadHttpRouteConfigurationsTest, BadRouteEntryConfig) {
  std::string json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": ["*"],
        "routes": [
          {
            "prefix": "/",
            "cluster": "www2",
            "timeout_ms" : "1234"
          }
        ]
      }
    ]
  }
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;

  EXPECT_THROW(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

TEST(BadHttpRouteConfigurationsTest, BadRouteEntryConfigPrefixAndPath) {
  std::string json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": ["*"],
        "routes": [
          {
            "prefix": "/",
            "path": "/foo",
            "cluster": "www2"
          }
        ]
      }
    ]
  }
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;

  EXPECT_THROW_WITH_MESSAGE(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
                            EnvoyException, "routes must specify one of prefix/path/regex");
}

TEST(BadHttpRouteConfigurationsTest, BadRouteEntryConfigPrefixAndRegex) {
  std::string json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": ["*"],
        "routes": [
          {
            "prefix": "/",
            "regex": "/[bc]at",
            "cluster": "www2"
          }
        ]
      }
    ]
  }
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;

  EXPECT_THROW_WITH_MESSAGE(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
                            EnvoyException, "routes must specify one of prefix/path/regex");
}

TEST(BadHttpRouteConfigurationsTest, BadRouteEntryConfigPathAndRegex) {
  std::string json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": ["*"],
        "routes": [
          {
            "path": "/foo",
            "regex": "/[bc]at",
            "cluster": "www2"
          }
        ]
      }
    ]
  }
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;

  EXPECT_THROW_WITH_MESSAGE(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
                            EnvoyException, "routes must specify one of prefix/path/regex");
  ;
}

TEST(BadHttpRouteConfigurationsTest, BadRouteEntryConfigPrefixAndPathAndRegex) {
  std::string json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": ["*"],
        "routes": [
          {
            "prefix": "/",
            "path": "/foo",
            "regex": "/[bc]at",
            "cluster": "www2"
          }
        ]
      }
    ]
  }
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;

  EXPECT_THROW_WITH_MESSAGE(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
                            EnvoyException, "routes must specify one of prefix/path/regex");
}

TEST(BadHttpRouteConfigurationsTest, BadRouteEntryConfigMissingPathSpecifier) {
  std::string json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": ["*"],
        "routes": [
          {
            "cluster": "www2"
          }
        ]
      }
    ]
  }
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;

  EXPECT_THROW_WITH_MESSAGE(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
                            EnvoyException, "routes must specify one of prefix/path/regex");
}

TEST(RouteMatcherTest, TestOpaqueConfig) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "default",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/api",
          "cluster": "ats",
          "opaque_config" : {
              "name1": "value1",
              "name2": "value2"
          }
        }
      ]
    }
  ]
}
)EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  const std::multimap<std::string, std::string>& opaque_config =
      config.route(genHeaders("api.lyft.com", "/api", "GET"), 0)->routeEntry()->opaqueConfig();

  EXPECT_EQ(opaque_config.find("name1")->second, "value1");
  EXPECT_EQ(opaque_config.find("name2")->second, "value2");
}

TEST(RoutePropertyTest, excludeVHRateLimits) {
  std::string json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": ["*"],
        "routes": [
          {
            "prefix": "/",
            "cluster": "www2"
          }
        ]
      }
    ]
  }
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
  std::unique_ptr<ConfigImpl> config_ptr;

  config_ptr.reset(new ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true));
  EXPECT_TRUE(config_ptr->route(headers, 0)->routeEntry()->includeVirtualHostRateLimits());

  json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": ["*"],
        "routes": [
          {
            "prefix": "/",
            "cluster": "www2",
            "rate_limits": [
              {
                "actions": [
                  {
                    "type": "remote_address"
                  }
                ]
              }
            ]
          }
        ]
      }
    ]
  }
  )EOF";

  config_ptr.reset(new ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true));
  EXPECT_FALSE(config_ptr->route(headers, 0)->routeEntry()->includeVirtualHostRateLimits());

  json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": ["*"],
        "routes": [
          {
            "prefix": "/",
            "cluster": "www2",
            "include_vh_rate_limits": true,
            "rate_limits": [
              {
                "actions": [
                  {
                    "type": "remote_address"
                  }
                ]
              }
            ]
          }
        ]
      }
    ]
  }
  )EOF";

  config_ptr.reset(new ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true));
  EXPECT_TRUE(config_ptr->route(headers, 0)->routeEntry()->includeVirtualHostRateLimits());
}

TEST(RoutePropertyTest, TestVHostCorsConfig) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "default",
      "domains": ["*"],
      "cors" : {
        "allow_origin": ["test-origin"],
        "allow_methods": "test-methods",
        "allow_headers": "test-headers",
        "expose_headers": "test-expose-headers",
        "max_age": "test-max-age",
        "allow_credentials": true
      },
      "routes": [
        {
          "prefix": "/api",
          "cluster": "ats"
        }
      ]
    }
  ]
}
)EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  const Router::CorsPolicy* cors_policy =
      config.route(genHeaders("api.lyft.com", "/api", "GET"), 0)
          ->routeEntry()
          ->virtualHost()
          .corsPolicy();

  EXPECT_EQ(cors_policy->enabled(), true);
  EXPECT_THAT(cors_policy->allowOrigins(), ElementsAreArray({"test-origin"}));
  EXPECT_EQ(cors_policy->allowMethods(), "test-methods");
  EXPECT_EQ(cors_policy->allowHeaders(), "test-headers");
  EXPECT_EQ(cors_policy->exposeHeaders(), "test-expose-headers");
  EXPECT_EQ(cors_policy->maxAge(), "test-max-age");
  EXPECT_EQ(cors_policy->allowCredentials(), true);
}

TEST(RoutePropertyTest, TestRouteCorsConfig) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "default",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/api",
          "cluster": "ats",
          "cors" : {
              "allow_origin": ["test-origin"],
              "allow_methods": "test-methods",
              "allow_headers": "test-headers",
              "expose_headers": "test-expose-headers",
              "max_age": "test-max-age",
              "allow_credentials": true
          }
        }
      ]
    }
  ]
}
)EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  const Router::CorsPolicy* cors_policy =
      config.route(genHeaders("api.lyft.com", "/api", "GET"), 0)->routeEntry()->corsPolicy();

  EXPECT_EQ(cors_policy->enabled(), true);
  EXPECT_THAT(cors_policy->allowOrigins(), ElementsAreArray({"test-origin"}));
  EXPECT_EQ(cors_policy->allowMethods(), "test-methods");
  EXPECT_EQ(cors_policy->allowHeaders(), "test-headers");
  EXPECT_EQ(cors_policy->exposeHeaders(), "test-expose-headers");
  EXPECT_EQ(cors_policy->maxAge(), "test-max-age");
  EXPECT_EQ(cors_policy->allowCredentials(), true);
}

TEST(RoutePropertyTest, TestBadCorsConfig) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "default",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/api",
          "cluster": "ats",
          "cors" : {
              "enabled": "true",
              "allow_credentials": "true"
          }
        }
      ]
    }
  ]
}
)EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;

  EXPECT_THROW(ConfigImpl(parseRouteConfigurationFromJson(json), runtime, cm, true),
               EnvoyException);
}

TEST(RouterMatcherTest, Decorator) {
  std::string json = R"EOF(
{
  "virtual_hosts": [
    {
      "name": "local_service",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/foo",
          "cluster": "foo",
          "decorator": {
            "operation": "myFoo"
          }
        },
        {
          "prefix": "/bar",
          "cluster": "bar"
        }
      ]
    }
  ]
}
  )EOF";

  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);

  EXPECT_FALSE(config.usesRuntime());

  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/foo", "GET");
    Router::RouteConstSharedPtr route = config.route(headers, 0);
    Tracing::MockSpan span;
    EXPECT_CALL(span, setOperation("myFoo"));
    route->decorator()->apply(span);
  }
  {
    Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/bar", "GET");
    Router::RouteConstSharedPtr route = config.route(headers, 0);
    EXPECT_EQ(nullptr, route->decorator());
  }
}

TEST(CustomRequestHeadersTest, AddNewHeader) {
  const std::string json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": [
          "lyft.com",
          "www.lyft.com",
          "w.lyft.com",
          "ww.lyft.com",
          "wwww.lyft.com"
        ],
        "request_headers_to_add": [
          {
            "key": "x-client-ip",
            "value": "%CLIENT_IP%"
          }
        ],
        "routes": [
          {
            "prefix": "/new_endpoint",
            "prefix_rewrite": "/api/new_endpoint",
            "cluster": "www2",
            "request_headers_to_add": [
              {
                "key": "x-client-ip",
                "value": "%CLIENT_IP%"
              }  
            ]
          }  
        ]
      }
    ],
    "request_headers_to_add": [
      {
        "key": "x-client-ip",
        "value": "%CLIENT_IP%"
      }
    ]
  }
  )EOF";
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  NiceMock<Envoy::Http::AccessLog::MockRequestInfo> request_info;
  ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true);
  const std::string downstream_addr = "127.0.0.1";
  Http::TestHeaderMapImpl headers = genHeaders("www.lyft.com", "/new_endpoint/foo", "GET");
  ON_CALL(request_info, getDownstreamAddress()).WillByDefault(ReturnRef(downstream_addr));
  const RouteEntry* route = config.route(headers, 0)->routeEntry();
  route->finalizeRequestHeaders(headers, request_info);
  EXPECT_EQ(downstream_addr, headers.get_("x-client-ip"));
}

TEST(CustomRequestHeadersTest, CustomHeaderWrongFormat) {
  const std::string json = R"EOF(
  {
    "virtual_hosts": [
      {
        "name": "www2",
        "domains": [
          "lyft.com",
          "www.lyft.com",
          "w.lyft.com",
          "ww.lyft.com",
          "wwww.lyft.com"
        ],
        "request_headers_to_add": [
          {
            "key": "x-client-ip",
            "value": "%CLIENT_IP%"
          }
        ],
        "routes": [
          {
            "prefix": "/new_endpoint",
            "prefix_rewrite": "/api/new_endpoint",
            "cluster": "www2",
            "request_headers_to_add": [
              {
                "key": "x-client-ip",
                "value": "%CLIENT_IP"
              }
            ]
          }
        ]
      }
    ],
    "request_headers_to_add": [
      {
        "key": "x-client-ip",
        "value": "%CLIENT_IP"
      }
    ]
  }
  )EOF";
  NiceMock<Runtime::MockLoader> runtime;
  NiceMock<Upstream::MockClusterManager> cm;
  NiceMock<Envoy::Http::AccessLog::MockRequestInfo> request_info;
  EXPECT_THROW_WITH_MESSAGE(
      ConfigImpl config(parseRouteConfigurationFromJson(json), runtime, cm, true), EnvoyException,
      "Incorrect header configuration. Expected variable format %<variable_name>%, actual format "
      "%CLIENT_IP");
}

} // namespace
} // namespace Router
} // namespace Envoy
