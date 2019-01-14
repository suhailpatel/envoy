#pragma once

#include <cstdint>
#include <string>

#include "envoy/stats/histogram.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/store.h"

#include "common/common/non_copyable.h"
#include "common/stats/metric_impl.h"

#include "circllhist.h"

namespace Envoy {
namespace Stats {

/**
 * Implementation of HistogramStatistics for circllhist.
 */
class HistogramStatisticsImpl : public HistogramStatistics, NonCopyable {
public:
  HistogramStatisticsImpl() : computed_quantiles_(supportedQuantiles().size(), 0.0) {}
  /**
   * HistogramStatisticsImpl object is constructed using the passed in histogram.
   * @param histogram_ptr pointer to the histogram for which stats will be calculated. This pointer
   * will not be retained.
   */
  HistogramStatisticsImpl(const histogram_t* histogram_ptr);

  void refresh(const histogram_t* new_histogram_ptr);

  // HistogramStatistics
  std::string summary() const override;
  const std::vector<double>& supportedQuantiles() const override;
  const std::vector<double>& computedQuantiles() const override { return computed_quantiles_; }
  const std::vector<double>& supportedBuckets() const override;
  const std::vector<double>& computedBuckets() const override { return computed_buckets_; }
  double sampleCount() const override { return sample_count_; }
  double sampleSum() const override { return sample_sum_; }

private:
  std::vector<double> computed_quantiles_;
  std::vector<double> computed_buckets_;
  double sample_count_;
  double sample_sum_;
};

/**
 * Histogram implementation for the heap.
 */
class HistogramImpl : public Histogram, public MetricImpl {
public:
  HistogramImpl(const std::string& name, Store& parent, std::string&& tag_extracted_name,
                std::vector<Tag>&& tags)
      : MetricImpl(std::move(tag_extracted_name), std::move(tags)), parent_(parent), name_(name) {}

  // Stats:;Metric
  std::string name() const override { return name_; }
  const char* nameCStr() const override { return name_.c_str(); }

  // Stats::Histogram
  void recordValue(uint64_t value) override { parent_.deliverHistogramToSinks(*this, value); }

  bool used() const override { return true; }

private:
  // This is used for delivering the histogram data to sinks.
  Store& parent_;

  const std::string name_;
};

/**
 * Null histogram implementation.
 * No-ops on all calls and requires no underlying metric or data.
 */
class NullHistogramImpl : public Histogram {
public:
  NullHistogramImpl() {}
  ~NullHistogramImpl() {}
  std::string name() const override { return ""; }
  const char* nameCStr() const override { return ""; }
  const std::string& tagExtractedName() const override { CONSTRUCT_ON_FIRST_USE(std::string, ""); }
  const std::vector<Tag>& tags() const override { CONSTRUCT_ON_FIRST_USE(std::vector<Tag>, {}); }
  void recordValue(uint64_t) override {}
  bool used() const override { return false; }
};

} // namespace Stats
} // namespace Envoy
