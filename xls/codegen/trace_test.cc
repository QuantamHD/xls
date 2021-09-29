// Copyright 2020 The XLS Authors
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

#include <random>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/statusor.h"
#include "xls/codegen/combinational_generator.h"
#include "xls/codegen/pipeline_generator.h"
#include "xls/common/status/matchers.h"
#include "xls/examples/sample_packages.h"
#include "xls/interpreter/function_interpreter.h"
#include "xls/interpreter/random_value.h"
#include "xls/ir/bits.h"
#include "xls/ir/function_builder.h"
#include "xls/ir/ir_parser.h"
#include "xls/ir/package.h"
#include "xls/scheduling/pipeline_schedule.h"
#include "xls/simulation/module_simulator.h"
#include "xls/simulation/module_testbench.h"
#include "xls/simulation/verilog_simulators.h"
#include "xls/simulation/verilog_test_base.h"

namespace xls {
namespace verilog {
namespace {

using status_testing::StatusIs;
using ::testing::HasSubstr;

constexpr char kTestName[] = "trace_test";
constexpr char kTestdataPath[] = "xls/codegen/testdata";

class TraceTest : public VerilogTestBase {};

constexpr char kSimpleTraceText[] = R"(
package SimpleTrace
fn main(tkn: token, cond: bits[1]) -> token {
  ret trace.1: token = trace(tkn, cond, format="This is a simple trace.", data_operands=[], id=1)
}
)";

TEST_P(TraceTest, CombinationalSimpleTrace) {
  XLS_ASSERT_OK_AND_ASSIGN(std::unique_ptr<Package> package,
                           Parser::ParsePackage(kSimpleTraceText));
  XLS_ASSERT_OK_AND_ASSIGN(Function * entry, package->EntryFunction());
  XLS_ASSERT_OK_AND_ASSIGN(
      auto result, GenerateCombinationalModule(entry, UseSystemVerilog()));

  ExpectVerilogEqualToGoldenFile(GoldenFilePath(kTestName, kTestdataPath),
                                 result.verilog_text);

  ModuleTestbench tb(result.verilog_text, result.signature, GetSimulator());

  // The combinational module doesn't a connected clock, but the clock can still
  // be used to sequence events in time.
  tb.NextCycle().Set("cond", 0);
  tb.ExpectTrace("This is a simple trace.");
  EXPECT_THAT(tb.Run(), StatusIs(absl::StatusCode::kNotFound,
                                 HasSubstr("This is a simple trace.")));

  tb.NextCycle().Set("cond", 1);
  XLS_ASSERT_OK(tb.Run());

  // Expect a second trace output
  tb.ExpectTrace("This is a simple trace.");
  EXPECT_THAT(tb.Run(), StatusIs(absl::StatusCode::kNotFound,
                                 HasSubstr("This is a simple trace.")));

  // Trigger a second output by changing cond
  tb.NextCycle().Set("cond", 0);
  tb.NextCycle().Set("cond", 1);
  XLS_ASSERT_OK(tb.Run());

  // Expect a third trace output
  tb.ExpectTrace("This is a simple trace.");
  tb.NextCycle();

  // Fail to find the third trace output because cond did not change.
  EXPECT_THAT(tb.Run(), StatusIs(absl::StatusCode::kNotFound,
                                 HasSubstr("This is a simple trace.")));
}

class TestDelayEstimator : public DelayEstimator {
 public:
  absl::StatusOr<int64_t> GetOperationDelayInPs(Node* node) const override {
    switch (node->op()) {
      case Op::kParam:
      case Op::kLiteral:
      case Op::kBitSlice:
      case Op::kConcat:
      case Op::kTrace:
        return 0;
      default:
        return 1;
    }
  }
};

// This is just a basic test to ensure that traces in clocked modules generate
// output.
// TODO(amfv): 2021-09-27 Figure out the rules for how traces should be
// distributed across pipeline stages and add more complex tests of that.
TEST_P(TraceTest, ClockedSimpleTraceTest) {
  XLS_ASSERT_OK_AND_ASSIGN(std::unique_ptr<Package> package,
                           Parser::ParsePackage(kSimpleTraceText));
  XLS_ASSERT_OK_AND_ASSIGN(Function * entry, package->EntryFunction());

  XLS_ASSERT_OK_AND_ASSIGN(
      PipelineSchedule schedule,
      PipelineSchedule::Run(entry, TestDelayEstimator(),
                            SchedulingOptions().pipeline_stages(1)));

  XLS_ASSERT_OK_AND_ASSIGN(
      ModuleGeneratorResult result,
      ToPipelineModuleText(
          schedule, entry,
          BuildPipelineOptions().use_system_verilog(UseSystemVerilog())));

  ExpectVerilogEqualToGoldenFile(GoldenFilePath(kTestName, kTestdataPath),
                                 result.verilog_text);

  ModuleTestbench tb(result.verilog_text, result.signature, GetSimulator());

  tb.NextCycle().Set("cond", 0);
  tb.ExpectTrace("This is a simple trace.");
  EXPECT_THAT(tb.Run(), StatusIs(absl::StatusCode::kNotFound,
                                 HasSubstr("This is a simple trace.")));

  tb.NextCycle().Set("cond", 1);
  // Advance a second cycle so that cond makes it through the pipeline to
  // trigger the trace.
  tb.NextCycle();
  XLS_ASSERT_OK(tb.Run());

  // Expect a second trace output
  tb.ExpectTrace("This is a simple trace.");
  // Fail to find the second trace because we haven't advanced the clock.
  EXPECT_THAT(tb.Run(), StatusIs(absl::StatusCode::kNotFound,
                                 HasSubstr("This is a simple trace.")));

  // Trigger a second output by advancing the clock even though cond is 0.
  tb.NextCycle().Set("cond", 0);
  XLS_ASSERT_OK(tb.Run());

  // Expect a third trace output
  tb.ExpectTrace("This is a simple trace.");

  // Fail to find it after advancing the clock because cond was 0 in the
  // previous cycle.
  EXPECT_THAT(tb.Run(), StatusIs(absl::StatusCode::kNotFound,
                                 HasSubstr("This is a simple trace.")));
}

INSTANTIATE_TEST_SUITE_P(TraceTestInstantiation, TraceTest,
                         testing::ValuesIn(kDefaultSimulationTargets),
                         ParameterizedTestName<TraceTest>);

}  // namespace
}  // namespace verilog
}  // namespace xls