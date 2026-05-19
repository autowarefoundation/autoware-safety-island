// Copyright 2021 Tier IV, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef AUTOWARE__INTERPOLATION__INTERPOLATION_UTILS_HPP_
#define AUTOWARE__INTERPOLATION__INTERPOLATION_UTILS_HPP_

#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>

#include "common/logger/logger.hpp"
using namespace common::logger;

namespace autoware::interpolation
{
inline bool isIncreasing(const std::vector<double> & x)
{
  if (x.empty()) {
    log_error("Interpolation: Points is empty.");
    std::exit(1);
  }

  for (size_t i = 0; i < x.size() - 1; ++i) {
    if (x.at(i) >= x.at(i + 1)) {
      return false;
    }
  }

  return true;
}

inline bool isNotDecreasing(const std::vector<double> & x)
{
  if (x.empty()) {
    log_error("Interpolation: Points is empty.");
    std::exit(1);
  }

  for (size_t i = 0; i < x.size() - 1; ++i) {
    if (x.at(i) > x.at(i + 1)) {
      return false;
    }
  }

  return true;
}

inline std::vector<double> validateKeys(
  const std::vector<double> & base_keys, const std::vector<double> & query_keys)
{
  // when vectors are empty
  if (base_keys.empty() || query_keys.empty()) {
    log_error("Interpolation: Points is empty.");
    std::exit(1);
  }

  // when size of vectors are less than 2
  if (base_keys.size() < 2) {
    log_error("Interpolation: The size of points is less than 2. base_keys.size() = %d", base_keys.size());
    std::exit(1);
  }

  // when indices are not sorted
  if (!isIncreasing(base_keys) || !isNotDecreasing(query_keys)) {
    log_error("Interpolation: Either base_keys or query_keys is not sorted.");
    std::exit(1);
  }

  // when query_keys is out of base_keys (This function does not allow exterior division.)
  constexpr double epsilon = 1e-3;
  if (
    query_keys.front() < base_keys.front() - epsilon ||
    base_keys.back() + epsilon < query_keys.back()) {
    log_error("Interpolation: query_keys is out of base_keys");
    std::exit(1);
  }

  // NOTE: Due to calculation error of double, a query key may be slightly out of base keys.
  //       Therefore, query keys are cropped here.
  auto validated_query_keys = query_keys;
  validated_query_keys.front() = std::max(validated_query_keys.front(), base_keys.front());
  validated_query_keys.back() = std::min(validated_query_keys.back(), base_keys.back());

  return validated_query_keys;
}

template <class T>
void validateKeysAndValues(
  const std::vector<double> & base_keys, const std::vector<T> & base_values)
{
  // when vectors are empty
  if (base_keys.empty() || base_values.empty()) {
    log_error("Interpolation: Points is empty.");
    std::exit(1);
  }

  // when size of vectors are less than 2
  if (base_keys.size() < 2 || base_values.size() < 2) {
    log_error("Interpolation: The size of points is less than 2. base_keys.size() = %d, base_values.size() = %d", base_keys.size(), base_values.size());
    std::exit(1);
  }

  // when sizes of indices and values are not same
  if (base_keys.size() != base_values.size()) {
    log_error("Interpolation: The size of base_keys and base_values are not the same.");
    std::exit(1);
  }
}
}  // namespace autoware::interpolation

#endif  // AUTOWARE__INTERPOLATION__INTERPOLATION_UTILS_HPP_
