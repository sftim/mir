/*
 * Copyright © 2013 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Robert Carr <robert.carr@canonical.com>
 */

#define MIR_INCLUDE_DEPRECATED_EVENT_HEADER

#include "android_input_receiver.h"

#include "mir/dispatch/multiplexing_dispatchable.h"
#include "mir/input/xkb_mapper.h"
#include "mir/input/input_receiver_report.h"
#include "mir/input/android/android_input_lexicon.h"

#include <boost/throw_exception.hpp>

#include <androidfw/InputTransport.h>

#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <system_error>

namespace mircv = mir::input::receiver;
namespace mircva = mircv::android;
namespace md = mir::dispatch;

namespace mia = mir::input::android;

mircva::InputReceiver::InputReceiver(droidinput::sp<droidinput::InputChannel> const& input_channel,
                                     std::function<void(MirEvent*)> const& event_handling_callback,
                                     std::shared_ptr<mircv::InputReceiverReport> const& report,
                                     AndroidClock clock)
  : input_channel(input_channel),
    handler{event_handling_callback},
    report(report),
    input_consumer(std::make_shared<droidinput::InputConsumer>(input_channel)),
    xkb_mapper(std::make_shared<mircv::XKBMapper>()),
    android_clock(clock)
{
    timer_fd = mir::Fd{timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC)};
    if (timer_fd == mir::Fd::invalid)
    {
        BOOST_THROW_EXCEPTION((std::system_error{errno,
                                                 std::system_category(),
                                                 "Failed to create IO timer"}));
    }

    int pipefds[2];
    if (pipe(pipefds) < 0)
    {
        BOOST_THROW_EXCEPTION((std::system_error{errno,
                                                 std::system_category(),
                                                 "Failed to create notification pipe for IO"}));
    }
    notify_receiver_fd = mir::Fd{pipefds[0]};
    notify_sender_fd = mir::Fd{pipefds[1]};

    dispatcher.add_watch(timer_fd, [this]()
    {
        consume_wake_notification(timer_fd);
        process_and_maybe_send_event();
    });

    dispatcher.add_watch(notify_receiver_fd, [this]()
    {
        consume_wake_notification(notify_receiver_fd);
        process_and_maybe_send_event();
    });

    dispatcher.add_watch(mir::Fd{input_channel->getFd()},
                         [this]() { process_and_maybe_send_event(); });
}

mircva::InputReceiver::InputReceiver(int fd,
                                     std::function<void(MirEvent*)> const& event_handling_callback,
                                     std::shared_ptr<mircv::InputReceiverReport> const& report,
                                     AndroidClock clock)
    : InputReceiver(new droidinput::InputChannel(droidinput::String8(""), fd),
                    event_handling_callback,
                    report,
                    clock)
{
}

mircva::InputReceiver::~InputReceiver()
{
}

mir::Fd mircva::InputReceiver::watch_fd() const
{
    return dispatcher.watch_fd();
}

bool mircva::InputReceiver::dispatch(md::FdEvents events)
{
    return dispatcher.dispatch(events);
}

md::FdEvents mircva::InputReceiver::relevant_events() const
{
    return dispatcher.relevant_events();
}

namespace
{

static void map_key_event(std::shared_ptr<mircv::XKBMapper> const& xkb_mapper, MirEvent &ev)
{
    // TODO: As XKBMapper is used to track modifier state we need to use a seperate instance
    // of XKBMapper per device id (or modify XKBMapper semantics)
    if (ev.type != mir_event_type_key)
        return;

    xkb_mapper->update_state_and_map_event(ev.key);
}

}

void mircva::InputReceiver::process_and_maybe_send_event()
{
    MirEvent ev;
    droidinput::InputEvent *android_event;
    uint32_t event_sequence_id;

    /*
     * Enable "Project Butter" input resampling in InputConsumer::consume():
     *   consumeBatches = true, so as to ensure the "cooked" event rate that
     *      clients experience is at least the minimum of event_rate_hz
     *      and the raw device event rate.
     *   frame_time = A regular interval. This provides a virtual frame
     *      interval during which InputConsumer will collect raw events,
     *      resample them and emit a "cooked" event back to us at roughly every
     *      60th of a second. "cooked" events are both smoothed and
     *      extrapolated/predicted into the future (for tool=finger) giving the
     *      appearance of lower latency. Getting a real frame time from the
     *      graphics logic (which is messy) does not appear to be necessary to
     *      gain significant benefit.
     *
     * Note event_rate_hz is only 55Hz. This allows rendering to catch up and
     * overtake the event rate every ~12th frame (200ms) on a 60Hz display.
     * Thus on every 12th+1 frame, there will be zero buffer lag in responding
     * to the cooked input event we have given the client.
     * This phase control is useful as it eliminates the one frame of lag you
     * would otherwise never catch up to if the event rate was exactly the same
     * as the display refresh rate.
     */

    nsecs_t const now = android_clock(SYSTEM_TIME_MONOTONIC);
    int const event_rate_hz = 55;
    nsecs_t const one_frame = 1000000000ULL / event_rate_hz;
    nsecs_t frame_time = (now / one_frame) * one_frame;

    auto result = input_consumer->consume(&event_factory,
                                          true,
                                          frame_time,
                                          &event_sequence_id,
                                          &android_event);
    if (result == droidinput::OK)
    {
        mia::Lexicon::translate(android_event, ev);

        map_key_event(xkb_mapper, ev);

        input_consumer->sendFinishedSignal(event_sequence_id, true);

        report->received_event(ev);

        // Send the event on its merry way.
        handler(&ev);
    }
    if (input_consumer->hasDeferredEvent())
    {
        // Ensure we get called again.
        wake();
    }
    else if (input_consumer->hasPendingBatch())
    {
        /* TODO: Feed in vsync-ish events (or work out when consume() would
         *       actually generate an event) rather than polling at 1000Hz
         */
        struct itimerspec const msec_delay = {
            { 0, 0 },
            { 0, 1000000 /* 10⁶ ns is 1ms */}
        };
        timerfd_settime(timer_fd, 0, &msec_delay, NULL);
    }
}

void mircva::InputReceiver::consume_wake_notification(mir::Fd const& fd)
{
    uint64_t dummy;
    if (read(fd, &dummy, sizeof(dummy)) != sizeof(dummy))
    {
        BOOST_THROW_EXCEPTION((std::system_error{errno,
                                                 std::system_category(),
                                                 "Failed to consume notification"}));
    }
}

void mircva::InputReceiver::wake()
{
    uint64_t dummy{0};
    if (write(notify_sender_fd, &dummy, sizeof(dummy)) != sizeof(dummy))
    {
        BOOST_THROW_EXCEPTION((std::system_error{errno,
                                                 std::system_category(),
                                                 "Failed to notify IO loop"}));
    }
}
