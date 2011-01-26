#!/usr/bin/env python
#
# callchannel.py
# Copyright (C) 2008-2010 Collabora Ltd.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

import dbus
import dbus.glib
import gobject
import sys

import pygst
pygst.require("0.10")
import gst

import tpfarsight
import farsight
from util import *

from telepathy.client.channel import Channel
from telepathy.constants import (
    CONNECTION_HANDLE_TYPE_NONE, CONNECTION_HANDLE_TYPE_CONTACT,
    CONNECTION_STATUS_CONNECTED, CONNECTION_STATUS_DISCONNECTED,
    MEDIA_STREAM_STATE_CONNECTED
    )
from telepathy.interfaces import (
    CHANNEL_INTERFACE, CONN_INTERFACE,
    CONNECTION_INTERFACE_REQUESTS,
    CONNECTION_INTERFACE_CONTACT_CAPABILITIES,
    CLIENT)

from constants import *

class CallChannel:
    def __init__ (self, bus, connection, object_path, properties):
        self.bus = bus
        self.conn = connection
        self.tfchannel = None

        self.obj = self.bus.get_object (self.conn.service_name, object_path)

        self.pipeline = gst.Pipeline()
        self.pipeline.get_bus().add_watch(self.async_handler)

        self.notifier = notifier = farsight.ElementAddedNotifier()
        notifier.set_properties_from_file("element-properties")
        notifier.add(self.pipeline)

        tpfarsight.tf_channel_new_async (connection.service_name,
            connection.object_path, object_path, self.tpfs_created)

    def accept (self):
        self.obj.Accept(dbus_interface=CHANNEL_TYPE_CALL)

    def async_handler (self, bus, message):
        if self.tfchannel != None:
            self.tfchannel.bus_message(message)
        return True

        self.pipeline = gst.Pipeline()

    def tpfs_created (self, source, result):
        tfchannel = self.tfchannel = source.new_finish(result)
        tfchannel.connect ("fs-conference-add", self.conference_added)
        tfchannel.connect ("content-added", self.content_added)


    def src_pad_added (self, content, handle, stream, pad, codec):
        print "SOURCE PAD ADDEDD YO !"
        print codec

        type = content.get_property ("media-type")
        if type == farsight.MEDIA_TYPE_AUDIO:
            sink = gst.parse_bin_from_description("audioconvert ! audioresample ! audioconvert ! autoaudiosink", True)
        elif type == farsight.MEDIA_TYPE_VIDEO:
            sink = gst.parse_bin_from_description("ffmpegcolorspace ! videoscale ! autovideosink", True)

        self.pipeline.add(sink)
        pad.link(sink.get_pad("sink"))
        sink.set_state(gst.STATE_PLAYING)

    def get_codec_config (self, media_type):
        if media_type == farsight.MEDIA_TYPE_VIDEO:
            codecs = [ farsight.Codec(farsight.CODEC_ID_ANY, "H264",
                farsight.MEDIA_TYPE_VIDEO, 0) ]
            if self.conn.GetProtocol() == "sip" :
                codecs += [ farsight.Codec(farsight.CODEC_ID_DISABLE, "THEORA",
                                        farsight.MEDIA_TYPE_VIDEO, 0) ]
            else:
                codecs += [ farsight.Codec(farsight.CODEC_ID_ANY, "THEORA",
                                        farsight.MEDIA_TYPE_VIDEO, 0) ]
            codecs += [
                farsight.Codec(farsight.CODEC_ID_ANY, "H263",
                                        farsight.MEDIA_TYPE_VIDEO, 0),
                farsight.Codec(farsight.CODEC_ID_DISABLE, "DV",
                                        farsight.MEDIA_TYPE_VIDEO, 0),
                farsight.Codec(farsight.CODEC_ID_ANY, "JPEG",
                                        farsight.MEDIA_TYPE_VIDEO, 0),
                farsight.Codec(farsight.CODEC_ID_ANY, "MPV",
                                       farsight.MEDIA_TYPE_VIDEO, 0),
            ]

        else:
            codecs = [
                farsight.Codec(farsight.CODEC_ID_ANY, "SPEEX",
                    farsight.MEDIA_TYPE_AUDIO, 16000 ),
                farsight.Codec(farsight.CODEC_ID_ANY, "SPEEX",
                    farsight.MEDIA_TYPE_AUDIO, 8000 )
                ]
        return codecs

    def content_added(self, channel, content):
        sinkpad = content.get_property ("sink-pad")

        mtype = content.get_property ("media-type")
        prefs = self.get_codec_config (mtype)
        if prefs != None:
            content.set_codec_preferences(prefs)

        content.connect ("src-pad-added", self.src_pad_added)

        if mtype == farsight.MEDIA_TYPE_AUDIO:
            src = gst.element_factory_make ("audiotestsrc")
            src.set_property("is-live", True)
        elif mtype == farsight.MEDIA_TYPE_VIDEO:
            src = gst.element_factory_make ("videotestsrc")
            src.set_property("is-live", True)

        self.pipeline.add(src)
        src.get_pad("src").link(sinkpad)
        src.set_state(gst.STATE_PLAYING)

    def conference_added (self, channel, conference):
        self.pipeline.add(conference)
        self.pipeline.set_state(gst.STATE_PLAYING)

