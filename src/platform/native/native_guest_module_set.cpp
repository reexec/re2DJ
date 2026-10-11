#include "native_guest_module_set.h"
#include "native_host_services.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "native_guest_module_image.h"

namespace re2dj::platform::native
{
namespace
{

void SetError(std::string* error, const char* message)
{
    if (error != nullptr)
    {
        *error = message;
    }
}

const runtime::ImportGate* FindGateByAddress(const runtime::ImportGateTable& gates,
                                             runtime::GuestAddress address)
{
    for (const runtime::ImportGate& gate : gates.gates())
    {
        if (gate.address == address)
        {
            return &gate;
        }
    }
    return nullptr;
}

bool StackContains(const NativeImportGateEvent& event,
                   std::uint32_t address,
                   std::size_t size)
{
    return address >= event.guest_stack_limit && address <= event.guest_stack_base &&
           size <= static_cast<std::size_t>(event.guest_stack_base - address);
}

}  // namespace

NativeGuestModuleSet::~NativeGuestModuleSet()
{
    for (const Mapping& mapping : mappings_)
    {
        if (mapping.memory != nullptr)
        {
            HostUnmap(mapping.memory, mapping.size);
        }
    }
}

bool NativeGuestModuleSet::Add(hle::modules::GuestModuleDescriptor descriptor,
                               runtime::ImportGateTable* gates,
                               std::uintptr_t bridge_address,
                               std::uintptr_t cleanup_address,
                               std::uint32_t first_candidate_base,
                               std::string* error)
{
    if (gates == nullptr || error == nullptr ||
        !hle::modules::ValidateGuestModuleDescriptor(descriptor, error))
    {
        if (error != nullptr && error->empty())
        {
            *error = "invalid native guest module arguments";
        }
        return false;
    }

    runtime::ImportGateTable staged_gates = *gates;
    std::vector<runtime::GuestAddress> export_gates;
    std::vector<Binding> staged_bindings;
    export_gates.reserve(descriptor.exports.size());
    staged_bindings.reserve(descriptor.exports.size());
    for (const hle::modules::GuestExportDescriptor& export_descriptor : descriptor.exports)
    {
        runtime::GuestAddress gate_address;
        const bool bound = !export_descriptor.name.empty()
                               ? staged_gates.BindByName(descriptor.name,
                                                        export_descriptor.name,
                                                        &gate_address,
                                                        error)
                               : staged_gates.BindByOrdinal(descriptor.name,
                                                           export_descriptor.ordinal.value(),
                                                           &gate_address,
                                                           error);
        const runtime::ImportGate* gate = FindGateByAddress(staged_gates, gate_address);
        if (!bound || gate == nullptr)
        {
            if (error->empty())
            {
                *error = "cannot bind native guest module export gate";
            }
            return false;
        }
        export_gates.push_back(gate_address);
        staged_bindings.push_back({*gate, export_descriptor});
    }

    NativeGuestModuleImage image;
    if (!MapNativeGuestModuleImage(descriptor,
                                   std::span<const runtime::GuestAddress>(export_gates),
                                   bridge_address,
                                   cleanup_address,
                                   first_candidate_base,
                                   &image,
                                   error))
    {
        return false;
    }
    if (!registry_.Register(descriptor, image.mapping, error))
    {
        ReleaseNativeGuestModuleImage(&image);
        return false;
    }

    mappings_.push_back({image.memory, image.mapping.image_size});
    image.memory = nullptr;
    for (Binding& binding : staged_bindings)
    {
        binding_index_.emplace(binding.gate.address.value(), bindings_.size());
        bindings_.push_back(std::move(binding));
    }
    *gates = std::move(staged_gates);
    error->clear();
    return true;
}

bool NativeGuestModuleSet::Dispatch(const NativeImportGateEvent& event,
                                    NativeImportGateResult* result,
                                    std::string* error,
                                    const hle::ImportCallServices* services) const
{
    if (result == nullptr || error == nullptr)
    {
        SetError(error, "invalid native guest module dispatch arguments");
        return false;
    }
    const Binding* binding = FindBinding(event.gate_address);
    if (binding == nullptr)
    {
        *error = "native guest module gate is unknown";
        return false;
    }

    const std::size_t argument_bytes =
        static_cast<std::size_t>(binding->descriptor.argument_count) * sizeof(std::uint32_t);
    const std::size_t stack_bytes = sizeof(std::uint32_t) + argument_bytes;
    if (!StackContains(event, event.stack_pointer, stack_bytes))
    {
        *error = "native guest module arguments lie outside the guest stack";
        return false;
    }
    // Most imports take a few arguments; those fit without an allocation.
    std::array<std::uint32_t, 16> small_arguments{};
    std::vector<std::uint32_t> large_arguments;
    std::span<std::uint32_t> arguments(small_arguments.data(), binding->descriptor.argument_count);
    if (binding->descriptor.argument_count > small_arguments.size())
    {
        large_arguments.resize(binding->descriptor.argument_count);
        arguments = std::span<std::uint32_t>(large_arguments);
    }
    if (argument_bytes != 0)
    {
        std::memcpy(arguments.data(),
                    reinterpret_cast<const void*>(
                        static_cast<std::uintptr_t>(event.stack_pointer + sizeof(std::uint32_t))),
                    argument_bytes);
    }

    hle::ImportReturn import_result;
    const hle::ImportCall call{binding->gate, arguments, services, event.instruction_pointer,
                               event.stack_pointer + static_cast<std::uint32_t>(sizeof(std::uint32_t))};
    if (!binding->descriptor.handler(call, &import_result, error))
    {
        if (error->empty())
        {
            *error = "native guest module handler failed";
        }
        return false;
    }

    NativeImportGateResult completed;
    completed.eax = import_result.eax;
    completed.edx = import_result.edx;
    completed.exit_process = import_result.exit_process;
    completed.exit_code = import_result.exit_code;
    completed.stack_bytes_to_pop =
        binding->descriptor.calling_convention == hle::CallingConvention::kStdcall
            ? static_cast<std::uint32_t>(argument_bytes)
            : 0;
    *result = completed;
    error->clear();
    return true;
}

const runtime::ImportGate* NativeGuestModuleSet::FindGate(std::string_view module,
                                                           std::string_view name) const
{
    for (const Binding& binding : bindings_)
    {
        if (binding.gate.module == module && !binding.gate.by_ordinal &&
            binding.gate.name == name)
        {
            return &binding.gate;
        }
    }
    return nullptr;
}

const runtime::ImportGate* NativeGuestModuleSet::FindGate(runtime::GuestAddress address) const
{
    const Binding* binding = FindBinding(address.value());
    return binding == nullptr ? nullptr : &binding->gate;
}

const hle::modules::GuestExportDescriptor* NativeGuestModuleSet::FindExport(runtime::GuestAddress address) const
{
    const Binding* binding = FindBinding(address.value());
    return binding == nullptr ? nullptr : &binding->descriptor;
}

const NativeGuestModuleSet::Binding* NativeGuestModuleSet::FindBinding(std::uint32_t gate_address) const
{
    const auto found = binding_index_.find(gate_address);
    return found == binding_index_.end() ? nullptr : &bindings_[found->second];
}

const hle::modules::GuestModuleRegistry& NativeGuestModuleSet::registry() const
{
    return registry_;
}

}  // namespace re2dj::platform::native
