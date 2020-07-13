/*
 * Copyright © 2019 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 or 3,
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
 * Authored By: William Wold <william.wold@canonical.com>
 */

#ifndef MIR_WAYLAND_OBJECT_H_
#define MIR_WAYLAND_OBJECT_H_

#include <boost/throw_exception.hpp>

#include <memory>
#include <stdexcept>

struct wl_resource;
struct wl_global;
struct wl_client;

namespace mir
{
namespace wayland
{
/// The base class of any object that wants to provide a destroyed flag
/// The destroyed flag is only created when needed and automatically set to true on destruction
/// This pattern is only safe in a single-threaded context
class LifetimeTracker
{
public:
    LifetimeTracker() = default;
    LifetimeTracker(LifetimeTracker const&) = delete;
    LifetimeTracker& operator=(LifetimeTracker const&) = delete;

    virtual ~LifetimeTracker();
    auto destroyed_flag() const -> std::shared_ptr<bool>;
    void mark_destroued() const;

private:
    std::shared_ptr<bool> mutable destroyed{nullptr};
};

class Resource
    : public virtual LifetimeTracker
{
public:
    template<int V>
    struct Version
    {
    };

    Resource();
};

/// A weak handle to a Wayland resource (or any Destroyable)
/// May only be safely used from the Wayland thread
template<typename T>
class Weak
{
public:
    Weak()
        : resource{nullptr},
          destroyed_flag{nullptr}
    {
    }

    explicit Weak(T* resource)
        : resource{resource},
          destroyed_flag{resource ? resource->destroyed_flag() : nullptr}
    {
    }

    Weak(Weak<T> const&) = default;
    auto operator=(Weak<T> const&) -> Weak<T>& = default;

    auto operator==(Weak<T> const& other) const -> bool
    {
        return resource == other->resource;
    }

    operator bool() const
    {
        return resource && !*destroyed_flag;
    }

    auto value() const -> T&
    {
        if (!*this)
        {
            BOOST_THROW_EXCEPTION(std::logic_error("Attempted access of destroyed Wayland resource"));
        }
        return *resource;
    }

private:
    T* resource;
    /// Is null if and only if resource is null
    /// If the target bool is true then resrouce has been freed and should not be used
    std::shared_ptr<bool> destroyed_flag;
};

template<typename T>
auto make_weak(T* resource) -> Weak<T>
{
    return Weak<T>{resource};
}

class Global
{
public:
    template<int V>
    struct Version
    {
    };

    explicit Global(wl_global* global);
    virtual ~Global();

    Global(Global const&) = delete;
    Global& operator=(Global const&) = delete;

    virtual auto interface_name() const -> char const* = 0;

    wl_global* const global;
};

void internal_error_processing_request(wl_client* client, char const* method_name);

}
}

#endif // MIR_WAYLAND_OBJECT_H_
