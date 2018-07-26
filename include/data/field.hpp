// Copyright (C) 1997, 1999-2001, 2008 Nathan Lamont
// Copyright (C) 2018 The Antares Authors
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

#ifndef ANTARES_DATA_FIELD_HPP_
#define ANTARES_DATA_FIELD_HPP_

#include <pn/fwd>
#include <pn/string>
#include <pn/value>
#include <sfz/sfz.hpp>

#include "data/enums.hpp"
#include "data/handle.hpp"
#include "data/range.hpp"
#include "data/tags.hpp"
#include "drawing/color.hpp"
#include "math/fixed.hpp"
#include "math/geometry.hpp"
#include "math/units.hpp"

namespace antares {

struct Level;
struct Initial;
struct Condition;
struct Race;

class path_value {
    enum class Kind { ROOT, KEY, INDEX };

  public:
    path_value(pn::value_cref x) : _kind{Kind::ROOT}, _value{x} {}

    pn::value_cref value() const { return _value; }

    path_value get(pn::string_view key) const {
        return path_value{this, Kind::KEY, key, 0, _value.as_map().get(key)};
    }
    path_value get(int64_t index) const {
        return path_value{this, Kind::INDEX, pn::string_view{}, index,
                          array_get(_value.as_array(), index)};
    }

    pn::string path() const;
    pn::string prefix() const;

  private:
    static pn::value_cref array_get(pn::array_cref a, int64_t index);

    path_value(
            const path_value* parent, Kind kind, pn::string_view key, int64_t index,
            pn::value_cref value)
            : _parent{parent}, _kind{kind}, _key{key}, _index{index}, _value{value} {}

    const path_value* _parent = nullptr;
    Kind              _kind;
    pn::string_view   _key;
    int64_t           _index;
    pn::value_cref    _value;
};

sfz::optional<bool> optional_bool(path_value x);
bool                required_bool(path_value x);

sfz::optional<int64_t> optional_int(path_value x);
int64_t                required_int(path_value x);
sfz::optional<int64_t> optional_int(path_value x, const std::initializer_list<int64_t>& ranges);
int64_t                required_int(path_value x, const std::initializer_list<int64_t>& ranges);

double required_double(path_value x);

sfz::optional<Fixed> optional_fixed(path_value x);
Fixed                required_fixed(path_value x);

sfz::optional<pn::string_view> optional_string(path_value x);
sfz::optional<pn::string>      optional_string_copy(path_value x);
pn::string_view                required_string(path_value x);
pn::string                     required_string_copy(path_value x);

sfz::optional<ticks> optional_ticks(path_value x);
ticks                required_ticks(path_value x);
sfz::optional<secs>  optional_secs(path_value x);

Tags optional_tags(path_value x);

sfz::optional<Handle<Admiral>>          optional_admiral(path_value x);
Handle<Admiral>                         required_admiral(path_value x);
NamedHandle<const BaseObject>           required_base(path_value x);
sfz::optional<Handle<const Initial>>    optional_initial(path_value x);
Handle<const Initial>                   required_initial(path_value x);
Handle<const Condition>                 required_condition(path_value x);
sfz::optional<NamedHandle<const Level>> optional_level(path_value x);
sfz::optional<Owner>                    optional_owner(path_value x);
Owner                                   required_owner(path_value x);
NamedHandle<const Race>                 required_race(path_value x);

sfz::optional<Range<int64_t>> optional_int_range(path_value x);
Range<int64_t>                required_int_range(path_value x);
sfz::optional<Range<Fixed>>   optional_fixed_range(path_value x);
Range<Fixed>                  required_fixed_range(path_value x);
sfz::optional<Range<ticks>>   optional_ticks_range(path_value x);
Range<ticks>                  required_ticks_range(path_value x);

sfz::optional<Point> optional_point(path_value x);
Point                required_point(path_value x);
sfz::optional<Rect>  optional_rect(path_value x);
Rect                 required_rect(path_value x);

sfz::optional<RgbColor> optional_color(path_value x);
RgbColor                required_color(path_value x);

sfz::optional<Hue> optional_hue(path_value x);
Hue                required_hue(path_value x);
Screen             required_screen(path_value x);
Zoom               required_zoom(path_value x);

template <typename T, int N>
sfz::optional<T> optional_enum(path_value x, const std::pair<pn::string_view, T> (&values)[N]) {
    if (x.value().is_null()) {
        return sfz::nullopt;
    } else if (x.value().is_string()) {
        pn::string_view s = x.value().as_string();
        for (auto kv : values) {
            if (s == kv.first) {
                sfz::optional<T> t;
                t.emplace(kv.second);
                return t;
            }
        }
    }

    pn::array keys;
    for (auto kv : values) {
        keys.push_back(kv.first.copy());
    }
    throw std::runtime_error(pn::format("{0}: must be one of {1}", x.path(), keys).c_str());
}

template <typename T, int N>
T required_enum(path_value x, const std::pair<pn::string_view, T> (&values)[N]) {
    if (x.value().is_string()) {
        for (auto kv : values) {
            pn::string_view s = x.value().as_string();
            if (s == kv.first) {
                return kv.second;
            }
        }
    }

    pn::array keys;
    for (auto kv : values) {
        keys.push_back(kv.first.copy());
    }
    throw std::runtime_error(pn::format("{0}: must be one of {1}", x.path(), keys).c_str());
}

template <typename T>
T required_object_type(path_value x, T (*get_type)(path_value x)) {
    if (!x.value().is_map()) {
        throw std::runtime_error(pn::format("{0}: must be map", x.path()).c_str());
    }
    return get_type(x.get("type"));
}

template <typename T>
struct default_reader;

#define DEFAULT_READER(T, FN)                         \
    template <>                                       \
    struct default_reader<T> {                        \
        static T read(path_value x) { return FN(x); } \
    }

DEFAULT_READER(bool, required_bool);
DEFAULT_READER(sfz::optional<bool>, optional_bool);
DEFAULT_READER(int64_t, required_int);
DEFAULT_READER(sfz::optional<int64_t>, optional_int);
DEFAULT_READER(double, required_double);
DEFAULT_READER(Fixed, required_fixed);
DEFAULT_READER(sfz::optional<Fixed>, optional_fixed);
DEFAULT_READER(sfz::optional<pn::string_view>, optional_string);
DEFAULT_READER(sfz::optional<pn::string>, optional_string_copy);
DEFAULT_READER(pn::string_view, required_string);
DEFAULT_READER(pn::string, required_string_copy);
DEFAULT_READER(sfz::optional<ticks>, optional_ticks);
DEFAULT_READER(ticks, required_ticks);
DEFAULT_READER(sfz::optional<secs>, optional_secs);
DEFAULT_READER(Tags, optional_tags);
DEFAULT_READER(sfz::optional<Handle<Admiral>>, optional_admiral);
DEFAULT_READER(Handle<Admiral>, required_admiral);
DEFAULT_READER(NamedHandle<const BaseObject>, required_base);
DEFAULT_READER(sfz::optional<Handle<const Initial>>, optional_initial);
DEFAULT_READER(Handle<const Initial>, required_initial);
DEFAULT_READER(Handle<const Condition>, required_condition);
DEFAULT_READER(sfz::optional<NamedHandle<const Level>>, optional_level);
DEFAULT_READER(sfz::optional<Owner>, optional_owner);
DEFAULT_READER(Owner, required_owner);
DEFAULT_READER(NamedHandle<const Race>, required_race);
DEFAULT_READER(sfz::optional<Range<int64_t>>, optional_int_range);
DEFAULT_READER(Range<int64_t>, required_int_range);
DEFAULT_READER(sfz::optional<Range<Fixed>>, optional_fixed_range);
DEFAULT_READER(Range<Fixed>, required_fixed_range);
DEFAULT_READER(sfz::optional<Range<ticks>>, optional_ticks_range);
DEFAULT_READER(Range<ticks>, required_ticks_range);
DEFAULT_READER(sfz::optional<Point>, optional_point);
DEFAULT_READER(Point, required_point);
DEFAULT_READER(sfz::optional<Rect>, optional_rect);
DEFAULT_READER(Rect, required_rect);
DEFAULT_READER(sfz::optional<RgbColor>, optional_color);
DEFAULT_READER(RgbColor, required_color);
DEFAULT_READER(sfz::optional<Hue>, optional_hue);
DEFAULT_READER(Hue, required_hue);
DEFAULT_READER(Screen, required_screen);
DEFAULT_READER(Zoom, required_zoom);

template <typename T>
struct field {
    std::function<void(T* t, path_value x)> set;

    constexpr field(std::nullptr_t) : set([](T*, path_value) {}) {}

    template <typename F, typename U>
    constexpr field(F(U::*field))
            : set([field](T* t, path_value x) { (t->*field) = default_reader<F>::read(x); }) {}

    template <typename F, typename U>
    constexpr field(F(U::*field), F (*reader)(path_value x))
            : set([field, reader](T* t, path_value x) { (t->*field) = reader(x); }) {}
};

template <typename T>
T required_struct(path_value x, const std::map<pn::string_view, field<T>>& fields) {
    if (x.value().is_map()) {
        T t;
        for (const auto& kv : fields) {
            path_value v = x.get(kv.first);
            kv.second.set(&t, v);
        }
        for (const auto& kv : x.value().as_map()) {
            pn::string_view k = kv.key();
            path_value      v = x.get(k);
            if (fields.find(k) == fields.end()) {
                throw std::runtime_error(pn::format("{0}unknown field", v.prefix()).c_str());
            }
        }
        return t;
    } else {
        throw std::runtime_error(pn::format("{0}must be map", x.prefix()).c_str());
    }
}

template <typename T>
sfz::optional<T> optional_struct(path_value x, const std::map<pn::string_view, field<T>>& fields) {
    if (x.value().is_null()) {
        return sfz::nullopt;
    } else if (x.value().is_map()) {
        return sfz::make_optional(required_struct(x, fields));
    } else {
        throw std::runtime_error(pn::format("{0}must be null or map", x.prefix()).c_str());
    }
}

template <typename T, T (*F)(path_value x)>
static std::vector<T> required_array(path_value x) {
    if (x.value().is_array()) {
        pn::array_cref a = x.value().as_array();
        std::vector<T> result;
        for (int i = 0; i < a.size(); ++i) {
            result.emplace_back(F(x.get(i)));
        }
        return result;
    } else {
        throw std::runtime_error(pn::format("{0}: must be array", x.path()).c_str());
    }
}

template <typename T, T (*F)(path_value x)>
static std::vector<T> optional_array(path_value x) {
    if (x.value().is_null()) {
        return {};
    } else if (x.value().is_array()) {
        pn::array_cref a = x.value().as_array();
        std::vector<T> result;
        for (int i = 0; i < a.size(); ++i) {
            result.emplace_back(F(x.get(i)));
        }
        return result;
    } else {
        throw std::runtime_error(pn::format("{0}: must be null or array", x.path()).c_str());
    }
}

template <typename T>
struct default_reader<std::vector<T>> {
    static std::vector<T> read(path_value x) {
        return optional_array<T, default_reader<T>::read>(x);
    }
};

}  // namespace antares

#endif  // ANTARES_DATA_FIELD_HPP_
