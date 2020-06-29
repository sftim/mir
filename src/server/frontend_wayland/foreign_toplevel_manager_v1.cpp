/*
 * Copyright © 2019 Canonical Ltd.
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
 * Authored by: William Wold <william.wold@canonical.com>
 */

#include "foreign_toplevel_manager_v1.h"

#include "wayland_utils.h"
#include "mir/frontend/surface_stack.h"
#include "mir/shell/shell.h"
#include "mir/shell/surface_specification.h"
#include "mir/scene/null_observer.h"
#include "mir/scene/null_surface_observer.h"
#include "mir/scene/surface.h"
#include "mir/log.h"

#include <algorithm>
#include <mutex>
#include <map>
#include <boost/throw_exception.hpp>

namespace mf = mir::frontend;
namespace ms = mir::scene;
namespace msh = mir::shell;
namespace geom = mir::geometry;
namespace mw = mir::wayland;

namespace mir
{
namespace frontend
{

/// An instance of the ForeignToplevelManagerV1 global, bound to a specific client
class ForeignToplevelManagerV1
    : public wayland::ForeignToplevelManagerV1
{
public:
    class Observer;

    ForeignToplevelManagerV1(wl_resource* new_resource, ForeignToplevelManagerV1Global& global);
    ~ForeignToplevelManagerV1();

private:
    /// Wayland requests
    ///@{
    void stop() override;
    ///@}

    std::shared_ptr<SurfaceStack> const surface_stack;
    std::shared_ptr<Observer> const observer;
};

/// Used by a client to aquire information about or control a specific toplevel
/// Instances of this class are created and managed by ForeignToplevelManagerV1::ObserverOwner::Observer
class ForeignToplevelHandleV1
    : public wayland::ForeignToplevelHandleV1
{
public:
    class ObserverOwner;
    class Observer;

    /// Sends the required .state event
    void send_state(MirWindowFocusState focused, MirWindowState state);

    /// Sends the .closed event and makes this surface innert
    void has_closed();

private:
    ForeignToplevelHandleV1(
        ForeignToplevelManagerV1 const& manager,
        std::shared_ptr<std::experimental::optional<ForeignToplevelHandleV1*>> weak_self);
    ~ForeignToplevelHandleV1();

    /// Calls the given function if possible, silently fails if not
    void attempt_operation(
        std::function<void(
            std::shared_ptr<shell::Shell> const&,
            std::shared_ptr<scene::Session> const&,
            std::shared_ptr<scene::Surface> const&)>&& operation);

    /// Modifies the surface if possible, silently fails if not
    void attempt_modify_surface(shell::SurfaceSpecification const& spec);

    /// Wayland requests
    ///@{
    void set_maximized();
    void unset_maximized();
    void set_minimized();
    void unset_minimized();
    void activate(struct wl_resource* seat);
    void close();
    void set_rectangle(struct wl_resource* surface, int32_t x, int32_t y, int32_t width, int32_t height);
    void destroy();
    void set_fullscreen(std::experimental::optional<struct wl_resource*> const& output);
    void unset_fullscreen();
    ///@}

    /// Allows weak pointers that are cleared when the Wayland object is destroyed
    /// Pointed to optional needs to be explicitly set to nullopt in the destructor
    std::shared_ptr<std::experimental::optional<ForeignToplevelHandleV1*>> const weak_self;

    /// This is to keep the object around as long as any Wayland objects depend on it
    /// When the observer owner is destroyed is disconnects the scene observer and destroyes all surface observers
    std::shared_ptr<ForeignToplevelManagerV1::ObserverOwner> const manager_observer_owner;

    std::weak_ptr<shell::Shell> const shell;
    std::weak_ptr<scene::Surface> const surface;

    /// Used to choose the state to unminimize/unfullscreen to
    ///@{
    MirWindowState cached_normal_state{mir_window_state_restored}; ///< always either restored or a maximized state
    bool cached_fullscreen{false};
    ///@}
};

class ForeignToplevelManagerV1::Observer
    : public ms::NullObserver
{
public:
    Observer(
        WlSeat& seat,
        std::shared_ptr<std::experimental::optional<ForeignToplevelManagerV1*>> wayland_toplevel_manager);
    ~Observer();
    Observer(Observer const&) = delete;
    Observer& operator=(Observer const&) = delete;

private:
    /// Shell observer
    ///@{
    void surface_added(std::shared_ptr<scene::Surface> const& surface) override;
    void surface_removed(std::shared_ptr<scene::Surface> const& surface) override;
    void surface_exists(std::shared_ptr<scene::Surface> const& surface) override;
    void end_observation() override;
    ///@}

    WlSeat& seat; ///< Used to spawn functions on the Wayland thread
    std::map<scene::Surface*, std::unique_ptr<ForeignToplevelHandleV1::ObserverOwner>> surface_observers;
    /// Can only be safely accessed on the Wayland thread
    std::shared_ptr<std::experimental::optional<ForeignToplevelManagerV1*>> wayland_toplevel_manager;
    std::mutex mutex;
};

/// Holds a surface observer
class ForeignToplevelHandleV1::ObserverOwner
{
public:
    ObserverOwner(
        std::shared_ptr<Executor> const& wayland_executor,
        std::shared_ptr<std::experimental::optional<ForeignToplevelManagerV1*>> wayland_toplevel_manager,
        scene::Surface* surface);
    ~ObserverOwner();
    ObserverOwner(ObserverOwner const&) = delete;
    ObserverOwner& operator=(ObserverOwner const&) = delete;

private:
    scene::Surface* const surface; ///< Used to add and remove the observer
    std::shared_ptr<Observer> const observer;
};

class ForeignToplevelHandleV1::Observer
    : public scene::NullSurfaceObserver
{
public:
    Observer(
        std::shared_ptr<Executor> const& wayland_executor,
        std::shared_ptr<std::experimental::optional<ForeignToplevelManagerV1*>> wayland_toplevel_manager,
        std::shared_ptr<scene::Surface> const& surface);
    ~Observer();
    Observer(Observer const&) = delete;
    Observer& operator=(Observer const&) = delete;

    /// Sets the surface to nullopt and closes the toplevel handle
    void invalidate_surface();

private:
    /// Expects calling function to manage mutex locking
    /// func is called on the Wayland thread
    /// if we don't currently have a toplevel, action is not called and no error is raised
    /// if the Wayland object is destroyed, action is not called and no error is raised
    void with_toplevel_handle(std::function<void(ForeignToplevelHandleV1*)>&& action);
    void create_toplevel_handle(); ///< Expects calling function to manage mutex locking
    void close_toplevel_handle(); ///< Expects calling function to manage mutex locking
    void create_or_close_toplevel_handle_as_needed(); ///< Expects calling function to manage mutex locking

    /// Surface observer
    ///@{
    void attrib_changed(scene::Surface const*, MirWindowAttrib attrib, int value) override;
    void renamed(scene::Surface const*, char const* name) override;
    void application_id_set_to(scene::Surface const*, std::string const& application_id) override;
    ///@}

    WlSeat& seat; ///< Used to spawn functions on the Wayland thread
    /// Reset in invalidate_surface() when the surface is removed from the shell
    std::weak_ptr<scene::Surface> weak_surface;
    /// Can only be safely accessed on the Wayland thread
    std::shared_ptr<std::experimental::optional<ForeignToplevelManagerV1*>> const wayland_toplevel_manager;
    /// Inner optional can only be safely accessed on the Wayland thread
    /// nullopt means there is no wayland toplevel handle (perhaps this surface is a popup or something)
    /// A pointer to nullopt means we created a wayland object, but it has been deleted
    std::experimental::optional<
        std::shared_ptr<std::experimental::optional<ForeignToplevelHandleV1*>>> wayland_toplevel_handle;
    std::mutex mutex;
};
}
}

// ForeignToplevelManagerV1Global

mf::ForeignToplevelManagerV1Global::ForeignToplevelManagerV1Global(
    wl_display* display,
    std::shared_ptr<shell::Shell> shell,
    WlSeat& seat,
    OutputManager* output_manager,
    std::shared_ptr<SurfaceStack> const& surface_stack)
    : Global{display, Version<2>()},
      shell{shell},
      seat{seat},
      output_manager{output_manager},
      surface_stack{surface_stack}
{
}

void mf::ForeignToplevelManagerV1Global::bind(wl_resource* new_resource)
{
    new ForeignToplevelManagerV1{new_resource, *this};
}

// ForeignToplevelManagerV1

mf::ForeignToplevelManagerV1::ForeignToplevelManagerV1(
    wl_resource* new_resource,
    ForeignToplevelManagerV1Global& global)
    : mw::ForeignToplevelManagerV1{new_resource, Version<2>()},
      weak_self{std::make_shared<std::experimental::optional<ForeignToplevelManagerV1*>>(this)},
      surface_stack{global.surface_stack},
      observer{std::make_shared<Observer>(global.seat, weak_self)}
{
}

mf::ForeignToplevelManagerV1::~ForeignToplevelManagerV1()
{
    *weak_self = std::experimental::nullopt;
}

auto mf::ForeignToplevelManagerV1::observer_owner() const -> std::shared_ptr<ObserverOwner>
{
    return observer;
}

void mf::ForeignToplevelManagerV1::stop()
{
    send_finished_event();
    destroy_wayland_object();
}

// ForeignToplevelManagerV1::ObserverOwner

mf::ForeignToplevelManagerV1::ObserverOwner::ObserverOwner(
    std::shared_ptr<SurfaceStack> const& surface_stack,
    WlSeat& seat,
    std::shared_ptr<std::experimental::optional<ForeignToplevelManagerV1*>> const& wayland_toplevel_manager)
    : surface_stack{surface_stack},
      observer{std::make_shared<Observer>(seat, wayland_toplevel_manager)}
{
    surface_stack->add_observer(observer);
}

mf::ForeignToplevelManagerV1::ObserverOwner::~ObserverOwner()
{
    surface_stack->remove_observer(observer);
}

// ForeignToplevelManagerV1::Observer

mf::ForeignToplevelManagerV1::Observer::Observer(
    WlSeat& seat,
    std::shared_ptr<std::experimental::optional<ForeignToplevelManagerV1*>> wayland_toplevel_manager)
    : seat{seat},
      wayland_toplevel_manager{wayland_toplevel_manager}
{
}

mf::ForeignToplevelManagerV1::Observer::~Observer()
{
}

void mf::ForeignToplevelManagerV1::Observer::surface_added(std::shared_ptr<scene::Surface> const& surface)
{
    std::lock_guard<std::mutex> lock{mutex};
    auto observer = std::make_unique<ForeignToplevelHandleV1::ObserverOwner>(seat, wayland_toplevel_manager, surface);
    surface_observers[surface.get()] = move(observer);
}

void mf::ForeignToplevelManagerV1::Observer::surface_removed(std::shared_ptr<scene::Surface> const& surface)
{
    std::lock_guard<std::mutex> lock{mutex};
    surface_observers.erase(surface.get());
}

void mf::ForeignToplevelManagerV1::Observer::surface_exists(std::shared_ptr<scene::Surface> const& surface)
{
    std::lock_guard<std::mutex> lock{mutex};
    auto observer = std::make_unique<ForeignToplevelHandleV1::ObserverOwner>(seat, wayland_toplevel_manager, surface);
    surface_observers[surface.get()] = move(observer);
}

void mf::ForeignToplevelManagerV1::Observer::end_observation()
{
    std::lock_guard<std::mutex> lock{mutex};
    surface_observers.clear();
}

// ForeignToplevelHandleV1::ObserverOwner

mf::ForeignToplevelHandleV1::ObserverOwner::ObserverOwner(
    WlSeat& seat,
    std::shared_ptr<std::experimental::optional<ForeignToplevelManagerV1*>> const wayland_toplevel_manager,
    scene::Surface* surface)
    : surface{surface},
      observer{std::make_shared<Observer>(seat, wayland_toplevel_manager, surface)}
{
    surface->add_observer(observer);
}

mf::ForeignToplevelHandleV1::ObserverOwner::~ObserverOwner()
{
    observer->invalidate_surface();
    surface->remove_observer(observer);
}

// ForeignToplevelHandleV1::Observer

mf::ForeignToplevelHandleV1::Observer::Observer(
    WlSeat& seat,
    std::shared_ptr<std::experimental::optional<ForeignToplevelManagerV1*>> const wayland_toplevel_manager,
    std::shared_ptr<ms::Surface> const& surface)
    : seat{seat},
      weak_surface{surface},
      wayland_toplevel_manager{wayland_toplevel_manager}
{
    create_or_close_toplevel_handle_as_needed();
}

mf::ForeignToplevelHandleV1::Observer::~Observer()
{
    invalidate_surface();
}

void mf::ForeignToplevelHandleV1::Observer::invalidate_surface()
{
    std::lock_guard<std::mutex> lock{mutex};
    weak_surface.reset();
    create_or_close_toplevel_handle_as_needed();
}

void mf::ForeignToplevelHandleV1::Observer::with_toplevel_handle(
    std::function<void(ForeignToplevelHandleV1*)>&& action)
{
    /// It is documented that action is not called if there is no toplevel handle
    if (!wayland_toplevel_handle)
        return;

    seat.spawn(
        [toplevel_handle = wayland_toplevel_handle.value(), action = std::move(action)]()
        {
            /// It is documented that action is not called if the toplevel handle is destroyed
            if (!*toplevel_handle)
                return;

            action(toplevel_handle->value());
        });
}

void mf::ForeignToplevelHandleV1::Observer::create_toplevel_handle()
{
    if (wayland_toplevel_handle)
        BOOST_THROW_EXCEPTION(std::logic_error("create_toplevel_handle() when toplevel already created"));

    auto const surface = weak_surface.lock();
    if (!surface)
        BOOST_THROW_EXCEPTION(std::logic_error("create_toplevel_handle() called after surface was destroyed"));

    wayland_toplevel_handle =
        std::make_shared<std::experimental::optional<ForeignToplevelHandleV1*>>(std::experimental::nullopt);
    std::string name = surface.value()->name();
    std::string app_id = surface.value()->application_id();
    auto focused = surface.value()->focus_state();
    auto state = surface.value()->state();

    seat.spawn(
        [toplevel_manager = wayland_toplevel_manager,
         toplevel_handle = wayland_toplevel_handle.value(),
         surface_id,
         name,
         app_id,
         focused,
         state]()
        {
            // If the manager has been destroyed we can't create a toplevel handle
            if (!*toplevel_manager)
                return;

            new ForeignToplevelHandleV1{*toplevel_manager->value(), surface, toplevel_handle};
            if (!*toplevel_handle)
                BOOST_THROW_EXCEPTION(std::logic_error("toplevel_handle not set up by constructor"));

            if (!name.empty())
                toplevel_handle->value()->send_title_event(name);
            if (!app_id.empty())
                toplevel_handle->value()->send_app_id_event(app_id);
            toplevel_handle->value()->send_state(focused, state);
            toplevel_handle->value()->send_done_event();
        });
}

void mf::ForeignToplevelHandleV1::Observer::close_toplevel_handle()
{
    if (!wayland_toplevel_handle)
    {
        log_warning("close_toplevel_handle() called when toplevel does not exist");
        return;
    }

    with_toplevel_handle([](ForeignToplevelHandleV1* toplevel_handle)
        {
            toplevel_handle->has_closed();
        });

    wayland_toplevel_handle = std::experimental::nullopt;
}

void mf::ForeignToplevelHandleV1::Observer::create_or_close_toplevel_handle_as_needed()
{
    bool should_have_toplevel = true;

    auto const surface = weak_surface.lock();
    if (surface)
    {
        auto& surface_value = this->surface.value();

        switch(surface_value->state())
        {
        case mir_window_state_attached:
        case mir_window_state_hidden:
            should_have_toplevel = false;
            break;

        default:
            break;
        }

        switch (surface_value->type())
        {
        case mir_window_type_normal:
        case mir_window_type_utility:
        case mir_window_type_freestyle:
            break;

        default:
            should_have_toplevel = false;
            break;
        }

        if (!surface_value->session().lock())
            should_have_toplevel = false;
    }
    else
    {
        should_have_toplevel = false;
    }

    if (should_have_toplevel && !wayland_toplevel_handle)
    {
        create_toplevel_handle();
    }
    else if (!should_have_toplevel && wayland_toplevel_handle)
    {
        close_toplevel_handle();
    }
}

void mf::ForeignToplevelHandleV1::Observer::attrib_changed(
    const scene::Surface*,
    MirWindowAttrib attrib,
    int value)
{
    std::lock_guard<std::mutex> lock{mutex};

    auto toplevel_handel_existed_before = wayland_toplevel_handle.operator bool();
    if (!surface)
        return;
    auto& surface_value = this->surface.value();

    switch (attrib)
    {
    case mir_window_attrib_state:
        create_or_close_toplevel_handle_as_needed();
        if (toplevel_handel_existed_before)
        {
            auto focused = surface_value->focus_state();
            auto state = static_cast<MirWindowState>(value);
            aquire_toplevel_handle([focused, state](ForeignToplevelHandleV1* toplevel_handle)
            {
                toplevel_handle->send_state(focused, state);
                toplevel_handle->send_done_event();
            });
        }
        break;

    case mir_window_attrib_focus:
    {
        auto focused = static_cast<MirWindowFocusState>(value);
        auto state = surface_value->state();
        aquire_toplevel_handle([focused, state](ForeignToplevelHandleV1* toplevel_handle)
            {
                toplevel_handle->send_state(focused, state);
                toplevel_handle->send_done_event();
            });
        break;
    }

    case mir_window_attrib_type:
        create_or_close_toplevel_handle_as_needed();
        break;

    default:
        break;
    }
}

void mf::ForeignToplevelHandleV1::Observer::renamed(ms::Surface const*, char const* name_c_str)
{
    std::lock_guard<std::mutex> lock{mutex};

    std::string name = name_c_str;
    aquire_toplevel_handle([name](ForeignToplevelHandleV1* toplevel_handle)
        {
            toplevel_handle->send_title_event(name);
            toplevel_handle->send_done_event();
        });
}

void mf::ForeignToplevelHandleV1::Observer::application_id_set_to(
    scene::Surface const*, std::string const& application_id)
{
    std::lock_guard<std::mutex> lock{mutex};

    std::string id = application_id;
    aquire_toplevel_handle([id](ForeignToplevelHandleV1* toplevel_handle)
        {
            toplevel_handle->send_app_id_event(id);
            toplevel_handle->send_done_event();
        });
}

// ForeignToplevelHandleV1

void mf::ForeignToplevelHandleV1::send_state(MirWindowFocusState focused, MirWindowState state)
{
    switch (state)
    {
    case mir_window_state_restored:
    case mir_window_state_maximized:
    case mir_window_state_horizmaximized:
    case mir_window_state_vertmaximized:
        cached_normal_state = state;
        cached_fullscreen = false;
        break;

    case mir_window_state_fullscreen:
        cached_fullscreen = true;
        break;

    default:
        break;
    }

    wl_array states;
    wl_array_init(&states);

    if (focused == mir_window_focus_state_focused)
    {
        if (uint32_t* state = static_cast<uint32_t*>(wl_array_add(&states, sizeof(uint32_t))))
            *state = State::activated;
    }

    switch (state)
    {
    case mir_window_state_maximized:
    case mir_window_state_horizmaximized:
    case mir_window_state_vertmaximized:
        if (uint32_t *state = static_cast<uint32_t*>(wl_array_add(&states, sizeof(uint32_t))))
            *state = State::maximized;
        break;

    case mir_window_state_fullscreen:
        if (uint32_t *state = static_cast<uint32_t*>(wl_array_add(&states, sizeof(uint32_t))))
            *state = State::fullscreen;
        break;

    case mir_window_state_minimized:
        if (uint32_t *state = static_cast<uint32_t*>(wl_array_add(&states, sizeof(uint32_t))))
            *state = State::minimized;
        break;

    default:
        break;
    }

    send_state_event(&states);
    wl_array_release(&states);
}

void mf::ForeignToplevelHandleV1::has_closed()
{
    send_closed_event();
    *weak_self = std::experimental::nullopt;
    surface.reset();
}

mf::ForeignToplevelHandleV1::ForeignToplevelHandleV1(
    ForeignToplevelManagerV1 const& manager,
    std::weak_ptr<Session> session,
    SurfaceId surface_id,
    std::shared_ptr<std::experimental::optional<ForeignToplevelHandleV1*>> weak_self)
    : mw::ForeignToplevelHandleV1{manager},
      weak_self{weak_self},
      manager_observer_owner{manager.observer_owner()},
      shell{manager_observer_owner->shell},
      session{session},
      surface_id{surface_id}
{
    *weak_self = this;
    manager.send_toplevel_event(resource);
}

mf::ForeignToplevelHandleV1::~ForeignToplevelHandleV1()
{
    *weak_self = std::experimental::nullopt;
}

void mf::ForeignToplevelHandleV1::attempt_operation(
        std::function<void(
            std::shared_ptr<msh::Shell> const&,
            std::shared_ptr<ms::Session> const&,
            std::shared_ptr<ms::Surface> const&)>&& operation)
{
    auto const shell = this->shell.lock();
    auto const surface = this->surface.lock();
    auto const session = surface ? surface->session().lock() : nullptr;
    if (shell && session && surface)
    {
        try
        {
            operation(shell, session, surface);
        }
        catch (std::out_of_range const&)
        {
            log_warning("Attempted operation on invalid surface");
        }
    }
}

void mf::ForeignToplevelHandleV1::attempt_modify_surface(shell::SurfaceSpecification const& spec)
{
    attempt_operation([&spec](auto shell, auto session, auto surface)
        {
            shell->modify_surface(session, surface, spec);
        });
}

void mf::ForeignToplevelHandleV1::set_maximized()
{
    if (cached_fullscreen)
    {
        cached_normal_state = mir_window_state_maximized;
    }
    else
    {
        shell::SurfaceSpecification spec;
        spec.state = mir_window_state_maximized;
        attempt_modify_surface(spec);
    }
}

void mf::ForeignToplevelHandleV1::unset_maximized()
{
    if (cached_fullscreen)
    {
        cached_normal_state = mir_window_state_restored;
    }
    else
    {
        shell::SurfaceSpecification spec;
        spec.state = mir_window_state_restored;
        attempt_modify_surface(spec);
    }
}

void mf::ForeignToplevelHandleV1::set_minimized()
{
    shell::SurfaceSpecification spec;
    spec.state = mir_window_state_minimized;
    attempt_modify_surface(spec);
}

void mf::ForeignToplevelHandleV1::unset_minimized()
{
    shell::SurfaceSpecification spec;
    spec.state = cached_normal_state;
    attempt_modify_surface(spec);
    activate(nullptr);
}

void mf::ForeignToplevelHandleV1::activate(struct wl_resource* /*seat*/)
{
    auto timestamp = std::numeric_limits<uint64_t>::max(); // TODO: make this correct
    attempt_operation([timestamp](auto shell, auto session, auto surface)
        {
            shell->raise_surface(session, surface, timestamp);
        });
}

void mf::ForeignToplevelHandleV1::close()
{
    attempt_operation([](auto, auto, auto surface)
        {
            surface->request_client_surface_close();
        });
}

void mf::ForeignToplevelHandleV1::set_rectangle(
    struct wl_resource* /*surface*/,
    int32_t /*x*/,
    int32_t /*y*/,
    int32_t /*width*/,
    int32_t /*height*/)
{
    // This would be used for the destination of a window minimization animation
    // Nothing must be done with this info. It is not a protocol violation to ignore it
}

void mf::ForeignToplevelHandleV1::destroy()
{
    destroy_wayland_object();
}

void mf::ForeignToplevelHandleV1::set_fullscreen(std::experimental::optional<struct wl_resource*> const& /*output*/)
{
    shell::SurfaceSpecification spec;
    spec.state = mir_window_state_fullscreen;
    attempt_modify_surface(spec);
}

void mf::ForeignToplevelHandleV1::unset_fullscreen()
{
    shell::SurfaceSpecification spec;
    spec.state = cached_normal_state;
    attempt_modify_surface(spec);
}
