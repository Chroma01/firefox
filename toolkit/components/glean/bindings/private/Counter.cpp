/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/glean/bindings/Counter.h"

#include "nsITelemetry.h"
#include "nsString.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/ResultVariant.h"
#include "mozilla/dom/GleanMetricsBinding.h"
#include "mozilla/glean/bindings/HistogramGIFFTMap.h"
#include "mozilla/glean/bindings/ScalarGIFFTMap.h"
#include "mozilla/glean/fog_ffi_generated.h"
#include "GIFFTFwd.h"

using mozilla::Telemetry::HistogramID;

/* static */
void accumulateToBoolean(HistogramID aId, const nsACString& aLabel,
                         int32_t aAmount) {
  MOZ_ASSERT(aAmount == 1,
             "When mirroring to boolean histograms, we only support "
             "accumulating one sample at a time.");
  if (aLabel.EqualsASCII("true")) {
    TelemetryHistogram::Accumulate(aId, true);
  } else if (aLabel.EqualsASCII("false")) {
    TelemetryHistogram::Accumulate(aId, false);
  } else {
    MOZ_ASSERT_UNREACHABLE(
        "When mirroring to boolean histograms, we only support labels 'true' "
        "and 'false'");
  }
}

/* static */
void accumulateToKeyedBoolean(HistogramID aId, const nsACString& aKey,
                              const nsACString& aCategory, int32_t aAmount) {
  MOZ_ASSERT(aAmount == 1,
             "When mirroring to keyed boolean histograms, we only support "
             "accumulating one sample at a time.");
  if (aCategory.EqualsASCII("true")) {
    TelemetryHistogram::Accumulate(aId, PromiseFlatCString(aKey), true);
  } else if (aCategory.EqualsASCII("false")) {
    TelemetryHistogram::Accumulate(aId, PromiseFlatCString(aKey), false);
  } else {
    MOZ_ASSERT_UNREACHABLE(
        "When mirroring to keyed boolean histograms, we only support labels "
        "'true' and 'false'");
  }
}

/* static */
void accumulateToKeyedCount(HistogramID aId, const nsCString& aLabel,
                            int32_t aAmount) {
  TelemetryHistogram::Accumulate(aId, aLabel, aAmount);
}

/* static */
void accumulateToCategorical(HistogramID aId, const nsCString& aLabel,
                             int32_t aAmount) {
  MOZ_ASSERT(aAmount == 1,
             "When mirroring to categorical histograms, we only support "
             "accumulating one sample at a time.");
  TelemetryHistogram::AccumulateCategorical(aId, aLabel);
}

/* static */
void accumulateToKeyedCategorical(HistogramID aId, const nsCString& aKey,
                                  const nsCString& aCategory, int32_t aAmount) {
  MOZ_ASSERT(aAmount == 1,
             "When mirroring to keyed categorical histograms, we only support "
             "accumulating one sample at a time.");
  TelemetryHistogram::AccumulateCategorical(aId, aKey, aCategory);
}

namespace mozilla::glean {

namespace impl {

template <>
void CounterMetric<CounterType::eBaseOrLabeled>::Add(int32_t aAmount) const {
  auto scalarId = ScalarIdForMetric(mId);
  if (aAmount >= 0) {
    if (scalarId) {
      TelemetryScalar::Add(scalarId.extract(), aAmount);
    } else if (IsSubmetricId(mId)) {
      bool mirrorsToKeyedScalar = false;
      GetLabeledMirrorLock().apply([&](const auto& lock) {
        auto tuple = lock.ref()->MaybeGet(mId);
        mirrorsToKeyedScalar = !!tuple;
        if (tuple && aAmount > 0) {
          TelemetryScalar::Add(std::get<0>(tuple.ref()),
                               std::get<1>(tuple.ref()), (uint32_t)aAmount);
        }
      });
      if (!mirrorsToKeyedScalar) {
        GetLabeledDistributionMirrorLock().apply([&](const auto& lock) {
          auto tuple = lock.ref()->MaybeGet(mId);
          if (tuple) {
            HistogramID hId = std::get<0>(tuple.ref());
            switch (TelemetryHistogram::GetHistogramType(hId)) {
              case nsITelemetry::HISTOGRAM_BOOLEAN:
                accumulateToBoolean(hId, std::get<1>(tuple.ref()), aAmount);
                break;
              case nsITelemetry::HISTOGRAM_COUNT:
                accumulateToKeyedCount(hId, std::get<1>(tuple.ref()), aAmount);
                break;
              case nsITelemetry::HISTOGRAM_CATEGORICAL:
                accumulateToCategorical(hId, std::get<1>(tuple.ref()), aAmount);
                break;
              default:
                MOZ_ASSERT_UNREACHABLE(
                    "Asked to mirror labeled_counter to unsupported histogram "
                    "type.");
                break;
            }
          }
        });
      }
    } else {
      auto hgramId = HistogramIdForMetric(mId);
      if (hgramId) {
        TelemetryHistogram::Accumulate(hgramId.extract(), aAmount);
      }
    }
  }
  fog_counter_add(mId, aAmount);
}

template <>
void CounterMetric<CounterType::eDualLabeled>::Add(int32_t aAmount) const {
  if (IsSubmetricId(mId)) {
    GetDualLabeledDistributionMirrorLock().apply([&](const auto& lock) {
      auto tuple = lock.ref()->MaybeGet(mId);
      if (tuple) {
        HistogramID hId = std::get<0>(tuple.ref());
        switch (TelemetryHistogram::GetHistogramType(hId)) {
          case nsITelemetry::HISTOGRAM_BOOLEAN:
            accumulateToKeyedBoolean(hId, std::get<1>(tuple.ref()),
                                     std::get<2>(tuple.ref()), aAmount);
            break;
          case nsITelemetry::HISTOGRAM_CATEGORICAL:
            accumulateToKeyedCategorical(hId, std::get<1>(tuple.ref()),
                                         std::get<2>(tuple.ref()), aAmount);
            break;
          default:
            MOZ_ASSERT_UNREACHABLE(
                "Asked to mirror dual_labeled_counter to unsupported "
                "histogram type.");
            break;
        }
      }
    });
  }
  fog_dual_labeled_counter_add(mId, aAmount);
}

template <>
Result<Maybe<int32_t>, nsCString>
CounterMetric<CounterType::eBaseOrLabeled>::TestGetValue(
    const nsACString& aPingName) const {
  nsCString err;
  if (fog_counter_test_get_error(mId, &err)) {
    return Err(err);
  }
  if (!fog_counter_test_has_value(mId, &aPingName)) {
    return Maybe<int32_t>();  // can't use Nothing() or templates will fail.
  }
  return Some(fog_counter_test_get_value(mId, &aPingName));
}

template <>
Result<Maybe<int32_t>, nsCString>
CounterMetric<CounterType::eDualLabeled>::TestGetValue(
    const nsACString& aPingName) const {
  nsCString err;
  if (fog_dual_labeled_counter_test_get_error(mId, &err)) {
    return Err(err);
  }
  if (!fog_dual_labeled_counter_test_has_value(mId, &aPingName)) {
    return Maybe<int32_t>();  // can't use Nothing() or templates will fail.
  }
  return Some(fog_dual_labeled_counter_test_get_value(mId, &aPingName));
}

}  // namespace impl

/* virtual */
JSObject* GleanCounter::WrapObject(JSContext* aCx,
                                   JS::Handle<JSObject*> aGivenProto) {
  return dom::GleanCounter_Binding::Wrap(aCx, this, aGivenProto);
}

void GleanCounter::Add(int32_t aAmount) {
  switch (mType) {
    case impl::CounterType::eBaseOrLabeled:
      impl::CounterMetric<impl::CounterType::eBaseOrLabeled>(mId).Add(aAmount);
      break;
    case impl::CounterType::eDualLabeled:
      impl::CounterMetric<impl::CounterType::eDualLabeled>(mId).Add(aAmount);
      break;
    default:
      MOZ_ASSERT_UNREACHABLE();
  }
}

dom::Nullable<int32_t> GleanCounter::TestGetValue(const nsACString& aPingName,
                                                  ErrorResult& aRv) {
  dom::Nullable<int32_t> ret;
  Result<Maybe<int32_t>, nsCString> result(Err(VoidCString()));
  switch (mType) {
    case impl::CounterType::eBaseOrLabeled:
      result = impl::CounterMetric<impl::CounterType::eBaseOrLabeled>(mId)
                   .TestGetValue(aPingName);
      break;
    case impl::CounterType::eDualLabeled:
      result = impl::CounterMetric<impl::CounterType::eDualLabeled>(mId)
                   .TestGetValue(aPingName);
      break;
    default:
      MOZ_ASSERT_UNREACHABLE();
  }
  if (result.isErr()) {
    aRv.ThrowDataError(result.unwrapErr());
    return ret;
  }
  auto optresult = result.unwrap();
  if (!optresult.isNothing()) {
    ret.SetValue(optresult.value());
  }
  return ret;
}

}  // namespace mozilla::glean
