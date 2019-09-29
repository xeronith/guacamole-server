/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "config.h"

#include "client.h"
#include "common/display.h"
#include "common/surface.h"
#include "rdp.h"
#include "rdp_bitmap.h"
#include "rdp_settings.h"

#include <cairo/cairo.h>
#include <freerdp/codec/bitmap.h>
#include <freerdp/codec/color.h>
#include <freerdp/freerdp.h>
#include <freerdp/gdi/gdi.h>
#include <guacamole/client.h>
#include <guacamole/socket.h>
#include <winpr/wtypes.h>

#include <stdio.h>
#include <stdlib.h>

BOOL guac_rdp_cache_bitmap(rdpContext* context, rdpBitmap* bitmap) {

    guac_client* client = ((rdp_freerdp_context*) context)->client;
    guac_rdp_client* rdp_client = (guac_rdp_client*) client->data;

    /* Allocate buffer */
    guac_common_display_layer* buffer = guac_common_display_alloc_buffer(
            rdp_client->display, bitmap->width, bitmap->height);

    /* Cache image data if present */
    if (bitmap->data != NULL) {

        /* Create surface from image data */
        cairo_surface_t* image = cairo_image_surface_create_for_data(
            bitmap->data, CAIRO_FORMAT_RGB24,
            bitmap->width, bitmap->height, 4*bitmap->width);

        /* Send surface to buffer */
        guac_common_surface_draw(buffer->surface, 0, 0, image);

        /* Free surface */
        cairo_surface_destroy(image);

    }

    /* Store buffer reference in bitmap */
    ((guac_rdp_bitmap*) bitmap)->layer = buffer;

    return TRUE;

}

BOOL guac_rdp_bitmap_new(rdpContext* context, rdpBitmap* bitmap) {

    /* Convert image data if present */
    if (bitmap->data != NULL && bitmap->format != PIXEL_FORMAT_XRGB32) {

        /* Allocate sufficient space for converted image */
        unsigned char* image_buffer = _aligned_malloc(bitmap->width * bitmap->height * 4, 16);

        /* Attempt image conversion */
        if (!freerdp_image_copy(image_buffer, PIXEL_FORMAT_XRGB32, 0, 0, 0,
                bitmap->width, bitmap->height, bitmap->data, bitmap->format,
                0, 0, 0, &context->gdi->palette, FREERDP_FLIP_NONE)) {
            _aligned_free(image_buffer);
        }

        /* If successful, replace original image with converted image */
        else {
            _aligned_free(bitmap->data);
            bitmap->data = image_buffer;
        }

    }

    /* No corresponding surface yet - caching is deferred. */
    ((guac_rdp_bitmap*) bitmap)->layer = NULL;

    /* Start at zero usage */
    ((guac_rdp_bitmap*) bitmap)->used = 0;

    return TRUE;

}

BOOL guac_rdp_bitmap_paint(rdpContext* context, rdpBitmap* bitmap) {

    guac_client* client = ((rdp_freerdp_context*) context)->client;
    guac_rdp_client* rdp_client = (guac_rdp_client*) client->data;

    guac_common_display_layer* buffer = ((guac_rdp_bitmap*) bitmap)->layer;

    int width = bitmap->right - bitmap->left + 1;
    int height = bitmap->bottom - bitmap->top + 1;

    /* If not cached, cache if necessary */
    if (buffer == NULL && ((guac_rdp_bitmap*) bitmap)->used >= 1)
        guac_rdp_cache_bitmap(context, bitmap);

    /* If cached, retrieve from cache */
    if (buffer != NULL)
        guac_common_surface_copy(buffer->surface, 0, 0, width, height,
                rdp_client->display->default_surface,
                bitmap->left, bitmap->top);

    /* Otherwise, draw with stored image data */
    else if (bitmap->data != NULL) {

        /* Create surface from image data */
        cairo_surface_t* image = cairo_image_surface_create_for_data(
            bitmap->data, CAIRO_FORMAT_RGB24,
            width, height, 4*bitmap->width);

        /* Draw image on default surface */
        guac_common_surface_draw(rdp_client->display->default_surface,
                bitmap->left, bitmap->top, image);

        /* Free surface */
        cairo_surface_destroy(image);

    }

    /* Increment usage counter */
    ((guac_rdp_bitmap*) bitmap)->used++;

    return TRUE;

}

void guac_rdp_bitmap_free(rdpContext* context, rdpBitmap* bitmap) {

    guac_client* client = ((rdp_freerdp_context*) context)->client;
    guac_rdp_client* rdp_client = (guac_rdp_client*) client->data;
    guac_common_display_layer* buffer = ((guac_rdp_bitmap*) bitmap)->layer;

    /* If cached, free buffer */
    if (buffer != NULL)
        guac_common_display_free_buffer(rdp_client->display, buffer);

}

BOOL guac_rdp_bitmap_setsurface(rdpContext* context, rdpBitmap* bitmap, BOOL primary) {

    guac_client* client = ((rdp_freerdp_context*) context)->client;
    guac_rdp_client* rdp_client = (guac_rdp_client*) client->data;

    if (primary)
        rdp_client->current_surface = rdp_client->display->default_surface;

    else {

        /* Make sure that the recieved bitmap is not NULL before processing */
        if (bitmap == NULL) {
            guac_client_log(client, GUAC_LOG_INFO, "NULL bitmap found in bitmap_setsurface instruction.");
            return TRUE;
        }

        /* If not available as a surface, make available. */
        if (((guac_rdp_bitmap*) bitmap)->layer == NULL)
            guac_rdp_cache_bitmap(context, bitmap);

        rdp_client->current_surface =
            ((guac_rdp_bitmap*) bitmap)->layer->surface;

    }

    return TRUE;

}

