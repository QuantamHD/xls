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

#include "xls/ir/format_strings.h"

#include "absl/strings/str_format.h"

namespace xls {

absl::StatusOr<std::vector<FormatStep>> ParseFormatString(
    absl::string_view format_string) {
  std::vector<FormatStep> steps;

  int64_t i = 0;
  auto consume_substr = [&i, format_string](absl::string_view m) -> bool {
    if (format_string.substr(i, m.length()) == m) {
      i = i + m.length();
      return true;
    }
    return false;
  };

  std::string fragment;
  fragment.reserve(format_string.length());

  auto push_fragment = [&fragment, &steps]() {
    if (!fragment.empty()) {
      steps.push_back(fragment);
      fragment.clear();
    }
  };

  while (i < format_string.length()) {
    if (consume_substr("{{")) {
      fragment += '{';
      continue;
    }
    if (consume_substr("}}")) {
      fragment += '}';
      continue;
    }
    if (consume_substr("{}")) {
      push_fragment();
      steps.push_back(FormatPreference::kDefault);
      continue;
    }
    if (consume_substr("{:d}")) {
      push_fragment();
      steps.push_back(FormatPreference::kDecimal);
      continue;
    }
    if (consume_substr("{:x}")) {
      push_fragment();
      steps.push_back(FormatPreference::kPlainHex);
      continue;
    }
    if (consume_substr("{:#x}")) {
      push_fragment();
      steps.push_back(FormatPreference::kHex);
      continue;
    }
    if (consume_substr("{:b}")) {
      push_fragment();
      steps.push_back(FormatPreference::kPlainBinary);
      continue;
    }
    if (consume_substr("{:#b}")) {
      push_fragment();
      steps.push_back(FormatPreference::kBinary);
      continue;
    }
    if (format_string[i] == '{') {
      size_t close_pos = format_string.find('}', i);
      if (close_pos != absl::string_view::npos) {
        return absl::InvalidArgumentError(absl::StrFormat(
            "Invalid or unsupported format specifier \"%s\" in format string "
            "\"%s\"",
            format_string.substr(i, close_pos - i + 1), format_string));
      } else {
        return absl::InvalidArgumentError(absl::StrFormat(
            "{ without matching } at position %d in format string \"%s\"", i,
            format_string));
      }
    }
    if (format_string[i] == '}') {
      return absl::InvalidArgumentError(absl::StrFormat(
          "} with no preceding { at position %d in format string \"%s\"", i,
          format_string));
    }

    fragment += format_string[i];
    i = i + 1;
  }

  push_fragment();
  return steps;
}

int64_t OperandsExpectedByFormat(absl::Span<const FormatStep> format) {
  return std::count_if(format.begin(), format.end(),
                       [](const FormatStep& step) {
                         return absl::holds_alternative<FormatPreference>(step);
                       });
}
}  // namespace xls
