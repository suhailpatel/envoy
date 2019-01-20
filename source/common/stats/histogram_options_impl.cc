#include "common/stats/histogram_options_impl.h"

#include "envoy/common/exception.h"

#include "common/common/utility.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Stats {

HistogramOptionsImpl::HistogramOptionsImpl(std::vector<double>& quantiles,
                                           std::vector<double>& buckets) {
  if (!quantiles.empty()) {
    for (const auto quantile : quantiles) {
      if (quantile < 0.0 || quantile > 100.0)
        throw EnvoyException(fmt::format("Quantile {} is not valid.", quantile));
    }
    supported_quantiles_ = quantiles;
  } else {
    supported_quantiles_ = defaultQuantiles();
  }

  if (!buckets.empty()) {
    supported_buckets_ = buckets;
  } else {
    supported_buckets_ = defaultBuckets();
  }
}

const std::vector<double>& HistogramOptionsImpl::defaultQuantiles() const {
  static const std::vector<double> default_quantiles = {0,    0.25, 0.5,   0.75,  0.90,
                                                        0.95, 0.99, 0.995, 0.999, 1};
  return default_quantiles;
}

const std::vector<double>& HistogramOptionsImpl::defaultBuckets() const {
  static const std::vector<double> default_buckets = {
      0.5,  1,    5,     10,    25,    50,     100,    250,     500,    1000,
      2500, 5000, 10000, 30000, 60000, 300000, 600000, 1800000, 3600000};
  return default_buckets;
}

} // namespace Stats
} // namespace Envoy
