#pragma once

#include "sre/runtime.hpp"

#include <type_traits>

namespace sre::layers {

inline std::string TypeOf(const sre::JsonValue* v) { return v ? v->TypeName() : "missing"; }

inline bool IsIntegerLike(const sre::JsonValue& v) {
  if (!v.IsNumber()) {
    return false;
  }
  const double n = v.AsNumber();
  const auto i = static_cast<long long>(n);
  return static_cast<double>(i) == n;
}

inline Status RunFixtureOrPass(
    const ScopedPlanView& plan_view,
    DiagnosticSink& diag,
    std::string_view layer_id,
    std::string_view error_code,
    std::string_view pointer,
    std::string_view check_id) {
  if (EmitFixtureMarkerViolation(plan_view, diag, layer_id, error_code, pointer, check_id)) {
    return Status::Failure();
  }
  return Status::Success();
}

}  // namespace sre::layers

#define SRE_REGISTER_LAYER_API(layer_id, fn)                                                                                          \
  do {                                                                                                                                \
    using SreExpectedFnSig =                                                                                                           \
        ::sre::layers::Status (*)(const ::sre::layers::ScopedPlanView&, ::sre::layers::DiagnosticSink&) noexcept;                   \
    static_assert(std::is_same_v<decltype(&(fn)), SreExpectedFnSig>, "Layer API signature drift detected.");                         \
    REGISTER_LAYER_API(layer_id, fn);                                                                                                 \
  } while (0)
