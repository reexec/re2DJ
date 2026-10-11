// IDirectDrawSurface7 of the ddraw facade, following the shared DirectX
// core's surface rules. Pixels live in guest memory, where the guest's locks
// and device contexts will reach them.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ddraw_interfaces.h"
#include "re2dj/directx/abi.h"
#include "re2dj/directx/direct3d_description.h"
#include "re2dj/directx/directdraw_surface.h"
#include "re2dj/graphics/color_depth.h"
#include "re2dj/graphics/legacy_draw_command.h"
#include "re2dj/graphics/legacy_texture.h"
#include "re2dj/graphics/true_color.h"
#include "re2dj/hle/guest_com.h"
#include "re2dj/hle/guest_gdi.h"
#include "re2dj/hle/guest_process.h"
#include "re2dj/hle/host_presentation.h"

namespace re2dj::hle::modules::ddraw
{
namespace
{

namespace dx = re2dj::directx;
using com::CallName;
using com::Fail;
using com::MethodProcess;
using com::Succeed;

// A surface's state: its shape, its pixels in guest memory, and the surfaces
// attached to it, whose references it holds.
struct SurfaceState final : GuestComState
{
    dx::SurfaceShape shape;
    // A DirectX 6 surface (IDirectDrawSurface4) rather than a DirectX 7 one;
    // the two share every method but QueryInterface's answer.
    bool directx6 = false;
    std::uint32_t pitch = 0;
    std::uint32_t pixels = 0;
    std::uint32_t back_buffer = 0;
    std::uint32_t depth_buffer = 0;
    // The clipper SetClipper attached, whose reference the surface holds (#15).
    std::uint32_t clipper = 0;
    // The surface's GDI device context, made at the first GetDC with a bitmap
    // over the surface's own pixels selected, and whether the guest holds it.
    std::uint32_t dc = 0;
    std::uint32_t dc_bitmap = 0;
    bool dc_held = false;
    // The area the last Lock handed out, which Unlock puts back on the host's
    // render target when this is the surface the guest presents from.
    bool locked = false;
    std::uint32_t lock_x = 0;
    std::uint32_t lock_y = 0;
    std::uint32_t lock_width = 0;
    std::uint32_t lock_height = 0;
    // The source blit color key, once the guest sets one.
    bool has_color_key = false;
    dx::DdColorKey color_key;
    // The render backend's names for the pixels: an identity unique to this
    // surface, a revision that moves whenever the guest changes them, and the
    // host copy taken at cached_revision.
    std::uint64_t identity = 0;
    std::uint64_t revision = 1;
    std::uint64_t cached_revision = 0;
    std::vector<std::uint8_t> cached_pixels;
    // The pixels at 8 bits per channel, from the first time 32-bit colour
    // needed them until the surface goes (graphics/true_color.h). Shared with
    // the DC's bitmap, so GDI drawing writes it too.
    std::shared_ptr<graphics::TrueColorPlane> true_color;
    // The host that may hold a texture made from the pixels; it outlives the
    // guest process.
    hle::HostPresentation* presentation = nullptr;

    // A DirectX 6 texture's IDirect3DTexture2 block, made at the first
    // QueryInterface for it. The surface owns the block's only reference;
    // the guest's references on it count on the surface.
    std::uint32_t texture = 0;

    std::vector<std::uint32_t> HeldReferences() const override { return {back_buffer, depth_buffer, texture, clipper}; }
    void ReleaseResources(GuestProcess& process) override
    {
        if (dc != 0)
        {
            process.gdi().Delete(dc);
            process.gdi().Delete(dc_bitmap);
            dc = 0;
            dc_bitmap = 0;
        }
        if (presentation != nullptr && cached_revision != 0)
        {
            presentation->DiscardTexture(identity);
            cached_revision = 0;
        }
        if (pixels != 0)
        {
            process.VirtualFree(pixels, 0, kMemRelease);
            pixels = 0;
        }
    }
};

SurfaceState* StateOf(GuestProcess& process, std::uint32_t surface)
{
    const GuestComObject* object = process.com().Find(surface);
    return object == nullptr ? nullptr : object->StateAs<SurfaceState>();
}

// Gives a surface a true-color plane, the RGB565 pixels widened, if it has
// none yet, and hands it to the surface's DC bitmap as well. With widen off
// the new plane stays black, for a caller about to overwrite all of it.
bool EnsureTrueColor(const ImportCall& call, GuestProcess& process, SurfaceState& state, bool widen,
                     std::string* error)
{
    if (state.true_color != nullptr || state.pixels == 0)
    {
        return true;
    }
    auto plane = std::make_shared<graphics::TrueColorPlane>(state.shape.width, state.shape.height);
    if (widen)
    {
        std::vector<std::uint8_t> row(static_cast<std::size_t>(state.shape.width) * 2);
        for (std::uint32_t y = 0; y < state.shape.height; ++y)
        {
            if (!com::ReadBytes(call, state.pixels + y * state.pitch, row, error))
            {
                return false;
            }
            graphics::WidenRgb565Row(row.data(), plane->Row(y), state.shape.width);
        }
    }
    state.true_color = std::move(plane);
    if (GuestBitmap* bitmap = state.dc_bitmap == 0 ? nullptr : process.gdi().FindBitmap(state.dc_bitmap))
    {
        bitmap->true_color = state.true_color;
    }
    return true;
}

// Matches a surface's plane, when it has one, to the RGB565 pixels in an area
// the guest may have written itself (graphics::ReconcileTrueColorRow).
bool ReconcileTrueColor(const ImportCall& call, SurfaceState& state, std::uint32_t x, std::uint32_t y,
                        std::uint32_t width, std::uint32_t height, std::string* error)
{
    if (state.true_color == nullptr || width == 0)
    {
        return true;
    }
    std::vector<std::uint8_t> row(static_cast<std::size_t>(width) * 2);
    for (std::uint32_t line = y; line < y + height; ++line)
    {
        if (!com::ReadBytes(call, state.pixels + line * state.pitch + x * 2, row, error))
        {
            return false;
        }
        graphics::ReconcileTrueColorRow(state.true_color->Row(line) + x, row.data(), width);
    }
    return true;
}

bool ReadGuestWord(const ImportCall& call, std::uint32_t address, std::uint32_t* value, std::string* error)
{
    std::array<std::uint8_t, 4> bytes{};
    std::string read_error;
    if (!call.services->ReadGuestBytes(runtime::GuestAddress(address), bytes, &read_error))
    {
        return Fail(error, CallName(call) + " cannot read guest memory: " + read_error);
    }
    *value = static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
             (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
    return true;
}

bool QueryInterface(const ImportCall& call, ImportReturn* result, std::string* error)
{
    GuestProcess* process = MethodProcess(call, result, 3, kSurfaceObject, error);
    if (process == nullptr)
    {
        return false;
    }
    if (call.arguments[2] == 0)
    {
        return Succeed(result, dx::kEPointer, error);
    }
    dx::Guid iid{};
    if (!com::ReadGuid(call, call.arguments[1], &iid, error))
    {
        return false;
    }
    SurfaceState* state = StateOf(*process, call.arguments[0]);
    // A DirectX 6 texture gives its IDirect3DTexture2, which shares the
    // surface's count, as the Windows DX6 facade's texture interface does.
    if (iid == dx::kIidDirect3DTexture2 && state != nullptr && state->directx6 &&
        (state->shape.caps & dx::kDdsCapsTexture) != 0)
    {
        if (state->texture == 0)
        {
            state->texture = CreateTexture2(call, *process, call.arguments[0], error);
            if (state->texture == 0)
            {
                return false;
            }
        }
        if (!com::WriteWord(call, call.arguments[2], state->texture, error))
        {
            return false;
        }
        process->com().AddRef(call.arguments[0]);
        return Succeed(result, dx::kDdOk, error);
    }
    // No game asks a surface for another interface; one that is not the
    // surface itself or a texture's is not modelled.
    const dx::Guid& own = state != nullptr && state->directx6 ? dx::kIidDirectDrawSurface4 : dx::kIidDirectDrawSurface7;
    if (iid != dx::kIidUnknown && iid != own)
    {
        return Fail(error, CallName(call) + " has no model of " + com::FormatGuid(iid));
    }
    if (!com::WriteWord(call, call.arguments[2], call.arguments[0], error))
    {
        return false;
    }
    process->com().AddRef(call.arguments[0]);
    return Succeed(result, dx::kDdOk, error);
}

// AddAttachedSurface(this, lpDDSAttachedSurface): a depth surface of the same
// DirectDraw object, replacing any earlier one.
bool AddAttachedSurface(const ImportCall& call, ImportReturn* result, std::string* error)
{
    GuestProcess* process = MethodProcess(call, result, 2, kSurfaceObject, error);
    if (process == nullptr)
    {
        return false;
    }
    const std::uint32_t attachment = call.arguments[1];
    if (attachment == 0)
    {
        return Succeed(result, dx::kDdErrInvalidParams, error);
    }
    const GuestComObject* self = process->com().Find(call.arguments[0]);
    const GuestComObject* attached = process->com().Find(attachment);
    SurfaceState* state = StateOf(*process, call.arguments[0]);
    SurfaceState* attached_state = StateOf(*process, attachment);
    if (attached == nullptr || attached_state == nullptr || attached->parent != self->parent)
    {
        return Succeed(result, dx::kDdErrInvalidObject, error);
    }
    const std::uint32_t attachable = dx::CheckAttachment(attached_state->shape);
    if (attachable != dx::kDdOk || state->depth_buffer == attachment)
    {
        return Succeed(result, attachable, error);
    }
    process->com().AddRef(attachment);
    const std::uint32_t previous = state->depth_buffer;
    state->depth_buffer = attachment;
    if (previous != 0)
    {
        process->com().Release(*process, previous);
    }
    return Succeed(result, dx::kDdOk, error);
}

// GetAttachedSurface(this, lpDDSCaps2, lplpDDAttachedSurface): the back
// buffer or depth surface asked for, AddRef'd, or DDERR_NOTFOUND.
bool GetAttachedSurface(const ImportCall& call, ImportReturn* result, std::string* error)
{
    GuestProcess* process = MethodProcess(call, result, 3, kSurfaceObject, error);
    if (process == nullptr)
    {
        return false;
    }
    if (call.arguments[1] == 0 || call.arguments[2] == 0)
    {
        return Succeed(result, dx::kDdErrInvalidParams, error);
    }
    dx::DdsCaps2 caps;
    std::array<std::uint8_t, sizeof(dx::DdsCaps2)> bytes{};
    std::string read_error;
    if (!call.services->ReadGuestBytes(runtime::GuestAddress(call.arguments[1]), bytes, &read_error))
    {
        return Fail(error, CallName(call) + " cannot read DDSCAPS2: " + read_error);
    }
    std::memcpy(&caps, bytes.data(), sizeof(caps));
    if (!com::WriteWord(call, call.arguments[2], 0, error))
    {
        return false;
    }
    const SurfaceState* state = StateOf(*process, call.arguments[0]);
    std::uint32_t found = 0;
    switch (dx::QueryAttachment(caps))
    {
    case dx::AttachmentQuery::kBackBuffer:
        found = state->back_buffer;
        break;
    case dx::AttachmentQuery::kDepth:
        found = state->depth_buffer;
        break;
    case dx::AttachmentQuery::kNone:
        break;
    }
    if (found == 0)
    {
        return Succeed(result, dx::kDdErrNotFound, error);
    }
    if (!com::WriteWord(call, call.arguments[2], found, error))
    {
        return false;
    }
    process->com().AddRef(found);
    return Succeed(result, dx::kDdOk, error);
}

bool GetCaps(const ImportCall& call, ImportReturn* result, std::string* error)
{
    GuestProcess* process = MethodProcess(call, result, 2, kSurfaceObject, error);
    if (process == nullptr)
    {
        return false;
    }
    if (call.arguments[1] == 0)
    {
        return Succeed(result, dx::kDdErrInvalidParams, error);
    }
    dx::DdsCaps2 caps;
    caps.caps = StateOf(*process, call.arguments[0])->shape.caps;
    return com::WriteStruct(call, call.arguments[1], caps, error) && Succeed(result, dx::kDdOk, error);
}

// GetPixelFormat(this, lpDDPixelFormat): RGB565 into a structure whose
// dwSize is right, DDERR_INVALIDPARAMS otherwise.
bool GetPixelFormat(const ImportCall& call, ImportReturn* result, std::string* error)
{
    if (MethodProcess(call, result, 2, kSurfaceObject, error) == nullptr)
    {
        return false;
    }
    std::uint32_t size = 0;
    if (call.arguments[1] == 0)
    {
        return Succeed(result, dx::kDdErrInvalidParams, error);
    }
    if (!ReadGuestWord(call, call.arguments[1], &size, error))
    {
        return false;
    }
    if (size != sizeof(dx::DdPixelFormat))
    {
        return Succeed(result, dx::kDdErrInvalidParams, error);
    }
    return com::WriteStruct(call, call.arguments[1], dx::Rgb565Format(), error) &&
           Succeed(result, dx::kDdOk, error);
}

// GetSurfaceDesc(this, lpDDSurfaceDesc2): the shared core's description into
// a structure whose dwSize is right, DDERR_INVALIDPARAMS otherwise.
bool GetSurfaceDesc(const ImportCall& call, ImportReturn* result, std::string* error)
{
    GuestProcess* process = MethodProcess(call, result, 2, kSurfaceObject, error);
    if (process == nullptr)
    {
        return false;
    }
    std::uint32_t size = 0;
    if (call.arguments[1] == 0)
    {
        return Succeed(result, dx::kDdErrInvalidParams, error);
    }
    if (!ReadGuestWord(call, call.arguments[1], &size, error))
    {
        return false;
    }
    if (size != sizeof(dx::DdSurfaceDesc2))
    {
        return Succeed(result, dx::kDdErrInvalidParams, error);
    }
    const SurfaceState* state = StateOf(*process, call.arguments[0]);
    return com::WriteStruct(call, call.arguments[1], dx::SurfaceDescription(state->shape, state->pitch), error) &&
           Succeed(result, dx::kDdOk, error);
}

// Whether surface is the one its DirectDraw object presents from: the render
// target whose picture lives on the host rather than in its pixels.
bool IsPresentationSurface(GuestProcess& process, std::uint32_t surface)
{
    const GuestComObject* object = process.com().Find(surface);
    const GuestComObject* direct_draw = object == nullptr ? nullptr : process.com().Find(object->parent);
    const DirectDrawState* display = direct_draw == nullptr ? nullptr : direct_draw->StateAs<DirectDrawState>();
    return display != nullptr && display->presentation_surface == surface;
}

// Copies the host render target's picture in the locked area into the
// surface's pixels (read) or back out of them (write), a row at a time.
bool CopyTargetArea(const ImportCall& call, const SurfaceState& state, bool read, bool* copied, std::string* error)
{
    *copied = false;
    HostPresentation* presentation = call.services->Presentation();
    if (presentation == nullptr)
    {
        return true;
    }
    const std::uint32_t row_bytes = state.lock_width * 2;
    std::vector<std::uint8_t> area(static_cast<std::size_t>(row_bytes) * state.lock_height);
    const std::uint32_t first = state.pixels + state.lock_y * state.pitch + state.lock_x * 2;
    std::string host_error;
    if (read)
    {
        if (!presentation->ReadTarget(state.lock_x, state.lock_y, state.lock_width, state.lock_height, area,
                                      row_bytes, &host_error))
        {
            return true;
        }
        for (std::uint32_t row = 0; row < state.lock_height; ++row)
        {
            const std::span<const std::uint8_t> line(area.data() + static_cast<std::size_t>(row) * row_bytes,
                                                     row_bytes);
            if (!com::WriteBytes(call, first + row * state.pitch, line, error))
            {
                return false;
            }
        }
        *copied = true;
        return true;
    }
    for (std::uint32_t row = 0; row < state.lock_height; ++row)
    {
        const std::span<std::uint8_t> line(area.data() + static_cast<std::size_t>(row) * row_bytes, row_bytes);
        std::string read_error;
        if (!call.services->ReadGuestBytes(runtime::GuestAddress(first + row * state.pitch), line, &read_error))
        {
            return Fail(error, CallName(call) + " cannot read the locked pixels: " + read_error);
        }
    }
    *copied = presentation->WriteTarget(state.lock_x, state.lock_y, state.lock_width, state.lock_height, area,
                                        row_bytes, &host_error);
    return true;
}

// Lock(this, lpDestRect, lpDDSurfaceDesc, dwFlags, hEvent): the surface's
// own pixels in guest memory, under the shared core's rules. For the surface
// the guest presents from, whose picture the host draws, the locked area is
// first read back from the host's render target, so the guest sees what it
// drew; a host that cannot read leaves DDERR_GENERIC.
bool Lock(const ImportCall& call, ImportReturn* result, std::string* error)
{
    GuestProcess* process = MethodProcess(call, result, 5, kSurfaceObject, error);
    if (process == nullptr)
    {
        return false;
    }
    const SurfaceState* state = StateOf(*process, call.arguments[0]);
    std::uint32_t size = 0;
    if (call.arguments[2] != 0 && !ReadGuestWord(call, call.arguments[2], &size, error))
    {
        return false;
    }
    dx::SurfaceRect rect;
    if (call.arguments[1] != 0 && !com::ReadStruct(call, call.arguments[1], &rect, error))
    {
        return false;
    }
    dx::SurfaceLockPlan plan = dx::PlanLock(state->shape, state->pitch,
                                            state->shape.has_pixels() && state->pixels != 0,
                                            call.arguments[2] != 0 && size == sizeof(dx::DdSurfaceDesc2),
                                            call.arguments[4] != 0, call.arguments[1] == 0 ? nullptr : &rect);
    if (plan.result != dx::kDdOk)
    {
        return Succeed(result, plan.result, error);
    }
    SurfaceState* locked = StateOf(*process, call.arguments[0]);
    locked->lock_x = call.arguments[1] == 0 ? 0 : static_cast<std::uint32_t>(rect.left);
    locked->lock_y = call.arguments[1] == 0 ? 0 : static_cast<std::uint32_t>(rect.top);
    locked->lock_width = plan.description.width;
    locked->lock_height = plan.description.height;
    if (IsPresentationSurface(*process, call.arguments[0]) && call.services->Presentation() != nullptr)
    {
        bool copied = false;
        if (!CopyTargetArea(call, *locked, true, &copied, error))
        {
            return false;
        }
        if (!copied)
        {
            return Succeed(result, dx::kDdErrGeneric, error);
        }
    }
    locked->locked = true;
    plan.description.surface = state->pixels + plan.offset;
    return com::WriteStruct(call, call.arguments[2], plan.description, error) &&
           Succeed(result, dx::kDdOk, error);
}

// Unlock(this, lpRect): the guest may have written anything into the
// pixels, so the host's copy of them is stale from here on. For the surface
// the guest presents from, the locked area goes back onto the host's render
// target, where its picture lives; a host that cannot take it leaves
// DDERR_GENERIC.
bool Unlock(const ImportCall& call, ImportReturn* result, std::string* error)
{
    GuestProcess* process = MethodProcess(call, result, 2, kSurfaceObject, error);
    if (process == nullptr)
    {
        return false;
    }
    SurfaceState* state = StateOf(*process, call.arguments[0]);
    ++state->revision;
    const bool was_locked = state->locked;
    state->locked = false;
    if (was_locked && !ReconcileTrueColor(call, *state, state->lock_x, state->lock_y, state->lock_width,
                                          state->lock_height, error))
    {
        return false;
    }
    if (was_locked && IsPresentationSurface(*process, call.arguments[0]) && call.services->Presentation() != nullptr)
    {
        bool copied = false;
        if (!CopyTargetArea(call, *state, false, &copied, error))
        {
            return false;
        }
        if (!copied)
        {
            return Succeed(result, dx::kDdErrGeneric, error);
        }
    }
    return Succeed(result, dx::kDdOk, error);
}

// GetDC(this, lphDC): the surface's DC, whose selected bitmap is the
// surface's RGB565 pixels, under the core's rules.
bool GetDC(const ImportCall& call, ImportReturn* result, std::string* error)
{
    GuestProcess* process = MethodProcess(call, result, 2, kSurfaceObject, error);
    if (process == nullptr)
    {
        return false;
    }
    if (call.arguments[1] == 0)
    {
        return Succeed(result, dx::kDdErrInvalidParams, error);
    }
    if (!com::WriteWord(call, call.arguments[1], 0, error))
    {
        return false;
    }
    SurfaceState* state = StateOf(*process, call.arguments[0]);
    const std::uint32_t checked = dx::CheckGetDc(state->shape.has_pixels(), state->dc_held);
    if (checked != dx::kDdOk)
    {
        return Succeed(result, checked, error);
    }
    // In 32-bit colour, what the guest draws through the DC is kept at 24 bits.
    if (graphics::TrueColorSelected() && !EnsureTrueColor(call, *process, *state, true, error))
    {
        return false;
    }
    if (state->dc == 0)
    {
        GuestBitmap bitmap;
        bitmap.width = state->shape.width;
        bitmap.height = state->shape.height;
        bitmap.bits_per_pixel = 16;
        bitmap.pitch = state->pitch;
        bitmap.bits = state->pixels;
        bitmap.top_down = true;
        const dx::DdPixelFormat format = dx::Rgb565Format();
        bitmap.masks = {format.red_mask, format.green_mask, format.blue_mask};
        bitmap.true_color = state->true_color;
        state->dc_bitmap = process->gdi().AddBitmap(std::move(bitmap));
        GuestDc dc;
        dc.bitmap = state->dc_bitmap;
        state->dc = process->gdi().AddDc(dc);
    }
    if (!com::WriteWord(call, call.arguments[1], state->dc, error))
    {
        return false;
    }
    state->dc_held = true;
    return Succeed(result, dx::kDdOk, error);
}

// ReleaseDC(this, hDC): the DC GetDC gave, while the guest holds it.
bool ReleaseDC(const ImportCall& call, ImportReturn* result, std::string* error)
{
    GuestProcess* process = MethodProcess(call, result, 2, kSurfaceObject, error);
    if (process == nullptr)
    {
        return false;
    }
    SurfaceState* state = StateOf(*process, call.arguments[0]);
    const std::uint32_t checked =
        dx::CheckReleaseDc(state->dc_held, call.arguments[1] != 0 && call.arguments[1] == state->dc);
    if (checked == dx::kDdOk)
    {
        // The guest drew through the DC.
        state->dc_held = false;
        ++state->revision;
    }
    return Succeed(result, checked, error);
}

// SetColorKey(this, dwFlags, lpDDColorKey): the source blit key, kept for
// drawing.
bool SetColorKey(const ImportCall& call, ImportReturn* result, std::string* error)
{
    GuestProcess* process = MethodProcess(call, result, 3, kSurfaceObject, error);
    if (process == nullptr)
    {
        return false;
    }
    const std::uint32_t checked = dx::CheckSetColorKey(call.arguments[1], call.arguments[2] != 0);
    if (checked != dx::kDdOk)
    {
        return Succeed(result, checked, error);
    }
    SurfaceState* state = StateOf(*process, call.arguments[0]);
    if (!com::ReadStruct(call, call.arguments[2], &state->color_key, error))
    {
        return false;
    }
    state->has_color_key = true;
    return Succeed(result, dx::kDdOk, error);
}

// Flip(this, lpDDSurfaceTargetOverride, dwFlags): presents the frame drawn
// into the back buffer. A surface without a back buffer, or an override other
// than its back buffer, is DDERR_NOTFLIPPABLE, as the Windows facade answers.
bool Flip(const ImportCall& call, ImportReturn* result, std::string* error)
{
    GuestProcess* process = MethodProcess(call, result, 3, kSurfaceObject, error);
    if (process == nullptr)
    {
        return false;
    }
    const SurfaceState* state = StateOf(*process, call.arguments[0]);
    if (state->back_buffer == 0 || (call.arguments[1] != 0 && call.arguments[1] != state->back_buffer))
    {
        return Succeed(result, dx::kDdErrNotFlippable, error);
    }
    hle::HostPresentation* presentation = call.services->Presentation();
    std::string present_error;
    if (presentation != nullptr && !presentation->Present(&present_error))
    {
        return Fail(error, CallName(call) + " cannot present: " + present_error);
    }
    return Succeed(result, dx::kDdOk, error);
}

// Presents the frame, as a blit onto the primary surface does.
bool PresentFromBlit(const ImportCall& call, ImportReturn* result, std::string* error)
{
    HostPresentation* presentation = call.services->Presentation();
    std::string present_error;
    if (presentation != nullptr && !presentation->Present(&present_error))
    {
        return Fail(error, CallName(call) + " cannot present: " + present_error);
    }
    return Succeed(result, dx::kDdOk, error);
}

// A left, top, right, bottom rectangle inside a surface of the given size;
// false for an empty one or one reaching outside.
bool InsideSurface(const std::array<std::int32_t, 4>& rect, std::uint32_t width, std::uint32_t height)
{
    return rect[0] >= 0 && rect[1] >= 0 && rect[2] > rect[0] && rect[3] > rect[1] &&
           rect[2] <= static_cast<std::int32_t>(width) && rect[3] <= static_cast<std::int32_t>(height);
}

// Copies a source rectangle to (x, y) of the target, as the Windows DX6
// facade's surface copy answers: a DC the guest holds on either side is
// DDERR_SURFACEBUSY, a source key the source has none of DDERR_NOCOLORKEY, a
// placement outside the target DDERR_INVALIDRECT; source-keyed pixels are
// left alone. Onto a back buffer, whose frame the host draws, the copy is
// also drawn there as a textured quad.
bool CopySurfaceRegion(const ImportCall& call,
                       ImportReturn* result,
                       GuestProcess& process,
                       std::uint32_t target_surface,
                       std::uint32_t x,
                       std::uint32_t y,
                       std::uint32_t source_surface,
                       const std::array<std::int32_t, 4>& rect,
                       bool keyed,
                       std::string* error)
{
    SurfaceState* target = StateOf(process, target_surface);
    const SurfaceState* source = StateOf(process, source_surface);
    if (target->pixels == 0 || source->pixels == 0 || target->dc_held || source->dc_held)
    {
        return Succeed(result, dx::kDdErrSurfaceBusy, error);
    }
    if (keyed && !source->has_color_key)
    {
        return Succeed(result, dx::kDdErrNoColorKey, error);
    }
    const auto width = static_cast<std::uint32_t>(rect[2] - rect[0]);
    const auto height = static_cast<std::uint32_t>(rect[3] - rect[1]);
    if (x > target->shape.width || y > target->shape.height || width > target->shape.width - x ||
        height > target->shape.height - y)
    {
        return Succeed(result, dx::kDdErrInvalidRect, error);
    }
    graphics::Rgb565ColorKey key;
    key.enabled = keyed;
    key.low = static_cast<std::uint16_t>(source->color_key.low);
    key.high = static_cast<std::uint16_t>(source->color_key.high);
    // In 32-bit colour the target keeps what the source has at 24 bits.
    if (graphics::TrueColorSelected() && !EnsureTrueColor(call, process, *target, true, error))
    {
        return false;
    }
    std::vector<std::uint8_t> from(static_cast<std::size_t>(width) * 2);
    std::vector<std::uint8_t> into(from.size());
    std::vector<std::uint32_t> source_line;
    for (std::uint32_t row = 0; row < height; ++row)
    {
        const std::uint32_t from_address =
            source->pixels + (static_cast<std::uint32_t>(rect[1]) + row) * source->pitch +
            static_cast<std::uint32_t>(rect[0]) * 2;
        const std::uint32_t into_address = target->pixels + (y + row) * target->pitch + x * 2;
        if (!com::ReadBytes(call, from_address, from, error) || !com::ReadBytes(call, into_address, into, error))
        {
            return false;
        }
        // The plane side, deciding the key on the source row as read. The
        // source plane row is read in full first, as the RGB565 row is, in
        // case both are one surface's.
        if (target->true_color != nullptr)
        {
            const std::uint32_t* source_plane = nullptr;
            if (source->true_color != nullptr)
            {
                const std::uint32_t* const first =
                    source->true_color->Row(static_cast<std::uint32_t>(rect[1]) + row) + rect[0];
                source_line.assign(first, first + width);
                source_plane = source_line.data();
            }
            graphics::CopyTrueColorRow(target->true_color->Row(y + row) + x, source_plane, from.data(), width, key);
        }
        for (std::size_t offset = 0; offset < from.size(); offset += 2)
        {
            const auto pixel = static_cast<std::uint16_t>(from[offset] | (from[offset + 1] << 8));
            if (!key.enabled || !graphics::IsRgb565ColorKeyMatch(pixel, key))
            {
                into[offset] = from[offset];
                into[offset + 1] = from[offset + 1];
            }
        }
        if (!com::WriteBytes(call, into_address, into, error))
        {
            return false;
        }
    }
    ++target->revision;
    if ((target->shape.caps & dx::kDdsCapsBackBuffer) == 0)
    {
        return Succeed(result, dx::kDdOk, error);
    }
    graphics::LegacyTextureView view;
    if (!SurfaceTextureView(call, process, source_surface, &view, error))
    {
        return false;
    }
    view.source_color_key = key;
    const auto source_width = static_cast<float>(source->shape.width);
    const auto source_height = static_cast<float>(source->shape.height);
    const float left = static_cast<float>(x);
    const float top = static_cast<float>(y);
    const float right = static_cast<float>(x + width);
    const float bottom = static_cast<float>(y + height);
    const float u0 = static_cast<float>(rect[0]) / source_width;
    const float v0 = static_cast<float>(rect[1]) / source_height;
    const float u1 = static_cast<float>(rect[2]) / source_width;
    const float v1 = static_cast<float>(rect[3]) / source_height;
    graphics::LegacyDrawCommand command;
    command.vertices = {
        {left, top, 0.0f, 1.0f, 0xffffffff, 0, u0, v0},
        {right, top, 0.0f, 1.0f, 0xffffffff, 0, u1, v0},
        {left, bottom, 0.0f, 1.0f, 0xffffffff, 0, u0, v1},
        {right, bottom, 0.0f, 1.0f, 0xffffffff, 0, u1, v1},
    };
    graphics::LegacyFixedFunctionState state;
    state.color_key_enabled = keyed;
    const GuestComObject* direct_draw = process.com().Find(process.com().Find(target_surface)->parent);
    const DirectDrawState* display = direct_draw == nullptr ? nullptr : direct_draw->StateAs<DirectDrawState>();
    HostPresentation* presentation = call.services->Presentation();
    std::string draw_error;
    if (presentation != nullptr && display != nullptr &&
        !presentation->Draw(command, state, display->display.mode.width, display->display.mode.height, &view,
                            &draw_error))
    {
        return Succeed(result, dx::kDdErrGeneric, error);
    }
    return Succeed(result, dx::kDdOk, error);
}

// Blt with no source: a color fill, answered as the Windows DX6 facade does:
// a source rectangle or flags other than DDBLT_COLORFILL are
// DDERR_UNSUPPORTED, a DDBLTFX of the wrong size DDERR_INVALIDPARAMS, a
// surface the guest holds a DC on DDERR_SURFACEBUSY, a rectangle outside the
// surface DDERR_INVALIDRECT. Filling the whole surface the guest presents
// from also clears the host's target, as a full Clear does.
bool ColorFill(const ImportCall& call, ImportReturn* result, GuestProcess* process, std::string* error)
{
    if (call.arguments[3] != 0 || call.arguments[4] != dx::kDdBltColorFill)
    {
        return Succeed(result, dx::kDdErrUnsupported, error);
    }
    const std::uint32_t effects = call.arguments[5];
    std::array<std::uint32_t, dx::kDdBltFxSize / 4> fx{};
    if (effects == 0)
    {
        return Succeed(result, dx::kDdErrInvalidParams, error);
    }
    if (!com::ReadStruct(call, effects, &fx, error))
    {
        return false;
    }
    if (fx[0] != dx::kDdBltFxSize)
    {
        return Succeed(result, dx::kDdErrInvalidParams, error);
    }
    SurfaceState* state = StateOf(*process, call.arguments[0]);
    if (state->pixels == 0 || state->dc_held)
    {
        return Succeed(result, dx::kDdErrSurfaceBusy, error);
    }
    const auto width = static_cast<std::int32_t>(state->shape.width);
    const auto height = static_cast<std::int32_t>(state->shape.height);
    std::array<std::int32_t, 4> rect = {0, 0, width, height};
    if (call.arguments[1] != 0 && !com::ReadStruct(call, call.arguments[1], &rect, error))
    {
        return false;
    }
    if (!InsideSurface(rect, state->shape.width, state->shape.height))
    {
        return Succeed(result, dx::kDdErrInvalidRect, error);
    }
    const auto [left, top, right, bottom] = rect;
    const auto color = static_cast<std::uint16_t>(fx[dx::kDdBltFxFillColorOffset / 4]);
    const bool whole = left == 0 && top == 0 && right == width && bottom == height;
    // The fill colour is an RGB565 pixel, so the plane gets it widened.
    if (whole)
    {
        if (!FillSurface(call, *process, call.arguments[0], color, graphics::WidenRgb565(color), error))
        {
            return false;
        }
    }
    else
    {
        std::vector<std::uint8_t> row(static_cast<std::size_t>(right - left) * 2);
        for (std::size_t x = 0; x < row.size(); x += 2)
        {
            row[x] = static_cast<std::uint8_t>(color);
            row[x + 1] = static_cast<std::uint8_t>(color >> 8);
        }
        for (std::int32_t y = top; y < bottom; ++y)
        {
            if (!com::WriteBytes(call, state->pixels + static_cast<std::uint32_t>(y) * state->pitch +
                                           static_cast<std::uint32_t>(left) * 2,
                                 row, error))
            {
                return false;
            }
            if (state->true_color != nullptr)
            {
                std::uint32_t* const plane_row = state->true_color->Row(static_cast<std::uint32_t>(y));
                std::fill(plane_row + left, plane_row + right, graphics::WidenRgb565(color));
            }
        }
        ++state->revision;
    }
    const GuestComObject* direct_draw = process->com().Find(process->com().Find(call.arguments[0])->parent);
    const DirectDrawState* display = direct_draw == nullptr ? nullptr : direct_draw->StateAs<DirectDrawState>();
    if (whole && display != nullptr && display->presentation_surface == call.arguments[0])
    {
        HostPresentation* presentation = call.services->Presentation();
        std::string clear_error;
        if (presentation != nullptr && !presentation->ClearTarget(color, &clear_error))
        {
            return Fail(error, CallName(call) + " cannot clear the host target: " + clear_error);
        }
    }
    return Succeed(result, dx::kDdOk, error);
}

// Blt(this, lpDestRect, lpDDSrcSurface, lpSrcRect, dwFlags, lpDDBltFx), as the
// Windows DX6 facade answers it. Without a source it is a color fill. With
// one, flags beyond DDBLT_KEYSRC and DDBLT_WAIT or any DDBLTFX are
// DDERR_UNSUPPORTED, and a rectangle outside its surface DDERR_INVALIDRECT
// (onto the primary, any non-empty one presents the frame); rectangles of
// different sizes (a stretch) are DDERR_UNSUPPORTED, and the rest is the
// surface copy BltFast makes.
bool Blt(const ImportCall& call, ImportReturn* result, std::string* error)
{
    GuestProcess* process = MethodProcess(call, result, 6, kSurfaceObject, error);
    if (process == nullptr)
    {
        return false;
    }
    const std::uint32_t source_surface = call.arguments[2];
    if (source_surface == 0)
    {
        return ColorFill(call, result, process, error);
    }
    const std::uint32_t flags = call.arguments[4];
    if ((flags & ~(dx::kDdBltKeySrc | dx::kDdBltWait)) != 0 || call.arguments[5] != 0)
    {
        return Succeed(result, dx::kDdErrUnsupported, error);
    }
    const SurfaceState* target = StateOf(*process, call.arguments[0]);
    const SurfaceState* source = StateOf(*process, source_surface);
    if (source == nullptr)
    {
        return Fail(error, CallName(call) + " from an object that is no facade surface is not modelled");
    }
    std::array<std::int32_t, 4> from = {0, 0, static_cast<std::int32_t>(source->shape.width),
                                        static_cast<std::int32_t>(source->shape.height)};
    std::array<std::int32_t, 4> into = {0, 0, static_cast<std::int32_t>(target->shape.width),
                                        static_cast<std::int32_t>(target->shape.height)};
    if ((call.arguments[3] != 0 && !com::ReadStruct(call, call.arguments[3], &from, error)) ||
        (call.arguments[1] != 0 && !com::ReadStruct(call, call.arguments[1], &into, error)))
    {
        return false;
    }
    const bool primary = (target->shape.caps & dx::kDdsCapsPrimarySurface) != 0;
    const bool into_valid = primary && call.arguments[1] != 0 ? into[2] > into[0] && into[3] > into[1]
                                                              : InsideSurface(into, target->shape.width,
                                                                              target->shape.height);
    if (!InsideSurface(from, source->shape.width, source->shape.height) || !into_valid)
    {
        return Succeed(result, dx::kDdErrInvalidRect, error);
    }
    if (primary)
    {
        return PresentFromBlit(call, result, error);
    }
    if (into[2] - into[0] != from[2] - from[0] || into[3] - into[1] != from[3] - from[1])
    {
        return Succeed(result, dx::kDdErrUnsupported, error);
    }
    return CopySurfaceRegion(call, result, *process, call.arguments[0], static_cast<std::uint32_t>(into[0]),
                             static_cast<std::uint32_t>(into[1]), source_surface, from,
                             (flags & dx::kDdBltKeySrc) != 0, error);
}

// BltFast(this, dwX, dwY, lpDDSrcSurface, lpSrcRect, dwTrans), answered as the
// Windows DX6 facade does: no source, or flags beyond DDBLTFAST_SRCCOLORKEY
// and DDBLTFAST_WAIT, is DDERR_UNSUPPORTED; a source rectangle outside the
// source is DDERR_INVALIDRECT; onto the primary it presents the frame, and
// otherwise it is the surface copy.
bool BltFast(const ImportCall& call, ImportReturn* result, std::string* error)
{
    GuestProcess* process = MethodProcess(call, result, 6, kSurfaceObject, error);
    if (process == nullptr)
    {
        return false;
    }
    const std::uint32_t flags = call.arguments[5];
    if (call.arguments[3] == 0 || (flags & ~(dx::kDdBltFastSrcColorKey | dx::kDdBltFastWait)) != 0)
    {
        return Succeed(result, dx::kDdErrUnsupported, error);
    }
    const SurfaceState* target = StateOf(*process, call.arguments[0]);
    const SurfaceState* source = StateOf(*process, call.arguments[3]);
    if (source == nullptr)
    {
        return Fail(error, CallName(call) + " from an object that is no facade surface is not modelled");
    }
    std::array<std::int32_t, 4> rect = {0, 0, static_cast<std::int32_t>(source->shape.width),
                                        static_cast<std::int32_t>(source->shape.height)};
    if (call.arguments[4] != 0 && !com::ReadStruct(call, call.arguments[4], &rect, error))
    {
        return false;
    }
    if (!InsideSurface(rect, source->shape.width, source->shape.height))
    {
        return Succeed(result, dx::kDdErrInvalidRect, error);
    }
    if ((target->shape.caps & dx::kDdsCapsPrimarySurface) != 0)
    {
        return PresentFromBlit(call, result, error);
    }
    return CopySurfaceRegion(call, result, *process, call.arguments[0], call.arguments[1], call.arguments[2],
                             call.arguments[3], rect, (flags & dx::kDdBltFastSrcColorKey) != 0, error);
}

// The facade never loses a surface.
bool IsLost(const ImportCall& call, ImportReturn* result, std::string* error)
{
    return MethodProcess(call, result, 1, kSurfaceObject, error) != nullptr && Succeed(result, dx::kDdOk, error);
}

// SetClipper(this, lpDDClipper): attaches a clipper, holding a reference to
// it and letting go of the one before; null detaches. Another object is
// DDERR_INVALIDOBJECT (#15).
bool SetClipper(const ImportCall& call, ImportReturn* result, std::string* error)
{
    GuestProcess* process = MethodProcess(call, result, 2, kSurfaceObject, error);
    if (process == nullptr)
    {
        return false;
    }
    const std::uint32_t clipper = call.arguments[1];
    if (clipper != 0 && !IsClipper(*process, clipper))
    {
        return Succeed(result, dx::kDdErrInvalidObject, error);
    }
    SurfaceState* state = StateOf(*process, call.arguments[0]);
    if (state == nullptr)
    {
        return Succeed(result, dx::kDdErrInvalidObject, error);
    }
    if (clipper != 0)
    {
        process->com().AddRef(clipper);
    }
    const std::uint32_t previous = state->clipper;
    state->clipper = clipper;
    if (previous != 0)
    {
        process->com().Release(*process, previous);
    }
    return Succeed(result, dx::kDdOk, error);
}

// GetClipper(this, lplpDDClipper): the attached clipper with a new reference,
// or DDERR_NOCLIPPERATTACHED.
bool GetClipper(const ImportCall& call, ImportReturn* result, std::string* error)
{
    GuestProcess* process = MethodProcess(call, result, 2, kSurfaceObject, error);
    if (process == nullptr)
    {
        return false;
    }
    if (call.arguments[1] == 0)
    {
        return Succeed(result, dx::kDdErrInvalidParams, error);
    }
    const SurfaceState* state = StateOf(*process, call.arguments[0]);
    if (state == nullptr || state->clipper == 0)
    {
        return Succeed(result, dx::kDdErrNoClipperAttached, error);
    }
    if (!com::WriteWord(call, call.arguments[1], state->clipper, error))
    {
        return false;
    }
    process->com().AddRef(state->clipper);
    return Succeed(result, dx::kDdOk, error);
}

// IDirectDrawSurface7 in vtable order (ddraw.h).
constexpr com::Method kMethods[] = {
    {"QueryInterface", 3, &QueryInterface},
    {"AddRef", 1, &com::AddRef},
    {"Release", 1, &com::Release},
    {"AddAttachedSurface", 2, &AddAttachedSurface},
    {"AddOverlayDirtyRect", 2, &UnimplementedExport},
    {"Blt", 6, &Blt},
    {"BltBatch", 4, &UnimplementedExport},
    {"BltFast", 6, &BltFast},
    {"DeleteAttachedSurface", 3, &UnimplementedExport},
    {"EnumAttachedSurfaces", 3, &UnimplementedExport},
    {"EnumOverlayZOrders", 4, &UnimplementedExport},
    {"Flip", 3, &Flip},
    {"GetAttachedSurface", 3, &GetAttachedSurface},
    {"GetBltStatus", 2, &UnimplementedExport},
    {"GetCaps", 2, &GetCaps},
    {"GetClipper", 2, &GetClipper},
    {"GetColorKey", 3, &UnimplementedExport},
    {"GetDC", 2, &GetDC},
    {"GetFlipStatus", 2, &UnimplementedExport},
    {"GetOverlayPosition", 3, &UnimplementedExport},
    {"GetPalette", 2, &UnimplementedExport},
    {"GetPixelFormat", 2, &GetPixelFormat},
    {"GetSurfaceDesc", 2, &GetSurfaceDesc},
    {"Initialize", 3, &UnimplementedExport},
    {"IsLost", 1, &IsLost},
    {"Lock", 5, &Lock},
    {"ReleaseDC", 2, &ReleaseDC},
    {"Restore", 1, &UnimplementedExport},
    {"SetClipper", 2, &SetClipper},
    {"SetColorKey", 3, &SetColorKey},
    {"SetOverlayPosition", 3, &UnimplementedExport},
    {"SetPalette", 2, &UnimplementedExport},
    {"Unlock", 2, &Unlock},
    {"UpdateOverlay", 6, &UnimplementedExport},
    {"UpdateOverlayDisplay", 2, &UnimplementedExport},
    {"UpdateOverlayZOrder", 3, &UnimplementedExport},
    {"GetDDInterface", 2, &UnimplementedExport},
    {"PageLock", 2, &UnimplementedExport},
    {"PageUnlock", 2, &UnimplementedExport},
    {"SetSurfaceDesc", 3, &UnimplementedExport},
    {"SetPrivateData", 5, &UnimplementedExport},
    {"GetPrivateData", 4, &UnimplementedExport},
    {"FreePrivateData", 2, &UnimplementedExport},
    {"GetUniquenessValue", 2, &UnimplementedExport},
    {"ChangeUniquenessValue", 1, &UnimplementedExport},
    {"SetPriority", 2, &UnimplementedExport},
    {"GetPriority", 2, &UnimplementedExport},
    {"SetLOD", 2, &UnimplementedExport},
    {"GetLOD", 2, &UnimplementedExport},
};

// One surface of a plan: its object, holding a reference to the DirectDraw
// object, and for a surface with pixels zeroed RGB565 memory in the guest's
// VirtualAlloc arena.
std::uint32_t MakeSurface(const ImportCall& call,
                          GuestProcess& process,
                          std::uint32_t direct_draw,
                          const dx::SurfaceShape& shape,
                          bool directx6,
                          std::string* error)
{
    // Unique for the life of the process, unlike a surface's address.
    static std::uint64_t next_identity = 1;
    auto state = std::make_shared<SurfaceState>();
    state->shape = shape;
    state->directx6 = directx6;
    state->identity = next_identity++;
    state->presentation = call.services->Presentation();
    if (shape.has_pixels())
    {
        state->pitch = dx::Rgb565Pitch(shape.width);
        const std::uint32_t size = state->pitch * shape.height;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> committed;
        if (process.VirtualAlloc(0, size, kMemCommit | kMemReserve, kPageReadWrite, &state->pixels, &committed) !=
            GuestMemoryResult::kOk)
        {
            Fail(error, CallName(call) + " has no guest memory for a " + std::to_string(shape.width) + "x" +
                            std::to_string(shape.height) + " surface");
            return 0;
        }
        for (const auto& [address, length] : committed)
        {
            const std::vector<std::uint8_t> zeros(length, 0);
            if (!com::WriteBytes(call, address, zeros, error))
            {
                process.VirtualFree(state->pixels, 0, kMemRelease);
                return 0;
            }
        }
        // Zeroed pixels widen to a black plane, which a new one already is.
        if (graphics::TrueColorSelected())
        {
            state->true_color = std::make_shared<graphics::TrueColorPlane>(shape.width, shape.height);
        }
    }
    GuestComObject object;
    object.kind = kSurfaceObject;
    object.parent = direct_draw;
    object.state = state;
    const std::uint32_t surface =
        directx6 ? com::CreateObject(call, process, kModule, kDirectDrawSurface4, DirectDrawSurface4Methods(), object, error)
                 : com::CreateObject(call, process, kModule, kDirectDrawSurface7, kMethods, object, error);
    if (surface == 0)
    {
        state->ReleaseResources(process);
        return 0;
    }
    process.com().AddRef(direct_draw);
    return surface;
}

}  // namespace

std::span<const com::Method> DirectDrawSurface7Methods()
{
    return kMethods;
}

std::span<const com::Method> DirectDrawSurface4Methods()
{
    // IDirectDrawSurface4 is IDirectDrawSurface7 without its last four
    // methods (SetPriority through GetLOD), in the same order.
    return std::span<const com::Method>(kMethods).first(std::size(kMethods) - 4);
}

bool SurfaceTextureView(const ImportCall& call,
                        GuestProcess& process,
                        std::uint32_t surface,
                        graphics::LegacyTextureView* view,
                        std::string* error)
{
    SurfaceState* state = StateOf(process, surface);
    if (state == nullptr || state->pixels == 0)
    {
        return Fail(error, CallName(call) + " has no pixels to texture from");
    }
    if (graphics::TrueColorSelected() && !EnsureTrueColor(call, process, *state, true, error))
    {
        return false;
    }
    if (state->cached_revision != state->revision)
    {
        state->cached_pixels.resize(static_cast<std::size_t>(state->pitch) * state->shape.height);
        if (!com::ReadBytes(call, state->pixels, state->cached_pixels, error))
        {
            return false;
        }
        state->cached_revision = state->revision;
    }
    view->pixels = state->cached_pixels.data();
    view->width = state->shape.width;
    view->height = state->shape.height;
    view->pitch = state->pitch;
    view->identity = state->identity;
    view->revision = state->revision;
    view->source_color_key.enabled = state->has_color_key;
    view->source_color_key.low = static_cast<std::uint16_t>(state->color_key.low);
    view->source_color_key.high = static_cast<std::uint16_t>(state->color_key.high);
    // The plane lives in host memory and the backend copies it at upload, so
    // it is handed over as it is rather than cached.
    view->true_color = state->true_color == nullptr ? nullptr : state->true_color->Row(0);
    view->true_color_stride = state->true_color == nullptr ? 0 : state->shape.width;
    return true;
}

bool FillSurface(const ImportCall& call,
                 GuestProcess& process,
                 std::uint32_t surface,
                 std::uint16_t color,
                 std::uint32_t true_color,
                 std::string* error)
{
    SurfaceState* state = StateOf(process, surface);
    if (state == nullptr || state->pixels == 0)
    {
        return Fail(error, CallName(call) + " has no pixels to fill");
    }
    // The whole plane is about to be written, so a new one need not widen.
    if (graphics::TrueColorSelected() && !EnsureTrueColor(call, process, *state, false, error))
    {
        return false;
    }
    if (state->true_color != nullptr)
    {
        graphics::FillTrueColorRectangle(state->true_color->View(), {0, 0, state->shape.width, state->shape.height},
                                         true_color);
    }
    // The rows are contiguous, so one write fills them and leaves one record
    // in the call log rather than one per row. Filled a pixel at a time in one
    // pass: a game clearing every frame does this hundreds of times a second.
    // Every host is x86, so a pixel's bytes in memory are the guest's order.
    const std::vector<std::uint16_t> pixels(static_cast<std::size_t>(state->pitch) * state->shape.height / 2, color);
    const std::span<const std::uint8_t> bytes(reinterpret_cast<const std::uint8_t*>(pixels.data()),
                                              pixels.size() * sizeof(std::uint16_t));
    if (!com::WriteBytes(call, state->pixels, bytes, error))
    {
        return false;
    }
    ++state->revision;
    return true;
}

std::vector<std::uint32_t> ExistingSurfaces(GuestProcess& process, std::uint32_t direct_draw)
{
    // A surface's identity grows with each one made, so it orders them by
    // age where their addresses would not.
    std::vector<std::pair<std::uint64_t, std::uint32_t>> surfaces;
    for (const std::uint32_t address : process.com().Addresses())
    {
        const GuestComObject* object = process.com().Find(address);
        const SurfaceState* state = object->StateAs<SurfaceState>();
        if (object->kind == kSurfaceObject && object->parent == direct_draw && state != nullptr)
        {
            surfaces.emplace_back(state->identity, address);
        }
    }
    std::sort(surfaces.begin(), surfaces.end(), [](const auto& left, const auto& right) { return left > right; });
    std::vector<std::uint32_t> addresses;
    for (const auto& [identity, address] : surfaces)
    {
        static_cast<void>(identity);
        addresses.push_back(address);
    }
    return addresses;
}

bool DescribeSurface(GuestProcess& process, std::uint32_t surface, dx::DdSurfaceDesc2* description)
{
    const SurfaceState* state = StateOf(process, surface);
    if (state == nullptr)
    {
        return false;
    }
    *description = dx::SurfaceDescription(state->shape, state->pitch);
    return true;
}

const dx::SurfaceShape* SurfaceShapeOf(GuestProcess& process, std::uint32_t surface)
{
    const SurfaceState* state = StateOf(process, surface);
    return state == nullptr ? nullptr : &state->shape;
}

bool LoadTextureSurface(const ImportCall& call,
                        GuestProcess& process,
                        std::uint32_t destination,
                        std::uint32_t source,
                        std::uint32_t* answer,
                        std::string* error)
{
    SurfaceState* target = StateOf(process, destination);
    const SurfaceState* from = StateOf(process, source);
    const GuestComObject* target_object = process.com().Find(destination);
    const GuestComObject* from_object = process.com().Find(source);
    if (target == nullptr || from == nullptr || target_object == nullptr || from_object == nullptr ||
        target_object->parent != from_object->parent || (target->shape.caps & dx::kDdsCapsTexture) == 0 ||
        (from->shape.caps & dx::kDdsCapsTexture) == 0 || target->pixels == 0 || from->pixels == 0)
    {
        *answer = dx::kDdErrInvalidObject;
        return true;
    }
    if (target->shape.width != from->shape.width || target->shape.height != from->shape.height)
    {
        *answer = dx::kD3dErrTextureLoadFailed;
        return true;
    }
    if (target->dc_held || from->dc_held)
    {
        *answer = dx::kDdErrSurfaceBusy;
        return true;
    }
    // In 32-bit colour the target keeps what the source has at 24 bits; the
    // whole plane is about to be written, so a new one need not widen.
    if (graphics::TrueColorSelected() && !EnsureTrueColor(call, process, *target, false, error))
    {
        return false;
    }
    // RGB565 rows, each row's padding cleared, and the plane's rows.
    const std::uint32_t row_bytes = target->shape.width * 2;
    std::vector<std::uint8_t> row(target->pitch, 0);
    for (std::uint32_t y = 0; y < target->shape.height; ++y)
    {
        if (!com::ReadBytes(call, from->pixels + y * from->pitch, std::span<std::uint8_t>(row).first(row_bytes),
                            error) ||
            !com::WriteBytes(call, target->pixels + y * target->pitch, row, error))
        {
            return false;
        }
        if (target->true_color == nullptr)
        {
            continue;
        }
        if (from->true_color != nullptr)
        {
            std::copy_n(from->true_color->Row(y), target->shape.width, target->true_color->Row(y));
        }
        else
        {
            graphics::WidenRgb565Row(row.data(), target->true_color->Row(y), target->shape.width);
        }
    }
    target->has_color_key = from->has_color_key;
    target->color_key = from->color_key;
    ++target->revision;
    *answer = dx::kDdOk;
    return true;
}

std::uint32_t CreateSurfaces(const ImportCall& call,
                             GuestProcess& process,
                             std::uint32_t direct_draw,
                             const dx::SurfacePlan& plan,
                             bool directx6,
                             std::string* error)
{
    const std::uint32_t surface = MakeSurface(call, process, direct_draw, plan.surface, directx6, error);
    if (surface == 0 || !plan.has_back_buffer)
    {
        return surface;
    }
    const std::uint32_t back_buffer = MakeSurface(call, process, direct_draw, plan.back_buffer, directx6, error);
    if (back_buffer == 0)
    {
        process.com().Release(process, surface);
        return 0;
    }
    // The primary holds the back buffer's first reference.
    StateOf(process, surface)->back_buffer = back_buffer;
    return surface;
}

}  // namespace re2dj::hle::modules::ddraw
