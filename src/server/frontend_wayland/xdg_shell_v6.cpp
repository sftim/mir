/*
 * Copyright © 2018-2019 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Christopher James Halse Rogers <christopher.halse.rogers@canonical.com>
 */

#include "xdg_shell_v6.h"

#include "wl_surface.h"
#include "window_wl_surface_role.h"
#include "wayland_utils.h"

#include "mir/frontend/mir_client_session.h"
#include "mir/frontend/wayland.h"
#include "mir/shell/surface_specification.h"
#include "mir/log.h"

namespace mf = mir::frontend;
namespace ms = mir::scene;
namespace msh = mir::shell;
namespace geom = mir::geometry;
namespace mw = mir::wayland;

namespace mir
{
namespace frontend
{

class XdgSurfaceV6 : wayland::XdgSurfaceV6
{
public:
    static XdgSurfaceV6* from(wl_resource* surface);

    XdgSurfaceV6(wl_resource* new_resource, WlSurface* surface, XdgShellV6 const& xdg_shell);
    ~XdgSurfaceV6() = default;

    void destroy() override;
    void get_toplevel(wl_resource* new_toplevel) override;
    void get_popup(wl_resource* new_popup, wl_resource* parent_surface, wl_resource* positioner) override;
    void set_window_geometry(int32_t x, int32_t y, int32_t width, int32_t height) override;
    void ack_configure(uint32_t serial) override;

    void send_configure();

    wayland::Weak<WindowWlSurfaceRole> const& window_role();

    using wayland::XdgSurfaceV6::client;
    using wayland::XdgSurfaceV6::resource;

private:
    void set_window_role(WindowWlSurfaceRole* role);

    wayland::Weak<WindowWlSurfaceRole> window_role_;
    WlSurface* const surface;

public:
    XdgShellV6 const& xdg_shell;
};

class XdgPopupV6 : wayland::XdgPopupV6, public WindowWlSurfaceRole
{
public:
    XdgPopupV6(
        wl_resource* new_resource,
        XdgSurfaceV6* xdg_surface,
        XdgSurfaceV6* parent_surface,
        wl_resource* positioner,
        WlSurface* surface);

    void grab(struct wl_resource* seat, uint32_t serial) override;
    void destroy() override;

    void handle_commit() override {};
    void handle_state_change(MirWindowState /*new_state*/) override {};
    void handle_active_change(bool /*is_now_active*/) override {};
    void handle_resize(
        std::experimental::optional<geometry::Point> const& new_top_left,
        geometry::Size const& new_size) override;
    void handle_close_request() override;

private:
    std::experimental::optional<geom::Point> cached_top_left;
    std::experimental::optional<geom::Size> cached_size;

    XdgSurfaceV6* const xdg_surface;
};

class XdgToplevelV6 : wayland::XdgToplevelV6, public WindowWlSurfaceRole
{
public:
    XdgToplevelV6(wl_resource* new_resource, XdgSurfaceV6* xdg_surface, WlSurface* surface);

    void destroy() override;
    void set_parent(std::experimental::optional<struct wl_resource*> const& parent) override;
    void set_title(std::string const& title) override;
    void set_app_id(std::string const& app_id) override;
    void show_window_menu(struct wl_resource* seat, uint32_t serial, int32_t x, int32_t y) override;
    void move(struct wl_resource* seat, uint32_t serial) override;
    void resize(struct wl_resource* seat, uint32_t serial, uint32_t edges) override;
    void set_max_size(int32_t width, int32_t height) override;
    void set_min_size(int32_t width, int32_t height) override;
    void set_maximized() override;
    void unset_maximized() override;
    void set_fullscreen(std::experimental::optional<struct wl_resource*> const& output) override;
    void unset_fullscreen() override;
    void set_minimized() override;

    void handle_commit() override {};
    void handle_state_change(MirWindowState /*new_state*/) override;
    void handle_active_change(bool /*is_now_active*/) override;
    void handle_resize(std::experimental::optional<geometry::Point> const& new_top_left,
                       geometry::Size const& new_size) override;
    void handle_close_request() override;

private:
    static XdgToplevelV6* from(wl_resource* surface);
    void send_toplevel_configure();

    XdgSurfaceV6* const xdg_surface;
};

class XdgPositionerV6 : public wayland::XdgPositionerV6, public shell::SurfaceSpecification
{
public:
    XdgPositionerV6(wl_resource* new_resource);

private:
    void destroy() override;
    void set_size(int32_t width, int32_t height) override;
    void set_anchor_rect(int32_t x, int32_t y, int32_t width, int32_t height) override;
    void set_anchor(uint32_t anchor) override;
    void set_gravity(uint32_t gravity) override;
    void set_constraint_adjustment(uint32_t constraint_adjustment) override;
    void set_offset(int32_t x, int32_t y) override;
};

}
}
namespace mf = mir::frontend;  // Keep CLion's parsing happy

class mf::XdgShellV6::Instance : public wayland::XdgShellV6
{
public:
    Instance(wl_resource* new_resource, mf::XdgShellV6* shell);

private:
    void destroy() override;
    void create_positioner(wl_resource* new_positioner) override;
    void get_xdg_surface(wl_resource* new_xdg_surface, wl_resource* surface) override;
    void pong(uint32_t serial) override;

    mf::XdgShellV6* const shell;
};

mf::XdgShellV6::Instance::Instance(wl_resource* new_resource, mf::XdgShellV6* shell)
    : mw::XdgShellV6{new_resource, Version<1>()},
      shell{shell}
{
}

void mf::XdgShellV6::Instance::destroy()
{
    destroy_wayland_object();
}

void mf::XdgShellV6::Instance::create_positioner(wl_resource* new_positioner)
{
    new XdgPositionerV6{new_positioner};
}

void mf::XdgShellV6::Instance::get_xdg_surface(wl_resource* new_xdg_surface, wl_resource* surface)
{
    new XdgSurfaceV6{new_xdg_surface, WlSurface::from(surface), *shell};
}

void mf::XdgShellV6::Instance::pong(uint32_t serial)
{
    (void)serial;
    // TODO
}

// XdgShellV6

mf::XdgShellV6::XdgShellV6(
    struct wl_display* display,
    std::shared_ptr<msh::Shell> shell,
    WlSeat& seat,
    OutputManager* output_manager) :
    Global(display, Version<1>()),
    shell{shell},
    seat{seat},
    output_manager{output_manager}
{
}

void mf::XdgShellV6::bind(wl_resource* new_resource)
{
    new Instance{new_resource, this};
}

// XdgSurfaceV6

mf::XdgSurfaceV6* mf::XdgSurfaceV6::from(wl_resource* surface)
{
    auto* tmp = wl_resource_get_user_data(surface);
    return static_cast<XdgSurfaceV6*>(static_cast<wayland::XdgSurfaceV6*>(tmp));
}

mf::XdgSurfaceV6::XdgSurfaceV6(wl_resource* new_resource, WlSurface* surface,
                               XdgShellV6 const& xdg_shell)
    : mw::XdgSurfaceV6(new_resource, Version<1>()),
      surface{surface},
      xdg_shell{xdg_shell}
{
}

void mf::XdgSurfaceV6::destroy()
{
    wl_resource_destroy(resource);
}

void mf::XdgSurfaceV6::get_toplevel(wl_resource* new_toplevel)
{
    auto toplevel = new XdgToplevelV6{new_toplevel, this, surface};
    set_window_role(toplevel);
}

void mf::XdgSurfaceV6::get_popup(wl_resource* new_popup, struct wl_resource* parent_surface, struct wl_resource* positioner)
{
    auto popup = new XdgPopupV6{new_popup, this, XdgSurfaceV6::from(parent_surface), positioner, surface};
    set_window_role(popup);
}

void mf::XdgSurfaceV6::set_window_geometry(int32_t x, int32_t y, int32_t width, int32_t height)
{
    if (window_role_)
    {
        window_role_.value().set_pending_offset(geom::Displacement{-x, -y});
        window_role_.value().set_pending_width(geom::Width{width});
        window_role_.value().set_pending_height(geom::Height{height});
    }
}

void mf::XdgSurfaceV6::ack_configure(uint32_t serial)
{
    (void)serial;
    // TODO
}

void mf::XdgSurfaceV6::send_configure()
{
    auto const serial = wl_display_next_serial(wl_client_get_display(wayland::XdgSurfaceV6::client));
    send_configure_event(serial);
}

mw::Weak<mf::WindowWlSurfaceRole> const& mf::XdgSurfaceV6::window_role()
{
    return window_role_;
}

void mf::XdgSurfaceV6::set_window_role(WindowWlSurfaceRole* role)
{
    if (window_role_)
    {
        log_warning("XdgSurfaceV6::window_role set multiple times");
    }

    window_role_ = mw::make_weak(role);
}

// XdgPopupV6

mf::XdgPopupV6::XdgPopupV6(
    wl_resource* new_resource,
    XdgSurfaceV6* xdg_surface,
    XdgSurfaceV6* parent_surface,
    wl_resource* positioner,
    WlSurface* surface)
    : mw::XdgPopupV6(new_resource, Version<1>()),
      WindowWlSurfaceRole(
          &xdg_surface->xdg_shell.seat,
          wayland::XdgPopupV6::client,
          surface,
          xdg_surface->xdg_shell.shell,
          xdg_surface->xdg_shell.output_manager),
      xdg_surface{xdg_surface}
{
    auto specification = static_cast<mir::shell::SurfaceSpecification*>(
                                static_cast<XdgPositionerV6*>(
                                    static_cast<wayland::XdgPositionerV6*>(
                                        wl_resource_get_user_data(positioner))));

    auto const& parent_role = parent_surface->window_role();
    auto parent_scene_surface = parent_role ? parent_role.value().scene_surface() : std::experimental::nullopt;

    specification->type = mir_window_type_freestyle;
    specification->placement_hints = mir_placement_hints_slide_any;
    if (parent_scene_surface)
        specification->parent = parent_scene_surface.value();
    else
        log_warning("mf::XdgSurfaceV6::get_popup() sent parent that is not yet mapped");

    apply_spec(*specification);
}

void mf::XdgPopupV6::grab(struct wl_resource* seat, uint32_t serial)
{
    (void)seat, (void)serial;
    // TODO
}

void mf::XdgPopupV6::destroy()
{
    wl_resource_destroy(resource);
}

void mf::XdgPopupV6::handle_resize(const std::experimental::optional<geometry::Point>& new_top_left,
                                   const geometry::Size& new_size)
{
    bool const needs_configure = (new_top_left != cached_top_left) || (new_size != cached_size);

    if (new_top_left)
        cached_top_left = new_top_left;

    cached_size = new_size;

    if (needs_configure && cached_top_left && cached_size)
    {
        send_configure_event(cached_top_left.value().x.as_int(),
                             cached_top_left.value().y.as_int(),
                             cached_size.value().width.as_int(),
                             cached_size.value().height.as_int());
        xdg_surface->send_configure();
    }
}

void mf::XdgPopupV6::handle_close_request()
{
    send_popup_done_event();
}

// XdgToplevelV6

mf::XdgToplevelV6::XdgToplevelV6(struct wl_resource* new_resource, XdgSurfaceV6* xdg_surface, WlSurface* surface)
    : mw::XdgToplevelV6(new_resource, Version<1>()),
      WindowWlSurfaceRole(
          &xdg_surface->xdg_shell.seat,
          wayland::XdgToplevelV6::client,
          surface,
          xdg_surface->xdg_shell.shell,
          xdg_surface->xdg_shell.output_manager),
      xdg_surface{xdg_surface}
{
    wl_array states;
    wl_array_init(&states);
    send_configure_event(0, 0, &states);
    wl_array_release(&states);
    xdg_surface->send_configure();
}

void mf::XdgToplevelV6::destroy()
{
    wl_resource_destroy(resource);
}

void mf::XdgToplevelV6::set_parent(std::experimental::optional<struct wl_resource*> const& parent)
{
    if (parent && parent.value())
    {
        WindowWlSurfaceRole::set_parent(XdgToplevelV6::from(parent.value())->scene_surface());
    }
    else
    {
        WindowWlSurfaceRole::set_parent({});
    }
}

void mf::XdgToplevelV6::set_title(std::string const& title)
{
    WindowWlSurfaceRole::set_title(title);
}

void mf::XdgToplevelV6::set_app_id(std::string const& app_id)
{
    WindowWlSurfaceRole::set_application_id(app_id);
}

void mf::XdgToplevelV6::show_window_menu(struct wl_resource* seat, uint32_t serial, int32_t x, int32_t y)
{
    (void)seat, (void)serial, (void)x, (void)y;
    // TODO
}

void mf::XdgToplevelV6::move(struct wl_resource* /*seat*/, uint32_t /*serial*/)
{
    initiate_interactive_move();
}

void mf::XdgToplevelV6::resize(struct wl_resource* /*seat*/, uint32_t /*serial*/, uint32_t edges)
{
    MirResizeEdge edge = mir_resize_edge_none;

    switch (edges)
    {
    case ResizeEdge::top:
        edge = mir_resize_edge_north;
        break;

    case ResizeEdge::bottom:
        edge = mir_resize_edge_south;
        break;

    case ResizeEdge::left:
        edge = mir_resize_edge_west;
        break;

    case ResizeEdge::right:
        edge = mir_resize_edge_east;
        break;

    case ResizeEdge::top_left:
        edge = mir_resize_edge_northwest;
        break;

    case ResizeEdge::bottom_left:
        edge = mir_resize_edge_southwest;
        break;

    case ResizeEdge::top_right:
        edge = mir_resize_edge_northeast;
        break;

    case ResizeEdge::bottom_right:
        edge = mir_resize_edge_southeast;
        break;

    default:;
    }

    initiate_interactive_resize(edge);
}

void mf::XdgToplevelV6::set_max_size(int32_t width, int32_t height)
{
    WindowWlSurfaceRole::set_max_size(width, height);
}

void mf::XdgToplevelV6::set_min_size(int32_t width, int32_t height)
{
    WindowWlSurfaceRole::set_min_size(width, height);
}

void mf::XdgToplevelV6::set_maximized()
{
    // We must process this request immediately (i.e. don't defer until commit())
    set_state_now(mir_window_state_maximized);
}

void mf::XdgToplevelV6::unset_maximized()
{
    // We must process this request immediately (i.e. don't defer until commit())
    set_state_now(mir_window_state_restored);
}

void mf::XdgToplevelV6::set_fullscreen(std::experimental::optional<struct wl_resource*> const& output)
{
    WindowWlSurfaceRole::set_fullscreen(output);
}

void mf::XdgToplevelV6::unset_fullscreen()
{
    // We must process this request immediately (i.e. don't defer until commit())
    // TODO: should we instead restore the previous state?
    set_state_now(mir_window_state_restored);
}

void mf::XdgToplevelV6::set_minimized()
{
    // We must process this request immediately (i.e. don't defer until commit())
    set_state_now(mir_window_state_minimized);
}

void mf::XdgToplevelV6::handle_state_change(MirWindowState /*new_state*/)
{
    send_toplevel_configure();
}

void mf::XdgToplevelV6::handle_active_change(bool /*is_now_active*/)
{
    send_toplevel_configure();
}

void mf::XdgToplevelV6::handle_resize(std::experimental::optional<geometry::Point> const& /*new_top_left*/,
                       geometry::Size const& /*new_size*/)
{
    send_toplevel_configure();
}

void mf::XdgToplevelV6::handle_close_request()
{
    send_close_event();
}

void mf::XdgToplevelV6::send_toplevel_configure()
{
    wl_array states;
    wl_array_init(&states);

    if (is_active())
    {
        if (uint32_t *state = static_cast<decltype(state)>(wl_array_add(&states, sizeof *state)))
            *state = State::activated;
    }

    switch (window_state())
    {
    case mir_window_state_maximized:
    case mir_window_state_horizmaximized:
    case mir_window_state_vertmaximized:
        if (uint32_t *state = static_cast<decltype(state)>(wl_array_add(&states, sizeof *state)))
            *state = State::maximized;
        break;

    case mir_window_state_fullscreen:
        if (uint32_t *state = static_cast<decltype(state)>(wl_array_add(&states, sizeof *state)))
            *state = State::fullscreen;
        break;

    default:
        break;
    }

    // 0 sizes means default for toplevel configure
    geom::Size size = requested_window_size().value_or(geom::Size{0, 0});

    send_configure_event(size.width.as_int(), size.height.as_int(), &states);
    wl_array_release(&states);

    xdg_surface->send_configure();
}

mf::XdgToplevelV6* mf::XdgToplevelV6::from(wl_resource* surface)
{
    auto* tmp = wl_resource_get_user_data(surface);
    return static_cast<XdgToplevelV6*>(static_cast<wayland::XdgToplevelV6*>(tmp));
}

// XdgPositionerV6

mf::XdgPositionerV6::XdgPositionerV6(wl_resource* new_resource)
    : mw::XdgPositionerV6(new_resource, Version<1>())
{
    // specifying gravity is not required by the xdg shell protocol, but is by Mir window managers
    surface_placement_gravity = mir_placement_gravity_center;
    aux_rect_placement_gravity = mir_placement_gravity_center;
}

void mf::XdgPositionerV6::destroy()
{
    wl_resource_destroy(resource);
}

void mf::XdgPositionerV6::set_size(int32_t width, int32_t height)
{
    this->width = geom::Width{width};
    this->height = geom::Height{height};
}

void mf::XdgPositionerV6::set_anchor_rect(int32_t x, int32_t y, int32_t width, int32_t height)
{
    aux_rect = geom::Rectangle{{x, y}, {width, height}};
}

void mf::XdgPositionerV6::set_anchor(uint32_t anchor)
{
    MirPlacementGravity placement = mir_placement_gravity_center;

    if (anchor & Anchor::top)
        placement = MirPlacementGravity(placement | mir_placement_gravity_north);

    if (anchor & Anchor::bottom)
        placement = MirPlacementGravity(placement | mir_placement_gravity_south);

    if (anchor & Anchor::left)
        placement = MirPlacementGravity(placement | mir_placement_gravity_west);

    if (anchor & Anchor::right)
        placement = MirPlacementGravity(placement | mir_placement_gravity_east);

    aux_rect_placement_gravity = placement;
}

void mf::XdgPositionerV6::set_gravity(uint32_t gravity)
{
    MirPlacementGravity placement = mir_placement_gravity_center;

    if (gravity & Gravity::top)
        placement = MirPlacementGravity(placement | mir_placement_gravity_south);

    if (gravity & Gravity::bottom)
        placement = MirPlacementGravity(placement | mir_placement_gravity_north);

    if (gravity & Gravity::left)
        placement = MirPlacementGravity(placement | mir_placement_gravity_east);

    if (gravity & Gravity::right)
        placement = MirPlacementGravity(placement | mir_placement_gravity_west);

    surface_placement_gravity = placement;
}

void mf::XdgPositionerV6::set_constraint_adjustment(uint32_t constraint_adjustment)
{
    (void)constraint_adjustment;
    // TODO
}

void mf::XdgPositionerV6::set_offset(int32_t x, int32_t y)
{
    aux_rect_placement_offset_x = x;
    aux_rect_placement_offset_y = y;
}

auto mf::XdgShellV6::get_window(wl_resource* surface) -> std::shared_ptr<scene::Surface>
{
    namespace mw = mir::wayland;

    if (mw::XdgSurfaceV6::is_instance(surface))
    {
        auto const v6surface = XdgSurfaceV6::from(surface);
        if (auto const& role = v6surface->window_role())
        {
            if (auto const scene_surface = role.value().scene_surface())
            {
                return scene_surface.value();
            }
        }

        log_debug("No window currently associated with wayland::XdgSurfaceV6 %p", surface);
    }

    return {};
}
