// Standard headers first: some engine headers pull in <windows.h>, whose min/max macros break them.
#include "ui/ImGuiTheme.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <set>
#include <fstream>
#include <sstream>

#include "scripting/ScriptGraphEditor.h"

#include "platform/FileDialogs.h"

#include "propulsion_ffi.h"

namespace Propulsion {

namespace fs = std::filesystem;

namespace {

constexpr float kNodeWidth = 12.0f;
constexpr float kValueNodeWidth = 10.0f;
constexpr float kDuplicateOffset = 2.0f;  // how far down and to the right a copy is put from its original (grid units)

ImU32 headerColorFor(const std::string& cls) {
    if (cls == "Event") return IM_COL32(190, 55, 55, 255);
    if (cls == "Flow") return IM_COL32(195, 125, 45, 255);
    if (cls == "Data") return IM_COL32(55, 145, 105, 255);
    return IM_COL32(55, 100, 190, 255);  // Action
}

/// Run a library call that returns an owned string (or null) and an error string.
std::string takeString(char* s) {
    std::string out = s ? s : "";
    propulsion_string_free(s);
    return out;
}

} // namespace

ScriptGraphEditor::ScriptGraphEditor(std::string absolutePath, Log log) : m_path(std::move(absolutePath)), m_log(std::move(log)) {
    // What can be added
    std::string catalogText = takeString(propulsion_graph_catalog());
    Json::Value catalog;
    std::string error;
    if (Json::parse(catalogText, catalog, error)) {
        for (size_t i = 0; i < catalog.size(); i++) {
            const Json::Value& c = catalog.at(i);
            m_catalog.push_back({c["name"].str(), c["class"].str(), c["category"].str(), c["pattern"].str(), c["produces"].str()});
        }
    }

    ScriptGraphHooks hooks;
    hooks.onPinEdited = [this](ScriptNode& node, ScriptPinField& field, const std::string& text) {
        std::string key = node.key, pin = field.name;
        m_pending.push_back([this, key, pin, text]() { edit("set_pin", key, pin, text); });
    };
    hooks.onConnectExec = [this](ScriptNode& from, int fromPinId, ScriptNode& to) {
        if (&from == &to) return;
        std::string parent;
        int slot = 0, index = 0;
        if (!execTarget(from, fromPinId, parent, slot, index)) return;
        std::string key = to.key;
        m_pending.push_back([this, key, parent, slot, index]() { edit("move", key, parent, std::to_string(slot), std::to_string(index)); });
    };
    hooks.onDeleteNodes = [this](const std::vector<ScriptNode*>& nodes) {
        std::vector<std::string> keys;
        for (const ScriptNode* n : nodes) keys.push_back(n->key);
        m_pending.push_back([this, keys]() { deleteNodes(keys); });
    };
    hooks.onDuplicateNodes = [this](const std::vector<ScriptNode*>& nodes) {
        std::vector<std::string> keys;
        for (const ScriptNode* n : nodes) keys.push_back(n->key);
        m_pending.push_back([this, keys]() { duplicateNodes(keys); });
    };
    hooks.onNodesMoved = [this](const std::vector<ScriptNode*>& moved) {
        // A Get node is not in the script: it only remembers where it was put.
        bool inScript = false;
        for (ScriptNode* n : moved) {
            if (!n->isGetter) {
                inScript = true;
                continue;
            }
            for (Getter& g : m_getters) {
                if ("getter:" + std::to_string(g.id) == n->key) g.pos = n->position;
            }
        }
        if (inScript) m_pending.push_back([this]() { commitLayout(); });
    };
    hooks.onConnectGetter = [this](ScriptNode& getter, ScriptNode& to, int toPinId) { plugGetter(getter, to, toPinId); };
    hooks.onConnectValue = [this](ScriptNode& value, ScriptNode& to, int toPinId) {
        if (to.isData()) return;
        const ScriptPinField* field = nullptr;
        for (const ScriptPinField& f : to.fields) {
            if (f.pinId == toPinId) field = &f;
        }
        if (!field || !field->wiredKey.empty() || field->kind == "choice" || field->kind == "flag") return;
        const std::string valueKey = value.key, key = to.key, pin = field->name;
        m_pending.push_back([this, valueKey, key, pin]() { edit("adopt_value", valueKey, key, pin); });
    };
    hooks.onDropVariableOnPin = [this](ScriptNode& node, ScriptPinField& field, const std::string& variable) {
        const std::string key = node.key, pin = field.name;
        m_pending.push_back([this, key, pin, variable]() { edit("set_pin", key, pin, variable); });
    };
    hooks.onDeleteConnection = [this](ScriptNode& from, ScriptNode& to, int toPinId) {
        const ScriptPinField* field = nullptr;
        for (const ScriptPinField& f : to.fields) {
            if (f.pinId == toPinId) field = &f;
        }
        if (!field || field->wiredKey.empty()) return;
        // Cutting the wire unplugs the pin. A Get node is only a view of the variable's name, so it is kept:
        // it goes back to being a loose Get node where it was, ready to be wired somewhere else. A computed
        // value node is replaced by a plain value, as when it is deleted.
        const std::string valueKey = field->wiredKey;
        const bool isGet = from.category == "Variables";
        const std::string variable = isGet ? from.name.substr(4) : std::string();
        const ImVec2 where = from.position;
        // A Get node still wired to other pins stays as it is; only the last wire leaves a loose one behind.
        int pins = 1;  // the node drawn is for its own pin, and for every pin shared with it
        for (const auto& shared : m_shared) {
            if (shared.second == from.key) pins++;
        }
        const bool stillWired = pins > 1;
        m_pending.push_back([this, valueKey, isGet, variable, where, stillWired]() {
            if (edit("unplug", valueKey) && isGet && !stillWired) addGetter(variable, where);
        });
    };
    hooks.onDropVariable = [this](const std::string& variable, ImVec2 gridPos) {
        m_pending.push_back([this, variable, gridPos]() { addGetter(variable, gridPos); });
    };
    hooks.onSelected = [this](ScriptNode* node) {
        m_selectedKey = node ? node->key : std::string();
        if (node) {
            m_focus = Focus::Node;
            m_selectedVar.clear();
            m_selectedRef.clear();
            m_selectedComponent.clear();
        } else if (m_focus == Focus::Node) {
            m_focus = Focus::None;
        }
    };
    hooks.drawMenu = [this](const ScriptMenuContext& ctx) { drawMenu(ctx); };
    hooks.searchResults = [this](const ScriptMenuContext& ctx) { return searchResults(ctx); };
    m_graph.setScriptHooks(std::move(hooks));

    reload();
}

// ---------------------------------------------------------------------------------------------
// Talking to the library
// ---------------------------------------------------------------------------------------------

void ScriptGraphEditor::refreshScripts() {
    m_scripts.clear();
    std::error_code ec;
    const fs::path self(m_path), dir = self.parent_path();
    int seen = 0;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        if (++seen > 4000) break;
        std::error_code ignore;
        if (!it->is_regular_file(ignore) || it->path().extension() != ".prop") continue;
        if (fs::equivalent(it->path(), self, ignore)) continue;
        m_scripts.push_back(fs::relative(it->path(), dir, ignore).generic_string());
    }
    std::sort(m_scripts.begin(), m_scripts.end());
}

std::string ScriptGraphEditor::addReference(const std::string& kind, const std::string& file) {
    auto taken = [&](const std::string& name) {
        for (const VariableInfo& v : m_vars) {
            if (v.name == name) return true;
        }
        for (const ReferenceInfo& r : m_refs) {
            if (r.name == name) return true;
        }
        return false;
    };
    std::string base = kind == "entity" ? "Entity" : "Entities";
    if (kind == "file") {
        if (file.empty()) return "";
        // The script's file name, with a capital: coin.prop -> Coin
        std::string stem = fs::path(file).stem().string();
        base = "Script";
        for (char& c : stem) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') c = '_';
        }
        if (!stem.empty() && !std::isdigit(static_cast<unsigned char>(stem[0]))) {
            stem[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(stem[0])));
            base = stem;
        }
    }
    std::string name = base;
    for (int n = 2; taken(name) && n < 200; n++) name = base + std::to_string(n);

    std::string tag = name;
    for (char& c : tag) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string decl;
    if (kind == "entity") {
        // `needs entity` has to say which script the entity runs: another script if there is one.
        const std::string target = !file.empty() ? file : (m_scripts.empty() ? fs::path(m_path).filename().generic_string() : m_scripts.front());
        decl = "needs entity:" + name + " file:\"" + target + "\" tag:\"" + tag + "\"";
    } else if (kind == "entities") {
        decl = "needs entities:" + name + " tag:\"" + tag + "\"" + (file.empty() ? "" : " file:\"" + file + "\"");
    } else {
        decl = "needs file:\"" + file + "\" as " + name;
    }
    if (!edit("add_reference", decl)) return "";
    m_selectedRef = name;
    m_selectedVar.clear();
    m_selectedComponent.clear();
    m_selectedKey.clear();
    m_graph.clearSelection();
    m_focus = Focus::Reference;
    m_focusVarName = true;
    return name;
}

void ScriptGraphEditor::reload() {
    refreshScripts();
    char* error = nullptr;
    char* json = propulsion_graph_load(m_path.c_str(), &error);
    if (!json) {
        m_message = takeString(error);
        if (m_message.empty()) m_message = "The script could not be read.";
        std::error_code ec;
        m_fileTime = fs::last_write_time(m_path, ec);
        refreshCode(false);  // the code pane still shows what is on disk
        return;
    }
    propulsion_string_free(error);
    m_message.clear();
    adopt(takeString(json));
}

// ---------------------------------------------------------------------------------------------
// Undo
// ---------------------------------------------------------------------------------------------
//
// The script text is the source of truth, and the layout file is the only other thing a graph edit
// changes, so undo is just putting both files back as they were. An edit (or a group of edits, like
// deleting ten nodes) first notes what the two files held, and if they differ afterwards that is one
// step.

static std::string readWhole(const std::string& path, bool& found) {
    std::ifstream in(path, std::ios::binary);
    found = in.is_open();
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// ---------------------------------------------------------------------------------------------
// The code pane's text
// ---------------------------------------------------------------------------------------------
//
// The file is the truth and the pane's buffer follows it: whenever the file has changed since the pane last
// looked, the buffer becomes the file's text. The exception is the pane's own write, which changed the file
// to exactly what the buffer holds, so the cursor and the undo history stay. Text typed and not yet written
// survives anything that leaves the file's text alone (selecting, moving nodes), and is dropped by anything
// that changes it.

void ScriptGraphEditor::refreshCode(bool matchesModel) {
    bool found = false;
    const std::string text = CodeBuffer::normalize(readWhole(m_path, found));
    m_codeFileOk = matchesModel && found;

    if (text != m_codeApplied) {
        if (text != m_code.text()) {
            if (m_codeDirty && m_log) m_log(LogCategory::ScriptGraph, LogLevel::Warning, "Code typed in the pane was dropped: the script was changed some other way.");
            m_code.setText(text);
        }
        m_codeApplied = text;
        m_codeDirty = false;
        m_codeHasError = false;
        m_codeSeenRevision = m_code.revision();
        m_codeFollowCursor = false;
    }
    // A file the graph can't show: mark where it stops, so it can be fixed here.
    if (!m_codeFileOk && !m_codeDirty) checkCode();
}

void ScriptGraphEditor::checkCode() {
    m_codeHasError = false;
    const std::string now = m_code.text();
    int line = 0, col = 0;
    char* message = nullptr;
    if (propulsion_graph_check_source(now.c_str(), &line, &col, &message)) {
        propulsion_string_free(message);
        return;
    }
    m_codeHasError = true;
    m_codeError.line = line;
    m_codeError.col = col;
    m_codeError.message = takeString(message);
}

void ScriptGraphEditor::codeChanged() {
    m_codeSeenRevision = m_code.revision();
    m_codeDirty = m_code.text() != m_codeApplied;
    m_codeEditedAt = ImGui::GetTime();
    m_codeHasError = false;
    if (m_codeDirty || !m_codeFileOk) checkCode();
}

bool ScriptGraphEditor::applyCode() {
    if (!m_codeDirty || m_codeHasError) return false;
    const unsigned revision = m_code.revision();
    m_applyingCode = true;
    const bool ok = edit("set_source", m_code.text());
    m_applyingCode = false;
    if (!ok) m_codeFailedRevision = revision;  // the file would not take it: don't ask again until the text changes
    return ok;
}

void ScriptGraphEditor::flushCode() {
    if (!m_applyingCode && m_codeDirty && !m_codeHasError) applyCode();
}

void ScriptGraphEditor::indexCode() {
    if (m_codeIndexedRevision == m_code.revision()) return;
    m_codeIndexedRevision = m_code.revision();
    const std::vector<std::string>& lines = m_code.lines();
    m_codeSection.assign(lines.size(), 0);
    m_codeLongest = 0;
    unsigned char section = 0;
    size_t longest = 0;
    for (size_t i = 0; i < lines.size(); i++) {
        const std::string& line = lines[i];
        if (line.rfind("header:", 0) == 0) section = 1;
        else if (line.rfind("implement:", 0) == 0) section = 2;
        m_codeSection[i] = section;
        // The longest line, counting a tab as the columns the pane draws it with.
        size_t tabs = 0;
        while (tabs < line.size() && line[tabs] == '\t') tabs++;
        const size_t width = tabs * kCodeTabColumns + (line.size() - tabs);
        if (width > longest) {
            longest = width;
            m_codeLongest = i;
        }
    }
}

ScriptGraphEditor::State ScriptGraphEditor::capture() const {
    State s;
    bool found = false;
    s.script = readWhole(m_path, found);
    s.layout = readWhole(layoutFile(), s.hasLayout);
    return s;
}

bool ScriptGraphEditor::restore(const State& state) {
    {
        std::ofstream out(m_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            m_message = "Could not write " + m_path;
            return false;
        }
        out << state.script;
    }
    std::error_code ec;
    if (state.hasLayout) {
        std::ofstream(layoutFile(), std::ios::binary | std::ios::trunc) << state.layout;
    } else {
        fs::remove(layoutFile(), ec);
    }
    m_graph.clearSelection();
    reload();
    markDirty();
    return true;
}

void ScriptGraphEditor::beginGroup() {
    if (m_groupDepth++ == 0) m_groupBefore = capture();
}

void ScriptGraphEditor::endGroup() {
    if (--m_groupDepth > 0) return;
    State after = capture();
    if (after == m_groupBefore) return;
    m_undo.push_back(std::move(m_groupBefore));
    if (m_undo.size() > 100) m_undo.erase(m_undo.begin());
    m_redo.clear();
}

bool ScriptGraphEditor::undo() {
    if (m_undo.empty() || m_groupDepth != 0) return false;
    State now = capture();
    State previous = std::move(m_undo.back());
    m_undo.pop_back();
    if (!restore(previous)) {
        m_undo.push_back(std::move(previous));
        return false;
    }
    m_redo.push_back(std::move(now));
    return true;
}

bool ScriptGraphEditor::redo() {
    if (m_redo.empty() || m_groupDepth != 0) return false;
    State now = capture();
    State next = std::move(m_redo.back());
    m_redo.pop_back();
    if (!restore(next)) {
        m_redo.push_back(std::move(next));
        return false;
    }
    m_undo.push_back(std::move(now));
    return true;
}

// ---------------------------------------------------------------------------------------------
// Edits
// ---------------------------------------------------------------------------------------------

bool ScriptGraphEditor::edit(const char* op, const std::string& a1, const std::string& a2, const std::string& a3, const std::string& a4) {
    flushCode();  // text typed in the code pane that parses goes in first, so this edit builds on it
    beginGroup();
    const bool hadLayout = m_hasSavedLayout;
    const bool ok = editRaw(op, a1, a2, a3, a4);
    // Once nodes have been placed by hand every node keeps its place, so a node added or made by an
    // edit (and the ones that follow it) are pinned where they are shown rather than re-arranged later.
    if (ok && hadLayout && std::string(op) != "set_positions") freezeLayout();
    endGroup();
    return ok;
}

void ScriptGraphEditor::freezeLayout() {
    bool anyUnplaced = false;
    const Json::Value& nodes = m_model["nodes"];
    for (size_t i = 0; i < nodes.size(); i++) {
        if (nodes.at(i)["pos"].isNull()) anyUnplaced = true;
    }
    if (!anyUnplaced) return;
    commitLayout();
}

void ScriptGraphEditor::commitLayout() {
    beginGroup();
    std::string payload;
    const Json::Value& nodes = m_model["nodes"];
    for (size_t i = 0; i < nodes.size(); i++) {
        // The model's own key: a Get node drawn for several pins is saved under each of them.
        const std::string key = nodes.at(i)["id"].str();
        ScriptNode* n = nodeByKey(key);
        if (!n) continue;
        const int x = static_cast<int>(std::lround(n->position.x));
        const int y = static_cast<int>(std::lround(n->position.y));
        n->position = ImVec2(static_cast<float>(x), static_cast<float>(y));
        payload += key + "\t" + std::to_string(x) + "\t" + std::to_string(y) + "\n";
    }
    if (!payload.empty()) editRaw("set_positions", payload, "", "", "");
    endGroup();
}

void ScriptGraphEditor::resetLayout() {
    beginGroup();
    // Only the script's nodes are arranged again: the ones that are not connected stay where they were put.
    editRaw("reset_layout", "", "", "", "");
    m_needsFit = true;
    endGroup();
}

void ScriptGraphEditor::addAt(const std::string& name, const std::string& parent, int slot, int index, bool useSpawn, ImVec2 spawn) {
    flushCode();
    beginGroup();
    const bool hadLayout = m_hasSavedLayout;
    int handlers = 0;
    for (const auto& entry : m_byKey) {
        if (entry.second->isEvent() && !entry.second->floating) handlers++;
    }
    bool isEvent = false;
    for (const CatalogEntry& e : m_catalog) {
        if (e.name == name && e.cls == "Event") isEvent = true;
    }
    if (editRaw("add", name, parent, std::to_string(slot), std::to_string(index))) {
        const std::string key = isEvent ? "h" + std::to_string(handlers) : parent + "." + std::to_string(slot) + "." + std::to_string(index);
        if (useSpawn) {
            if (ScriptNode* n = nodeByKey(key)) n->position = spawn;
        }
        if (hadLayout || useSpawn) commitLayout();
    }
    endGroup();
}

void ScriptGraphEditor::addFloating(const std::string& name, ImVec2 spawn) {
    flushCode();
    beginGroup();
    const int x = static_cast<int>(std::lround(spawn.x)), y = static_cast<int>(std::lround(spawn.y));
    editRaw("add_floating", name, std::to_string(x), std::to_string(y), "");
    endGroup();
}

bool ScriptGraphEditor::deleteNodes(const std::vector<std::string>& allKeys) {
    flushCode();
    // Get nodes are not in the script: they just go.
    std::vector<std::string> keys;
    for (const std::string& k : allKeys) {
        if (k.rfind("getter:", 0) == 0) {
            removeGetter(k);
            continue;
        }
        keys.push_back(k);
        // A Get node drawn for several pins unplugs all of them.
        for (const auto& shared : m_shared) {
            if (shared.second == k && std::find(allKeys.begin(), allKeys.end(), shared.first) == allKeys.end()) keys.push_back(shared.first);
        }
    }
    auto isValue = [](const std::string& k) { return k.find(".v.") != std::string::npos; };
    auto ownerOf = [](const std::string& k) { return k.substr(0, k.find(".v.")); };

    std::set<std::string> statements;
    for (const std::string& k : keys) {
        if (!isValue(k)) statements.insert(k);
    }
    // A node whose ancestor is going too needs no deleting of its own (and would refuse: it won't be there).
    auto insideAnother = [&](const std::string& k) {
        for (const std::string& a : statements) {
            if (a != k && k.size() > a.size() && k.compare(0, a.size(), a) == 0 && k[a.size()] == '.') return true;
        }
        return false;
    };

    std::vector<std::string> values, doomed;
    for (const std::string& k : keys) {
        if (isValue(k)) {
            const std::string owner = ownerOf(k);
            if (!statements.count(owner) && !insideAnother(owner)) values.push_back(k);
        } else if (!insideAnother(k)) {
            doomed.push_back(k);
        }
    }
    // Later nodes first, so that deleting one never changes the key of one still to be deleted.
    auto path = [](const std::string& k) {
        std::vector<int> p;
        std::stringstream ss(k.substr(1));
        std::string part;
        while (std::getline(ss, part, '.')) p.push_back(std::atoi(part.c_str()));
        return p;
    };
    std::sort(doomed.begin(), doomed.end(), [&](const std::string& a, const std::string& b) { return path(a) > path(b); });

    beginGroup();
    std::string firstProblem;
    for (const std::string& k : values) {
        if (!editRaw("unplug", k, "", "", "") && firstProblem.empty()) firstProblem = m_message;
    }
    for (const std::string& k : doomed) {
        if (!editRaw("delete", k, "", "", "") && firstProblem.empty()) firstProblem = m_message;
    }
    if (m_hasSavedLayout) freezeLayout();
    endGroup();
    m_message = firstProblem;
    return firstProblem.empty();
}

bool ScriptGraphEditor::duplicateNodes(const std::vector<std::string>& keys) {
    flushCode();
    // What each copy needs, read now: the graph is rebuilt after every edit, and the nodes with it.
    struct Copy {
        std::string key, variable;
        ImVec2 at;
    };
    std::vector<Copy> copies;
    for (const std::string& key : keys) {
        const ScriptNode* n = nodeByKey(key);
        if (!n) continue;
        Copy c;
        c.key = n->key;
        // A Get node only carries a variable's name, so its copy is another Get node (which is not in the script either).
        if (n->carriesVariable()) c.variable = n->name.substr(4);
        c.at = ImVec2(std::round(n->position.x) + kDuplicateOffset, std::round(n->position.y) + kDuplicateOffset);
        copies.push_back(c);
    }

    beginGroup();
    std::string firstProblem;
    for (const Copy& c : copies) {
        if (!c.variable.empty()) {
            addGetter(c.variable, c.at);
        } else if (!editRaw("duplicate", c.key, std::to_string(static_cast<int>(c.at.x)), std::to_string(static_cast<int>(c.at.y)), "") && firstProblem.empty()) {
            firstProblem = m_message;
        }
    }
    endGroup();
    m_message = firstProblem;
    return firstProblem.empty();
}

bool ScriptGraphEditor::editRaw(const char* op, const std::string& a1, const std::string& a2, const std::string& a3, const std::string& a4) {
    char* error = nullptr;
    char* json = propulsion_graph_edit(m_path.c_str(), op, a1.c_str(), a2.c_str(), a3.c_str(), a4.c_str(), &error);
    if (!json) {
        std::string why = takeString(error);
        m_message = "Not changed: " + (why.empty() ? std::string("that edit isn't possible") : why);
        if (m_log) m_log(LogCategory::ScriptGraph, LogLevel::Warning, m_message);
        return false;
    }
    propulsion_string_free(error);
    m_message.clear();
    adopt(takeString(json));
    markDirty();
    return true;
}

void ScriptGraphEditor::markDirty() {
    if (m_compile.done) m_compile.stale = true;
}

void ScriptGraphEditor::compile() {
    flushCode();
    reload();  // from the file as it is now
    m_compile = CompileResult();
    m_compile.done = true;
    if (!m_message.empty()) {
        // The file can't even be read as a script.
        Problem p;
        p.message = m_message;
        m_compile.problems.push_back(p);
    } else {
        m_compile.problems = m_liveProblems;
    }
    for (const Problem& p : m_compile.problems) (p.error ? m_compile.errors : m_compile.warnings)++;
    m_resultsOpen = true;
    if (m_log) {
        const std::string name = fs::path(m_path).filename().string();
        if (m_compile.errors == 0) {
            m_log(LogCategory::Compile, LogLevel::Info, name + ": compiled" + (m_compile.warnings ? " with " + std::to_string(m_compile.warnings) + " warning(s)" : std::string()));
        } else {
            m_log(LogCategory::Compile, LogLevel::Error, name + ": FAILED, " + std::to_string(m_compile.errors) + " error(s); Play will not start until they are fixed");
        }
        for (const Problem& p : m_compile.problems) m_log(LogCategory::Compile, p.error ? LogLevel::Error : LogLevel::Warning, name + ": " + p.message);
    }
}

// ---------------------------------------------------------------------------------------------
// Get nodes (variables carried to a field)
// ---------------------------------------------------------------------------------------------

ScriptNode* ScriptGraphEditor::makeGetterNode(int id, const std::string& variable, ImVec2 pos) {
    auto node = std::make_unique<ScriptNode>(m_graph.getNextNodeId(), "Get " + variable, pos);
    node->key = "getter:" + std::to_string(id);
    node->nodeClass = "Data";
    node->category = "Variables";
    node->isGetter = true;
    node->comment = "Drag from the output pin onto a field to plug this variable in.";
    node->size.x = kValueNodeWidth;
    node->headerColor = IM_COL32(55, 145, 105, 255);
    node->addOutput("Value", PinType::Any, m_graph.getNextPinId());
    ScriptNode* raw = node.get();
    m_byKey[raw->key] = raw;
    m_graph.addNode(std::move(node));
    return raw;
}

const std::vector<ScriptGraphEditor::Fit>& ScriptGraphEditor::fitsFor(const std::string& variable) {
    auto cached = m_fits.find(variable);
    if (cached != m_fits.end()) return cached->second;
    std::vector<Fit>& fits = m_fits[variable];
    char* error = nullptr;
    char* json = propulsion_graph_fits(m_path.c_str(), variable.c_str(), &error);
    if (!json) {
        takeString(error);
        return fits;
    }
    propulsion_string_free(error);
    Json::Value list;
    std::string parseError;
    if (Json::parse(takeString(json), list, parseError)) {
        for (size_t i = 0; i < list.size(); i++) {
            const Json::Value& f = list.at(i);
            fits.push_back({f["node"].str(), f["pin"].str(), f["category"].str(), f["text"].str()});
        }
    }
    return fits;
}

void ScriptGraphEditor::addNodeFor(const ScriptNode& getter, const Fit& fit, ImVec2 spawn) {
    // A statement has to be somewhere in the script, so it goes at the end of the last handler (a handler is
    // made if there is none); where it is drawn is where the wire was let go.
    int handlers = 0, last = -1;
    for (const auto& entry : m_byKey) {
        const ScriptNode* n = entry.second;
        if (!n->isEvent() || n->floating) continue;
        handlers++;
        if (n->name != "Define Action" && n->key.size() > 1 && n->key[0] == 'h') last = (std::max)(last, std::atoi(n->key.c_str() + 1));
    }
    std::string host = last >= 0 ? "h" + std::to_string(last) : std::string();
    int index = 0;
    for (const auto& entry : m_byKey) {
        const ScriptNode* n = entry.second;
        if (!host.empty() && n->parentKey == host && n->slot == 0 && !n->isData()) index++;
    }

    const bool reused = !getter.isGetter;
    const std::string getterKey = getter.key;
    const ImVec2 at = getter.position;
    const int x = static_cast<int>(std::lround(at.x)), y = static_cast<int>(std::lround(at.y));
    // A pin given a phrase (a condition: `Alive is true`) gets a Compare node, which the Get node can't be wired into.
    const std::string variable = getter.name.substr(4);
    const bool computed = fit.text != variable && fit.text != variable + " is true";
    m_pending.push_back([this, host, index, handlers, fit, spawn, reused, getterKey, x, y, computed]() mutable {
        beginGroup();
        if (host.empty()) {
            if (!editRaw("add", "On Begin Play", "", "0", "0")) {
                endGroup();
                return;
            }
            host = "h" + std::to_string(handlers);
        }
        addAt(fit.node, host, 0, index, true, spawn);
        const std::string key = host + ".0." + std::to_string(index);
        if (edit("set_pin", key, fit.pin, fit.text)) {
            if (!reused || !computed) {
                editRaw("set_positions", key + ".v." + fit.pin + "\t" + std::to_string(x) + "\t" + std::to_string(y) + "\n", "", "", "");
            }
            if (!reused) removeGetter(getterKey);
        }
        endGroup();
    });
}

void ScriptGraphEditor::plugGetter(ScriptNode& getter, ScriptNode& to, int toPinId) {
    const ScriptPinField* field = nullptr;
    for (const ScriptPinField& f : to.fields) {
        if (f.pinId == toPinId) field = &f;
    }
    if (!field || field->kind == "choice" || field->kind == "flag" || (!field->wiredKey.empty() && !field->carried)) return;
    // A Get node already wired into a pin stays where it is and keeps its wire: plugging it into a second pin
    // puts the variable's name there too, and saves that pin's value node at the same place, so that the two
    // are drawn as one node with two wires (in the script each pin only names the variable).
    const bool reused = !getter.isGetter;
    const std::string getterKey = getter.key, variable = getter.name.substr(4), key = to.key, pin = field->name;
    const ImVec2 at = getter.position;
    const int x = static_cast<int>(std::lround(at.x)), y = static_cast<int>(std::lround(at.y));
    // The variable goes into the pin, and the Get node stays: it becomes a node of the graph at the same
    // place, wired to the pin, kept in the layout file (the script itself only says the variable's name).
    m_pending.push_back([this, getterKey, variable, key, pin, x, y, reused]() {
        beginGroup();
        if (edit("set_pin", key, pin, variable)) {
            editRaw("set_positions", key + ".v." + pin + "\t" + std::to_string(x) + "\t" + std::to_string(y) + "\n", "", "", "");
            if (!reused) removeGetter(getterKey);
        }
        endGroup();
    });
}

void ScriptGraphEditor::addGetter(const std::string& variable, ImVec2 gridPos) {
    bool declared = false;
    for (const VariableInfo& v : m_vars) declared = declared || v.name == variable;
    for (const ReferenceInfo& r : m_refs) declared = declared || (r.name == variable && r.kind != "file");
    if (!declared) return;
    Getter g;
    g.id = m_nextGetter++;
    g.variable = variable;
    g.pos = gridPos;
    m_getters.push_back(g);
    makeGetterNode(g.id, variable, gridPos);
}

void ScriptGraphEditor::removeGetter(const std::string& key) {
    m_getters.erase(std::remove_if(m_getters.begin(), m_getters.end(), [&](const Getter& g) { return "getter:" + std::to_string(g.id) == key; }), m_getters.end());
    if (ScriptNode* n = nodeByKey(key)) {
        const int id = n->id;
        m_byKey.erase(key);
        m_graph.removeNode(id);
    }
}

void ScriptGraphEditor::adopt(const std::string& json) {
    Json::Value model;
    std::string error;
    if (!Json::parse(json, model, error)) {
        m_message = "The script library sent something unreadable: " + error;
        return;
    }
    std::error_code ec;
    m_fileTime = fs::last_write_time(m_path, ec);
    m_model = std::move(model);
    m_fits.clear();

    m_vars.clear();
    for (size_t i = 0; i < m_model["variables"].size(); i++) {
        const Json::Value& v = m_model["variables"].at(i);
        VariableInfo info;
        info.name = v["name"].str();
        info.kind = v["kind"].str("variable");
        info.type = v["type"].str();
        info.value = v["value"].str();
        info.parts = v["parts"].str();
        info.text = v["text"].str();
        info.exposed = v["exposed"].boolean();
        m_vars.push_back(std::move(info));
    }
    m_locals.clear();
    for (size_t i = 0; i < m_model["locals"].size(); i++) {
        const Json::Value& l = m_model["locals"].at(i);
        LocalInfo info;
        info.name = l["name"].str();
        info.type = l["type"].str();
        info.node = l["node"].str();
        info.scope = l["scope"].str();
        info.line = l["line"].integer();
        m_locals.push_back(std::move(info));
    }
    m_types.clear();
    for (size_t i = 0; i < m_model["types"].size(); i++) {
        const Json::Value& t = m_model["types"].at(i);
        TypeInfo info;
        info.name = t["name"].str();
        info.kind = t["kind"].str();
        for (size_t k = 0; k < t["values"].size(); k++) info.values.push_back(t["values"].at(k).str());
        for (size_t k = 0; k < t["fields"].size(); k++) info.fields.emplace_back(t["fields"].at(k)["name"].str(), t["fields"].at(k)["type"].str());
        m_types.push_back(std::move(info));
    }
    m_refs.clear();
    for (size_t i = 0; i < m_model["references"].size(); i++) {
        const Json::Value& r = m_model["references"].at(i);
        ReferenceInfo info;
        info.name = r["name"].str();
        info.kind = r["kind"].str();
        info.file = r["file"].str();
        info.tag = r["tag"].str();
        info.when = r["when"].str();
        info.text = r["text"].str();
        m_refs.push_back(std::move(info));
    }
    m_liveProblems.clear();
    for (size_t i = 0; i < m_model["problems"].size(); i++) {
        const Json::Value& jp = m_model["problems"].at(i);
        Problem p;
        p.error = jp["severity"].str() != "warning";
        p.message = jp["message"].str();
        p.node = jp["node"].str();
        p.variable = jp["variable"].str();
        p.line = jp["line"].integer();
        m_liveProblems.push_back(std::move(p));
    }
    // Which line each node is written on, for the code pane. A value node (a phrase plugged into a pin)
    // is written on its statement's line, and only the statement or event itself owns that line.
    m_nodeLine.clear();
    m_lineNode.clear();
    for (size_t i = 0; i < m_model["nodes"].size(); i++) {
        const Json::Value& jn = m_model["nodes"].at(i);
        const int line = static_cast<int>(jn["line"].integer());
        if (line <= 0) continue;
        m_nodeLine[jn["id"].str()] = line;
        if (jn["feeds"].isNull()) m_lineNode[line] = jn["id"].str();
    }
    refreshCode(true);

    m_needs.clear();
    for (size_t i = 0; i < m_model["components"].size(); i++) m_needs.push_back(m_model["components"].at(i).str());
    m_problems.clear();
    for (size_t i = 0; i < m_model["errors"].size(); i++) {
        const Json::Value& e = m_model["errors"].at(i);
        m_problems.emplace_back(e["line"].integer(), e["message"].str());
    }

    // Whether any node has a saved position. When the layout has just been thrown away (reset, or an
    // undo back to before it was saved) the nodes are arranged automatically again.
    bool hasSaved = false;
    for (size_t i = 0; i < m_model["nodes"].size(); i++) {
        if (!m_model["nodes"].at(i)["pos"].isNull() && !m_model["nodes"].at(i)["floating"].boolean()) hasSaved = true;
    }
    const bool layoutThrownAway = m_hasSavedLayout && !hasSaved;
    m_hasSavedLayout = hasSaved;

    if (layoutThrownAway || !updateInPlace(m_model)) rebuild(m_model);
    m_loaded = true;
}

// ---------------------------------------------------------------------------------------------
// Building the graph
// ---------------------------------------------------------------------------------------------

static std::string signatureOf(const Json::Value& model) {
    std::string s;
    const Json::Value& nodes = model["nodes"];
    for (size_t i = 0; i < nodes.size(); i++) {
        const Json::Value& n = nodes.at(i);
        s += n["id"].str() + "|" + n["name"].str() + (n["floating"].boolean() ? "|f|" : "|");
        for (size_t k = 0; k < n["execs"].size(); k++) s += n["execs"].at(k)["label"].str() + ">" + n["execs"].at(k)["to"].str() + ",";
        s += "|";
        for (size_t k = 0; k < n["pins"].size(); k++) s += n["pins"].at(k)["name"].str() + (n["pins"].at(k)["wired"].isNull() ? "" : "~") + ",";
        s += ";";
    }
    return s;
}

bool ScriptGraphEditor::updateInPlace(const Json::Value& model) {
    if (!m_loaded || signatureOf(model) != m_signature) return false;
    const Json::Value& nodes = model["nodes"];
    for (size_t i = 0; i < nodes.size(); i++) {
        const Json::Value& jn = nodes.at(i);
        ScriptNode* node = nodeByKey(jn["id"].str());
        if (!node) return false;
        node->error = jn["error"].str();
        node->comment = jn["comment"].str();
        const Json::Value& pos = jn["pos"];
        if (!pos.isNull() && pos.size() == 2) node->position = ImVec2(static_cast<float>(pos.at(0).integer()), static_cast<float>(pos.at(1).integer()));
        for (size_t k = 0; k < node->fields.size() && k < jn["pins"].size(); k++) {
            ScriptPinField& f = node->fields[k];
            f.text = jn["pins"].at(k)["text"].str();
            // The dropdown follows the header: a variable added or deleted changes what it offers.
            f.picks = jn["pins"].at(k)["picks"].boolean();
            f.toggle = jn["pins"].at(k)["toggle"].boolean();
            f.choices.clear();
            for (size_t c = 0; c < jn["pins"].at(k)["choices"].size(); c++) f.choices.push_back(jn["pins"].at(k)["choices"].at(c).str());
        }
    }
    return true;
}

void ScriptGraphEditor::rebuild(const Json::Value& model) {
    m_graph.clear();
    m_byKey.clear();
    m_shared.clear();
    m_feeders.clear();
    m_signature = signatureOf(model);

    const Json::Value& nodes = model["nodes"];

    // Nodes and their pins
    std::map<std::string, std::string> getNodeAt;  // variable + place -> the Get node drawn there
    for (size_t i = 0; i < nodes.size(); i++) {
        const Json::Value& jn = nodes.at(i);
        // A Get node of the same variable saved at the same place as one already drawn is that node, wired to one more pin.
        if (jn["category"].str() == "Variables" && jn["pos"].size() == 2) {
            const std::string at = jn["name"].str() + "@" + std::to_string(jn["pos"].at(0).integer()) + "," + std::to_string(jn["pos"].at(1).integer());
            auto drawn = getNodeAt.find(at);
            if (drawn != getNodeAt.end()) {
                m_shared[jn["id"].str()] = drawn->second;
                continue;
            }
            getNodeAt[at] = jn["id"].str();
        }
        auto node = std::make_unique<ScriptNode>(m_graph.getNextNodeId(), jn["name"].str(), ImVec2(0, 0));
        node->key = jn["id"].str();
        node->nodeClass = jn["class"].str();
        node->category = jn["category"].str();
        node->comment = jn["comment"].str();
        node->error = jn["error"].str();
        node->parentKey = jn["parent"].str();
        node->slot = jn["slot"].integer(-1);
        node->index = jn["index"].integer(-1);
        node->feedsKey = jn["feeds"]["node"].str();
        node->feedsPin = jn["feeds"]["pin"].str();
        node->size.x = node->isData() ? kValueNodeWidth : kNodeWidth;
        node->headerColor = headerColorFor(node->nodeClass);
        if (node->category == "Variables") node->headerColor = IM_COL32(55, 145, 105, 255);  // a Get node
        node->canDelete = true;
        node->floating = jn["floating"].boolean();

        node->hasExecIn = !node->isEvent() && !node->isData();
        if (node->hasExecIn) node->addInput("", PinType::Exec, m_graph.getNextPinId());

        const Json::Value& pins = jn["pins"];
        for (size_t k = 0; k < pins.size(); k++) {
            const Json::Value& jp = pins.at(k);
            ScriptPinField f;
            f.pinId = m_graph.getNextPinId();
            f.name = jp["name"].str();
            f.text = jp["text"].str();
            f.kind = jp["kind"].str();
            f.optional = jp["optional"].boolean();
            f.editable = jp["editable"].boolean(true);
            f.picks = jp["picks"].boolean();
            f.toggle = jp["toggle"].boolean();
            for (size_t c = 0; c < jp["choices"].size(); c++) f.choices.push_back(jp["choices"].at(c).str());
            f.wiredKey = jp["wired"].str();
            node->addInput(f.name, PinType::Any, f.pinId);
            node->fields.push_back(std::move(f));
        }

        if (node->isData()) {
            node->addOutput("Value", PinType::Any, m_graph.getNextPinId());
        } else {
            const Json::Value& execs = jn["execs"];
            for (size_t k = 0; k < execs.size(); k++) {
                node->execLabels.push_back(execs.at(k)["label"].str());
                node->execTargets.push_back(execs.at(k)["to"].str());
                node->addOutput(node->execLabels.back(), PinType::Exec, m_graph.getNextPinId());
            }
        }
        m_byKey[node->key] = node.get();
        m_graph.addNode(std::move(node));
    }

    // Wires: execution order, and values flowing into pins
    for (const auto& entry : m_byKey) {
        ScriptNode* node = entry.second;
        for (size_t k = 0; k < node->execTargets.size(); k++) {
            ScriptNode* target = nodeByKey(node->execTargets[k]);
            if (target && target->hasExecIn && !target->inputs.empty()) {
                m_graph.addConnection(node->id, node->outputs[k].id, target->id, target->inputs[0].id);
            }
        }
        for (ScriptPinField& f : node->fields) {
            if (f.wiredKey.empty()) continue;
            ScriptNode* source = nodeByKey(f.wiredKey);
            if (source && !source->outputs.empty()) {
                f.carried = source->category == "Variables";
                m_graph.addConnection(source->id, source->outputs[0].id, node->id, f.pinId);
                m_feeders[node->key].push_back(f.wiredKey);
            }
        }
    }

    layout();

    // Get nodes from the Variables panel are not in the script; they stay until wired in or removed
    // (or until their variable is deleted).
    m_getters.erase(std::remove_if(m_getters.begin(), m_getters.end(),
                                   [&](const Getter& g) {
                                       for (const VariableInfo& v : m_vars) {
                                           if (v.name == g.variable) return false;
                                       }
                                       for (const ReferenceInfo& r : m_refs) {
                                           if (r.name == g.variable && r.kind != "file") return false;
                                       }
                                       return true;
                                   }),
                    m_getters.end());
    // A loose Get node left where a wired one is again (an undo of cutting its wire) has done its job.
    m_getters.erase(std::remove_if(m_getters.begin(), m_getters.end(),
                                   [&](const Getter& g) {
                                       for (const auto& entry : m_byKey) {
                                           const ScriptNode* n = entry.second;
                                           if (n->category == "Variables" && !n->isGetter && n->name == "Get " + g.variable && n->position.x == g.pos.x && n->position.y == g.pos.y) return true;
                                       }
                                       return false;
                                   }),
                    m_getters.end());
    for (const Getter& g : m_getters) makeGetterNode(g.id, g.variable, g.pos);

    ScriptNode* selected = nodeByKey(m_selectedKey);
    m_graph.selectNode(selected ? selected->id : -1);
}

// How automatic placement reads: left to right, then down, like a page.
//  - A node that follows another sits to its right on the same line (its header in line with it), with kNodeGap grid squares between.
//  - A block's body starts to the right of the block, and the node after the block goes to the right of everything the block holds.
//  - A line holds kNodesPerRow nodes; the next one starts a new line at the left edge, kNodeGap below the lowest node of the graph.
//  - A node put in the middle of a line bumps the rest of that line to the right to make room.
//  - Value nodes sit below the line, in the column just left of the node they feed, kNodeGap below whatever is above them.
namespace {
constexpr float kNodeGap = 2.0f;
constexpr int kNodesPerRow = 10;
}  // namespace

float ScriptGraphEditor::nodeHeight(const ScriptNode* n) const {
    return 1.5f + static_cast<float>((std::max)(n->inputs.size(), n->outputs.size()));
}

void ScriptGraphEditor::layout() {
    const Json::Value& nodes = m_model["nodes"];

    // A node that is not connected to the script is wherever it was put, and the script's own nodes are arranged
    // without regard to it (it is not in the way of anything: the user placed it).
    for (size_t i = 0; i < nodes.size(); i++) {
        if (!nodes.at(i)["floating"].boolean()) continue;
        ScriptNode* n = nodeByKey(nodes.at(i)["id"].str());
        if (!n) continue;
        const Json::Value& pos = nodes.at(i)["pos"];
        if (!pos.isNull() && pos.size() == 2) n->position = ImVec2(static_cast<float>(pos.at(0).integer()), static_cast<float>(pos.at(1).integer()));
    }

    // Positions that were saved (by dragging nodes, or placing one) win; the rest are placed around them.
    std::set<std::string> saved;
    for (size_t i = 0; i < nodes.size(); i++) {
        ScriptNode* n = nodeByKey(nodes.at(i)["id"].str());
        if (!n || n->floating) continue;
        const Json::Value& pos = nodes.at(i)["pos"];
        if (pos.isNull() || pos.size() != 2) continue;
        n->position = ImVec2(static_cast<float>(pos.at(0).integer()), static_cast<float>(pos.at(1).integer()));
        saved.insert(n->key);
    }

    struct Placer {
        ScriptGraphEditor& ed;
        std::set<std::string> done, saved, fresh;  // placed (saved or not), saved by the user, and placed just now

        static float right(const ScriptNode* n) { return n->position.x + n->size.x; }
        float bottom(const ScriptNode* n) const { return n->position.y + ed.nodeHeight(n); }
        static bool hasNext(const ScriptNode* n) { return !n->execLabels.empty() && n->execLabels.back() == "Next"; }
        static size_t bodies(const ScriptNode* n) { return n->execLabels.size() - (hasNext(n) ? 1 : 0); }
        bool placed(const ScriptNode* n) const { return done.count(n->key) != 0; }
        ScriptNode* follower(const ScriptNode* n) const { return hasNext(n) ? ed.nodeByKey(n->execTargets.back()) : nullptr; }

        std::vector<ScriptNode*> placedNodes() const {
            std::vector<ScriptNode*> out;
            for (const auto& entry : ed.m_byKey) {
                if (!entry.second->floating && done.count(entry.first)) out.push_back(entry.second);
            }
            return out;
        }
        /// What is inside `n`: its bodies and their value nodes, and (if `own`) the value nodes feeding `n` itself.
        std::vector<ScriptNode*> below(const ScriptNode* n, bool own) const {
            const std::string prefix = n->key + ".", values = prefix + "v.";
            std::vector<ScriptNode*> out;
            for (ScriptNode* p : placedNodes()) {
                if (p->key.compare(0, prefix.size(), prefix) != 0) continue;
                if (!own && p->key.compare(0, values.size(), values) == 0) continue;
                out.push_back(p);
            }
            return out;
        }
        bool anyStatement() const {
            for (const ScriptNode* p : placedNodes()) {
                if (!p->isData()) return true;
            }
            return false;
        }
        float leftEdge() const {
            bool any = false;
            float x = 0.0f;
            for (const ScriptNode* p : placedNodes()) {
                if (p->isData()) continue;
                x = any ? (std::min)(x, p->position.x) : p->position.x;
                any = true;
            }
            return x;
        }
        float lowest() const {
            float y = 0.0f;
            for (const ScriptNode* p : placedNodes()) y = (std::max)(y, bottom(p));
            return y;
        }
        int onLine(float y) const {
            int count = 0;
            for (const ScriptNode* p : placedNodes()) count += (!p->isData() && std::fabs(p->position.y - y) < 0.5f) ? 1 : 0;
            return count;
        }
        bool overlaps(const ScriptNode* a, float x, float y, const ScriptNode* b) const {
            return x < right(b) && b->position.x < x + a->size.x && y < bottom(b) && b->position.y < y + ed.nodeHeight(a);
        }

        /// Where a statement goes: to the right of what comes before it, or on a new line.
        void placeExec(ScriptNode* u, const ScriptNode* before, bool body, size_t bodyIndex) {
            float x = 0.0f, y = 0.0f;
            if (!before) {
                x = leftEdge();
                y = anyStatement() ? std::ceil(lowest() + kNodeGap) : 0.0f;
            } else if (body) {
                x = right(before) + kNodeGap;
                y = before->position.y;
                if (bodyIndex > 0) {
                    float low = y;
                    for (const ScriptNode* p : below(before, false)) low = (std::max)(low, bottom(p));
                    if (low > y) y = std::ceil(low + kNodeGap);
                }
            } else {
                x = right(before);
                for (const ScriptNode* p : below(before, false)) {
                    if (!p->isData()) x = (std::max)(x, right(p));
                }
                x += kNodeGap;
                y = before->position.y;
            }
            // A full line goes on to the next one, unless the node is going between two that are already on this line.
            const ScriptNode* next = follower(u);
            const bool between = next && saved.count(next->key) && std::fabs(next->position.y - y) < 0.5f;
            if (before && !between && onLine(y) >= kNodesPerRow) {
                x = leftEdge();
                y = std::ceil(lowest() + kNodeGap);
            }
            u->position = ImVec2(x, y);
            done.insert(u->key);
            fresh.insert(u->key);
        }

        /// The first free place for `d` at column `x`, going down from `y`: kNodeGap below whatever is in the way.
        float freeBelow(const ScriptNode* d, float x, float y) const {
            y = std::ceil(y);
            const std::vector<ScriptNode*> others = placedNodes();
            for (int guard = 0; guard < 500; guard++) {
                const ScriptNode* hit = nullptr;
                for (const ScriptNode* p : others) {
                    if (p != d && overlaps(d, x, y, p)) {
                        hit = p;
                        break;
                    }
                }
                if (!hit) break;
                y = std::ceil(bottom(hit) + kNodeGap);
            }
            return y;
        }

        void placeFeeders(const ScriptNode* n, float fromY) {
            auto it = ed.m_feeders.find(n->key);
            if (it == ed.m_feeders.end()) return;
            const std::vector<std::string> keys = it->second;
            for (const std::string& dk : keys) {
                ScriptNode* d = ed.nodeByKey(dk);
                if (!d || d->floating || placed(d)) continue;
                const float x = n->position.x - d->size.x - kNodeGap;
                const float y = freeBelow(d, x, fromY);
                d->position = ImVec2(x, y);
                done.insert(d->key);
                fresh.insert(d->key);
                placeFeeders(d, y);
            }
        }

        void moveWithItsContents(ScriptNode* n, float dx) {
            for (ScriptNode* p : below(n, true)) p->position.x += dx;
            n->position.x += dx;
        }

        /// The nodes after `from` on its line (and everything they hold) move right by `dx`.
        void bump(ScriptNode* from, float dx) {
            const float line = from->position.y;
            for (ScriptNode* n = from; n && std::fabs(n->position.y - line) < 0.5f;) {
                ScriptNode* after = follower(n);
                moveWithItsContents(n, dx);
                n = after;
            }
        }

        /// Something new was put in or under `n`: the node after it, on the same line, makes room.
        void keepFollowerClear(const ScriptNode* n) {
            ScriptNode* next = follower(n);
            if (!next || !saved.count(next->key)) return;
            bool changed = fresh.count(n->key) != 0;
            float needed = right(n);
            for (const ScriptNode* p : below(n, false)) {
                changed = changed || fresh.count(p->key) != 0;
                if (!p->isData()) needed = (std::max)(needed, right(p));
            }
            needed += kNodeGap;
            if (changed && std::fabs(next->position.y - n->position.y) < 0.5f && next->position.x < needed) bump(next, needed - next->position.x);
        }

        void process(ScriptNode* n, const ScriptNode* before, bool body, size_t bodyIndex) {
            if (!placed(n)) placeExec(n, before, body, bodyIndex);
            placeFeeders(n, n->position.y + 2.0f);
            for (size_t k = 0; k < bodies(n); k++) {
                if (!n->execTargets[k].empty()) chain(n->execTargets[k], n, true, k);
            }
            keepFollowerClear(n);
        }

        void chain(const std::string& first, ScriptNode* before, bool body, size_t bodyIndex) {
            std::string key = first;
            while (!key.empty()) {
                ScriptNode* n = ed.nodeByKey(key);
                if (!n) break;
                process(n, before, body, bodyIndex);
                before = n;
                body = false;
                key = hasNext(n) ? n->execTargets.back() : std::string();
            }
        }
    };

    Placer placer{*this, {}, saved, {}};
    placer.done = saved;
    for (size_t i = 0; i < nodes.size(); i++) {
        if (nodes.at(i)["class"].str() != "Event" || nodes.at(i)["floating"].boolean()) continue;
        ScriptNode* handler = nodeByKey(nodes.at(i)["id"].str());
        if (handler) placer.process(handler, nullptr, false, 0);
    }

    // Anything the walk above never reached goes under everything, at the left.
    for (size_t i = 0; i < nodes.size(); i++) {
        ScriptNode* n = nodeByKey(nodes.at(i)["id"].str());
        if (!n || n->floating || placer.placed(n)) continue;
        n->position = ImVec2(placer.leftEdge(), std::ceil(placer.lowest() + kNodeGap));
        placer.done.insert(n->key);
        placer.fresh.insert(n->key);
    }

    // A node just placed that landed on another (a script edited in a text editor, an old layout) moves down until it is clear.
    for (const std::string& key : placer.fresh) {
        ScriptNode* n = nodeByKey(key);
        if (!n) continue;
        for (int attempt = 0; attempt < 400; attempt++) {
            bool taken = false;
            for (const ScriptNode* p : placer.placedNodes()) {
                if (p != n && placer.overlaps(n, n->position.x, n->position.y, p)) {
                    taken = true;
                    break;
                }
            }
            if (!taken) break;
            n->position.y += 1.0f;
        }
    }
}

ScriptNode* ScriptGraphEditor::nodeByKey(const std::string& key) const {
    auto it = m_byKey.find(key);
    if (it != m_byKey.end()) return it->second;
    auto shared = m_shared.find(key);
    if (shared == m_shared.end()) return nullptr;
    it = m_byKey.find(shared->second);
    return it == m_byKey.end() ? nullptr : it->second;
}

ScriptNode* ScriptGraphEditor::nodeById(int id) const {
    for (const auto& entry : m_byKey) {
        if (entry.second->id == id) return entry.second;
    }
    return nullptr;
}

bool ScriptGraphEditor::execTarget(const ScriptNode& from, int outputPinId, std::string& parent, int& slot, int& index) const {
    for (size_t j = 0; j < from.outputs.size(); j++) {
        if (from.outputs[j].id != outputPinId) continue;
        if (from.outputs[j].type != PinType::Exec || j >= from.execLabels.size()) return false;
        if (from.isEvent()) {
            parent = from.key;
            slot = 0;
            index = 0;
        } else if (from.execLabels[j] == "Next") {
            parent = from.parentKey;
            slot = from.slot;
            index = from.index + 1;
        } else {
            parent = from.key;
            slot = static_cast<int>(j);
            index = 0;
        }
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// The menus
// ---------------------------------------------------------------------------------------------

void ScriptGraphEditor::statementMenu(const std::string& parent, int slot, int index, bool useSpawn, ImVec2 spawn) {
    std::vector<std::string> categories;
    for (const CatalogEntry& e : m_catalog) {
        if ((e.cls == "Action" || e.cls == "Flow") && std::find(categories.begin(), categories.end(), e.category) == categories.end()) {
            categories.push_back(e.category);
        }
    }
    for (const std::string& category : categories) {
        if (!ImGui::BeginMenu(category.c_str())) continue;
        for (const CatalogEntry& e : m_catalog) {
            if ((e.cls != "Action" && e.cls != "Flow") || e.category != category) continue;
            if (ImGui::MenuItem(e.name.c_str())) {
                std::string name = e.name;
                m_pending.push_back([this, name, parent, slot, index, useSpawn, spawn]() { addAt(name, parent, slot, index, useSpawn, spawn); });
            }
            if (ImGui::IsItemHovered()) PropulsionMKI::UI::tooltip("%s", e.pattern.c_str());
        }
        ImGui::EndMenu();
    }
}

void ScriptGraphEditor::floatingMenu(ImVec2 spawn) {
    std::vector<std::string> categories;
    for (const CatalogEntry& e : m_catalog) {
        if ((e.cls == "Action" || e.cls == "Flow") && std::find(categories.begin(), categories.end(), e.category) == categories.end()) {
            categories.push_back(e.category);
        }
    }
    for (const std::string& category : categories) {
        if (!ImGui::BeginMenu(category.c_str())) continue;
        for (const CatalogEntry& e : m_catalog) {
            if ((e.cls != "Action" && e.cls != "Flow") || e.category != category) continue;
            if (ImGui::MenuItem(e.name.c_str())) {
                std::string name = e.name;
                m_pending.push_back([this, name, spawn]() { addFloating(name, spawn); });
            }
            if (ImGui::IsItemHovered()) PropulsionMKI::UI::tooltip("%s", e.pattern.c_str());
        }
        ImGui::EndMenu();
    }
}

void ScriptGraphEditor::floatingValueMenu(ImVec2 spawn) {
    std::vector<std::string> categories;
    for (const CatalogEntry& e : m_catalog) {
        if (e.cls == "Data" && std::find(categories.begin(), categories.end(), e.category) == categories.end()) categories.push_back(e.category);
    }
    for (const std::string& category : categories) {
        if (!ImGui::BeginMenu(category.c_str())) continue;
        for (const CatalogEntry& e : m_catalog) {
            if (e.cls != "Data" || e.category != category) continue;
            if (ImGui::MenuItem(e.name.c_str())) {
                std::string name = e.name;
                m_pending.push_back([this, name, spawn]() { addFloating(name, spawn); });
            }
            if (ImGui::IsItemHovered()) PropulsionMKI::UI::tooltip("%s", e.pattern.c_str());
        }
        ImGui::EndMenu();
    }
}

void ScriptGraphEditor::drawMenu(const ScriptMenuContext& ctx) {
    ScriptNode* node = ctx.nodeId >= 0 ? nodeById(ctx.nodeId) : nullptr;

    switch (ctx.kind) {
        case ScriptMenuContext::Kind::Empty: {
            if (ImGui::BeginMenu("Add Event")) {
                for (const CatalogEntry& e : m_catalog) {
                    if (e.cls != "Event") continue;
                    if (ImGui::MenuItem(e.name.c_str())) {
                        std::string name = e.name;
                        const ImVec2 spawn = ctx.spawn;
                        m_pending.push_back([this, name, spawn]() { addFloating(name, spawn); });
                    }
                    if (ImGui::IsItemHovered()) PropulsionMKI::UI::tooltip("%s", e.pattern.c_str());
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Add Node")) {
                floatingMenu(ctx.spawn);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Add Value")) {
                floatingValueMenu(ctx.spawn);
                ImGui::EndMenu();
            }
            ScriptNode* selected = nodeByKey(m_selectedKey);
            if (selected && selected->hasExecIn) {
                if (ImGui::BeginMenu(("Add after " + selected->name).c_str())) {
                    statementMenu(selected->parentKey, selected->slot, selected->index + 1, true, ctx.spawn);
                    ImGui::EndMenu();
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Fit view")) m_needsFit = true;
            if (m_hasSavedLayout && ImGui::MenuItem("Reset layout")) m_pending.push_back([this]() { resetLayout(); });
            if (ImGui::MenuItem("Reload from file")) m_pending.push_back([this]() { reload(); });
            break;
        }
        case ScriptMenuContext::Kind::Node: {
            if (!node) break;
            ImGui::TextDisabled("%s", node->name.c_str());
            ImGui::Separator();
            {
                // A node selected along with others is copied with them. The copies are connected to nothing.
                const bool several = m_graph.selectedCount() > 1 && node->selected;
                const std::string title = several ? "Duplicate " + std::to_string(m_graph.selectedCount()) + " selected nodes" : "Duplicate";
                if (ImGui::MenuItem(title.c_str(), "Ctrl+D")) {
                    std::vector<std::string> keys;
                    if (several) {
                        for (int id : m_graph.selectedNodeIds()) {
                            if (const ScriptNode* sel = nodeById(id)) keys.push_back(sel->key);
                        }
                    } else {
                        keys.push_back(node->key);
                    }
                    m_pending.push_back([this, keys]() { duplicateNodes(keys); });
                }
                if (ImGui::IsItemHovered()) PropulsionMKI::UI::tooltip("Makes a copy with the same settings that is connected to nothing.");
                ImGui::Separator();
            }
            if (node->isGetter) {
                if (ImGui::MenuItem("Remove")) {
                    const std::string key = node->key;
                    m_pending.push_back([this, key]() { removeGetter(key); });
                }
                break;
            }
            if (m_graph.selectedCount() > 1 && node->selected) {
                const std::string title = "Delete " + std::to_string(m_graph.selectedCount()) + " selected nodes";
                if (ImGui::MenuItem(title.c_str())) {
                    std::vector<std::string> keys;
                    for (int id : m_graph.selectedNodeIds()) {
                        if (const ScriptNode* sel = nodeById(id)) keys.push_back(sel->key);
                    }
                    m_pending.push_back([this, keys]() { deleteNodes(keys); });
                }
                ImGui::Separator();
            }
            if (node->isData()) {
                if (ImGui::MenuItem(node->floating ? "Delete" : "Remove (use a plain value instead)")) {
                    std::string key = node->key;
                    m_pending.push_back([this, key]() { edit("unplug", key); });
                }
                break;
            }
            if (node->name == "Branch") {
                // True and False are always there; more conditions are added between them.
                if (ImGui::MenuItem("Add Else If")) {
                    std::string key = node->key;
                    m_pending.push_back([this, key]() { edit("add_branch", key, "else_if"); });
                }
                for (size_t k = 0; k < node->execLabels.size(); k++) {
                    if (node->execLabels[k] == "Else if") {
                        if (ImGui::MenuItem(("Remove Else If " + std::to_string(k)).c_str())) {
                            std::string key = node->key;
                            int slot = static_cast<int>(k);
                            m_pending.push_back([this, key, slot]() { edit("remove_branch", key, std::to_string(slot)); });
                        }
                    }
                }
                ImGui::Separator();
            }
            if (node->name == "Switch") {
                if (ImGui::MenuItem("Add Case")) {
                    std::string key = node->key;
                    m_pending.push_back([this, key]() { edit("add_branch", key, "case"); });
                }
                if (ImGui::MenuItem("Add Otherwise")) {
                    std::string key = node->key;
                    m_pending.push_back([this, key]() { edit("add_branch", key, "otherwise"); });
                }
                size_t cases = 0;
                for (const std::string& label : node->execLabels) cases += label.rfind("Case ", 0) == 0 ? 1 : 0;
                for (size_t k = 0; k < node->execLabels.size(); k++) {
                    const std::string& label = node->execLabels[k];
                    if (label == "Otherwise" || (label.rfind("Case ", 0) == 0 && cases > 1)) {
                        if (ImGui::MenuItem(("Remove " + label).c_str())) {
                            std::string key = node->key;
                            int slot = static_cast<int>(k);
                            m_pending.push_back([this, key, slot]() { edit("remove_branch", key, std::to_string(slot)); });
                        }
                    }
                }
                ImGui::Separator();
            }
            if (node->hasExecIn && ImGui::BeginMenu("Add after")) {
                statementMenu(node->parentKey, node->slot, node->index + 1);
                ImGui::EndMenu();
            }
            const bool hasNext = !node->execLabels.empty() && node->execLabels.back() == "Next";
            const size_t bodies = node->execLabels.size() - (hasNext ? 1 : 0);
            for (size_t k = 0; k < bodies; k++) {
                std::string title = "Add at the start of " + node->execLabels[k];
                if (ImGui::BeginMenu(title.c_str())) {
                    statementMenu(node->key, static_cast<int>(k), 0);
                    ImGui::EndMenu();
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem(node->isEvent() ? "Delete this event and everything in it" : "Delete")) {
                std::string key = node->key;
                m_pending.push_back([this, key]() { edit("delete", key); });
            }
            break;
        }
        case ScriptMenuContext::Kind::Pin: {
            if (!node) break;
            const ScriptPinField* field = nullptr;
            for (const ScriptPinField& f : node->fields) {
                if (f.pinId == ctx.pinId) field = &f;
            }
            if (!field || field->kind == "choice" || field->kind == "flag") break;
            ImGui::TextDisabled("%s", field->name.c_str());
            ImGui::Separator();
            if (!field->wiredKey.empty()) {
                if (ImGui::MenuItem("Remove the computed value")) {
                    std::string key = field->wiredKey;
                    m_pending.push_back([this, key]() { edit("unplug", key); });
                }
                break;
            }
            if (!m_vars.empty() && ImGui::BeginMenu("Use variable")) {
                for (const VariableInfo& v : m_vars) {
                    // A list pin offers lists; everything else offers the rest (the checker has the last word).
                    if ((field->name == "List") != v.isList()) continue;
                    const std::string label = v.name + "  (" + v.type + ")";
                    if (ImGui::MenuItem(label.c_str())) {
                        std::string key = node->key, pin = field->name, name = v.name;
                        m_pending.push_back([this, key, pin, name]() { edit("set_pin", key, pin, name); });
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Compute with")) {
                std::vector<std::string> categories;
                for (const CatalogEntry& e : m_catalog) {
                    if (e.cls != "Data") continue;
                    if (field->kind == "condition" && e.produces != "condition") continue;
                    if (std::find(categories.begin(), categories.end(), e.category) == categories.end()) categories.push_back(e.category);
                }
                for (const std::string& category : categories) {
                    if (!ImGui::BeginMenu(category.c_str())) continue;
                    for (const CatalogEntry& e : m_catalog) {
                        if (e.cls != "Data" || e.category != category) continue;
                        if (field->kind == "condition" && e.produces != "condition") continue;
                        if (ImGui::MenuItem(e.name.c_str())) {
                            std::string name = e.name, key = node->key, pin = field->name;
                            m_pending.push_back([this, key, pin, name]() { edit("plug", key, pin, name); });
                        }
                        if (ImGui::IsItemHovered()) PropulsionMKI::UI::tooltip("%s", e.pattern.c_str());
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            break;
        }
        case ScriptMenuContext::Kind::ExecDrop: {
            if (!node) break;
            std::string parent;
            int slot = 0, index = 0;
            if (!execTarget(*node, ctx.pinId, parent, slot, index)) break;
            ImGui::TextDisabled("Add a node here");
            ImGui::Separator();
            statementMenu(parent, slot, index, true, ctx.spawn);
            break;
        }
        case ScriptMenuContext::Kind::GetterDrop: {
            if (!node || !node->carriesVariable()) break;
            // The nodes that are not there yet and have a pin that takes this variable (the checker says which),
            // by category. A node with several such pins has a submenu of them.
            const std::vector<Fit>& fits = fitsFor(node->name.substr(4));
            ImGui::TextDisabled("Connect %s to", node->name.substr(4).c_str());
            ImGui::Separator();
            if (fits.empty()) {
                ImGui::TextDisabled("Nothing takes this variable");
                break;
            }
            std::vector<std::string> categories;
            for (const Fit& f : fits) {
                if (std::find(categories.begin(), categories.end(), f.category) == categories.end()) categories.push_back(f.category);
            }
            for (const std::string& category : categories) {
                if (!ImGui::BeginMenu(category.c_str())) continue;
                for (size_t i = 0; i < fits.size(); i++) {
                    if (fits[i].category != category) continue;
                    // The pins of this node are the run of entries from here that share its name.
                    size_t end = i;
                    while (end < fits.size() && fits[end].node == fits[i].node) end++;
                    if (end - i == 1) {
                        if (ImGui::MenuItem(fits[i].node.c_str())) addNodeFor(*node, fits[i], ctx.spawn);
                        if (ImGui::IsItemHovered()) PropulsionMKI::UI::tooltip("Into %s: %s", fits[i].pin.c_str(), fits[i].text.c_str());
                    } else if (ImGui::BeginMenu(fits[i].node.c_str())) {
                        for (size_t k = i; k < end; k++) {
                            if (ImGui::MenuItem(fits[k].pin.c_str())) addNodeFor(*node, fits[k], ctx.spawn);
                        }
                        ImGui::EndMenu();
                    }
                    i = end - 1;
                }
                ImGui::EndMenu();
            }
            break;
        }
    }
}

std::vector<ScriptSearchResult> ScriptGraphEditor::searchResults(const ScriptMenuContext& ctx) {
    std::vector<ScriptSearchResult> results;
    ScriptNode* node = ctx.nodeId >= 0 ? nodeById(ctx.nodeId) : nullptr;
    const ImVec2 spawn = ctx.spawn;

    // The statements statementMenu offers, to go at parent/slot/index.
    auto statements = [&](const std::string& parent, int slot, int index, bool useSpawn) {
        for (const CatalogEntry& e : m_catalog) {
            if (e.cls != "Action" && e.cls != "Flow") continue;
            const std::string name = e.name;
            results.push_back({name, e.category, e.pattern, e.pattern, [this, name, parent, slot, index, useSpawn, spawn]() {
                                   m_pending.push_back([this, name, parent, slot, index, useSpawn, spawn]() { addAt(name, parent, slot, index, useSpawn, spawn); });
                               }});
        }
    };

    switch (ctx.kind) {
        case ScriptMenuContext::Kind::Empty: {
            for (const CatalogEntry& e : m_catalog) {
                if (e.cls != "Event") continue;
                const std::string name = e.name;
                results.push_back({name, "Event", e.pattern, e.pattern, [this, name, spawn]() {
                                       m_pending.push_back([this, name, spawn]() { addFloating(name, spawn); });
                                   }});
            }
            for (const CatalogEntry& e : m_catalog) {
                if (e.cls != "Action" && e.cls != "Flow" && e.cls != "Data") continue;
                const std::string name = e.name;
                results.push_back({name, e.category, e.pattern, e.pattern, [this, name, spawn]() {
                                       m_pending.push_back([this, name, spawn]() { addFloating(name, spawn); });
                                   }});
            }
            const ScriptNode* selected = nodeByKey(m_selectedKey);
            if (selected && selected->hasExecIn) {
                const size_t before = results.size();
                statements(selected->parentKey, selected->slot, selected->index + 1, true);
                for (size_t i = before; i < results.size(); i++) results[i].category = "after " + selected->name;
            }
            break;
        }
        case ScriptMenuContext::Kind::Pin: {
            if (!node) break;
            const ScriptPinField* field = nullptr;
            for (const ScriptPinField& f : node->fields) {
                if (f.pinId == ctx.pinId) field = &f;
            }
            if (!field || field->kind == "choice" || field->kind == "flag" || !field->wiredKey.empty()) break;
            const std::string key = node->key, pin = field->name;
            for (const VariableInfo& v : m_vars) {
                if ((pin == "List") != v.isList()) continue;
                const std::string name = v.name;
                results.push_back({name + "  (" + v.type + ")", "Variable", "", "Use the variable " + name, [this, key, pin, name]() {
                                       m_pending.push_back([this, key, pin, name]() { edit("set_pin", key, pin, name); });
                                   }});
            }
            for (const CatalogEntry& e : m_catalog) {
                if (e.cls != "Data") continue;
                if (field->kind == "condition" && e.produces != "condition") continue;
                const std::string name = e.name;
                results.push_back({name, e.category, e.pattern, e.pattern, [this, key, pin, name]() {
                                       m_pending.push_back([this, key, pin, name]() { edit("plug", key, pin, name); });
                                   }});
            }
            break;
        }
        case ScriptMenuContext::Kind::ExecDrop: {
            if (!node) break;
            std::string parent;
            int slot = 0, index = 0;
            if (!execTarget(*node, ctx.pinId, parent, slot, index)) break;
            statements(parent, slot, index, true);
            break;
        }
        case ScriptMenuContext::Kind::GetterDrop: {
            if (!node || !node->carriesVariable()) break;
            const std::vector<Fit>& fits = fitsFor(node->name.substr(4));
            const int getterId = node->id;
            for (size_t i = 0; i < fits.size(); i++) {
                // A node with several pins that take the variable is listed once for each, by the pin.
                size_t same = 0;
                for (const Fit& other : fits) same += other.node == fits[i].node ? 1 : 0;
                const Fit fit = fits[i];
                const std::string label = same > 1 ? fit.node + " (" + fit.pin + ")" : fit.node;
                results.push_back({label, fit.category, "", "Into " + fit.pin + ": " + fit.text, [this, getterId, fit, spawn]() {
                                       if (const ScriptNode* getter = nodeById(getterId)) addNodeFor(*getter, fit, spawn);
                                   }});
            }
            break;
        }
        case ScriptMenuContext::Kind::Node:
            break;  // the commands on a node are not nodes to find
    }
    return results;
}

// ---------------------------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------------------------

void ScriptGraphEditor::setObject(const std::string& objectName, std::vector<ComponentInfo> components, int sharedBy,
                                  std::function<void()> objectEditor, std::function<void()> addComponent) {
    m_objectName = objectName;
    m_components = std::move(components);
    m_sharedBy = sharedBy;
    m_objectEditor = std::move(objectEditor);
    m_addComponent = std::move(addComponent);
}

void ScriptGraphEditor::render() {
    // The file may have been changed in a text editor.
    std::error_code ec;
    auto time = fs::last_write_time(m_path, ec);
    if (!ec && time != m_fileTime) {
        // Changed outside the graph: what undo would restore may no longer fit, so it starts over.
        reload();
        m_undo.clear();
        m_redo.clear();
        markDirty();
    }

    ImGui::TextUnformatted(fs::path(m_path).filename().string().c_str());
    ImGui::SameLine();
    drawCompileStatus();
    ImGui::SameLine();
    ImGui::BeginDisabled(!canUndo());
    if (ImGui::SmallButton("Undo")) undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!canRedo());
    if (ImGui::SmallButton("Redo")) redo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::SmallButton("Fit view")) m_needsFit = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("Reload")) reload();
    ImGui::SameLine();
    if (ImGui::SmallButton("Open as text")) {
        if (!PropulsionMKI::Platform::openWithDefaultApp(m_path) && m_log) m_log(LogCategory::ScriptGraph, LogLevel::Error, "Could not open " + m_path + ". Open it in any text editor.");
    }
    ImGui::SameLine();
    // The code pane's switch is lit while the pane is open.
    const bool codeWasOpen = m_codeOpen;
    if (codeWasOpen) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::SmallButton("Code")) m_codeOpen = !m_codeOpen;
    if (codeWasOpen) ImGui::PopStyleColor();
    if (!m_codeOpen) {
        // Closing the pane ends the typing in it: what parses is saved, and the pane gives up the keyboard.
        if (codeWasOpen) m_pending.push_back([this]() { flushCode(); });
        m_codeFocused = false;
    }
    m_codeButton = ImVec2((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f, (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f);
    if (ImGui::IsItemHovered()) PropulsionMKI::UI::tooltip("Show or hide the script's code beside the graph.\nClick a line to select its node; select a node to see its line.");
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        PropulsionMKI::UI::tooltip("Right-click the canvas to add nodes.\nIn any add menu (or after dragging a wire out), just start typing to search it; Up/Down and Enter pick.\nDrag a box on empty canvas to select several nodes; Delete removes them, Ctrl+D copies them (the copies are connected to nothing).\nDrag from a white arrow to reorder.\nRight-click a field > Use variable to attach a variable.\nCtrl+Z undoes, Ctrl+Y redoes.");
    }

    if (!m_message.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 120, 120, 255));
        ImGui::TextWrapped("%s", m_message.c_str());
        ImGui::PopStyleColor();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F7) && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) compile();

    // The panels on the left (Components, Variables), the graph, and the Details panel on the right.
    drawPanels(ImGui::GetContentRegionAvail());

    // Edits asked for while drawing are done now that nothing is being iterated.
    if (!m_pending.empty()) {
        std::vector<std::function<void()>> work = std::move(m_pending);
        m_pending.clear();
        for (auto& f : work) f();
    }
}

} // namespace Propulsion
