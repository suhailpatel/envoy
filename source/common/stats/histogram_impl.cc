#include "common/stats/histogram_impl.h"

#include <algorithm>
#include <string>

#include "envoy/common/exception.h"
#include "common/common/utility.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Stats {

HistogramStatisticsImpl::HistogramStatisticsImpl(const histogram_t* histogram_ptr, HistogramOptionsPtr options) {
  options_ = std::move(options);

  hist_approx_quantile(histogram_ptr, supportedQuantiles().data(), supportedQuantiles().size(), computed_quantiles_.data());

  sample_count_ = hist_sample_count(histogram_ptr);
  sample_sum_ = hist_approx_sum(histogram_ptr);
  for (size_t i = 0; i < supportedBuckets().size(); ++i) {
    computed_buckets_[i] = hist_approx_count_below(histogram_ptr, supportedBuckets()[i]);
  }
}

std::string HistogramStatisticsImpl::quantileSummary() const {
  std::vector<std::string> summary;
  summary.reserve(supportedQuantiles().size());
  for (size_t i = 0; i < supportedQuantiles().size(); ++i) {
    summary.push_back(fmt::format("P{}: {}", 100 * supportedQuantiles()[i], computed_quantiles_[i]));
  }
  return absl::StrJoin(summary, ", ");
}

std::string HistogramStatisticsImpl::bucketSummary() const {
  std::vector<std::string> bucket_summary;
  bucket_summary.reserve(supportedBuckets().size());
  for (size_t i = 0; i < supportedBuckets().size(); ++i) {
    bucket_summary.push_back(fmt::format("B{}: {}", supportedBuckets()[i], computed_buckets_[i]));
  }
  return absl::StrJoin(bucket_summary, ", ");
}

/**
 * Clears the old computed values and refreshes it with values computed from passed histogram.
 */
void HistogramStatisticsImpl::refresh(const histogram_t* new_histogram_ptr) {
  std::fill(computed_quantiles_.begin(), computed_quantiles_.end(), 0.0);
  ASSERT(supportedQuantiles().size() == computed_quantiles_.size());
  hist_approx_quantile(new_histogram_ptr, supportedQuantiles().data(), supportedQuantiles().size(),
                       computed_quantiles_.data());

  sample_count_ = hist_sample_count(new_histogram_ptr);
  sample_sum_ = hist_approx_sum(new_histogram_ptr);

  ASSERT(supportedBuckets().size() == computed_buckets_.size());
  computed_buckets_.clear();
  const std::vector<double>& supported_buckets = supportedBuckets();
  computed_buckets_.reserve(supported_buckets.size());
  for (const auto bucket : supported_buckets) {
    computed_buckets_.emplace_back(hist_approx_count_below(new_histogram_ptr, bucket));
  }
}

} // namespace Stats
} // namespace Envoy
