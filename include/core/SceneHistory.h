#pragma once

/**
 * SceneHistory.h
 * Duplicating a scene object (the name of the copy, the copy itself), and undo and redo of objects being added to or removed from the scene. Header-only so it can be
 * tested without the editor.
 *
 * A copy is a new instance of the same Object asset, with the same settings (scale, rotation, color, and an overridden mesh
 * or material), under a new name, in the same place as the original. What only exists while playing (script, input mapping,
 * collision switched off by a script, look pitch) is not carried over.
 *
 * The history is a list of steps in `UndoState`: a move, an object added to the scene (created, duplicated), an object removed
 * from it, or an object edited (name, visibility, mesh, material). Steps refer to objects by their position in the scene, and every change to that list is itself a step, undone in the
 * reverse of the order it was made in; so each step meets the scene exactly as it was when the step was made.
 */

#include "Engine.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Propulsion {

/// The name for a copy of `name`: its trailing `_NN` counter (if it has one) is taken off, and the first free `Base_NN` at or
/// above the original's number is used (`Cube_01` -> `Cube_02`, or `Cube_03` if that is taken; `Player` -> `Player_01`).
/// `exists` says whether a name is already used in the scene.
inline std::string copyNameFor(const std::string& name, const std::function<bool(const std::string&)>& exists) {
    std::string base = name;
    int counter = 1;
    size_t digits = name.size();
    while (digits > 0 && std::isdigit(static_cast<unsigned char>(name[digits - 1]))) digits--;
    if (digits < name.size() && digits > 1 && name[digits - 1] == '_') {
        base = name.substr(0, digits - 1);
        counter = std::atoi(name.c_str() + digits) + 1;
    }
    for (;; counter++) {
        const std::string candidate = base + "_" + std::string(counter < 10 ? "0" : "") + std::to_string(counter);
        if (!exists(candidate)) return candidate;
    }
}

/// A copy of `source` named `name`, in the same place.
inline SceneObject copyOfSceneObject(const SceneObject& source, const std::string& name) {
    SceneObject copy = source;
    copy.name = name;
    copy.scriptFile.clear();
    copy.inputMappingId.clear();
    copy.collisionEnabled = true;
    copy.lookPitch = 0.0f;
    return copy;
}

/// Note in the history that `object` was added to the scene at `index` (created, duplicated), when `selectionBefore` was
/// selected. Anything that could have been redone no longer can be.
inline void recordSceneChange(std::vector<UndoState>& undo, std::vector<UndoState>& redo, UndoState::Kind kind, int index, const SceneObject& object, int selectionBefore) {
    UndoState step(index, object.position, object.rotation, object.quaternion, object.scale);
    step.kind = kind;
    step.object = std::make_shared<SceneObject>(object);
    step.selectionBefore = selectionBefore;
    undo.push_back(std::move(step));
    redo.clear();
    const size_t maxUndoSteps = 100;
    if (undo.size() > maxUndoSteps) undo.erase(undo.begin());
}

inline void recordAddedObject(std::vector<UndoState>& undo, std::vector<UndoState>& redo, int index, const SceneObject& object, int selectionBefore) {
    recordSceneChange(undo, redo, UndoState::Kind::Added, index, object, selectionBefore);
}

/// Whether two objects are the same as far as editing goes (everything the Objects panel can change).
inline bool sameSceneObject(const SceneObject& a, const SceneObject& b) {
    return a.name == b.name && a.position == b.position && a.rotation == b.rotation && a.quaternion == b.quaternion && a.scale == b.scale &&
           a.color == b.color && a.visible == b.visible && a.sourceObjectGUID == b.sourceObjectGUID && a.meshGUID == b.meshGUID &&
           a.materialGUID == b.materialGUID && a.overridesMesh == b.overridesMesh && a.overridesMaterial == b.overridesMaterial;
}

/// Note that the object at `index` was edited: `before` is how it was, `now` how it is. Nothing is noted if they are the same.
/// Returns whether a step was made.
inline bool recordObjectEdit(std::vector<UndoState>& undo, std::vector<UndoState>& redo, int index, const SceneObject& before, const SceneObject& now, int selection) {
    if (sameSceneObject(before, now)) return false;
    recordSceneChange(undo, redo, UndoState::Kind::Edited, index, before, selection);
    return true;
}

/// Note that the object at `index` (`object`, as it is) is about to be deleted.
inline void recordRemovedObject(std::vector<UndoState>& undo, std::vector<UndoState>& redo, int index, const SceneObject& object, int selectionBefore) {
    recordSceneChange(undo, redo, UndoState::Kind::Removed, index, object, selectionBefore);
}

namespace detail {

/// Take the object the step is about out of the scene (it must be where the step says, under the name it had). The step is
/// kept, with the object as it is now (moves included), in `other`.
inline bool takeOut(const UndoState& step, std::vector<SceneObject>& scene, std::vector<UndoState>& other) {
    if (!step.object || step.objectIndex < 0 || step.objectIndex >= static_cast<int>(scene.size())) return false;
    if (scene[step.objectIndex].name != step.object->name) return false;
    UndoState kept = step;
    kept.object = std::make_shared<SceneObject>(scene[step.objectIndex]);
    scene.erase(scene.begin() + step.objectIndex);
    other.push_back(std::move(kept));
    return true;
}

/// Put the object the step is about back into the scene where it was, and keep the step in `other`.
inline bool putBack(const UndoState& step, std::vector<SceneObject>& scene, std::vector<UndoState>& other) {
    if (!step.object || step.objectIndex < 0 || step.objectIndex > static_cast<int>(scene.size())) return false;
    scene.insert(scene.begin() + step.objectIndex, *step.object);
    other.push_back(step);
    return true;
}

/// An edit: the object at the step's position is swapped for the one the step holds, and the step keeps the one that was there.
inline bool swapEdit(const UndoState& step, std::vector<SceneObject>& scene, std::vector<UndoState>& other) {
    if (!step.object || step.objectIndex < 0 || step.objectIndex >= static_cast<int>(scene.size())) return false;
    UndoState kept = step;
    kept.object = std::make_shared<SceneObject>(scene[step.objectIndex]);
    scene[step.objectIndex] = *step.object;
    other.push_back(std::move(kept));
    return true;
}

} // namespace detail

/// Undo a step that added, removed or edited an object: an added object is taken out (and what was selected before is selected
/// again), a removed one comes back (selected), an edited one goes back to how it was (selected). The step moves to `redo`. False, and nothing changed, if the scene is not as the step
/// expects, or it is a move.
inline bool undoSceneChange(const UndoState& step, std::vector<SceneObject>& scene, int& selected, std::vector<UndoState>& redo) {
    if (step.kind == UndoState::Kind::Added) {
        if (!detail::takeOut(step, scene, redo)) return false;
        selected = step.selectionBefore < static_cast<int>(scene.size()) ? step.selectionBefore : -1;
        return true;
    }
    if (step.kind == UndoState::Kind::Removed) {
        if (!detail::putBack(step, scene, redo)) return false;
        selected = step.objectIndex;
        return true;
    }
    if (step.kind == UndoState::Kind::Edited) {
        if (!detail::swapEdit(step, scene, redo)) return false;
        selected = step.objectIndex;  // so the change can be seen
        return true;
    }
    return false;
}

/// Redo it: an added object comes back (selected), a removed one goes again (nothing selected). The step moves to `undo`.
inline bool redoSceneChange(const UndoState& step, std::vector<SceneObject>& scene, int& selected, std::vector<UndoState>& undo) {
    if (step.kind == UndoState::Kind::Added) {
        if (!detail::putBack(step, scene, undo)) return false;
        selected = step.objectIndex;
        return true;
    }
    if (step.kind == UndoState::Kind::Removed) {
        if (!detail::takeOut(step, scene, undo)) return false;
        selected = -1;
        return true;
    }
    if (step.kind == UndoState::Kind::Edited) {
        if (!detail::swapEdit(step, scene, undo)) return false;
        selected = step.objectIndex;
        return true;
    }
    return false;
}

} // namespace Propulsion
