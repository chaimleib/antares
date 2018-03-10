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

#include "data/level.hpp"

#include <sfz/sfz.hpp>

#include "data/field.hpp"
#include "data/plugin.hpp"
#include "data/races.hpp"
#include "data/resource.hpp"

namespace macroman = sfz::macroman;

namespace antares {

static std::vector<Level::Initial>                    optional_initial_array(path_value x);
static std::vector<std::unique_ptr<Level::Condition>> optional_condition_array(path_value x);
static std::vector<Level::Briefing>                   optional_briefing_array(path_value x);

Level* Level::get(int n) { return &plug.levels[n]; }

Race* Race::get(int n) { return &plug.races[n]; }

bool read_from(pn::file_view in, ScenarioInfo* info) {
    pn::value  x;
    pn_error_t error;
    if (!pn::parse(in, x, &error)) {
        return false;
    }
    pn::map_cref m = x.as_map();
    for (pn::string_view field :
         {"title", "download_url", "author", "author_url", "version", "splash", "starmap"}) {
        if (m.get(field).as_string().empty()) {
            return false;
        }
    }
    for (pn::string_view field :
         {"warp_in_flare", "warp_out_flare", "player_body", "energy_blob"}) {
        if (!m.has(field) || !m.get(field).is_int()) {
            return false;
        }
    }

    info->titleString       = m.get("title").as_string().copy();
    info->downloadURLString = m.get("download_url").as_string().copy();
    info->authorNameString  = m.get("author").as_string().copy();
    info->authorURLString   = m.get("author_url").as_string().copy();
    info->version           = m.get("version").as_string().copy();
    info->warpInFlareID     = Handle<BaseObject>(m.get("warp_in_flare").as_int());
    info->warpOutFlareID    = Handle<BaseObject>(m.get("warp_out_flare").as_int());
    info->playerBodyID      = Handle<BaseObject>(m.get("player_body").as_int());
    info->energyBlobID      = Handle<BaseObject>(m.get("energy_blob").as_int());

    info->intro_text = m.get("intro").as_string().copy();
    info->about_text = m.get("about").as_string().copy();

    info->publisher_screen = nullptr;  // Don’t have permission to show ASW logo.
    info->ego_screen       = Resource::texture("pictures/credit");
    info->splash_screen    = Resource::texture(m.get("splash").as_string());
    info->starmap          = Resource::texture(m.get("starmap").as_string());

    return true;
}

static Level::Player required_player(path_value x, LevelType level_type) {
    if (x.value().is_map()) {
        Level::Player p;
        p.earningPower = optional_fixed(x.get("earning_power")).value_or(Fixed::zero());
        switch (level_type) {
            case LevelType::DEMO:
                p.name       = required_string(x.get("name")).copy();
                p.playerRace = required_race(x.get("race"));
                break;
            case LevelType::SOLO:
                p.playerType = required_player_type(x.get("type"));
                p.name       = required_string(x.get("name")).copy();
                p.playerRace = required_race(x.get("race"));
                break;
            case LevelType::NET:
                p.playerType   = required_player_type(x.get("type"));
                p.netRaceFlags = 0;  // TODO(sfiera): fill
                break;
        }
        return p;
    } else {
        throw std::runtime_error(pn::format("{0}: must be map", x.path()).c_str());
    }
}

static std::vector<Level::Player> required_player_array(path_value x, LevelType level_type) {
    if (x.value().is_array()) {
        std::vector<Level::Player> p;
        for (int i = 0; i < x.value().as_array().size(); ++i) {
            p.push_back(required_player(x.get(i), level_type));
        }
        return p;
    } else {
        throw std::runtime_error(pn::format("{0}: must be array", x.path()).c_str());
    }
}

Level level(pn::value_cref x0) {
    if (!x0.is_map()) {
        throw std::runtime_error("must be map");
    }

    path_value x{x0};
    Level      l;
    l.type          = required_level_type(x.get("type"));
    l.chapter       = required_int(x.get("chapter"));
    l.name          = required_string(x.get("title")).copy();
    l.players       = required_player_array(x.get("players"), l.type);
    l.initials      = optional_initial_array(x.get("initials"));
    l.conditions    = optional_condition_array(x.get("conditions"));
    l.briefings     = optional_briefing_array(x.get("briefings"));
    l.starMap       = optional_point(x.get("starmap")).value_or(Point{0, 0});
    l.songID        = required_int(x.get("song"));
    l.score_strings = optional_string_array(x.get("score"));

    l.startTime   = optional_secs(x.get("start_time")).value_or(secs(0));
    l.is_training = optional_bool(x.get("is_training")).value_or(false);
    l.angle       = optional_int(x.get("angle")).value_or(-1);
    l.parTime     = game_ticks(optional_secs(x.get("par_time")).value_or(secs(0)));
    l.parKills    = optional_int(x.get("par_kills")).value_or(0);
    l.parLosses   = optional_int(x.get("par_losses")).value_or(0);

    switch (l.type) {
        case LevelType::DEMO: break;

        case LevelType::SOLO:
            l.own_no_ships_text = optional_string(x.get("no_ships")).value_or("").copy();
            l.prologue          = optional_string(x.get("prologue")).value_or("").copy();
            l.epilogue          = optional_string(x.get("epilogue")).value_or("").copy();
            break;

        case LevelType::NET:
            l.own_no_ships_text = required_string(x.get("own_no_ships")).copy();
            l.foe_no_ships_text = required_string(x.get("foe_no_ships")).copy();
            l.description       = required_string(x.get("description")).copy();
            break;
    }

    return l;
}

static std::unique_ptr<Level::Condition> autopilot_condition(path_value x) {
    std::unique_ptr<Level::AutopilotCondition> c(new Level::AutopilotCondition);
    c->value = required_bool(x.get("value"));
    return std::move(c);
}

static std::unique_ptr<Level::Condition> building_condition(path_value x) {
    std::unique_ptr<Level::BuildingCondition> c(new Level::BuildingCondition);
    c->value = required_bool(x.get("value"));
    return std::move(c);
}

static std::unique_ptr<Level::Condition> computer_condition(path_value x) {
    std::unique_ptr<Level::ComputerCondition> c(new Level::ComputerCondition);
    c->screen = required_screen(x.get("screen"));
    c->line   = optional_int(x.get("line")).value_or(-1);
    return std::move(c);
}

static std::unique_ptr<Level::Condition> counter_condition(path_value x) {
    std::unique_ptr<Level::CounterCondition> c(new Level::CounterCondition);
    c->player  = required_admiral(x.get("player"));
    c->counter = required_int(x.get("counter"));
    c->value   = required_int(x.get("value"));
    return std::move(c);
}

static std::unique_ptr<Level::Condition> destroyed_condition(path_value x) {
    std::unique_ptr<Level::DestroyedCondition> c(new Level::DestroyedCondition);
    c->initial = required_initial(x.get("initial"));
    c->value   = required_bool(x.get("value"));
    return std::move(c);
}

static std::unique_ptr<Level::Condition> distance_condition(path_value x) {
    std::unique_ptr<Level::DistanceCondition> c(new Level::DistanceCondition);
    c->value = required_int(x.get("value"));
    return std::move(c);
}

static std::unique_ptr<Level::Condition> health_condition(path_value x) {
    std::unique_ptr<Level::HealthCondition> c(new Level::HealthCondition);
    c->value = required_double(x.get("value"));
    return std::move(c);
}

static std::unique_ptr<Level::Condition> message_condition(path_value x) {
    std::unique_ptr<Level::MessageCondition> c(new Level::MessageCondition);
    c->id   = required_int(x.get("id"));
    c->page = required_int(x.get("page"));
    return std::move(c);
}

static std::unique_ptr<Level::Condition> ordered_condition(path_value x) {
    return std::unique_ptr<Level::OrderedCondition>(new Level::OrderedCondition);
}

static std::unique_ptr<Level::Condition> owner_condition(path_value x) {
    std::unique_ptr<Level::OwnerCondition> c(new Level::OwnerCondition);
    c->player = required_admiral(x.get("player"));
    return std::move(c);
}

static std::unique_ptr<Level::Condition> ships_condition(path_value x) {
    std::unique_ptr<Level::ShipsCondition> c(new Level::ShipsCondition);
    c->player = required_admiral(x.get("player"));
    c->value  = required_int(x.get("value"));
    return std::move(c);
}

static std::unique_ptr<Level::Condition> speed_condition(path_value x) {
    std::unique_ptr<Level::SpeedCondition> c(new Level::SpeedCondition);
    c->value = required_fixed(x.get("value"));
    return std::move(c);
}

static std::unique_ptr<Level::Condition> subject_condition(path_value x) {
    std::unique_ptr<Level::SubjectCondition> c(new Level::SubjectCondition);
    c->value = required_subject_value(x.get("value"));
    return std::move(c);
}

static std::unique_ptr<Level::Condition> time_condition(path_value x) {
    std::unique_ptr<Level::TimeCondition> c(new Level::TimeCondition);
    c->value = required_ticks(x.get("value"));
    return std::move(c);
}

static std::unique_ptr<Level::Condition> zoom_condition(path_value x) {
    std::unique_ptr<Level::ZoomCondition> c(new Level::ZoomCondition);
    c->value = required_zoom(x.get("value"));
    return std::move(c);
}

static std::unique_ptr<Level::Condition> condition(path_value x) {
    if (!x.value().is_map()) {
        throw std::runtime_error(pn::format("{0}: must be map", x.path()).c_str());
    }

    pn::string_view                   type = required_string(x.get("type"));
    std::unique_ptr<Level::Condition> c;
    if (type == "autopilot") {
        c = autopilot_condition(x);
    } else if (type == "building") {
        c = building_condition(x);
    } else if (type == "computer") {
        c = computer_condition(x);
    } else if (type == "counter") {
        c = counter_condition(x);
    } else if (type == "destroyed") {
        c = destroyed_condition(x);
    } else if (type == "distance") {
        c = distance_condition(x);
    } else if (type == "false") {
        return std::unique_ptr<Level::Condition>(new Level::FalseCondition);
    } else if (type == "health") {
        c = health_condition(x);
    } else if (type == "message") {
        c = message_condition(x);
    } else if (type == "ordered") {
        c = ordered_condition(x);
    } else if (type == "owner") {
        c = owner_condition(x);
    } else if (type == "ships") {
        c = ships_condition(x);
    } else if (type == "speed") {
        c = speed_condition(x);
    } else if (type == "subject") {
        c = subject_condition(x);
    } else if (type == "time") {
        c = time_condition(x);
    } else if (type == "zoom") {
        c = zoom_condition(x);
    } else {
        throw std::runtime_error(pn::format("unknown type: {0}", type).c_str());
    }

    c->op                = required_condition_op(x.get("op"));
    c->persistent        = optional_bool(x.get("persistent")).value_or(false);
    c->initially_enabled = !optional_bool(x.get("initially_disabled")).value_or(false);
    c->subject           = optional_initial(x.get("subject")).value_or(Level::Initial::none());
    c->object            = optional_initial(x.get("object")).value_or(Level::Initial::none());
    c->action            = required_action_array(x.get("action"));

    return c;
}

static std::vector<std::unique_ptr<Level::Condition>> optional_condition_array(path_value x) {
    if (x.value().is_null()) {
        return {};
    } else if (x.value().is_array()) {
        std::vector<std::unique_ptr<Level::Condition>> v;
        for (int i = 0; i < x.value().as_array().size(); ++i) {
            v.push_back(condition(x.get(i)));
        }
        return v;
    } else {
        throw std::runtime_error(pn::format("{0}: must be null or array", x.path()).c_str());
    }
}

static Level::Briefing briefing(path_value x) {
    return required_struct<Level::Briefing>(
            x, {
                       {"object",
                        {&Level::Briefing::object, optional_initial, Level::Initial::none()}},
                       {"title", {&Level::Briefing::title, required_string_copy}},
                       {"content", {&Level::Briefing::content, required_string_copy}},
               });
}

static std::vector<Level::Briefing> optional_briefing_array(path_value x) {
    if (x.value().is_null()) {
        return {};
    } else if (x.value().is_array()) {
        std::vector<Level::Briefing> v;
        for (int i = 0; i < x.value().as_array().size(); ++i) {
            v.push_back(briefing(x.get(i)));
        }
        return v;
    } else {
        throw std::runtime_error(pn::format("{0}: must be null or array", x.path()).c_str());
    }
}

static Level::Initial initial(path_value x) {
    if (!x.value().is_map()) {
        throw std::runtime_error(pn::format("{0}: must be map", x.path()).c_str());
    }

    Level::Initial i;
    i.base    = required_base(x.get("base"));
    i.owner   = optional_admiral(x.get("owner")).value_or(Handle<Admiral>(-1));
    i.at      = required_point(x.get("at"));
    i.earning = optional_fixed(x.get("earning")).value_or(Fixed::zero());

    i.name_override   = optional_string(x.get("rename")).value_or("").copy();
    i.sprite_override = optional_int(x.get("sprite_override")).value_or(-1);

    i.target = optional_initial(x.get("target")).value_or(Level::Initial::none());

    i.attributes = Level::Initial::Attributes(optional_initial_attributes(x.get("attributes")));

    std::vector<int> build = optional_int_array(x.get("build"));
    if (build.size() >= kMaxTypeBaseCanBuild) {
        throw std::runtime_error(pn::format(
                                         "{0}: has {1} elements, more than max of {2}",
                                         x.get("build").path(), build.size(), kMaxTypeBaseCanBuild)
                                         .c_str());
    }
    for (int x = 0; x < kMaxTypeBaseCanBuild; ++x) {
        if (x < build.size()) {
            i.build[x] = build[x];
        } else {
            i.build[x] = -1;
        }
    }

    return i;
}

static std::vector<Level::Initial> optional_initial_array(path_value x) {
    if (x.value().is_null()) {
        return {};
    } else if (x.value().is_array()) {
        std::vector<Level::Initial> v;
        for (int i = 0; i < x.value().as_array().size(); ++i) {
            v.push_back(initial(x.get(i)));
        }
        return v;
    } else {
        throw std::runtime_error(pn::format("{0}: must be null or array", x.path()).c_str());
    }
}

}  // namespace antares
