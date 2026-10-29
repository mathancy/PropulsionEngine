#pragma once

/**
 * PlayPhysics.h
 * What happens to the scene's objects each frame of Play after the scripts have run: characters fall, jump, and stop at
 * solid objects, and the scripted objects that touch or bump into each other are found. Header-only, and it knows
 * nothing about the renderer, so it is tested on its own (tests/collision_test.cpp).
 *
 * The scripts move objects by writing a new position (`move Self forward by 5 per second`). That is only what the
 * script *wants*. For a character, the move it asked for is carried out against the solid objects, with gravity and a
 * jump added, and the position it ended up at is what the object has, and what the script starts from next frame.
 *
 * Contacts (for `when Self touches X`, `when Self stops touching X` and `when Self hits X`) are between scripted objects
 * only, since a script can only name the files of other scripted objects:
 *   touching  their shapes overlap, or are resting against each other (within a hair)
 *   hit       a character was blocked by a solid object (reported once, when the contact starts)
 */

#include "core/CollisionSettings.h"
#include "physics/Collision.h"

#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Propulsion {
namespace Physics {

/// What the physics knows about one scene object. Built when Play starts, in the same order as the scene.
struct Body {
    bool hasShape = false;                         // has a mesh, so it has a shape
    bool solid = false;                            // has a shape, and its Object is solid (characters can't pass through it)
    ShapeKind kind = ShapeKind::Box;
    glm::vec3 boundsMin{-0.5f}, boundsMax{0.5f};   // its mesh's bounds, before the object's own scale and turn
    bool character = false;                       // has a Character Movement component
    CharacterMovementSettings movement;
    CharacterState state;                         // where the character stood at the end of the last frame, and how it moves
    bool scripted = false;                        // runs a script, so it can touch and hit other scripted objects
};

/// Where an object is right now (the scripts have just moved it).
struct Pose {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
    bool collisionEnabled = true;  // false for a destroyed object, or one a script made non-solid (`make Collision.Enabled false`)
};

/// Which scripted objects are touching or have hit each other, and what changed in the last step. Objects are scene
/// indexes; a pair is always (lower, higher).
struct Contacts {
    using Pair = std::pair<size_t, size_t>;
    std::set<Pair> overlapping;  // touching after the last step
    std::set<Pair> blocking;     // a character was being blocked by the other after the last step
    std::vector<Pair> began;     // started touching in the last step
    std::vector<Pair> ended;     // stopped touching in the last step
    std::vector<Pair> hit;       // a character started being blocked by the other in the last step
};

/// A script move this long (in one frame) is a jump to somewhere else, like setting the location, not walking.
inline constexpr float kTeleportDistance = 20.0f;
/// Objects this close count as touching, so something resting on another one is touching it.
inline constexpr float kTouchSlop = 0.02f;

/// The shape of a body where its pose puts it. A character is its capsule, whatever its mesh. False if it has none.
inline bool shapeOfBody(const Body& body, const Pose& pose, int id, Shape& out) {
    if (body.character) {
        const CharacterParams p = body.movement.paramsAt(pose.scale);
        out = capsuleShape(pose.position, p.radius, p.halfHeight, id);
        return true;
    }
    if (!body.hasShape) return false;
    out = shapeFromMesh(body.kind, body.boundsMin, body.boundsMax, pose.position, pose.rotation, pose.scale, id);
    return true;
}

/// Every solid object, as the shape it makes where it is now, except the body `skip` (a character doesn't stop itself,
/// and a trace doesn't hit the one that made it). Each shape's `id` is the body's index. `withCharacters` false leaves out
/// the characters too.
inline std::vector<Shape> solidShapes(const std::vector<Body>& bodies, const std::vector<Pose>& poses, size_t skip = static_cast<size_t>(-1),
                                      bool withCharacters = true) {
    std::vector<Shape> solids;
    if (bodies.size() != poses.size()) return solids;
    for (size_t i = 0; i < bodies.size(); ++i) {
        Shape s;
        if (i != skip && (withCharacters || !bodies[i].character) && bodies[i].solid && poses[i].collisionEnabled &&
            shapeOfBody(bodies[i], poses[i], static_cast<int>(i), s)) {
            solids.push_back(s);
        }
    }
    return solids;
}

/// What a script's `trace` finds: the nearest solid object along the line (a ball of `radius` slid along it), skipping
/// the body `asker` that traced (-1 if not one). The hit's `id` is the body's index.
inline TraceHit traceBodies(const std::vector<Body>& bodies, const std::vector<Pose>& poses, int asker, const glm::vec3& from,
                            const glm::vec3& to, float radius) {
    return traceShapes(solidShapes(bodies, poses, asker >= 0 ? static_cast<size_t>(asker) : static_cast<size_t>(-1)), from, to, radius);
}

/// How much of the way from `from` to `to` a ball of `radius` can travel before it meets a solid object (1 if the way is
/// clear, 0 if it starts inside one), skipping the body `asker` (-1 if not one). For a spring arm's camera, which like
/// Unreal's passes through characters (it would be pulled in to the face of anyone who walks behind the player).
inline float clearFraction(const std::vector<Body>& bodies, const std::vector<Pose>& poses, int asker, const glm::vec3& from,
                           const glm::vec3& to, float radius) {
    const float length = glm::distance(from, to);
    if (!(length > 0.0f)) return 1.0f;
    const TraceHit hit = traceShapes(
        solidShapes(bodies, poses, asker >= 0 ? static_cast<size_t>(asker) : static_cast<size_t>(-1), false), from, to, radius);
    return hit.blocked ? glm::clamp(hit.distance / length, 0.0f, 1.0f) : 1.0f;
}

/// Whether the Input Action `action` went down this frame in the input of body `body` (each Object has its own Input
/// Mapping, so two characters can jump with different keys).
using ActionStarted = std::function<bool(size_t body, const std::string& action)>;

/// Advance every character by `deltaTime`. A character jumps when its `jumpAction` started this frame (`started` says).
/// The position of each character is replaced with where the physics left it, and its index is added to `moved` (when
/// given) so the caller can tell the script. `contacts`, when given, is brought up to date.
inline void stepBodies(std::vector<Body>& bodies, std::vector<Pose>& poses, float deltaTime, const ActionStarted& started,
                       std::vector<size_t>* moved = nullptr, Contacts* contacts = nullptr) {
    if (deltaTime <= 0.0f || bodies.size() != poses.size()) return;

    // Scripts move things, so the shapes are made fresh every frame.
    const std::vector<Shape> solids = solidShapes(bodies, poses);

    std::set<Contacts::Pair> blockingNow;
    std::vector<Shape> others;
    std::vector<int> touched;
    for (size_t i = 0; i < bodies.size(); ++i) {
        Body& body = bodies[i];
        Pose& pose = poses[i];
        if (!body.character || !pose.collisionEnabled) continue;  // (a destroyed character, or one a script made a ghost)

        // The script moved the object from where the physics left it last frame: that move is what it wants to do.
        glm::vec3 wanted = pose.position - body.state.position;
        if (glm::length(wanted) > kTeleportDistance) {
            body.state.position = pose.position;
            body.state.velocityZ = 0.0f;
            wanted = glm::vec3(0.0f);
        }

        others.clear();
        for (const Shape& s : solids) {
            if (s.id != static_cast<int>(i)) others.push_back(s);  // a character doesn't stop itself
        }
        touched.clear();
        const bool jump = !body.movement.jumpAction.empty() && started && started(i, body.movement.jumpAction);
        moveCharacter(body.state, body.movement.paramsAt(pose.scale), wanted, jump, deltaTime, others, &touched);

        pose.position = body.state.position;
        if (moved) moved->push_back(i);

        if (body.scripted) {
            for (int id : touched) {
                const size_t j = static_cast<size_t>(id);
                if (j < bodies.size() && bodies[j].scripted) blockingNow.insert(Contacts::Pair((std::min)(i, j), (std::max)(i, j)));
            }
        }
    }

    if (!contacts) return;

    // Who is touching whom now: every pair of scripted objects whose shapes meet.
    std::vector<Shape> shapes(bodies.size());
    std::vector<bool> present(bodies.size(), false);
    for (size_t i = 0; i < bodies.size(); ++i) {
        present[i] = bodies[i].scripted && poses[i].collisionEnabled && shapeOfBody(bodies[i], poses[i], static_cast<int>(i), shapes[i]);
    }
    std::set<Contacts::Pair> overlappingNow;
    for (size_t i = 0; i < bodies.size(); ++i) {
        if (!present[i]) continue;
        for (size_t j = i + 1; j < bodies.size(); ++j) {
            if (!present[j]) continue;
            if (glm::distance(shapes[i].center, shapes[j].center) > shapes[i].boundingRadius() + shapes[j].boundingRadius() + kTouchSlop) continue;
            if (shapesOverlap(shapes[i], shapes[j], kTouchSlop)) overlappingNow.insert(Contacts::Pair(i, j));
        }
    }

    contacts->began.clear();
    contacts->ended.clear();
    contacts->hit.clear();
    for (const Contacts::Pair& p : overlappingNow) {
        if (!contacts->overlapping.count(p)) contacts->began.push_back(p);
    }
    for (const Contacts::Pair& p : contacts->overlapping) {
        if (!overlappingNow.count(p)) contacts->ended.push_back(p);
    }
    for (const Contacts::Pair& p : blockingNow) {
        if (!contacts->blocking.count(p)) contacts->hit.push_back(p);
    }
    contacts->overlapping = std::move(overlappingNow);
    contacts->blocking = std::move(blockingNow);
}

} // namespace Physics
} // namespace Propulsion
