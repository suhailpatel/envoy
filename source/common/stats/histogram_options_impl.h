#pragma once

#include "envoy/stats/histogram.h"

namespace Envoy {
namespace Stats {

class HistogramOptionsImpl : public HistogramOptions {
public:
  HistogramOptionsImpl() : supported_buckets_(defaultBuckets()), supported_quantiles_(defaultQuantiles()) {};

  /**
   * HistogramOptionsImpl object is constructed using the specified quantiles
   * @param quantiles vector of quantile values which define which percentiles to compute for the
   * given histogram. Values must be between 0 and 100 inclusive
   * @param buckets vector of buckets which are used to extract sample counts for the provided histogram
   * @throw an EnvoyException if the quantiles are not between 0.0 to 100 (inclusive)
  */
  HistogramOptionsImpl(std::vector<double>& quantiles, std::vector<double>& buckets);

  const std::vector<double>& supportedBuckets() const override { return supported_buckets_; }
  const std::vector<double>& supportedQuantiles() const override { return supported_quantiles_; }

private:
  const std::vector<double>& defaultBuckets() const;
  const std::vector<double>& defaultQuantiles() const;

  std::vector<double> supported_buckets_;
  std::vector<double> supported_quantiles_;
};

}
}