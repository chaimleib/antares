// Copyright (C) 1997, 1999-2001, 2008 Nathan Lamont
// Copyright (C) 2008-2017 The Antares Authors
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

// Space Object Handling >> MUST BE INITED _AFTER_ SCENARIOMAKER << (uses Ares Scenarios file)

#include "data/base-object.hpp"

namespace antares {

static int32_t required_int32(path_value x) {
    return required_int(x, {-0x80000000ll, 0x80000000ll});
}

static sfz::optional<int32_t> optional_int32(path_value x) {
    sfz::optional<int64_t> i = optional_int(x, {-0x80000000ll, 0x80000000ll});
    if (i.has_value()) {
        return sfz::make_optional<int32_t>(*i);
    }
    return sfz::nullopt;
}

int32_t optional_object_order_flags(path_value x) {
    if (x.value().is_null()) {
        return 0;
    } else if (x.value().is_map()) {
        static const pn::string_view flags[32] = {"stronger_than_target",
                                                  "base",
                                                  "not_base",
                                                  "local",
                                                  "remote",
                                                  "only_escort_not_base",
                                                  "friend",
                                                  "foe",

                                                  "bit09",
                                                  "bit10",
                                                  "bit11",
                                                  "bit12",
                                                  "bit13",
                                                  "bit14",
                                                  "bit15",
                                                  "bit16",

                                                  "bit17",
                                                  "bit18",
                                                  "hard_matching_friend",
                                                  "hard_matching_foe",
                                                  "hard_friendly_escort_only",
                                                  "hard_no_friendly_escort",
                                                  "hard_remote",
                                                  "hard_local",

                                                  "hard_foe",
                                                  "hard_friend",
                                                  "hard_not_base",
                                                  "hard_base"};

        int32_t bit    = 0x00000001;
        int32_t result = 0x00000000;
        for (pn::string_view flag : flags) {
            if (optional_bool(x.get(flag)).value_or(false)) {
                result |= bit;
            }
            bit <<= 1;
        }
        return result;
    } else {
        throw std::runtime_error(pn::format("{0}: must be null or map", x.path()).c_str());
    }
}

int32_t optional_object_build_flags(path_value x) {
    if (x.value().is_null()) {
        return 0;
    } else if (x.value().is_map()) {
        static const pn::string_view flags[32] = {"uncaptured_base_exists",
                                                  "sufficient_escorts_exist",
                                                  "this_base_needs_protection",
                                                  "friend_up_trend",
                                                  "friend_down_trend",
                                                  "foe_up_trend",
                                                  "foe_down_trend",
                                                  "matching_foe_exists",

                                                  "bit09",
                                                  "bit10",
                                                  "bit11",
                                                  "bit12",
                                                  "bit13",
                                                  "bit14",
                                                  "bit15",
                                                  "bit16",

                                                  "bit17",
                                                  "bit18",
                                                  "bit19",
                                                  "bit20",
                                                  "bit21",
                                                  "bit22",
                                                  "only_engaged_by",
                                                  "can_only_engage"};

        int32_t bit    = 0x00000001;
        int32_t result = 0x00000000;
        for (pn::string_view flag : flags) {
            if (optional_bool(x.get(flag)).value_or(false)) {
                result |= bit;
            }
            bit <<= 1;
        }
        return result;
    } else {
        throw std::runtime_error(pn::format("{0}: must be null or map", x.path()).c_str());
    }
}

fixedPointType required_fixed_point(path_value x) {
    if (x.value().is_map()) {
        Fixed px = required_fixed(x.get("x"));
        Fixed py = required_fixed(x.get("y"));
        return {px, py};
    } else {
        throw std::runtime_error(pn::format("{0}: must be map", x.path()).c_str());
    }
}

std::vector<fixedPointType> optional_fixed_point_array(path_value x) {
    if (x.value().is_null()) {
        return {};
    } else if (x.value().is_array()) {
        pn::array_cref              a = x.value().as_array();
        std::vector<fixedPointType> result;
        for (int i = 0; i < a.size(); ++i) {
            result.emplace_back(required_fixed_point(x.get(i)));
        }
        return result;
    } else {
        throw std::runtime_error(pn::format("{0}: must be null or array", x.path()).c_str());
    }
}

sfz::optional<BaseObject::Weapon> optional_weapon(path_value x) {
    if (x.value().is_null()) {
        return sfz::nullopt;
    } else if (x.value().is_map()) {
        BaseObject::Weapon w;
        w.base      = required_base(x.get("base"));
        w.positions = optional_fixed_point_array(x.get("positions"));
        return sfz::make_optional(std::move(w));
    } else {
        throw std::runtime_error(pn::format("{0}: must be null or map", x.path()).c_str());
    }
}

static sfz::optional<int16_t> optional_layer(path_value x) {
    sfz::optional<int64_t> i = optional_int(x, {1, 4});
    if (i.has_value()) {
        return sfz::make_optional<int16_t>(*i);
    }
    return sfz::nullopt;
}

static sfz::optional<int32_t> optional_scale(path_value x) {
    sfz::optional<Fixed> f = optional_fixed(x);
    if (f.has_value()) {
        return sfz::make_optional<int32_t>(f->val() << 4);
    }
    return sfz::nullopt;
}

objectFrameType::Rotation required_rotation_frame(path_value x) {
    using Rotation = objectFrameType::Rotation;
    return required_struct<Rotation>(
            x, {
                       {"sprite", {&Rotation::sprite, required_string_copy}},
                       {"layer", {&Rotation::layer, optional_layer, 0}},
                       {"scale", {&Rotation::scale, optional_scale, 4096}},
                       {"frames", {&Rotation::frames, required_int_range}},
                       {"turn_rate", {&Rotation::turn_rate, optional_fixed, Fixed::zero()}},
               });
}

objectFrameType::Animation required_animation_frame(path_value x) {
    using Animation = objectFrameType::Animation;
    return required_struct<Animation>(
            x, {
                       {"sprite", {&Animation::sprite, required_string_copy}},
                       {"layer", {&Animation::layer, optional_layer, 0}},
                       {"scale", {&Animation::scale, optional_scale, 4096}},
                       {"frames",
                        {&Animation::frames, optional_fixed_range,
                         Range<Fixed>{Fixed::zero(), Fixed::from_val(1)}}},
                       {"direction",
                        {&Animation::direction, optional_animation_direction,
                         AnimationDirection::NONE}},
                       {"speed", {&Animation::speed, optional_fixed, Fixed::zero()}},
                       {"first",
                        {&Animation::first, optional_fixed_range,
                         Range<Fixed>{Fixed::zero(), Fixed::from_val(1)}}},
               });
}

objectFrameType::Vector required_vector_frame(path_value x) {
    if (x.value().is_map()) {
        objectFrameType::Vector v;
        v.kind     = required_vector_kind(x.get("kind"));
        v.accuracy = required_int(x.get("accuracy"));
        v.range    = required_int(x.get("range"));

        sfz::optional<RgbColor> color = optional_color(x.get("color"));
        sfz::optional<Hue>      hue   = optional_hue(x.get("hue"));
        if (v.kind == VectorKind::BOLT) {
            v.visible    = color.has_value();
            v.bolt_color = color.value_or(RgbColor::clear());
            v.beam_hue   = Hue::GRAY;
        } else {
            v.visible    = hue.has_value();
            v.bolt_color = RgbColor::clear();
            v.beam_hue   = hue.value_or(Hue::GRAY);
        }
        return v;
    } else {
        throw std::runtime_error(pn::format("{0}: must be map", x.path()).c_str());
    }
}

uint32_t optional_usage(path_value x) {
    if (x.value().is_null()) {
        return 0;
    } else if (x.value().is_map()) {
        static const pn::string_view flags[3] = {"transportation", "attacking", "defense"};
        uint32_t                     bit      = 0x00000001;
        uint32_t                     result   = 0x00000000;
        for (pn::string_view flag : flags) {
            if (optional_bool(x.get(flag)).value_or(false)) {
                result |= bit;
            }
            bit <<= 1;
        }
        return result;
    } else {
        throw std::runtime_error(pn::format("{0}: must be null or map", x.path()).c_str());
    }
}

objectFrameType::Weapon required_device_frame(path_value x) {
    using Weapon = objectFrameType::Weapon;
    return required_struct<Weapon>(
            x, {
                       {"usage", {&Weapon::usage, optional_usage}},
                       {"energy_cost", {&Weapon::energyCost, optional_int32, 0}},
                       {"fire_time", {&Weapon::fireTime, required_ticks}},
                       {"ammo", {&Weapon::ammo, optional_int32, -1}},
                       {"range", {&Weapon::range, required_int32}},
                       {"inverse_speed", {&Weapon::inverseSpeed, optional_fixed, Fixed::zero()}},
                       {"restock_cost", {&Weapon::restockCost, optional_int32, -1}},
               });
}

static sfz::optional<BaseObject::Icon> optional_icon(path_value x) {
    return optional_struct<BaseObject::Icon>(
            x, {
                       {"shape", {&BaseObject::Icon::shape, required_icon_shape}},
                       {"size", {&BaseObject::Icon::size, required_int}},
               });
}

static sfz::optional<BaseObject::Loadout> optional_loadout(path_value x) {
    return optional_struct<BaseObject::Loadout>(
            x, {
                       {"pulse", {&BaseObject::Loadout::pulse, optional_weapon}},
                       {"beam", {&BaseObject::Loadout::beam, optional_weapon}},
                       {"special", {&BaseObject::Loadout::special, optional_weapon}},
               });
}

BaseObject base_object(pn::value_cref x0) {
    if (!x0.is_map()) {
        throw std::runtime_error("must be map");
    }

    path_value x{x0};
    BaseObject o;
    o.attributes = optional_object_attributes(x.get("attributes"));
    o.buildFlags = optional_object_build_flags(x.get("build_flags"));
    o.orderFlags = optional_object_order_flags(x.get("order_flags"));

    o.name       = required_string(x.get("long_name")).copy();
    o.short_name = required_string(x.get("short_name")).copy();
    o.portrait   = optional_string(x.get("portrait")).value_or("").copy();

    o.price                = optional_int(x.get("price")).value_or(0);
    o.destinationClass     = optional_int(x.get("destination_class")).value_or(0);
    o.warpOutDistance      = optional_int(x.get("warp_out_distance")).value_or(0);
    o.health               = optional_int(x.get("health")).value_or(0);
    o.damage               = optional_int(x.get("damage")).value_or(0);
    o.energy               = optional_int(x.get("energy")).value_or(0);
    o.skillNum             = optional_int(x.get("skill_num")).value_or(0);
    o.skillDen             = optional_int(x.get("skill_den")).value_or(0);
    o.occupy_count         = optional_int(x.get("occupy_count")).value_or(-1);
    o.arriveActionDistance = optional_int(x.get("arrive_action_distance")).value_or(0);

    o.offenseValue  = optional_fixed(x.get("offense")).value_or(Fixed::zero());
    o.maxVelocity   = optional_fixed(x.get("max_velocity")).value_or(Fixed::zero());
    o.warpSpeed     = optional_fixed(x.get("warp_speed")).value_or(Fixed::zero());
    o.mass          = optional_fixed(x.get("mass")).value_or(Fixed::zero());
    o.maxThrust     = optional_fixed(x.get("max_thrust")).value_or(Fixed::zero());
    o.friendDefecit = optional_fixed(x.get("friend_deficit")).value_or(Fixed::zero());
    o.buildRatio    = optional_fixed(x.get("build_ratio")).value_or(Fixed::zero());

    o.buildTime = optional_ticks(x.get("build_time")).value_or(ticks(0));

    o.shieldColor = optional_color(x.get("shield_color"));

    o.initial_velocity = optional_fixed_range(x.get("initial_velocity"))
                                 .value_or(Range<Fixed>{Fixed::zero(), Fixed::zero()});
    o.initial_age = optional_ticks_range(x.get("initial_age"))
                            .value_or(Range<ticks>{ticks(-1), ticks(-1)});
    o.initial_direction =
            optional_int_range(x.get("initial_direction")).value_or(Range<int64_t>{0, 0});

    o.destroy  = optional_action_array(x.get("on_destroy"));
    o.expire   = optional_action_array(x.get("on_expire"));
    o.create   = optional_action_array(x.get("on_create"));
    o.collide  = optional_action_array(x.get("on_collide"));
    o.activate = optional_action_array(x.get("on_activate"));
    o.arrive   = optional_action_array(x.get("on_arrive"));

    o.icon    = optional_icon(x.get("icon")).value_or(BaseObject::Icon{IconShape::SQUARE, 0});
    o.weapons = optional_loadout(x.get("weapons")).value_or(BaseObject::Loadout{});

    if (o.attributes & kShapeFromDirection) {
        o.frame.rotation = required_rotation_frame(x.get("rotation"));
    } else if (o.attributes & kIsSelfAnimated) {
        o.frame.animation = required_animation_frame(x.get("animation"));
    } else if (o.attributes & kIsVector) {
        o.frame.vector = required_vector_frame(x.get("vector"));
    } else {
        o.frame.weapon = required_device_frame(x.get("device"));
    }

    o.destroyDontDie  = optional_bool(x.get("destroy_dont_die")).value_or(false);
    o.expireDontDie   = optional_bool(x.get("expire_dont_die")).value_or(false);
    o.activate_period = optional_ticks_range(x.get("activate_period"))
                                .value_or(Range<ticks>{ticks(0), ticks(0)});

    o.levelKeyTag  = optional_string(x.get("level_tag")).value_or("").copy();
    o.engageKeyTag = optional_string(x.get("engage_tag")).value_or("").copy();
    o.orderKeyTag  = optional_string(x.get("order_tag")).value_or("").copy();

    return o;
}

}  // namespace antares
