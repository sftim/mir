/*
 * Copyright © 2014 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Alexandros Frantzis <alexandros.frantzis@canonical.com>
 */

#ifndef MIR_GRAPHICS_ANCILLARY_BUFFERS_CONFIG_H_
#define MIR_GRAPHICS_ANCILLARY_BUFFERS_CONFIG_H_

namespace mir
{
namespace graphics
{

/**
 * Interface to the configuration of ancillary buffers.
 */
class AncillaryBuffersConfig
{
public:
    virtual ~AncillaryBuffersConfig() = default;

    /**
     * Gets the bits to use for each pixel in the depth buffer.
     */
    virtual int depth_buffer_bits() const = 0;

    /**
     * Gets the bits to use for each pixel in the stencil buffer.
     */
    virtual int stencil_buffer_bits() const = 0;

protected:
    AncillaryBuffersConfig() = default;
    AncillaryBuffersConfig(AncillaryBuffersConfig const&) = delete;
    AncillaryBuffersConfig& operator=(AncillaryBuffersConfig const&) = delete;
};

}
}

#endif /* MIR_GRAPHICS_ANCILLARY_BUFFERS_CONFIG_H_ */
