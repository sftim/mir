/*
 * Copyright © 2012 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 2 or 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Alexandros Frantzis <alexandros.frantzis@canonical.com>
 */

#include "platform.h"
#include "buffer_allocator.h"
#include "display.h"
#include "mir/console_services.h"
#include "mir/graphics/platform_authentication.h"
#include "mir/graphics/native_buffer.h"
#include "mir/graphics/platform_authentication.h"
#include "mir/emergency_cleanup_registry.h"
#include "mir/udev/wrapper.h"
#include "mesa_extensions.h"
#include "mir/renderer/gl/texture_target.h"
#include "mir/graphics/buffer_basic.h"
#include "mir/graphics/egl_error.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include MIR_SERVER_GL_H
#include MIR_SERVER_GLEXT_H

#define MIR_LOG_COMPONENT "platform-graphics-gbm-kms"
#include "mir/log.h"

#include <boost/throw_exception.hpp>
#include <stdexcept>
#include <mir/renderer/gl/texture_source.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

namespace mg = mir::graphics;
namespace mgg = mg::gbm;
namespace mgmh = mgg::helpers;

mgg::Platform::Platform(std::shared_ptr<DisplayReport> const& listener,
                        std::shared_ptr<ConsoleServices> const& vt,
                        EmergencyCleanupRegistry&,
                        BypassOption bypass_option)
    : udev{std::make_shared<mir::udev::Context>()},
      drm{helpers::DRMHelper::open_all_devices(udev, *vt)},
      // We assume the first DRM device is the boot GPU, and arbitrarily pick it as our
      // shell renderer.
      //
      // TODO: expose multiple rendering GPUs to the shell.
      gbm{std::make_shared<mgmh::GBMHelper>(drm.front()->fd)},
      listener{listener},
      vt{vt},
      bypass_option_{bypass_option}
{
    auth_factory = std::make_unique<DRMNativePlatformAuthFactory>(*drm.front());
}

mir::UniqueModulePtr<mg::GraphicBufferAllocator> mgg::Platform::create_buffer_allocator(
    mg::Display const& output)
{
    return make_module_ptr<mgg::BufferAllocator>(output, gbm->device, bypass_option_, mgg::BufferImportMethod::gbm_native_pixmap);
}

mir::UniqueModulePtr<mg::Display> mgg::Platform::create_display(
    std::shared_ptr<DisplayConfigurationPolicy> const& initial_conf_policy, std::shared_ptr<GLConfig> const& gl_config)
{
    return make_module_ptr<mgg::Display>(
        drm,
        gbm,
        vt,
        bypass_option_,
        initial_conf_policy,
        gl_config,
        listener);
}

mg::NativeDisplayPlatform* mgg::Platform::native_display_platform()
{
    return auth_factory.get();
}

MirServerEGLNativeDisplayType mgg::Platform::egl_native_display() const
{
    return gbm->device;
}

mgg::BypassOption mgg::Platform::bypass_option() const
{
    return bypass_option_;
}

std::vector<mir::ExtensionDescription> mgg::Platform::extensions() const
{
    return mgg::mesa_extensions();
}
