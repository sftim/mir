/*
 * Copyright © 2012, 2014 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Kevin DuBois <kevin.dubois@canonical.com>
 */

#include "mir/default_server_configuration.h"
#include "mir/graphics/display.h"
#include "mir/graphics/renderable.h"
#include "mir/graphics/display_buffer.h"
#include "mir/graphics/platform.h"
#include "mir/graphics/graphic_buffer_allocator.h"
#include "mir/graphics/buffer_properties.h"
#include "mir/report_exception.h"

#include "testdraw/graphics_region_factory.h"
#include "testdraw/patterns.h"

#include <chrono>
#include <csignal>
#include <iostream>

namespace mg=mir::graphics;
namespace ml=mir::logging;
namespace mo=mir::options;
namespace geom=mir::geometry;

namespace
{
volatile std::sig_atomic_t running = true;

void signal_handler(int /*signum*/)
{
    running = false;
}

class DemoOverlayClient
{
public:
    DemoOverlayClient(
        mg::GraphicBufferAllocator& buffer_allocator,
        mg::BufferProperties const& buffer_properties, uint32_t color)
         : front_buffer(buffer_allocator.alloc_buffer(buffer_properties)),
           back_buffer(buffer_allocator.alloc_buffer(buffer_properties)),
           region_factory(mir::test::draw::create_graphics_region_factory()),
           color{color},
           last_tick{std::chrono::high_resolution_clock::now()}
    {
    }

    void update_green_channel()
    {
        char green_value = (color >> 8) & 0xFF;
        green_value += compute_update_value();
        color &= 0xFFFF00FF;
        color |= (green_value << 8);

        mir::test::draw::DrawPatternSolid fill{color};
        fill.draw(*region_factory->graphic_region_from_handle(*back_buffer->native_buffer_handle()));
        std::swap(front_buffer, back_buffer);
    }

    std::shared_ptr<mg::Buffer> last_rendered()
    {
        return front_buffer;
    }

private:
    int compute_update_value()
    {
        float const update_ratio{3.90625}; //this will give an update of 256 in 1s  
        auto current_tick = std::chrono::high_resolution_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            current_tick - last_tick).count();
        float update_value = elapsed_ms / update_ratio;
        last_tick = current_tick;
        return static_cast<int>(update_value);
    }

    std::shared_ptr<mg::Buffer> front_buffer;
    std::shared_ptr<mg::Buffer> back_buffer;
    std::shared_ptr<mir::test::draw::GraphicsRegionFactory> region_factory;
    unsigned int color;
    std::chrono::time_point<std::chrono::high_resolution_clock> last_tick;
};

class DemoRenderable : public mg::Renderable
{
public:
    DemoRenderable(std::shared_ptr<DemoOverlayClient> const& client, geom::Rectangle rect)
        : client(client),
          position(rect)
    {
    }

    std::shared_ptr<mg::Buffer> buffer(void const*) const override
    {
        return client->last_rendered();
    }

    bool alpha_enabled() const
    {
        return false;
    }

    geom::Rectangle screen_position() const
    {
        return position;
    }

    float alpha() const override
    {
        return 1.0f;
    }

    glm::mat4 transformation() const override
    {
        return trans;
    }

    bool shaped() const
    {
        return false;
    }

    bool should_be_rendered_in(geom::Rectangle const& rect) const override
    {
        return rect.overlaps(position);
    }

    int buffers_ready_for_compositor() const override
    {
        return 1;
    }

private:
    std::shared_ptr<DemoOverlayClient> const client;
    geom::Rectangle const position;
    glm::mat4 const trans;
};
}

int main(int argc, char const** argv)
try
{

    /* Set up graceful exit on SIGINT and SIGTERM */
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    mir::DefaultServerConfiguration conf{argc, argv};

    auto platform = conf.the_graphics_platform();
    auto display = platform->create_display(
        conf.the_display_configuration_policy(),
        conf.the_ancillary_buffers_config());
    auto buffer_allocator = platform->create_buffer_allocator(conf.the_buffer_initializer());

     mg::BufferProperties buffer_properties{
        geom::Size{512, 512},
        mir_pixel_format_abgr_8888,
        mg::BufferUsage::hardware
    };

    auto client1 = std::make_shared<DemoOverlayClient>(*buffer_allocator, buffer_properties,0xFF0000FF);
    auto client2 = std::make_shared<DemoOverlayClient>(*buffer_allocator, buffer_properties,0xFFFFFF00);

    std::list<std::shared_ptr<mg::Renderable>> renderlist
    {
        std::make_shared<DemoRenderable>(client1, geom::Rectangle{{0,0} , {512, 512}}),
        std::make_shared<DemoRenderable>(client2, geom::Rectangle{{80,80} , {592,592}})
    };

    while (running)
    {
        display->for_each_display_buffer([&](mg::DisplayBuffer& buffer)
        {
            buffer.make_current();
            client1->update_green_channel();
            client2->update_green_channel();
            auto render_fn = [](mg::Renderable const&) {};
            buffer.render_and_post_update(renderlist, render_fn);
        });
    }
   return 0;
}
catch (...)
{
    mir::report_exception(std::cerr);
    return 1;
}
