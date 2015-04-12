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

#include "game/action.hpp"

#include <set>
#include <sfz/sfz.hpp>

#include "data/resource.hpp"
#include "data/space-object.hpp"
#include "data/string-list.hpp"
#include "drawing/color.hpp"
#include "drawing/sprite-handling.hpp"
#include "game/admiral.hpp"
#include "game/beam.hpp"
#include "game/globals.hpp"
#include "game/labels.hpp"
#include "game/messages.hpp"
#include "game/minicomputer.hpp"
#include "game/motion.hpp"
#include "game/player-ship.hpp"
#include "game/scenario-maker.hpp"
#include "game/space-object.hpp"
#include "game/starfield.hpp"
#include "math/macros.hpp"
#include "math/random.hpp"
#include "math/rotation.hpp"
#include "math/special.hpp"
#include "math/units.hpp"
#include "video/transitions.hpp"

using sfz::BytesSlice;
using sfz::Exception;
using sfz::ReadSource;
using sfz::String;
using sfz::StringSlice;
using sfz::range;
using sfz::read;
using std::set;
using std::unique_ptr;

namespace antares {

const size_t kActionQueueLength     = 120;

struct actionQueueType {
    objectActionType            *action;
    int32_t                         actionNum;
    int32_t                         actionToDo;
    int32_t                         scheduledTime;
    actionQueueType         *nextActionQueue;
    int32_t                         nextActionQueueNum;
    spaceObjectType         *subjectObject;
    int32_t                         subjectObjectNum;
    int32_t                         subjectObjectID;
    spaceObjectType         *directObject;
    int32_t                         directObjectNum;
    int32_t                         directObjectID;
    Point                       offset;
};

static actionQueueType* gFirstActionQueue = NULL;
static int32_t gFirstActionQueueNumber = -1;

static unique_ptr<actionQueueType[]> gActionQueueData;

static baseObjectType kZeroBaseObject;
static spaceObjectType kZeroSpaceObject = {0, &kZeroBaseObject};

#ifdef DATA_COVERAGE
set<int32_t> covered_actions;
#endif  // DATA_COVERAGE

static void queue_action(
        objectActionType *action, int32_t actionNumber, int32_t actionToDo,
        int32_t delayTime, spaceObjectType *subjectObject,
        spaceObjectType *directObject, Point* offset);

bool action_filter_applies_to(const objectActionType& action, const baseObjectType& target) {
    if (action.exclusiveFilter == 0xffffffff) {
        return action.levelKeyTag == target.levelKeyTag;
    } else {
        return (action.inclusiveFilter & target.attributes) == action.inclusiveFilter;
    }
}

bool action_filter_applies_to(const objectActionType& action, const spaceObjectType& target) {
    if (action.exclusiveFilter == 0xffffffff) {
        return action.levelKeyTag == target.baseType->levelKeyTag;
    } else {
        return (action.inclusiveFilter & target.attributes) == action.inclusiveFilter;
    }
}

static void create_object(
        objectActionType* action, spaceObjectType* subject, spaceObjectType* focus,
        Point* offset) {
    const auto& create = action->argument.createObject;
    const int32_t type = create.whichBaseType;
    const auto baseObject = mGetBaseObjectPtr(type);
    auto count = create.howManyMinimum;
    if (create.howManyRange > 0) {
        count += focus->randomSeed.next(create.howManyRange);
    }
    for (int i = 0; i < count; ++i) {
        fixedPointType vel = {0, 0};
        if (create.velocityRelative) {
            vel = focus->velocity;
        }
        int32_t direction = 0;
        if (baseObject->attributes & kAutoTarget) {
            direction = subject->targetAngle;
        } else if (create.directionRelative) {
            direction = focus->direction;
        }
        coordPointType at = focus->location;
        if (offset != NULL) {
            at.h += offset->h;
            at.v += offset->v;
        }

        const int32_t distance = create.randomDistance;
        if (distance > 0) {
            at.h += focus->randomSeed.next(distance * 2) - distance;
            at.v += focus->randomSeed.next(distance * 2) - distance;
        }

        int32_t n = CreateAnySpaceObject(type, &vel, &at, direction, focus->owner, 0, -1);
        if (n < 0) {
            continue;
        }

        spaceObjectType* product = mGetSpaceObjectPtr(n);
        if (product->attributes & kCanAcceptDestination) {
            uint32_t save_attributes = product->attributes;
            product->attributes &= ~kStaticDestination;
            if (product->owner >= 0) {
                if (action->reflexive) {
                    if (action->verb != kCreateObjectSetDest) {
                        SetObjectDestination(product, focus);
                    } else if (focus->destObjectPtr) {
                        SetObjectDestination(product, focus->destObjectPtr);
                    }
                }
            } else if (action->reflexive) {
                product->destObjectPtr = focus;
                product->timeFromOrigin = kTimeToCheckHome;
                product->runTimeFlags &= ~kHasArrived;
                product->destinationObject = focus->entryNumber; //a->destinationObject;
                product->destObjectDest = focus->destinationObject;
                product->destObjectID = focus->id;
                product->destObjectDestID = focus->destObjectID;
            }
            product->attributes = save_attributes;
        }
        product->targetObjectNumber = focus->targetObjectNumber;
        product->targetObjectID = focus->targetObjectID;
        product->closestObject = product->targetObjectNumber;

        //  ugly though it is, we have to fill in the rest of
        //  a new beam's fields after it's created.
        if (product->attributes & kIsBeam) {
            if (product->frame.beam.beam->beamKind != eKineticBeamKind) {
                // special beams need special post-creation acts
                Beams::set_attributes(product, focus);
            }
        }
    }
}

static void play_sound(objectActionType* action, spaceObjectType* focus) {
    const auto& sound = action->argument.playSound;
    auto id = sound.idMinimum;
    auto priority = static_cast<soundPriorityType>(sound.priority);
    if (sound.idRange > 0) {
        id += focus->randomSeed.next(sound.idRange + 1);
    }
    if (sound.absolute) {
        PlayVolumeSound(id, sound.volumeMinimum, sound.persistence, priority);
    } else {
        mPlayDistanceSound(sound.volumeMinimum, focus, id, sound.persistence, priority);
    }
}

static void make_sparks(objectActionType* action, spaceObjectType* focus) {
    const auto& sparks = action->argument.makeSparks;
    Point location;
    if (focus->sprite != NULL) {
        location.h = focus->sprite->where.h;
        location.v = focus->sprite->where.v;
    } else {
        int32_t l = (focus->location.h - gGlobalCorner.h) * gAbsoluteScale;
        l >>= SHIFT_SCALE;
        if ((l > -kSpriteMaxSize) && (l < kSpriteMaxSize)) {
            location.h = l + viewport.left;
        } else {
            location.h = -kSpriteMaxSize;
        }

        l = (focus->location.v - gGlobalCorner.v) * gAbsoluteScale;
        l >>= SHIFT_SCALE; /*+ CLIP_TOP*/;
        if ((l > -kSpriteMaxSize) && (l < kSpriteMaxSize)) {
            location.v = l + viewport.top;
        } else {
            location.v = -kSpriteMaxSize;
        }
    }
    globals()->starfield.make_sparks(
            sparks.howMany, sparks.speed, sparks.velocityRange, sparks.color, &location);
}

static void die(objectActionType* action, spaceObjectType* focus, spaceObjectType* subject) {
    bool destroy = false;
    switch (action->argument.killObject.dieType) {
        case kDieExpire:
            if (subject) {
                focus = subject;
            } else {
                return;
            }
            break;

        case kDieDestroy:
            if (subject) {
                focus = subject;
                destroy = true;
            } else {
                return;;
            }
            break;
    }

    // if the object is occupied by a human, eject him since he can't die
    if ((focus->attributes & (kIsPlayerShip | kRemoteOrHuman)) &&
            !focus->baseType->destroyDontDie) {
        CreateFloatingBodyOfPlayer(focus);
    }
    if (destroy) {
        DestroyObject(focus);
    } else {
        focus->active = kObjectToBeFreed;
    }
}

static void nil_target(objectActionType* action, spaceObjectType* focus) {
    focus->targetObjectNumber = kNoShip;
    focus->targetObjectID = kNoShip;
    focus->lastTarget = kNoShip;
}

static void alter(
        objectActionType* action,
        spaceObjectType* focus, spaceObjectType* subject, spaceObjectType* object) {
    const auto alter = action->argument.alterObject;
    int32_t l;
    Fixed f, f2, aFixed;
    int16_t angle;
    coordPointType newLocation;
    baseObjectType* baseObject;
    switch (alter.alterType) {
        case kAlterDamage:
            AlterObjectHealth(focus, alter.minimum);
            break;

        case kAlterEnergy:
            AlterObjectEnergy(focus, alter.minimum);
            break;

        case kAlterHidden:
            // Preserves old behavior; shouldn't really be adding one to alter.range.
            for (auto i: range(alter.minimum, alter.minimum + alter.range + 1)) {
                UnhideInitialObject(i);
            }
            break;

        case kAlterCloak:
            AlterObjectCloakState(focus, true);
            break;

        case kAlterSpin:
            if (focus->attributes & kCanTurn) {
                if (focus->attributes & kShapeFromDirection) {
                    f = mMultiplyFixed(
                            focus->baseType->frame.rotation.maxTurnRate,
                            alter.minimum + focus->randomSeed.next(alter.range));
                } else {
                    f = mMultiplyFixed(
                            2 /*kDefaultTurnRate*/,
                            alter.minimum + focus->randomSeed.next(alter.range));
                }
                f2 = focus->baseType->mass;
                if (f2 == 0) {
                    f = -1;
                } else {
                    f = mDivideFixed(f, f2);
                }
                focus->turnVelocity = f;
            }
            break;

        case kAlterOffline:
            f = alter.minimum + focus->randomSeed.next(alter.range);
            f2 = focus->baseType->mass;
            if (f2 == 0) {
                focus->offlineTime = -1;
            } else {
                focus->offlineTime = mDivideFixed(f, f2);
            }
            focus->offlineTime = mFixedToLong(focus->offlineTime);
            break;

        case kAlterVelocity:
            if (subject) {
                // active (non-reflexive) altering of velocity means a PUSH, just like
                //  two objects colliding.  Negative velocity = slow down
                if (object && (object != &kZeroSpaceObject)) {
                    if (alter.relative) {
                        if ((object->baseType->mass > 0) &&
                            (object->maxVelocity > 0)) {
                            if (alter.minimum >= 0) {
                                // if the minimum >= 0, then PUSH the object like collision
                                f = subject->velocity.h - object->velocity.h;
                                f /= object->baseType->mass;
                                f <<= 6L;
                                object->velocity.h += f;
                                f = subject->velocity.v - object->velocity.v;
                                f /= object->baseType->mass;
                                f <<= 6L;
                                object->velocity.v += f;

                                // make sure we're not going faster than our top speed

                                if (object->velocity.h == 0) {
                                    if (object->velocity.v < 0) {
                                        angle = 180;
                                    } else {
                                        angle = 0;
                                    }
                                } else {
                                    aFixed = MyFixRatio(object->velocity.h, object->velocity.v);

                                    angle = AngleFromSlope(aFixed);
                                    if (object->velocity.h > 0) {
                                        angle += 180;
                                    }
                                    if (angle >= 360) {
                                        angle -= 360;
                                    }
                                }
                            } else {
                                // if the minumum < 0, then STOP the object like applying breaks
                                f = object->velocity.h;
                                f = mMultiplyFixed(f, alter.minimum);
                                object->velocity.h += f;
                                f = object->velocity.v;
                                f = mMultiplyFixed(f, alter.minimum);
                                object->velocity.v += f;

                                // make sure we're not going faster than our top speed
                                if (object->velocity.h == 0) {
                                    if (object->velocity.v < 0) {
                                        angle = 180;
                                    } else {
                                        angle = 0;
                                    }
                                } else {
                                    aFixed = MyFixRatio(object->velocity.h, object->velocity.v);

                                    angle = AngleFromSlope(aFixed);
                                    if (object->velocity.h > 0) {
                                        angle += 180;
                                    }
                                    if (angle >= 360) {
                                        angle -= 360;
                                    }
                                }
                            }

                            // get the maxthrust of new vector
                            GetRotPoint(&f, &f2, angle);
                            f = mMultiplyFixed(object->maxVelocity, f);
                            f2 = mMultiplyFixed(object->maxVelocity, f2);

                            if (f < 0) {
                                if (object->velocity.h < f) {
                                    object->velocity.h = f;
                                }
                            } else {
                                if (object->velocity.h > f) {
                                    object->velocity.h = f;
                                }
                            }

                            if (f2 < 0) {
                                if (object->velocity.v < f2) {
                                    object->velocity.v = f2;
                                }
                            } else {
                                if (object->velocity.v > f2) {
                                    object->velocity.v = f2;
                                }
                            }
                        }
                    } else {
                        GetRotPoint(&f, &f2, subject->direction);
                        f = mMultiplyFixed(alter.minimum, f);
                        f2 = mMultiplyFixed(alter.minimum, f2);
                        focus->velocity.h = f;
                        focus->velocity.v = f2;
                    }
                } else {
                    // reflexive alter velocity means a burst of speed in the direction
                    // the object is facing, where negative speed means backwards. Object can
                    // excede its max velocity.
                    // Minimum value is absolute speed in direction.
                    GetRotPoint(&f, &f2, focus->direction);
                    f = mMultiplyFixed(alter.minimum, f);
                    f2 = mMultiplyFixed(alter.minimum, f2);
                    if (alter.relative) {
                        focus->velocity.h += f;
                        focus->velocity.v += f2;
                    } else {
                        focus->velocity.h = f;
                        focus->velocity.v = f2;
                    }
                }
            }
            break;

        case kAlterMaxVelocity:
            if (alter.minimum < 0) {
                focus->maxVelocity = focus->baseType->maxVelocity;
            } else {
                focus->maxVelocity = alter.minimum;
            }
            break;

        case kAlterThrust:
            f = alter.minimum + focus->randomSeed.next(alter.range);
            if (alter.relative) {
                focus->thrust += f;
            } else {
                focus->thrust = f;
            }
            break;

        case kAlterBaseType:
            if (action->reflexive || (object && (object != &kZeroSpaceObject)))
            ChangeObjectBaseType(focus, alter.minimum, -1, alter.relative);
            break;

        case kAlterOwner:
            if (alter.relative) {
                // if it's relative AND reflexive, we take the direct
                // object's owner, since relative & reflexive would
                // do nothing.
                if (action->reflexive && object && (object != &kZeroSpaceObject)) {
                    AlterObjectOwner(focus, object->owner, true);
                } else {
                    AlterObjectOwner(focus, subject->owner, true);
                }
            } else {
                AlterObjectOwner(focus, alter.minimum, false);
            }
            break;

        case kAlterConditionTrueYet:
            if (alter.range <= 0) {
                gThisScenario->condition(alter.minimum)->set_true_yet(alter.relative);
            } else {
                for (auto l: range(alter.minimum, alter.minimum + alter.range + 1)) {
                    gThisScenario->condition(l)->set_true_yet(alter.relative);
                }
            }
            break;

        case kAlterOccupation:
            AlterObjectOccupation(focus, subject->owner, alter.minimum, true);
            break;

        case kAlterAbsoluteCash:
            if (alter.relative) {
                if (focus != &kZeroSpaceObject) {
                    PayAdmiralAbsolute(focus->owner, alter.minimum);
                }
            } else {
                PayAdmiralAbsolute(alter.range, alter.minimum);
            }
            break;

        case kAlterAge:
            l = alter.minimum + focus->randomSeed.next(alter.range);

            if (alter.relative) {
                if (focus->age >= 0) {
                    focus->age += l;

                    if (focus->age < 0) {
                        focus->age = 0;
                    }
                } else {
                    focus->age += l;
                }
            } else {
                focus->age = l;
            }
            break;

        case kAlterLocation:
            if (alter.relative) {
                if (object && (object != &kZeroSpaceObject)) {
                    newLocation.h = subject->location.h;
                    newLocation.v = subject->location.v;
                } else {
                    newLocation.h = object->location.h;
                    newLocation.v = object->location.v;
                }
            } else {
                newLocation.h = newLocation.v = 0;
            }
            newLocation.h += focus->randomSeed.next(alter.minimum << 1) - alter.minimum;
            newLocation.v += focus->randomSeed.next(alter.minimum << 1) - alter.minimum;
            focus->location.h = newLocation.h;
            focus->location.v = newLocation.v;
            break;

        case kAlterAbsoluteLocation:
            if (alter.relative) {
                focus->location.h += alter.minimum;
                focus->location.v += alter.range;
            } else {
                focus->location = Translate_Coord_To_Scenario_Rotation(
                    alter.minimum, alter.range);
            }
            break;

        case kAlterWeapon1:
            focus->pulse.type = alter.minimum;
            if (focus->pulse.type != kNoWeapon) {
                baseObject = focus->pulse.base = mGetBaseObjectPtr(focus->pulse.type);
                focus->pulse.ammo = baseObject->frame.weapon.ammo;
                focus->pulse.time = focus->pulse.position = 0;
                if (baseObject->frame.weapon.range > focus->longestWeaponRange) {
                    focus->longestWeaponRange = baseObject->frame.weapon.range;
                }
                if (baseObject->frame.weapon.range < focus->shortestWeaponRange) {
                    focus->shortestWeaponRange = baseObject->frame.weapon.range;
                }
            } else {
                focus->pulse.base = NULL;
                focus->pulse.ammo = 0;
                focus->pulse.time = 0;
            }
            break;

        case kAlterWeapon2:
            focus->beam.type = alter.minimum;
            if (focus->beam.type != kNoWeapon) {
                baseObject = focus->beam.base = mGetBaseObjectPtr(focus->beam.type);
                focus->beam.ammo = baseObject->frame.weapon.ammo;
                focus->beam.time = focus->beam.position = 0;
                if (baseObject->frame.weapon.range > focus->longestWeaponRange) {
                    focus->longestWeaponRange = baseObject->frame.weapon.range;
                }
                if (baseObject->frame.weapon.range < focus->shortestWeaponRange) {
                    focus->shortestWeaponRange = baseObject->frame.weapon.range;
                }
            } else {
                focus->beam.base = NULL;
                focus->beam.ammo = 0;
                focus->beam.time = 0;
            }
            break;

        case kAlterSpecial:
            focus->special.type = alter.minimum;
            if (focus->special.type != kNoWeapon) {
                baseObject = focus->special.base = mGetBaseObjectPtr(focus->special.type);
                focus->special.ammo = baseObject->frame.weapon.ammo;
                focus->special.time = focus->special.position = 0;
                if (baseObject->frame.weapon.range > focus->longestWeaponRange) {
                    focus->longestWeaponRange = baseObject->frame.weapon.range;
                }
                if (baseObject->frame.weapon.range < focus->shortestWeaponRange) {
                    focus->shortestWeaponRange = baseObject->frame.weapon.range;
                }
            } else {
                focus->special.base = NULL;
                focus->special.ammo = 0;
                focus->special.time = 0;
            }
            break;

        case kAlterLevelKeyTag:
            break;

        default:
            break;
    }
}

static void land_at(
        objectActionType* action, spaceObjectType* focus, spaceObjectType* subject) {
    // even though this is never a reflexive verb, we only effect ourselves
    if (subject->attributes & (kIsPlayerShip | kRemoteOrHuman)) {
        CreateFloatingBodyOfPlayer(subject);
    }
    subject->presenceState = kLandingPresence;
    subject->presenceData = subject->baseType->naturalScale |
        (action->argument.landAt.landingSpeed << kPresenceDataHiWordShift);
}

static void enter_warp(
        objectActionType* action, spaceObjectType* focus, spaceObjectType* subject) {
    subject->presenceState = kWarpInPresence;
    subject->presenceData = subject->baseType->warpSpeed;
    subject->attributes &= ~kOccupiesSpace;
    fixedPointType newVel = {0, 0};
    CreateAnySpaceObject(
            globals()->scenarioFileInfo.warpInFlareID, &newVel,
            &subject->location, subject->direction, kNoOwner, 0, -1);
}

static void change_score(objectActionType* action, spaceObjectType* focus) {
    const auto& score = action->argument.changeScore;
    int32_t admiral;
    if ((score.whichPlayer == -1) && (focus != &kZeroSpaceObject)) {
        admiral = focus->owner;
    } else {
        admiral = mGetRealAdmiralNum(score.whichPlayer);
    }
    if (admiral >= 0) {
        AlterAdmiralScore(admiral, score.whichScore, score.amount);
    }
}

static void declare_winner(objectActionType* action, spaceObjectType* focus) {
    const auto& winner = action->argument.declareWinner;
    int32_t admiral;
    if ((winner.whichPlayer == -1) && (focus != &kZeroSpaceObject)) {
        admiral = focus->owner;
    } else {
        admiral = mGetRealAdmiralNum(winner.whichPlayer);
    }
    DeclareWinner(admiral, winner.nextLevel, winner.textID);
}

static void display_message(objectActionType* action, spaceObjectType* focus) {
    const auto& message = action->argument.displayMessage;
    Messages::start(message.resID, message.resID + message.pageNum - 1);
}

static void set_destination(
        objectActionType* action, spaceObjectType* focus, spaceObjectType* subject) {
    uint32_t save_attributes = subject->attributes;
    subject->attributes &= ~kStaticDestination;
    SetObjectDestination(subject, focus);
    subject->attributes = save_attributes;
}

static void activate_special(
        objectActionType* action, spaceObjectType* focus, spaceObjectType* subject) {
    // TODO(sfiera): replace with fire_weapon() if we can show it's OK.
    ActivateObjectSpecial(subject);
}

static void color_flash(objectActionType* action, spaceObjectType* focus) {
    uint8_t tinyColor = GetTranslateColorShade(
            action->argument.colorFlash.color,
            action->argument.colorFlash.shade);
    globals()->transitions.start_boolean(
            action->argument.colorFlash.length,
            action->argument.colorFlash.length, tinyColor);
}

static void enable_keys(objectActionType* action, spaceObjectType* focus) {
    globals()->keyMask = globals()->keyMask & ~action->argument.keys.keyMask;
}

static void disable_keys(objectActionType* action, spaceObjectType* focus) {
    globals()->keyMask = globals()->keyMask | action->argument.keys.keyMask;
}

static void set_zoom(objectActionType* action, spaceObjectType* focus) {
    if (action->argument.zoom.zoomLevel != globals()->gZoomMode) {
        globals()->gZoomMode = static_cast<ZoomType>(action->argument.zoom.zoomLevel);
        PlayVolumeSound(kComputerBeep3, kMediumVolume, kMediumPersistence, kLowPrioritySound);
        StringList strings(kMessageStringID);
        StringSlice string = strings.at(globals()->gZoomMode + kZoomStringOffset - 1);
        Messages::set_status(string, kStatusLabelColor);
    }
}

static void computer_select(objectActionType* action, spaceObjectType* focus) {
    MiniComputer_SetScreenAndLineHack(
            action->argument.computerSelect.screenNumber,
            action->argument.computerSelect.lineNumber);
}

static void assume_initial_object(objectActionType* action, spaceObjectType* focus) {
    Scenario::InitialObject* initialObject = gThisScenario->initial(
            action->argument.assumeInitial.whichInitialObject + GetAdmiralScore(0, 0));
    if (initialObject) {
        initialObject->realObjectID = focus->id;
        initialObject->realObjectNumber = focus->entryNumber;
    }
}

void execute_actions(
        int32_t whichAction, int32_t actionNum,
        spaceObjectType* const original_subject, spaceObjectType* const original_object,
        Point* offset, bool allowDelay) {
    if (whichAction < 0) {
        return;
    }

    bool checkConditions = false;

    const auto begin = mGetObjectActionPtr(whichAction);
    const auto end = begin + actionNum;
    for (auto action = begin; action != end; ++action) {
#ifdef DATA_COVERAGE
        covered_actions.insert(action - mGetObjectActionPtr(0));
#endif  // DATA_COVERAGE

        if (action->verb == kNoAction) {
            break;
        }
        spaceObjectType* subject = original_subject;
        if (action->initialSubjectOverride != kNoShip) {
            subject = GetObjectFromInitialNumber(action->initialSubjectOverride);
        }
        spaceObjectType* object = original_object;
        if (action->initialDirectOverride != kNoShip) {
            object = GetObjectFromInitialNumber(action->initialDirectOverride);
        }

        if ((action->delay > 0) && allowDelay) {
            queue_action(
                    action, action - mGetObjectActionPtr(0), end - action,
                    action->delay, subject, object, offset);
            return;
        }
        allowDelay = true;

        auto focus = object;
        if (action->reflexive || !focus) {
            focus = subject;
        }

        // This pair of conditions is a workaround for a bug which
        // manifests itself for example in the implementation of "Hold
        // Position".  When an object is instructed to hold position, it
        // gains its own location as its destination, triggering its
        // arrive action, but its target is nulled out.
        //
        // Arrive actions are typically only specified on objects with
        // non-zero order flags (so that a transport won't attempt to
        // land on a bunker station, for example).  So, back when Ares
        // ran without protected memory, and NULL pointed to a
        // zeroed-out area of the address space, the flags would prevent
        // the arrive action from triggering.
        //
        // It's not correct to always inhibit the action here, because
        // the arrive action should be triggered when the focus
        // doesn't have flags.  But we need to prevent it in the case of
        // transports somehow, so we emulate the old behavior of
        // pointing to a zeroed-out object.
        if (object == NULL) {
            object = &kZeroSpaceObject;
        }
        if (subject == NULL) {
            subject = &kZeroSpaceObject;
        }

        if (focus == NULL) {
            focus = &kZeroSpaceObject;
        } else if ((action->owner < -1)
                || ((action->owner == -1) && (object->owner == subject->owner))
                || ((action->owner == 1) && (object->owner != subject->owner))
                || (action->owner > 1)
                || !action_filter_applies_to(*action, *object)) {
            continue;
        }

        switch (action->verb) {
            case kCreateObject:
            case kCreateObjectSetDest:  create_object(action, focus, subject, offset); break;
            case kPlaySound:            play_sound(action, focus); break;
            case kMakeSparks:           make_sparks(action, focus); break;
            case kDie:                  die(action, focus, subject); break;
            case kNilTarget:            nil_target(action, focus); break;
            case kAlter:                alter(action, focus, subject, object); break;
            case kLandAt:               land_at(action, focus, subject); break;
            case kEnterWarp:            enter_warp(action, focus, subject); break;
            case kChangeScore:          change_score(action, focus); break;
            case kDeclareWinner:        declare_winner(action, focus); break;
            case kDisplayMessage:       display_message(action, focus); break;
            case kSetDestination:       set_destination(action, focus, subject); break;
            case kActivateSpecial:      activate_special(action, focus, subject); break;
            case kColorFlash:           color_flash(action, focus); break;
            case kEnableKeys:           enable_keys(action, focus); break;
            case kDisableKeys:          disable_keys(action, focus); break;
            case kSetZoom:              set_zoom(action, focus); break;
            case kComputerSelect:       computer_select(action, focus); break;
            case kAssumeInitialObject:  assume_initial_object(action, focus); break;
        }

        switch (action->verb) {
            case kChangeScore:
            case kDisplayMessage:
                checkConditions = true;
                break;
        }
    }

    if (checkConditions) {
        CheckScenarioConditions(0);
    }
}

void reset_action_queue() {
    gActionQueueData.reset(new actionQueueType[kActionQueueLength]);

    gFirstActionQueueNumber = -1;
    gFirstActionQueue = NULL;

    actionQueueType* action = gActionQueueData.get();
    for (int32_t i = 0; i < kActionQueueLength; i++) {
        action->actionNum = -1;
        action->actionToDo = 0;
        action->action = NULL;
        action->nextActionQueueNum = -1;
        action->nextActionQueue = NULL;
        action->scheduledTime = -1;
        action->subjectObject = NULL;
        action->subjectObjectNum = -1;
        action->subjectObjectID = -1;
        action->directObject = NULL;
        action->directObjectNum = -1;
        action->directObjectID = -1;
        action->offset.h = action->offset.v = 0;
        action++;
    }
}

static void queue_action(
        objectActionType *action, int32_t actionNumber, int32_t actionToDo,
        int32_t delayTime, spaceObjectType *subjectObject,
        spaceObjectType *directObject, Point* offset) {
    int32_t queueNumber = 0;
    actionQueueType* actionQueue = gActionQueueData.get();
    while (actionQueue->action && (queueNumber < kActionQueueLength)) {
        actionQueue++;
        queueNumber++;
    }

    if (queueNumber == kActionQueueLength) {
        return;
    }
    actionQueue->action = action;
    actionQueue->actionNum = actionNumber;
    actionQueue->scheduledTime = delayTime;
    actionQueue->actionToDo = actionToDo;

    if (offset) {
        actionQueue->offset = *offset;
    } else {
        actionQueue->offset = Point{0, 0};
    }

    actionQueue->subjectObject = subjectObject;
    if (subjectObject) {
        actionQueue->subjectObjectNum = subjectObject->entryNumber;
        actionQueue->subjectObjectID = subjectObject->id;
    } else {
        actionQueue->subjectObjectNum = -1;
        actionQueue->subjectObjectID = -1;
    }

    actionQueue->directObject = directObject;
    if (directObject) {
        actionQueue->directObjectNum = directObject->entryNumber;
        actionQueue->directObjectID = directObject->id;
    } else {
        actionQueue->directObjectNum = -1;
        actionQueue->directObjectID = -1;
    }

    actionQueueType* previousQueue = NULL;
    actionQueueType* nextQueue = gFirstActionQueue;
    while (nextQueue && (nextQueue->scheduledTime < delayTime)) {
        previousQueue = nextQueue;
        nextQueue = nextQueue->nextActionQueue;
    }
    if (previousQueue) {
        actionQueue->nextActionQueue = previousQueue->nextActionQueue;
        actionQueue->nextActionQueueNum = previousQueue->nextActionQueueNum;

        previousQueue->nextActionQueue = actionQueue;
        previousQueue->nextActionQueueNum = queueNumber;
    } else {
        actionQueue->nextActionQueue = gFirstActionQueue;
        actionQueue->nextActionQueueNum = gFirstActionQueueNumber;
        gFirstActionQueue = actionQueue;
        gFirstActionQueueNumber = queueNumber;
    }
}

void execute_action_queue(int32_t unitsToDo) {
    for (int32_t i = 0; i < kActionQueueLength; i++) {
        auto actionQueue = &gActionQueueData[i];
        if (actionQueue->action) {
            actionQueue->scheduledTime -= unitsToDo;
        }
    }

    while (gFirstActionQueue
            && gFirstActionQueue->action
            && (gFirstActionQueue->scheduledTime <= 0)) {
        int32_t subjectid = -1;
        if (gFirstActionQueue->subjectObject && gFirstActionQueue->subjectObject->active) {
            subjectid = gFirstActionQueue->subjectObject->id;
        }

        int32_t directid = -1;
        if (gFirstActionQueue->directObject && gFirstActionQueue->directObject->active) {
            directid = gFirstActionQueue->directObject->id;
        }
        if ((subjectid == gFirstActionQueue->subjectObjectID)
                && (directid == gFirstActionQueue->directObjectID)) {
            execute_actions(
                    gFirstActionQueue->actionNum,
                    gFirstActionQueue->actionToDo,
                    gFirstActionQueue->subjectObject, gFirstActionQueue->directObject,
                    &gFirstActionQueue->offset, false);
        }
        gFirstActionQueue->actionNum = -1;
        gFirstActionQueue->actionToDo = 0;
        gFirstActionQueue->action = NULL;
        gFirstActionQueue->scheduledTime = -1;
        gFirstActionQueue->subjectObject = NULL;
        gFirstActionQueue->subjectObjectNum = -1;
        gFirstActionQueue->subjectObjectID = -1;
        gFirstActionQueue->directObject = NULL;
        gFirstActionQueue->directObjectNum = -1;
        gFirstActionQueue->directObjectID = -1;
        gFirstActionQueue->offset = Point{0, 0};

        auto actionQueue = gFirstActionQueue;

        gFirstActionQueueNumber = gFirstActionQueue->nextActionQueueNum;
        gFirstActionQueue = gFirstActionQueue->nextActionQueue;

        actionQueue->nextActionQueueNum = -1;
        actionQueue->nextActionQueue = NULL;
    }
}

}  // namespace antares
