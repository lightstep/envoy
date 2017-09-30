#include "mocks.h"

#include "common/network/address_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

namespace Envoy {
namespace LocalInfo {

MockLocalInfo::MockLocalInfo() : address_(new Network::Address::Ipv4Instance("127.0.0.1")) {
  ON_CALL(*this, address()).WillByDefault(Return(address_));
  ON_CALL(*this, zoneName()).WillByDefault(ReturnPointee(&zone_name_));
  ON_CALL(*this, clusterName()).WillByDefault(ReturnPointee(&cluster_name_));
  ON_CALL(*this, nodeName()).WillByDefault(ReturnPointee(&node_name_));
  ON_CALL(*this, node()).WillByDefault(ReturnRef(node_));
}

MockLocalInfo::~MockLocalInfo() {}

} // namespace LocalInfo
} // namespace Envoy
