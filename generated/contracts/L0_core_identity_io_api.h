#pragma once

#include <type_traits>

namespace sre::layers {
struct ScopedPlanView;
struct DiagnosticSink;
struct Status;
Status ValidateCoreIdentityIo(const ScopedPlanView& plan_view, DiagnosticSink& diag) noexcept;
void RegisterCoreIdentityIoApi() noexcept;
}  // namespace sre::layers

#define REGISTER_LAYER_API(layer_id, ...) static_assert(true, "api registered")
