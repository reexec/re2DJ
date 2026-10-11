#ifndef RE2DJ_PLATFORM_NATIVE_NATIVE_GUEST_MODULE_SET_H_
#define RE2DJ_PLATFORM_NATIVE_NATIVE_GUEST_MODULE_SET_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "native_import_bridge.h"
#include "re2dj/hle/modules/guest_module_registry.h"
#include "re2dj/runtime/pe_loader.h"

namespace re2dj::platform::native
{

inline constexpr std::uint32_t kDefaultNativeGuestModuleBase = 0x6F000000U;

class NativeGuestModuleSet
{
public:
    NativeGuestModuleSet() = default;
    ~NativeGuestModuleSet();

    NativeGuestModuleSet(const NativeGuestModuleSet&) = delete;
    NativeGuestModuleSet& operator=(const NativeGuestModuleSet&) = delete;

    bool Add(hle::modules::GuestModuleDescriptor descriptor,
             runtime::ImportGateTable* gates,
             std::uintptr_t bridge_address,
             std::uintptr_t cleanup_address,
             std::uint32_t first_candidate_base,
             std::string* error);

    bool Dispatch(const NativeImportGateEvent& event,
                  NativeImportGateResult* result,
                  std::string* error,
                  const hle::ImportCallServices* services = nullptr) const;

    const runtime::ImportGate* FindGate(std::string_view module,
                                        std::string_view name) const;

    const runtime::ImportGate* FindGate(runtime::GuestAddress address) const;

    // The export a gate calls, or null for a gate outside the set.
    const hle::modules::GuestExportDescriptor* FindExport(runtime::GuestAddress address) const;

    const hle::modules::GuestModuleRegistry& registry() const;

private:
    struct Mapping
    {
        void* memory = nullptr;
        std::uint32_t size = 0;
    };

    struct Binding
    {
        runtime::ImportGate gate;
        hle::modules::GuestExportDescriptor descriptor;
    };

    hle::modules::GuestModuleRegistry registry_;
    std::vector<Mapping> mappings_;
    std::vector<Binding> bindings_;
    // Gate address to its index in bindings_: every import looks its gate up.
    std::unordered_map<std::uint32_t, std::size_t> binding_index_;

    const Binding* FindBinding(std::uint32_t gate_address) const;
};

}  // namespace re2dj::platform::native

#endif  // RE2DJ_PLATFORM_NATIVE_NATIVE_GUEST_MODULE_SET_H_
