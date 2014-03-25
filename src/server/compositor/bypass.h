/*
 * Copyright © 2013 Canonical Ltd.
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
 * Authored by: Daniel van Vugt <daniel.van.vugt@canonical.com>
 */

#ifndef MIR_COMPOSITOR_BYPASS_H_
#define MIR_COMPOSITOR_BYPASS_H_

#include "mir/graphics/renderable.h"
#include "mir/compositor/scene.h"

namespace mir
{
namespace graphics
{
class DisplayBuffer;
}
namespace compositor
{

class BypassMatcher
{
public:
    BypassMatcher(geometry::Rectangle const& rect);
private:
    geometry::Rectangle const view_area;
};

graphics::RenderableList::iterator find_bypass_buffer_from(
    graphics::RenderableList const& list,
    geometry::Rectangle const& view_area);

class BypassFilter : public FilterForScene
{
public:
    BypassFilter(const graphics::DisplayBuffer &display_buffer);
    bool operator()(const graphics::Renderable &) override;
    bool fullscreen_on_top() const;

private:
    bool all_orthogonal = true;
    bool topmost_fits = false;
    const graphics::DisplayBuffer &display_buffer;
};

class BypassMatch : public OperatorForScene
{
public:
    void operator()(const graphics::Renderable &) override;
    const graphics::Renderable *topmost_fullscreen() const;

private:
    // This has to be a pointer. We have no control over Renderable lifetime
    const graphics::Renderable *latest = nullptr;
};

} // namespace compositor
} // namespace mir

#endif // MIR_COMPOSITOR_BYPASS_H_
