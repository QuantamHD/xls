// Copyright 2021 The XLS Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_format.h"
#include "xls/common/logging/logging.h"
#include "xls/common/status/matchers.h"
#include "xls/noc/simulation/noc_traffic_injector.h"
#include "xls/noc/simulation/sample_network_graphs.h"
#include "xls/noc/simulation/sim_objects.h"
#include "xls/noc/simulation/simulator_to_traffic_injector_shim.h"
#include "xls/noc/simulation/traffic_description.h"

namespace xls::noc {
namespace {

TEST(SimTrafficTest, BackToBackNetwork0) {
  // Construct traffic flows
  NocTrafficManager traffic_mgr;

  XLS_ASSERT_OK_AND_ASSIGN(TrafficFlowId flow0_id,
                           traffic_mgr.CreateTrafficFlow());
  TrafficFlow& flow0 = traffic_mgr.GetTrafficFlow(flow0_id);
  flow0.SetName("flow0")
      .SetSource("SendPort0")
      .SetDestination("RecvPort0")
      .SetVC("VC0")
      .SetTrafficRateInMiBps(3 * 1024)
      .SetPacketSizeInBits(128)
      .SetBurstProbInMils(7);

  XLS_ASSERT_OK_AND_ASSIGN(TrafficModeId mode0_id,
                           traffic_mgr.CreateTrafficMode());
  TrafficMode& mode0 = traffic_mgr.GetTrafficMode(mode0_id);
  mode0.SetName("Mode 0").RegisterTrafficFlow(flow0_id);

  // Build and assign simulation objects
  NetworkConfigProto proto;
  NetworkManager graph;
  NocParameters params;
  XLS_ASSERT_OK(BuildNetworkGraphLinear000(&proto, &graph, &params));

  // Create global routing table.
  DistributedRoutingTableBuilderForTrees route_builder;
  XLS_ASSERT_OK_AND_ASSIGN(DistributedRoutingTable routing_table,
                           route_builder.BuildNetworkRoutingTables(
                               graph.GetNetworkIds()[0], graph, params));

  // Build input traffic model
  RandomNumberInterface rnd;
  int64_t cycle_time_in_ps = 400;
  rnd.SetSeed(1000);
  XLS_ASSERT_OK_AND_ASSIGN(
      NocTrafficInjector traffic_injector,
      NocTrafficInjectorBuilder().Build(
          cycle_time_in_ps, mode0_id,
          routing_table.GetSourceIndices().GetNetworkComponents(),
          routing_table.GetSinkIndices().GetNetworkComponents(),
          params.GetNetworkParam(graph.GetNetworkIds()[0])
              ->GetVirtualChannels(),
          traffic_mgr, graph, params, rnd));

  // Build simulator objects.
  NocSimulator simulator;
  XLS_ASSERT_OK(simulator.Initialize(graph, params, routing_table,
                                     graph.GetNetworkIds()[0]));
  simulator.Dump();

  // Hook traffic injector and simulator together.
  NocSimulatorToNocTrafficInjectorShim injector_shim(simulator,
                                                     traffic_injector);
  traffic_injector.SetSimulatorShim(injector_shim);
  simulator.RegisterPreCycleService(injector_shim);

  // Retrieve sink object.
  XLS_ASSERT_OK_AND_ASSIGN(
      NetworkComponentId recv_port_0,
      FindNetworkComponentByName("RecvPort0", graph, params));

  for (int64_t i = 0; i < 200'000; ++i) {
    XLS_ASSERT_OK(simulator.RunCycle());
  }

  XLS_ASSERT_OK_AND_ASSIGN(SimNetworkInterfaceSink * sim_recv_port_0,
                           simulator.GetSimNetworkInterfaceSink(recv_port_0));

  double measured_traffic_sent =
      traffic_injector.MeasuredTrafficRateInMiBps(cycle_time_in_ps, 0);
  double measured_traffic_recv =
      sim_recv_port_0->MeasuredTrafficRateInMiBps(cycle_time_in_ps);

  EXPECT_EQ(static_cast<int64_t>(measured_traffic_recv) / 10,
            static_cast<int64_t>(measured_traffic_sent) / 10);
  EXPECT_EQ(static_cast<int64_t>(measured_traffic_sent) / 100, 3 * 1024 / 100);
}

TEST(SimTrafficTest, BackToBackNetwork0_Replay) {
  // Construct traffic flows
  NocTrafficManager traffic_mgr;

  XLS_ASSERT_OK_AND_ASSIGN(TrafficFlowId flow0_id,
                           traffic_mgr.CreateTrafficFlow());
  TrafficFlow& flow0 = traffic_mgr.GetTrafficFlow(flow0_id);
  flow0.SetName("flow0")
      .SetSource("SendPort0")
      .SetDestination("RecvPort0")
      .SetVC("VC0")
      .SetPacketSizeInBits(64)
      .SetClockCycleTimes({0, 1, 2, 3, 4, 5, 6, 7, 8, 9});

  XLS_ASSERT_OK_AND_ASSIGN(TrafficModeId mode0_id,
                           traffic_mgr.CreateTrafficMode());
  TrafficMode& mode0 = traffic_mgr.GetTrafficMode(mode0_id);
  mode0.SetName("Mode 0").RegisterTrafficFlow(flow0_id);

  // Build and assign simulation objects
  NetworkConfigProto proto;
  NetworkManager graph;
  NocParameters params;
  XLS_ASSERT_OK(BuildNetworkGraphLinear000(&proto, &graph, &params));

  // Create global routing table.
  DistributedRoutingTableBuilderForTrees route_builder;
  XLS_ASSERT_OK_AND_ASSIGN(DistributedRoutingTable routing_table,
                           route_builder.BuildNetworkRoutingTables(
                               graph.GetNetworkIds()[0], graph, params));

  // Build input traffic model
  RandomNumberInterface rnd;
  int64_t cycle_time_in_ps = 400;
  rnd.SetSeed(1000);
  XLS_ASSERT_OK_AND_ASSIGN(
      NocTrafficInjector traffic_injector,
      NocTrafficInjectorBuilder().Build(
          cycle_time_in_ps, mode0_id,
          routing_table.GetSourceIndices().GetNetworkComponents(),
          routing_table.GetSinkIndices().GetNetworkComponents(),
          params.GetNetworkParam(graph.GetNetworkIds()[0])
              ->GetVirtualChannels(),
          traffic_mgr, graph, params, rnd));

  // Build simulator objects.
  NocSimulator simulator;
  XLS_ASSERT_OK(simulator.Initialize(graph, params, routing_table,
                                     graph.GetNetworkIds()[0]));
  simulator.Dump();

  // Hook traffic injector and simulator together.
  NocSimulatorToNocTrafficInjectorShim injector_shim(simulator,
                                                     traffic_injector);
  traffic_injector.SetSimulatorShim(injector_shim);
  simulator.RegisterPreCycleService(injector_shim);

  for (int64_t i = 0; i < 15; ++i) {
    XLS_ASSERT_OK(simulator.RunCycle());
  }

  // Retrieve router info
  EXPECT_EQ(simulator.GetRouters().size(), 1);
  EXPECT_EQ(simulator.GetRouters()[0].GetUtilizationCycleCount(), 10);
}

}  // namespace
}  // namespace xls::noc
