// Copyright (C) 1997, 1999-2001, 2008 Nathan Lamont
// Copyright (C) 2008-2012 The Antares Authors
//
// This file is part of Antares, a tactical space combat game.
//
// Antares is free software: you can redistribute it and/or modify it
// under the terms of the Lesser GNU General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Antares is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with Antares.  If not, see http://www.gnu.org/licenses/

#include "drawing/shapes.hpp"

#include <algorithm>

#include "drawing/color.hpp"
#include "drawing/pix-map.hpp"
#include "lang/casts.hpp"
#include "math/macros.hpp"
#include "video/driver.hpp"

using sfz::Exception;
using std::min;

namespace antares {

// must be square
void draw_triangle_up(PixMap *destPix, const RgbColor& color) {
    int32_t size = destPix->size().width;
    Rect r(0, 0, size, 2);
    for (int32_t i = 0; i < size; i += 2) {
        r.bottom = min(r.bottom, size);
        destPix->view(r).fill(color);
        r.inset(1, 0);
        r.offset(0, 2);
    }
}

static void draw_plus(PixMap::View pix, RgbColor color) {
    int32_t size = pix.size().width;
    if (size <= 3) {
        pix.fill(color);
        return;
    }
    int32_t half = size / 2;
    pix.view(Rect(0, half - 1, size, half + 2)).fill(color);
    pix.view(Rect(half - 1, 0, half + 2, size)).fill(color);
}

// Compatibility shim.  The original implementation of this function
// didn't properly fill the rect, so this function trims the rect
// correspondingly and forwards to draw_plus().
void draw_compat_plus(PixMap *destPix, const RgbColor& color) {
    Rect bounds = destPix->size().as_rect();
    if (bounds.right != 1) {
        ++bounds.left;
        --bounds.bottom;
        if (bounds.right % 2) {
            --bounds.right;
            --bounds.bottom;
        }
    }
    draw_plus(destPix->view(bounds), color);
}

static void draw_diamond(PixMap::View pix, RgbColor color) {
    int32_t size = pix.size().width;
    int32_t half = (size + 1) / 2;
    for (int i = 0; i < half; ++i) {
        Rect r = pix.size().as_rect();
        r.inset(i, half - i - 1);
        pix.view(r).fill(color);
    }
}

// Compatibility shim.  The original implementation of this function
// didn't properly fill the rect, so this function trims the rect
// correspondingly and forwards to draw_diamond().
void draw_compat_diamond(PixMap *destPix, const RgbColor& color) {
    Rect bounds = destPix->size().as_rect();
    if (bounds.right != 1) {
        ++bounds.left;
        --bounds.bottom;
    }
    if (bounds.right == 3) {
        destPix->view(bounds).fill(color);
    } else {
        draw_diamond(destPix->view(bounds), color);
    }
}

void DrawNateVBracket(
        PixMap *destPix, const Rect& destRect, const Rect& clipRect,
        const RgbColor& color) {
    destPix->view(
            Rect(destRect.left, destRect.top, destRect.right, destRect.top + 1)).fill(color);
    destPix->view(
            Rect(destRect.left, destRect.bottom - 1, destRect.right, destRect.bottom)).fill(color);

    destPix->set(destRect.left, destRect.top + 1, color);
    destPix->set(destRect.right - 1, destRect.top + 1, color);

    destPix->set(destRect.left, destRect.bottom - 2, color);
    destPix->set(destRect.right - 1, destRect.bottom - 2, color);
}

void draw_vbracket(const Rect& rect, const RgbColor& color) {
    Point ul(rect.left, rect.top);
    Point ur(rect.right - 1, rect.top);
    Point ll(rect.left, rect.bottom - 1);
    Point lr(rect.right - 1, rect.bottom - 1);

    VideoDriver::driver()->draw_line(ul, ur, color);
    VideoDriver::driver()->draw_line(ul, Point(ul.h, ul.v + 1), color);
    VideoDriver::driver()->draw_line(ur, Point(ur.h, ur.v + 1), color);

    VideoDriver::driver()->draw_line(ll, lr, color);
    VideoDriver::driver()->draw_line(ll, Point(ll.h, ll.v - 1), color);
    VideoDriver::driver()->draw_line(lr, Point(lr.h, lr.v - 1), color);
}

void draw_shaded_rect(
        Rect rect,
        const RgbColor& fill_color, const RgbColor& light_color, const RgbColor& dark_color) {
    rect.right--;
    rect.bottom--;

    VideoDriver::driver()->draw_line(
            Point(rect.left, rect.bottom), Point(rect.left, rect.top), light_color);
    VideoDriver::driver()->draw_line(
        Point(rect.left, rect.top), Point(rect.right, rect.top), light_color);

    VideoDriver::driver()->draw_line(
        Point(rect.right, rect.top), Point(rect.right, rect.bottom), dark_color);
    VideoDriver::driver()->draw_line(
        Point(rect.right, rect.bottom), Point(rect.left, rect.bottom), dark_color);
    rect.left++;
    rect.top++;

    if ((rect.height() > 0) && (rect.width() > 0)) {
        VideoDriver::driver()->fill_rect(rect, fill_color);
    }
}

}  // namespace antares
