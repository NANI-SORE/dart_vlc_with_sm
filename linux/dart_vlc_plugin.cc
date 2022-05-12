// This file is a part of dart_vlc (https://github.com/alexmercerind/dart_vlc)
//
// Copyright (C) 2021-2022 Hitesh Kumar Saini <saini123hitesh@gmail.com>
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 3 of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#include "include/dart_vlc/dart_vlc_plugin.h"

#include <flutter_linux/flutter_linux.h>
#include <gtk/gtk.h>

#include <cstring>
#include <unordered_map>

#include "core.h"
#include "include/dart_vlc/dart_vlc_video_outlet.h"

#define DART_VLC_PLUGIN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), dart_vlc_plugin_get_type(), DartVlcPlugin))

struct _DartVlcPlugin {
  GObject parent_instance;
  FlMethodChannel* method_channel;
  FlTextureRegistrar* texture_registrar;
};

std::unordered_map<int32_t, VideoOutlet*> g_video_outlets;

G_DEFINE_TYPE(DartVlcPlugin, dart_vlc_plugin, g_object_get_type())

static void dart_vlc_plugin_handle_method_call(DartVlcPlugin* self,
                                               FlMethodCall* method_call) {
  FlMethodResponse* response = nullptr;
  const gchar* method_name = fl_method_call_get_name(method_call);
  if (strcmp(method_name, "PlayerRegisterTexture") == 0) {
    auto arguments = fl_method_call_get_args(method_call);
    int32_t player_id =
        fl_value_get_int(fl_value_lookup_string(arguments, "playerId"));
    auto [it, added] = g_video_outlets.try_emplace(player_id, nullptr);
    if (added) {
      it->second = video_outlet_new();
      FL_PIXEL_BUFFER_TEXTURE_GET_CLASS(it->second)->copy_pixels =
          video_outlet_copy_pixels;
      fl_texture_registrar_register_texture(self->texture_registrar,
                                            FL_TEXTURE(it->second));
      auto video_outlet_private =
          (VideoOutletPrivate*)video_outlet_get_instance_private(it->second);
      video_outlet_private->texture_id =
          reinterpret_cast<int64_t>(FL_TEXTURE(it->second));
      auto player = g_players->Get(player_id);
      player->SetVideoFrameCallback(
          [texture_registrar = self->texture_registrar,
           video_outlet_ptr = it->second,
           video_outlet_private = video_outlet_private](
              uint8_t* frame, int32_t width, int32_t height) -> void {
            video_outlet_private->buffer = frame;
            video_outlet_private->video_width = width;
            video_outlet_private->video_height = height;
            fl_texture_registrar_mark_texture_frame_available(
                texture_registrar, FL_TEXTURE(video_outlet_ptr));
          });

      response = FL_METHOD_RESPONSE(fl_method_success_response_new(
          fl_value_new_int(video_outlet_private->texture_id)));
    }

  } else if (strcmp(method_name, "PlayerUnregisterTexture") == 0) {
    auto arguments = fl_method_call_get_args(method_call);
    int32_t player_id =
        fl_value_get_int(fl_value_lookup_string(arguments, "playerId"));
    if (g_video_outlets.find(player_id) == g_video_outlets.end()) {
      response = FL_METHOD_RESPONSE(fl_method_error_response_new(
          "-2", "Texture was not found.", fl_value_new_null()));
    } else {
      g_video_outlets.erase(player_id);
      auto player = g_players->Get(player_id);
      player->SetVideoFrameCallback(nullptr);
      response = FL_METHOD_RESPONSE(
          fl_method_success_response_new(fl_value_new_null()));
    }
  } else {
    response = FL_METHOD_RESPONSE(fl_method_not_implemented_response_new());
  }
  fl_method_call_respond(method_call, response, nullptr);
}

static void dart_vlc_plugin_dispose(GObject* object) {
  G_OBJECT_CLASS(dart_vlc_plugin_parent_class)->dispose(object);
}

static void dart_vlc_plugin_class_init(DartVlcPluginClass* klass) {
  G_OBJECT_CLASS(klass)->dispose = dart_vlc_plugin_dispose;
}

static void dart_vlc_plugin_init(DartVlcPlugin* self) {}

static void method_call_cb(FlMethodChannel* channel, FlMethodCall* method_call,
                           gpointer user_data) {
  DartVlcPlugin* plugin = DART_VLC_PLUGIN(user_data);
  dart_vlc_plugin_handle_method_call(plugin, method_call);
}

void dart_vlc_plugin_register_with_registrar(FlPluginRegistrar* registrar) {
  DartVlcPlugin* plugin =
      DART_VLC_PLUGIN(g_object_new(dart_vlc_plugin_get_type(), nullptr));
  g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();
  plugin->method_channel =
      fl_method_channel_new(fl_plugin_registrar_get_messenger(registrar),
                            "dart_vlc", FL_METHOD_CODEC(codec));
  plugin->texture_registrar =
      fl_plugin_registrar_get_texture_registrar(registrar);
  fl_method_channel_set_method_call_handler(
      plugin->method_channel, method_call_cb, g_object_ref(plugin),
      g_object_unref);
  g_object_unref(plugin);
}
