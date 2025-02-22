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

#ifndef XLS_IR_INTERVAL_SET_H_
#define XLS_IR_INTERVAL_SET_H_

#include <stdint.h>

#include <functional>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "xls/common/logging/log_message.h"
#include "xls/common/logging/logging.h"
#include "xls/ir/bits.h"
#include "xls/ir/bits_ops.h"
#include "xls/ir/interval.h"

namespace xls {

// This type represents a set of intervals.
class IntervalSet {
 public:
  // Create an empty `IntervalSet` with a `BitCount()` of -1. Every method in
  // this class fails if called on an `IntervalSet` with bit count -1, so you
  // must assign to a default constructed interval set before calling any method
  // on it.
  IntervalSet() : is_normalized_(true), bit_count_(-1), intervals_() {}

  // Create an empty `IntervalSet` with the given bit count.
  explicit IntervalSet(int64_t bit_count)
      : is_normalized_(true), bit_count_(bit_count), intervals_() {}

  // Returns an interval set that covers every bit pattern with the given width.
  static IntervalSet Maximal(int64_t bit_count);

  // Returns an interval set that covers exactly the given bit pattern.
  static IntervalSet Precise(const Bits& bits);

  // Returns the number of intervals in the set.
  // Does not check for normalization, as this function can be used to check if
  // normalization is required (e.g.: to prevent blowup in memory usage while
  // building a large set of intervals).
  int64_t NumberOfIntervals() const { return intervals_.size(); }

  // Get all the intervals contained within this interval set.
  // The set must be normalized prior to calling this.
  absl::Span<const Interval> Intervals() const {
    XLS_CHECK(is_normalized_);
    return intervals_;
  }

  // Returns the `BitCount()` of all intervals in the interval set.
  int64_t BitCount() const {
    XLS_CHECK_GE(bit_count_, 0);
    return bit_count_;
  }

  // Add an interval to this interval set.
  void AddInterval(const Interval& interval) {
    is_normalized_ = false;
    XLS_CHECK_EQ(BitCount(), interval.BitCount());
    intervals_.push_back(interval);
  }

  // Modify the set of intervals in this to be exactly the given set.
  // If the set of intervals is empty, then the `BitCount()` is set to -1.
  // Otherwise, the `BitCount()` is set to the `BitCount()` of the
  // given intervals.
  void SetIntervals(absl::Span<Interval const> intervals);

  // Normalize the set of intervals so that the following statements are true:
  //
  // 1. The union of the set of points contained within all intervals after
  //    normalization is the same as that before normalization
  //    (i.e.: normalization does not affect the smeantics of a set of
  //    intervals).
  // 2. After normalization, the set contains no improper intervals.
  // 3. After normalization, no two intervals in the set will overlap or abut.
  // 4. After normalization, the result of a call to `Intervals()` will be
  //    sorted in lexicographic order (with the underlying ordering given by
  //    interpreting each `Bits` as an unsigned integer).
  // 5. The result of a call to `Intervals()` has the smallest possible size
  //    of any set of intervals representing the same set of points that
  //    contains no improper intervals (hence the name "normalization").
  void Normalize();

  // Return the smallest single proper interval that contains all points in this
  // interval set. If the set of points is empty, returns `absl::nullopt`.
  absl::optional<Interval> ConvexHull() const;

  // Call the given function on each point contained within this set of
  // intervals. The function returns a `bool` that, if true, ends the iteration
  // early and results in `ForEachElement` returning true. If the iteration does
  // not end early, false is returned.
  //
  // CHECK fails if this interval set is not normalized, as that can lead to
  // unexpectedly calling the callback on the same point twice.
  bool ForEachElement(std::function<bool(const Bits&)> callback) const;

  // Returns a normalized set of intervals comprising the union of the two given
  // interval sets.
  static IntervalSet Combine(const IntervalSet& lhs, const IntervalSet& rhs);

  // Returns the number of points covered by the intervals in this interval set,
  // if that is expressible as an `int64_t`. Otherwise, returns `absl::nullopt`.
  // CHECK fails if the interval set is not normalized.
  absl::optional<int64_t> Size() const;

  // Do any of the intervals cover the given point?
  bool Covers(const Bits& bits) const;

  // Do any of the intervals cover zero?
  bool CoversZero() const { return Covers(UBits(0, BitCount())); }

  // Do any of the intervals cover one?
  bool CoversOne() const { return Covers(UBits(1, BitCount())); }

  // Do any of the intervals cover `Bits::AllOnes(BitCount())`?
  bool CoversMax() const { return Covers(Bits::AllOnes(BitCount())); }

  // Do the intervals only cover one point?
  bool IsPrecise() const;

  // Do the intervals cover every point?
  // `Normalize()` must be called prior to calling this method.
  bool IsMaximal() const;

  // Returns true iff this set of intervals is normalized.
  bool IsNormalized() const;

  // Print this set of intervals as a string.
  std::string ToString() const;

  friend bool operator==(IntervalSet lhs, IntervalSet rhs) {
    lhs.Normalize();
    rhs.Normalize();
    if (lhs.bit_count_ != rhs.bit_count_) {
      return false;
    }
    return lhs.intervals_ == rhs.intervals_;
  }

 private:
  bool is_normalized_;
  int64_t bit_count_;
  std::vector<Interval> intervals_;
};

}  // namespace xls

#endif  // XLS_IR_INTERVAL_SET_H_
