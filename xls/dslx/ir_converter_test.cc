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

// Tests that explicit check the IR output generated by various DSL constructs.
//
// This amounts to whitebox testing of the IR converter end-to-end, whereas DSLX
// tests (i.e. in dslx/tests) are testing functional correctness of results
// (which is more blackbox with respect to the IR conversion process).

#include "xls/dslx/ir_converter.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/flags/flag.h"
#include "xls/common/file/filesystem.h"
#include "xls/common/file/get_runfile_path.h"
#include "xls/common/init_xls.h"
#include "xls/common/logging/log_lines.h"
#include "xls/common/status/matchers.h"
#include "xls/common/update_golden_files.inc"
#include "xls/dslx/parse_and_typecheck.h"

ABSL_FLAG(std::string, xls_source_dir, "",
          "Absolute path to root of XLS source directory to modify when "
          "--test_update_golden_files is given");

namespace xls::dslx {
namespace {

constexpr ConvertOptions kFailNoPos = {
    .emit_positions = false,
};

void ExpectIr(absl::string_view got, absl::string_view test_name) {
  std::string suffix =
      absl::StrCat("dslx/testdata/ir_converter_test_", test_name, ".ir");
  if (absl::GetFlag(FLAGS_test_update_golden_files)) {
    std::string path =
        absl::StrCat(absl::GetFlag(FLAGS_xls_source_dir), "/", suffix);
    XLS_ASSERT_OK(SetFileContents(path, got));
    return;
  }
  XLS_ASSERT_OK_AND_ASSIGN(std::filesystem::path runfile,
                           GetXlsRunfilePath(absl::StrCat("xls/", suffix)));
  XLS_ASSERT_OK_AND_ASSIGN(std::string want, GetFileContents(runfile));
  XLS_LOG_LINES(INFO, got);
  EXPECT_EQ(got, want);
}

std::string TestName() {
  return ::testing::UnitTest::GetInstance()->current_test_info()->name();
}

absl::StatusOr<std::string> ConvertOneFunctionForTest(
    absl::string_view program, absl::string_view fn_name,
    ImportData& import_data, const ConvertOptions& options) {
  XLS_ASSIGN_OR_RETURN(TypecheckedModule tm,
                       ParseAndTypecheck(program, /*path=*/"test_module.x",
                                         /*module_name=*/"test_module",
                                         /*import_data=*/&import_data));
  return ConvertOneFunction(tm.module, /*entry_function_name=*/fn_name,
                            /*import_data=*/&import_data,
                            /*symbolic_bindings=*/nullptr, options);
}

absl::StatusOr<std::string> ConvertOneFunctionForTest(
    absl::string_view program, absl::string_view fn_name,
    const ConvertOptions& options = ConvertOptions{}) {
  ImportData import_data;
  return ConvertOneFunctionForTest(program, fn_name, import_data, options);
}

absl::StatusOr<std::string> ConvertModuleForTest(
    absl::string_view program, const ConvertOptions& options = ConvertOptions{},
    ImportData* import_data = nullptr) {
  absl::optional<ImportData> import_data_value;
  if (import_data == nullptr) {
    import_data_value.emplace();
    import_data = &*import_data_value;
  }
  XLS_ASSIGN_OR_RETURN(
      TypecheckedModule tm,
      ParseAndTypecheck(program, "test_module.x", "test_module", import_data));
  XLS_ASSIGN_OR_RETURN(std::string converted,
                       ConvertModule(tm.module, import_data, options));
  return converted;
}

TEST(IrConverterTest, NamedConstant) {
  const char* program =
      R"(fn f() -> u32 {
  let foo: u32 = u32:42;
  foo
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertOneFunctionForTest(program, "f"));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, Concat) {
  const char* program =
      R"(fn f(x: bits[31]) -> u32 {
  bits[1]:1 ++ x
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertOneFunctionForTest(program, "f"));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, TwoPlusTwo) {
  const char* program =
      R"(fn two_plus_two() -> u32 {
  u32:2 + u32:2
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertOneFunctionForTest(program, "two_plus_two"));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, SignedDiv) {
  const char* program =
      R"(fn signed_div(x: s32, y: s32) -> s32 {
  x / y
})";
  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertOneFunctionForTest(program, "signed_div"));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, NegativeX) {
  const char* program =
      R"(fn negate(x: u32) -> u32 {
  -x
})";
  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertOneFunctionForTest(program, "negate"));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, LetBinding) {
  const char* program =
      R"(fn f() -> u32 {
  let x: u32 = u32:2;
  x+x
})";
  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertOneFunctionForTest(program, "f"));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, LetTupleBinding) {
  const char* program =
      R"(fn f() -> u32 {
  let t = (u32:2, u32:3);
  let (x, y) = t;
  x+y
})";
  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertOneFunctionForTest(program, "f"));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, LetTupleBindingNested) {
  const char* program =
      R"(fn f() -> u32 {
  let t = (u32:2, (u32:3, (u32:4,), u32:5));
  let (x, (y, (z,), a)) = t;
  x+y+z+a
})";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertOneFunctionForTest(program, "f",
                                ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, Struct) {
  const char* program =
      R"(struct S {
  zub: u8,
  qux: u8,
}

fn f(a: S, b: S) -> u8 {
  let foo = a.zub + b.qux;
  (S { zub: u8:42, qux: u8:0 }).zub + (S { zub: u8:22, qux: u8:11 }).zub
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertOneFunctionForTest(program, "f",
                                ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, Index) {
  const char* program =
      R"(fn f(x: uN[32][4]) -> u32 {
  x[u32:0]
})";
  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertOneFunctionForTest(program, "f"));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, TupleOfParameters) {
  const char* program =
      R"(fn f(x: u8, y: u8) -> (u8, u8) {
  (x, y)
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertOneFunctionForTest(program, "f"));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, TupleOfLiterals) {
  const char* program =
      R"(fn f() -> (u8, u8) {
  (u8:0xaa, u8:0x55)
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertOneFunctionForTest(program, "f"));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, CountedFor) {
  const char* program =
      R"(fn f() -> u32 {
  for (i, accum): (u32, u32) in range(u32:0, u32:4) {
    accum + i
  }(u32:0)
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertOneFunctionForTest(program, "f",
                                ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, CountedForDestructuring) {
  const char* program =
      R"(fn f() -> u32 {
  let t = for (i, (x, y)): (u32, (u32, u8)) in range(u32:0, u32:4) {
    (x + i, y)
  }((u32:0, u8:0));
  t[0]
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertOneFunctionForTest(program, "f",
                                ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, CountedForParametricConst) {
  const char* program =
      R"(fn f<N: u32>(x: bits[N]) -> u32 {
  for (i, accum): (u32, u32) in range(u32:0, N) {
    accum + i
  }(u32:0)
}
fn main() -> u32 {
  f(bits[2]:0)
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, CountedForInvokingFunctionFromBody) {
  const char* program =
      R"(fn my_id(x: u32) -> u32 { x }
fn f() -> u32 {
  for (i, accum): (u32, u32) in range(u32:0, u32:4) {
    my_id(accum + i)
  }(u32:0)
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, CountedForVariableRange) {
  const char* program =
      R"(fn f(x:u32) -> u32 {
  for (i, accum): (u32, u32) in range(u32:0, x) {
    accum + i
  }(u32:0)
}
)";
  auto status_or_ir = ConvertOneFunctionForTest(
      program, "f", ConvertOptions{.emit_positions = false});
  ASSERT_FALSE(status_or_ir.ok());
}

TEST(IrConverterTest, ExtendConversions) {
  const char* program =
      R"(fn main(x: u8, y: s8) -> (u32, u32, s32, s32) {
  (x as u32, y as u32, x as s32, y as s32)
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, TupleIndex) {
  const char* program =
      R"(fn main() -> u8 {
  let t = (u32:3, u8:4);
  t[1]
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, BasicStruct) {
  const char* program =
      R"(
struct Point {
  x: u32,
  y: u32,
}

fn f(xy: u32) -> Point {
  Point { x: xy, y: xy }
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, InvokeNullary) {
  const char* program =
      R"(fn callee() -> u32 {
  u32:42
}
fn caller() -> u32 {
  callee()
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertModuleForTest(program));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, Match) {
  const char* program =
      R"(
fn f(x: u8) -> u2 {
  match x {
    u8:42 => u2:0,
    u8:64 => u2:1,
    _ => u2:2
  }
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, MatchDense) {
  const char* program =
      R"(
fn f(x: u2) -> u8 {
  match x {
    u2:0 => u8:42,
    u2:1 => u8:64,
    u2:2 => u8:128,
    _ => u8:255
  }
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, EnumUse) {
  const char* program =
      R"(
enum Foo : u32 {
  THING = 0,
  OTHER = 1,
}
fn f(x: Foo) -> Foo {
  Foo::OTHER if x == Foo::THING else Foo::THING
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, ArrayEllipsis) {
  const char* program =
      R"(
fn main() -> u8[2] {
  u8[2]:[0, ...]
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, NonConstArrayEllipsis) {
  const char* program =
      R"(
fn main(x: bits[8]) -> u8[4] {
  u8[4]:[u8:0, x, ...]
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, ArrayUpdate) {
  const char* program =
      R"(
fn main(input: u8[2]) -> u8[2] {
  update(input, u32:1, u8:0x42)
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, SplatStructInstance) {
  const char* program =
      R"(
struct Point {
  x: u32,
  y: u32,
}

fn f(p: Point, new_y: u32) -> Point {
  Point { y: new_y, ..p }
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, BoolLiterals) {
  const char* program =
      R"(
fn f(x: u8) -> bool {
  true if x == u8:42 else false
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, MatchIdentity) {
  const char* program =
      R"(
fn f(x: u8) -> u2 {
  match x {
    u8:42 => u2:3,
    _ => x as u2
  }
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, Ternary) {
  const char* program =
      R"(fn main(x: bool) -> u8 {
  u8:42 if x else u8:24
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertModuleForTest(program));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, MatchPackageLevelConstant) {
  const char* program =
      R"(const FOO = u8:0xff;
fn f(x: u8) -> u2 {
  match x {
    FOO => u2:0,
    _ => x as u2
  }
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, ParametricInvocation) {
  const char* program =
      R"(
fn parametric_id<N: u32>(x: bits[N]) -> bits[N] {
  x+(N as bits[N])
}

fn main(x: u8) -> u8 {
  parametric_id(x)
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, MatchUnderLet) {
  const char* program =
      R"(
fn main(x: u8) -> u8 {
  let t = match x {
    u8:42 => u8:0xff,
    _ => x
  };
  t
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, WidthSlice) {
  const char* program =
      R"(
fn f(x: u32, y: u32) -> u8 {
  x[2+:u8]+x[y+:u8]
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, SingleElementBitsArrayParam) {
  const char* program =
      R"(
fn f(x: u32[1]) -> u32[1] {
  x
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, SingleElementEnumArrayParam) {
  const char* program =
      R"(
enum Foo : u2 {}
fn f(x: Foo[1]) -> Foo[1] {
  x
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, BitSliceCast) {
  const char* program =
      R"(
fn main(x: u2) -> u1 {
  x as u1
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, MatchDenseConsts) {
  const char* program =
      R"(
type MyU2 = u2;
const ZERO = MyU2:0;
const ONE = MyU2:1;
const TWO = MyU2:2;
fn f(x: u2) -> u8 {
  match x {
    ZERO => u8:42,
    ONE => u8:64,
    TWO => u8:128,
    _ => u8:255
  }
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, CountedForWithLoopInvariants) {
  const char* program =
      R"(
fn f(outer_thing_1: u32, outer_thing_2: u32) -> u32 {
  let outer_thing_3: u32 = u32:42;
  let outer_thing_4: u32 = u32:24;
  for (i, accum): (u32, u32) in range(u32:0, u32:4) {
    accum + i + outer_thing_1 + outer_thing_2 + outer_thing_3 + outer_thing_4
  }(u32:0)
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, CountedForWithTupleAccumulator) {
  const char* program =
      R"(
fn f() -> (u32, u32) {
  for (i, (a, b)): (u32, (u32, u32)) in range(u32:0, u32:4) {
    (a+b, b+u32:1)
  }((u32:0, u32:1))
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, InvokeMultipleArgs) {
  const char* program =
      R"(fn callee(x: bits[32], y: bits[32]) -> bits[32] {
  x + y
}
fn caller() -> u32 {
  callee(u32:2, u32:3)
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertModuleForTest(program));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, CastOfAdd) {
  const char* program =
      R"(
fn main(x: u8, y: u8) -> u32 {
  (x + y) as u32
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, IdentityFinalArg) {
  const char* program =
      R"(
fn main(x0: u19, x3: u29) -> u29 {
  let x15: u29 = u29:0;
  let x17: u19 = (x0) + (x15 as u19);
  x3
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, ModuleLevelConstantDims) {
  const char* program =
      R"(
const BATCH_SIZE = u32:17;

fn main(x: u32[BATCH_SIZE]) -> u32 {
  x[u32:16]
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, Signex) {
  const char* program =
      R"(
fn main(x: u8) -> u32 {
  signex(x, u32:0)
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, OneHotSelSplatVariadic) {
  const char* program =
      R"(
fn main(s: u2) -> u32 {
  one_hot_sel(s, u32[2]:[2, 3])
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, BitSliceSyntax) {
  const char* program =
      R"(
fn f(x: u4) -> u2 {
  x[:2]+x[-2:]+x[1:3]+x[-3:-1]+x[0:-2]
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, InvocationMultiSymbol) {
  const char* program =
      R"(fn parametric<M: u32, N: u32, R: u32 = M + N>(x: bits[M], y: bits[N]) -> bits[R] {
  x ++ y
}
fn main() -> u8 {
  parametric(bits[3]:0, bits[5]:1)
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertModuleForTest(program));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, ArrayConcat0) {
  const char* program =
      R"(
fn f(in1: u32[2]) -> u32 {
  let x : u32[4] = in1 ++ in1;
  x[u32:0]
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, PackageLevelConstantArray) {
  const char* program =
      R"(const FOO = u8[2]:[1, 2];
fn f() -> u8[2] { FOO }
fn g() -> u8[2] { FOO }
)";
  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertModuleForTest(program));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, MatchWithlet) {
  const char* program =
      R"(
fn f(x: u8) -> u2 {
  match x {
    u8:42 => let x = u2:0; x,
    u8:64 => let x = u2:1; x,
    _ => let x = u2:2; x
  }
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, SignexAcceptsSignedOutputType) {
  const char* program =
      R"(
fn main(x: u8) -> s32 {
  signex(x, s32:0)
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, StructWithConstSizedArray) {
  const char* program =
      R"(
const THING_COUNT = u32:2;
type Foo = (
  u32[THING_COUNT]
);
fn get_thing(x: Foo, i: u32) -> u32 {
  let things: u32[THING_COUNT] = x[0];
  things[i]
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

// Tests that a simple constexpr function can be evaluated at compile time
// (which we observe at IR conversion time).
TEST(IrConverterTest, ConstexprFunction) {
  const char* program =
      R"(
const MY_CONST = u32:5;
fn constexpr_fn(arg: u32) -> u32 {
  arg * MY_CONST
}

fn f() -> u32 {
  let x = constexpr_fn(MY_CONST);
  x
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, NestedTupleSignature) {
  const char* program =
      R"(
    type Foo = u3;

    type MyTup = (u6, u1);

    type TupOfThings = (u1, MyTup, Foo);

    type MoreStructured = (
      TupOfThings[3],
      u3,
      u1,
    );

    type Data = (u64, u1);

    fn main(r: u9, l: u10, input: MoreStructured) -> (u9, u10, Data) {
      (u9:0, u10:0, (u64:0, u1:0))
    }
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, ArrayUpdateInLoop) {
  const char* program =
      R"(
fn main() -> u8[2] {
  for (i, accum): (u32, u8[2]) in range(u32:0, u32:2) {
    update(accum, i, i as u8)
  }(u8[2]:[0, 0])
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, Identity) {
  const char* program =
      R"(fn main(x: u8) -> u8 {
  x
})";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, PackageLevelConstantArrayAccess) {
  const char* program =
      R"(
const FOO = u8[2]:[1, 2];
fn f() -> u8 { FOO[u32:0] }
fn g() -> u8 { FOO[u32:1] }
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, TransitiveParametricInvocation) {
  const char* program =
      R"(
fn parametric_id<N: u32>(x: bits[N]) -> bits[N] {
  x+(N as bits[N])
}
fn parametric_id_wrapper<M: u32>(x: bits[M]) -> bits[M] {
  parametric_id(x)
}
fn main(x: u8) -> u8 {
  parametric_id_wrapper(x)
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, ParametricIrConversion) {
  const char* program =
      R"(
fn parametric<N: u32>(x: bits[N]) -> u32 {
  N
}

fn main() -> u32 {
  parametric(bits[2]:0) + parametric(bits[3]:0)
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, UnconditionalFail) {
  const char* program = R"(
fn main() -> u32 {
  fail!(u32:42)
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertModuleForTest(program, kFailNoPos));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, FailInTernaryConsequent) {
  const char* program = R"(
fn main(x: u32) -> u32 {
  fail!(x) if x == u32:0 else x
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertModuleForTest(program, kFailNoPos));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, FailInTernaryAlternate) {
  const char* program = R"(
fn main(x: u32) -> u32 {
  x if x == u32:0 else fail!(x)
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertModuleForTest(program, kFailNoPos));
  ExpectIr(converted, TestName());
}

// Fail within one arm of a match expression.
TEST(IrConverterTest, FailInMatch) {
  const char* program = R"(
fn main(x: u32) -> u32 {
  match x {
    u32:42 => fail!(x),
    _ => x
  }
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertModuleForTest(program, kFailNoPos));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, FailInMatchInvocation) {
  const char* program = R"(
fn do_fail(x: u32) -> u32 {
  fail!(x)
}

fn main(x: u32) -> u32 {
  match x {
    u32:42 => do_fail(x),
    _ => x
  }
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertModuleForTest(program, kFailNoPos));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, MatchMultiFail) {
  const char* program = R"(
fn main(x: u32) -> u32 {
  match x {
    u32:42 => fail!(x),
    _ => fail!(x+u32:1)
  }
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertModuleForTest(program, kFailNoPos));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, InvokeMethodThatFails) {
  const char* program = R"(
fn does_fail() -> u32 {
  fail!(u32:42)
}

fn main(x: u32) -> u32 {
  does_fail()
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertModuleForTest(program, kFailNoPos));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, InvokeParametricThatFails) {
  const char* program = R"(
fn does_fail<N: u32>() -> bits[N] {
  fail!(bits[N]:42)
}

fn main(x: u32) -> u32 {
  does_fail<u32:32>()
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertModuleForTest(program, kFailNoPos));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, InvokeParametricThatInvokesFailing) {
  const char* program = R"(
fn does_fail() -> u32 {
  fail!(u32:42)
}

fn calls_failing<N: u32>() -> bits[N] {
  does_fail()
}

fn main(x: u32) -> u32 {
  calls_failing<u32:32>()
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertModuleForTest(program, kFailNoPos));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, FailInsideFor) {
  const char* program = R"(
fn main(x: u32) -> u32 {
  for (i, x): (u32, u32) in range(u32:0, u32:1) {
    fail!(x)
  }(u32:0)
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertModuleForTest(program, kFailNoPos));
  ExpectIr(converted, TestName());
}

// Even though the fail comes after the `for` construct, we currently prepare
// the `for` to be capable of failing, since the fallibility marking happens at
// the function scope.
TEST(IrConverterTest, FailOutsideFor) {
  const char* program = R"(
fn main(x: u32) -> u32 {
  let x = for (i, x): (u32, u32) in range(u32:0, u32:1) {
    x
  }(u32:0);
  fail!(x)
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertModuleForTest(program, kFailNoPos));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, FailInsideForWithTupleAccum) {
  const char* program = R"(
fn main(x: u32) -> (u32, u32) {
  for (i, (x, y)): (u32, (u32, u32)) in range(u32:0, u32:1) {
    fail!((x, y))
  }((u32:0, u32:0))
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertModuleForTest(program, kFailNoPos));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, CountedForParametricRefInBody) {
  const char* program =
      R"(
fn f<N:u32>(init: bits[N]) -> bits[N] {
  for (i, accum): (u32, bits[N]) in range(u32:0, u32:4) {
    accum as bits[N]
  }(init)
}

fn main() -> u32 {
  f(u32:0)
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertModuleForTest(program, kFailNoPos));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, SignedComparisonsViaSignedNumbers) {
  const char* program =
      R"(
fn main(x: s32, y: s32) -> bool {
  x > y && x < y && x >= y && x <= y
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

// Tests that a parametric constexpr function can be evaluated at compile time
// (IR conversion time).
TEST(IrConverterTest, ParametricConstexprFn) {
  const char* program =
      R"(
pub const MY_CONST = u32:5;
fn constexpr_fn<N:u32>(arg: bits[N]) -> bits[N] {
  arg * MY_CONST
}

fn f() -> u32 {
  let x = constexpr_fn(MY_CONST);
  x
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, ConstexprImport) {
  // Place the *imported* module into the import cache.
  ImportData import_data;
  const char* imported_program = R"(
import std

pub const MY_CONST = bits[32]:5;
pub const MY_OTHER_CONST = std::clog2(MY_CONST);

pub fn constexpr_fn(arg: u32) -> u32 {
  arg * MY_CONST
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      TypecheckedModule tm,
      ParseAndTypecheck(imported_program, "fake/imported/stuff.x",
                        "fake.imported.stuff", &import_data));
  const char* importer_program = R"(
import fake.imported.stuff

fn f() -> u32 {
  let x = stuff::constexpr_fn(stuff::MY_OTHER_CONST);
  x
}
)";
  (void)tm;  // Module is in the import cache.

  // Convert the *importer* module to IR.
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(importer_program,
                           ConvertOptions{.emit_positions = false},
                           &import_data));
  ExpectIr(converted, TestName());
}

// Tests that a parametric constexpr function can be imported.
TEST(IrConverterTest, ParametricConstexprImport) {
  // Place the *imported* module into the import cache.
  ImportData import_data;
  const char* imported_program = R"(
pub const MY_CONST = bits[32]:5;

pub fn constexpr_fn<N:u32>(arg: bits[N]) -> bits[N] {
  arg * MY_CONST
}

)";
  XLS_ASSERT_OK_AND_ASSIGN(
      TypecheckedModule tm,
      ParseAndTypecheck(imported_program, "fake/imported/stuff.x",
                        "fake.imported.stuff", &import_data));
  const char* importer_program = R"(
import fake.imported.stuff

fn f() -> u32 {
  let x = stuff::constexpr_fn(stuff::MY_CONST);
  x
}
)";
  (void)tm;  // Already placed in import cache.

  // Convert the *importer* module to IR.
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(importer_program,
                           ConvertOptions{.emit_positions = false},
                           &import_data));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, BitSliceUpdate) {
  const char* program =
      R"(
fn main(x: u32, y: u16, z: u8) -> u32 {
  bit_slice_update(x, y, z)
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, TokenIdentityFunction) {
  absl::string_view program = "fn main(x: token) -> token { x }";
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(program, ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, ImportEnumValue) {
  ImportData import_data;

  const std::string kImportModule = R"(
import std

pub const MY_CONST = u32:5;
pub enum ImportEnum : u16 {
  SINGLE_MY_CONST = MY_CONST as u16,
  SOMETHING_MY_CONST = std::clog2(MY_CONST) as u16 * u16:2,
  TRIPLE_MY_CONST = (MY_CONST * u32:3) as u16,
}
)";
  XLS_ASSERT_OK_AND_ASSIGN(
      TypecheckedModule tm,
      ParseAndTypecheck(kImportModule, "fake/imported/stuff.x",
                        "fake.imported.stuff", &import_data));
  (void)tm;  // Already placed in import cache.

  const std::string kImporterModule = R"(
import fake.imported.stuff

type ImportedEnum = stuff::ImportEnum;

fn main(x: u32) -> u32 {
  stuff::ImportEnum::TRIPLE_MY_CONST as u32 +
      (ImportedEnum::SOMETHING_MY_CONST as u32) +
      (stuff::ImportEnum::SINGLE_MY_CONST as u32)
})";

  // Convert the importer module to IR.
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertModuleForTest(kImporterModule,
                           ConvertOptions{.emit_positions = false},
                           &import_data));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, ConvertOneFunctionWithImport) {
  ImportData import_data;
  const std::string kImportModule = R"(
pub fn a() -> u32 {
  u32:42
}
)";
  XLS_ASSERT_OK(
      ParseAndTypecheck(kImportModule, "a.x", "a", &import_data).status());

  const std::string kImporterModule = R"(
import a

fn main(x: u32) -> u32 {
  a::a()
})";

  // Convert the importer module to IR.
  XLS_ASSERT_OK_AND_ASSIGN(
      std::string converted,
      ConvertOneFunctionForTest(kImporterModule, "main", import_data,
                                ConvertOptions{.emit_positions = false}));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, ConvertCoverOp) {
  const std::string kProgram = R"(
fn main(x: u32, y: u32) {
  let foo = x == y;
  cover!("x_equals_y", foo)
}
)";

  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertModuleForTest(kProgram, kFailNoPos));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, ConvertGateOp) {
  const std::string kProgram = R"(
fn main(p: bool, x: u32) -> u32 {
  gate!(p, x)
}
)";

  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertModuleForTest(kProgram, kFailNoPos));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, PublicFnGetsTokenWrapper) {
  const std::string kProgram = R"(
fn callee_callee(x:u32) -> u32 {
  let _ = fail!(x > u32:3);
  x
}

pub fn main(x:u32) -> u32 {
  callee_callee(x)
}

fn callee(x:u32) -> u32 {
  main(x)
}
)";

  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertModuleForTest(kProgram, kFailNoPos));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, NonpublicFnDoesNotGetTokenWrapper) {
  const std::string kProgram = R"(
fn callee_callee(x:u32) -> u32 {
  let _ = fail!(x > u32:3);
  x
}

fn main(x:u32) -> u32 {
  callee_callee(x)
}

fn callee(x:u32) -> u32 {
  main(x)
}
)";

  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertModuleForTest(kProgram, kFailNoPos));
  ExpectIr(converted, TestName());
}

TEST(IrConverterTest, HandlesChannelDecls) {
  const std::string kProgram = R"(
fn main(x:u32) -> () {
  let (p0, c0) = chan u32;
  let (p1, c1) = chan u64;
  let (p2, c2) = chan (u64, (u64, (u64)));
  let (p3, c3) = chan (u64, (u64, u64[4]));
  ()
}

)";

  ConvertOptions options;
  options.emit_fail_as_assert = false;
  options.emit_positions = false;
  options.verify_ir = false;
  XLS_ASSERT_OK_AND_ASSIGN(std::string converted,
                           ConvertModuleForTest(kProgram, options));
  ExpectIr(converted, TestName());
}

}  // namespace
}  // namespace xls::dslx

int main(int argc, char* argv[]) {
  xls::InitXls(argv[0], argc, argv);
  return RUN_ALL_TESTS();
}
