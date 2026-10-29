#pragma once

/**
 * ScriptGraphEditor.h
 * Shows a PropulsionScript (.prop) file as a node graph, and edits the file through it.
 *
 * The text file is always the source of truth. The graph is built from it (by the PropulsionScript
 * library, through propulsion_graph_load), every edit made in the graph is sent back to the library as an
 * operation (propulsion_graph_edit), which rewrites the file only if the result is still a valid script,
 * and then the graph is rebuilt from the file. So a refused edit never leaves the graph and the text
 * disagreeing, and editing the file in a text editor updates the graph too.
 *
 * The one thing the text does not hold is a node that is not connected to anything yet (a floating node, see
 * addFloating): it is kept in the layout file beside the script, and joins the script when it is wired in.
 *
 * It draws with the same NodeGraph canvas the material editor uses.
 */

#include "nodegraph/NodeGraph.h"
#include "core/OutputLog.h"
#include "scripting/CodeBuffer.h"
#include "utils/Json.h"

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Propulsion {

class ScriptGraphEditor {
public:
    using Log = LogFn;

    ScriptGraphEditor(std::string absolutePath, Log log);

    /// Draw the editor into the current window, using the space that is left.
    void render();

    const std::string& path() const { return m_path; }

    /// The canvas, for looking at what is on it.
    const NodeGraph& graph() const { return m_graph; }

    /// The last reason an edit or a load was refused (empty if all is well).
    const std::string& message() const { return m_message; }

    /// Make one edit to the script, the way a gesture in the graph does (see propulsion_graph_edit for
    /// the operations). Returns false, with `message()` set, if the script would no longer be valid.
    /// Every edit that changes the script or the layout is one undo step.
    bool edit(const char* op, const std::string& a1 = "", const std::string& a2 = "", const std::string& a3 = "", const std::string& a4 = "");

    /// Delete several nodes (by key) as one undo step. Nodes inside a deleted node go with it; a value
    /// node is replaced by a plain value. Returns false, with `message()` set, if any was refused.
    bool deleteNodes(const std::vector<std::string>& keys);

    /// Copy several nodes (by key) as one undo step. A copy is connected to nothing: nothing runs before, after or
    /// inside it, and a computed value that was plugged into one of its pins is a plain value. It keeps the node's
    /// own settings and is a little below and to the right of the original. Like any node that is not wired in, it is
    /// not part of the script's text. Returns false, with `message()` set, if any was refused.
    bool duplicateNodes(const std::vector<std::string>& keys);

    /// Save where every node is now (after nodes were dragged) as one undo step.
    void commitLayout();

    /// Forget the saved layout and arrange the nodes automatically again (one undo step).
    void resetLayout();

    /// Undo and redo of graph edits (the script text and the layout come back exactly as they were).
    bool undo();
    bool redo();
    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }

    /// A variable declared in the script's header.
    struct VariableInfo {
        std::string name, kind, type, value, parts, text;
        bool exposed = false;
        bool isList() const { return type.rfind("list of ", 0) == 0; }
    };
    /// A name made inside a handler or action by a Set Local node (`remember`). It is not in the header: it exists from its
    /// node to the end of the block the node is in.
    struct LocalInfo {
        std::string name, type, node, scope;  // type: "" if its value doesn't check; node: the Set Local node's key; scope: the handler it is in
        int line = 0;
    };
    /// Something the header says this script reaches outside itself for: one entity found by tag, every
    /// entity with a tag (a live list), or another script.
    struct ReferenceInfo {
        std::string name, kind, file, tag, when, text;  // kind: entity, entities, file
    };
    /// An enum or a struct the script can use as a type (declared in its header), or the built-in `hit`.
    struct TypeInfo {
        std::string name, kind;  // kind: enum, struct
        std::vector<std::string> values;                           // an enum's values
        std::vector<std::pair<std::string, std::string>> fields;  // a struct's fields: name, type
    };
    /// A component shown in the Components panel.
    struct ComponentInfo {
        std::string name;    // Mesh, Material, Transform
        std::string detail;  // what it holds (the mesh's name), shown beside it
        std::string note;    // what it is, for the tooltip and the details panel
        std::string parent;  // the component it belongs to ("" = a direct component of the Object): Material and Transform are under Mesh
        std::vector<std::pair<std::string, std::string>> details;  // label / value rows for the details panel
        ImU32 swatch = 0;    // a color to show in the details panel (a material's base color), 0 for none
        /// Widgets for editing this component, drawn in the details panel in place of the read-only rows when set.
        /// The host provides it (it knows what the component is made of), so the editor stays free of engine types.
        std::function<void()> editor;
    };

    /// One thing Compile found.
    struct Problem {
        bool error = true;       // false: a warning (the script still runs)
        std::string message;
        std::string node;        // the node it is about, if any
        std::string variable;    // the variable it is about, if any
        int line = 0;
    };

    /// What the last Compile found, and whether the script has changed since.
    struct CompileResult {
        bool done = false;       // Compile has been run (for this open editor)
        bool stale = false;      // the script was edited after it
        int errors = 0;
        int warnings = 0;
        std::vector<Problem> problems;
    };

    /// Check the script as Play would: every problem that would make it fail is listed with the node it
    /// is on, and the Compiler Results panel opens. Nothing is written.
    void compile();
    const CompileResult& lastCompile() const { return m_compile; }

    /// What the Components panel shows: the Object asset this script belongs to (empty if none) and its
    /// components, set by the host each frame. `sharedBy` is how many Object assets use this script.
    /// `objectEditor` draws the Object's own properties (its script) in the details panel when the Object is selected,
    /// and `addComponent` draws the "+ Add Component" control under the list. Both are optional.
    void setObject(const std::string& objectName, std::vector<ComponentInfo> components, int sharedBy,
                   std::function<void()> objectEditor = {}, std::function<void()> addComponent = {});

    /// Select a component in the Components panel so its details show (`"@object"` is the Object itself).
    void selectComponent(const std::string& name);

    const std::vector<VariableInfo>& variables() const { return m_vars; }
    /// The Local Variables sub-section of the Variables panel: every Set Local node's name, in script order.
    const std::vector<LocalInfo>& locals() const { return m_locals; }
    const std::vector<ReferenceInfo>& references() const { return m_refs; }
    /// The enums and structs the script can use as types (and `hit`).
    const std::vector<TypeInfo>& types() const { return m_types; }

    /// Add a reference to the header with a free name and select it (what the + Reference menu does).
    /// `kind` is entity, entities or file; `file` is the script it names (for a file reference, which
    /// script; for the others, an optional one). Returns its name, or empty if it was refused.
    std::string addReference(const std::string& kind, const std::string& file = "");

    /// The other scripts this one can name, as paths relative to it (what the pickers in Details offer).
    const std::vector<std::string>& scriptChoices() const { return m_scripts; }

    /// Where the + Reference button and a reference's row were drawn last frame (for tests).
    ImVec2 addReferenceButton() const { return m_addReferenceButton; }
    /// The reference Details is showing (empty if it is showing something else).
    std::string selectedReferenceName() const { return m_focus == Focus::Reference ? m_selectedRef : std::string(); }
    ImVec2 referenceRow(const std::string& name) const {
        auto it = m_referenceRows.find(name);
        return it == m_referenceRows.end() ? ImVec2(0, 0) : it->second;
    }

    /// Declare a new variable with a free name and make it the selected one (what the + button does).
    /// Returns its name, or empty if it was refused.
    std::string addVariable();

    /// Where the + Variable button was drawn last frame (screen position; for tests that click it).
    ImVec2 addVariableButton() const { return m_addButton; }

    /// Where a variable's row in the Variables panel was drawn last frame (for tests that drag it).
    ImVec2 variableRow(const std::string& name) const {
        auto it = m_variableRows.find(name);
        return it == m_variableRows.end() ? ImVec2(0, 0) : it->second;
    }

    /// Where a local's row in the Variables panel was drawn last frame (for tests that click it); (0, 0) if it isn't shown.
    ImVec2 localRow(const std::string& name) const {
        auto it = m_localRows.find(name);
        return it == m_localRows.end() ? ImVec2(0, 0) : it->second;
    }

    /// Zoom and move the view so every node is in it, on the next frame (what the Fit view button does).
    void requestFit() { m_needsFit = true; }

    /// Where the Local Variables heading was drawn last frame (for tests).
    ImVec2 localsHeader() const { return m_localsHeader; }

    /// Where the Compile button was drawn last frame (for tests).
    ImVec2 compileButton() const { return m_compileButton; }

    /// Put a Get node for a variable on the canvas (what dropping it from the Variables panel does).
    void addGetter(const std::string& variable, ImVec2 gridPos);

    /// The code pane to the right of Details: the script's text, which can be typed in. The selected node's
    /// lines are marked; clicking a line selects its node, and a node picked in the graph scrolls to its line.
    ///
    /// Typing changes the text in the pane only. Once it has parsed and typing has paused (or the pane is
    /// left) it is written to the file as one undo step and the graph is rebuilt from it, exactly as after any
    /// edit made in the graph. Text that doesn't parse is marked where it stops, and is not written: the graph
    /// keeps showing the last good version. The file is the truth: if it changes some other way (a graph edit,
    /// undo, another editor) text typed but not yet written is dropped.
    bool codeOpen() const { return m_codeOpen; }
    void setCodeOpen(bool open) { m_codeOpen = open; }
    /// The text in the pane, line by line. This is what the file holds, except while typing is unwritten
    /// (and it shows a file that is too broken for a graph as it is).
    const std::vector<std::string>& codeLines() const { return m_code.lines(); }
    const CodeBuffer& code() const { return m_code; }
    /// Text has been typed that is not in the file yet.
    bool codeDirty() const { return m_codeDirty; }
    /// The pane is where typing goes.
    bool codeFocused() const { return m_codeFocused; }
    /// The pane's lines belong to the graph's nodes: the file could be shown as a graph, and the text is the file's.
    bool codeLinked() const { return m_codeFileOk && !m_codeDirty; }
    /// Where the typed text stops being a script (the line and column are 1-based), if it does.
    struct CodeError {
        int line = 0, col = 0;
        std::string message;
    };
    const CodeError* codeError() const { return m_codeHasError ? &m_codeError : nullptr; }
    /// Write the typed text to the file now (what pausing does). False if there is none, or it doesn't parse.
    bool applyCode();
    /// The line (1-based) a node is written on, or 0 if the key is not a node of the script.
    int codeLineOf(const std::string& nodeKey) const {
        auto it = m_nodeLine.find(nodeKey);
        return it == m_nodeLine.end() ? 0 : it->second;
    }
    /// Where a line's row was drawn last frame (screen position of its left edge, at mid height; (0,0) if it was
    /// not drawn), and where a character of it was (line 1-based, `col` a byte offset), and where the Code button
    /// was drawn (for tests).
    ImVec2 codeRow(int line) const {
        auto it = m_codeRows.find(line);
        return it == m_codeRows.end() ? ImVec2(0, 0) : it->second;
    }
    ImVec2 codeCharPos(int line, int col) const;
    ImVec2 codeButton() const { return m_codeButton; }
    /// The key of the node the selection is on (empty if none).
    const std::string& selectedNodeKey() const { return m_selectedKey; }

private:
    /// What undo keeps: the script's text and the layout file's text (if there is one).
    struct State {
        std::string script;
        std::string layout;
        bool hasLayout = false;
        bool operator==(const State& o) const { return script == o.script && hasLayout == o.hasLayout && layout == o.layout; }
    };
    State capture() const;
    bool restore(const State& state);
    void beginGroup();
    void endGroup();
    bool editRaw(const char* op, const std::string& a1, const std::string& a2, const std::string& a3, const std::string& a4);
    void freezeLayout();
    void addAt(const std::string& name, const std::string& parent, int slot, int index, bool useSpawn, ImVec2 spawn);
    /// Put a node on the canvas that is not connected to anything: it is kept beside the script and is not part of
    /// its text until it is wired in. Any event, action or flow node can be added this way.
    void addFloating(const std::string& name, ImVec2 spawn);
    std::string layoutFile() const { return m_path + ".layout"; }

    struct CatalogEntry {
        std::string name, cls, category, pattern, produces;
    };

    // Talking to the library
    void reload();
    void adopt(const std::string& json);

    // Building the graph
    void rebuild(const Json::Value& model);
    bool updateInPlace(const Json::Value& model);
    void layout();
    float nodeHeight(const ScriptNode* n) const;

    // The menus
    void drawMenu(const ScriptMenuContext& ctx);
    /// What drawMenu would offer for `ctx`, flat, for the menu's search.
    std::vector<ScriptSearchResult> searchResults(const ScriptMenuContext& ctx);
    void statementMenu(const std::string& parent, int slot, int index, bool useSpawn = false, ImVec2 spawn = ImVec2(0, 0));
    /// The statement nodes, by category, each added as a node that is not connected to anything.
    void floatingMenu(ImVec2 spawn);
    /// The value nodes, by category, each added as a node that is plugged into nothing.
    void floatingValueMenu(ImVec2 spawn);
    ScriptNode* nodeById(int id) const;
    ScriptNode* nodeByKey(const std::string& key) const;
    /// Where a node added at an execution output of `from` goes: parent, body slot, index.
    bool execTarget(const ScriptNode& from, int outputPinId, std::string& parent, int& slot, int& index) const;

    // The panels beside the graph (ScriptGraphPanels.cpp)
    void drawPanels(const ImVec2& size);
    void drawComponentsPanel();
    void drawVariablesPanel();
    void drawReferencesPanel();
    void drawDetailsPanel();
    void drawVariableDetails(const VariableInfo& v);
    void drawReferenceDetails(const ReferenceInfo& r);
    void setReferenceDecl(const ReferenceInfo& r, const std::string& file, const std::string& tag, const std::string& when);
    void refreshScripts();
    void drawComponentDetails();
    void drawNodeDetails();
    void drawCompileResults(float height);
    void drawCompileStatus();
    void selectProblem(const Problem& p);
    /// Select a node of the script (as clicking it does), and bring it into view in the graph if asked.
    enum class View { Keep, Center, CenterIfHidden };
    void selectNodeByKey(const std::string& key, View view);
    void drawCodePanel();
    /// Read the script's text again for the code pane. `matchesModel` is false when the file could not be
    /// turned into a graph (the pane still shows it, but its lines no longer belong to any node).
    void refreshCode(bool matchesModel);
    /// The pane's text changed (typed, pasted, undone): is it different from the file, and does it parse?
    void codeChanged();
    /// Whether the pane's text parses (sets m_codeHasError and where it stops).
    void checkCode();
    /// What typing in the pane does with the keyboard this frame; true if the text or cursor changed.
    bool codeKeys();
    /// The lines (and which section each is in) and the longest line, for drawing; kept for the text as it is.
    void indexCode();
    /// Write typed text that parses to the file before something else edits the file.
    void flushCode();
    float codeX(const std::string& line, int col) const;
    int codeColAt(const std::string& line, float x) const;
    ScriptNode* makeGetterNode(int id, const std::string& variable, ImVec2 pos);
    void removeGetter(const std::string& key);
    /// Plug the variable a Get node carries into the value pin `toPinId` of `to` (what wiring it does).
    void plugGetter(ScriptNode& getter, ScriptNode& to, int toPinId);
    /// A node that is not in the script yet and has a pin that can take a variable (the script library asks the
    /// checker, see propulsion_graph_fits). `text` is what the pin is given: the variable, or for a condition `X is true`.
    struct Fit {
        std::string node, pin, category, text;
    };
    /// The new nodes that can take `variable` (asked once, until the script changes).
    const std::vector<Fit>& fitsFor(const std::string& variable);
    /// Add the node a Get node's wire was let go for, with the variable plugged into `fit.pin`, as one undo step.
    /// The Get node becomes the pin's value (or stays wired, if it was feeding other pins too).
    void addNodeFor(const ScriptNode& getter, const Fit& fit, ImVec2 spawn);
    void markDirty();
    const ComponentInfo* componentNamed(const std::string& name) const;
    bool textField(const char* id, const std::string& shown, std::string& typed);
    void setVariableDecl(const VariableInfo& v, const std::string& type, const std::string& value, const std::string& parts, bool exposed);
    const VariableInfo* selectedVariable() const;
    const ReferenceInfo* selectedReference() const;

    std::string m_path;
    Log m_log;
    NodeGraph m_graph;

    std::vector<CatalogEntry> m_catalog;
    std::map<std::string, std::vector<Fit>> m_fits;  // by variable; forgotten whenever the script is read again
    Json::Value m_model;
    std::string m_signature;       // what the graph's shape is, to tell a text change from a structure change
    std::map<std::string, ScriptNode*> m_byKey;
    /// Several pins can be fed by one Get node on the canvas: each pin has a value node of its own in the model
    /// (so cutting one wire unplugs just that pin), and the ones of the same variable saved at the same place
    /// are drawn as one. This maps such a value node's key to the key of the node that is drawn for it.
    std::map<std::string, std::string> m_shared;
    std::map<std::string, std::vector<std::string>> m_feeders;  // statement key -> the value nodes feeding it
    std::vector<VariableInfo> m_vars;
    std::vector<LocalInfo> m_locals;
    std::vector<ReferenceInfo> m_refs;
    std::vector<TypeInfo> m_types;
    std::vector<std::string> m_scripts;  // the other scripts beside this one, relative to it
    std::vector<std::string> m_needs;  // components the script says it needs
    std::vector<std::pair<int, std::string>> m_problems;

    std::string m_message;         // why the last edit was refused, or why the file can't be shown
    bool m_loaded = false;
    bool m_hasSavedLayout = false; // some node has a saved position (so the layout is not just automatic)

    std::vector<State> m_undo;
    std::vector<State> m_redo;
    int m_groupDepth = 0;
    State m_groupBefore;
    bool m_needsFit = true;
    std::string m_selectedKey;

    // The panels (sizes are in font heights, so they follow UI scaling)
    std::string m_objectName;
    std::vector<ComponentInfo> m_components;
    int m_sharedBy = 0;
    std::function<void()> m_objectEditor;   // see setObject
    std::function<void()> m_addComponent;
    std::string m_selectedVar;
    std::string m_selectedRef;
    std::string m_selectedComponent;   // "" none, "@object" the Object itself, or a component's name
    enum class Focus { None, Variable, Reference, Component, Node };
    Focus m_focus = Focus::None;       // what the Details panel shows
    bool m_focusVarName = false;
    ImVec2 m_addButton = ImVec2(0, 0);
    ImVec2 m_addReferenceButton = ImVec2(0, 0);
    std::map<std::string, ImVec2> m_referenceRows;
    float m_sideWidthEm = 15.0f;    // width of the left column
    float m_detailsWidthEm = 17.0f; // width of the Details column on the right
    float m_codeWidthEm = 28.0f;    // width of the code pane, when it is open
    bool m_codeOpen = false;
    CodeBuffer m_code;                         // the text in the pane
    std::string m_codeApplied;                 // the file's text, as the pane last saw it (normalized)
    bool m_codeFileOk = false;                 // the file could be turned into a graph (so its lines belong to nodes)
    bool m_codeDirty = false;                  // the pane's text is not the file's
    bool m_codeFocused = false;                // typing goes to the pane
    bool m_applyingCode = false;
    bool m_applyQueued = false;
    unsigned m_codeSeenRevision = 0;           // the buffer's revision that codeChanged() last looked at
    unsigned m_codeFailedRevision = ~0u;       // a revision the file would not take (not tried again until it changes)
    double m_codeEditedAt = 0;                 // when the text was last typed in (ImGui time)
    double m_codeActiveAt = 0;                 // when the cursor last moved or text was typed (so it shows while that goes on)
    bool m_codeHasError = false;
    CodeError m_codeError;
    bool m_codeFollowCursor = false;           // scroll to the cursor next time it is drawn
    bool m_codeDragging = false;
    int m_codePageLines = 20;                  // what Page Up and Page Down move by
    std::vector<unsigned char> m_codeSection;  // per line: 0 before the header, 1 in header:, 2 in implement:
    size_t m_codeLongest = 0;                  // the index of the longest line (sets the width to scroll)
    unsigned m_codeIndexedRevision = ~0u;
    float m_codeGutterWidth = 0, m_codeCharWidth = 0;  // as drawn last frame
    std::map<std::string, int> m_nodeLine;     // node key -> the line it is written on (1-based)
    std::map<int, std::string> m_lineNode;     // line -> the statement or event written there
    std::map<int, ImVec2> m_codeRows;
    ImVec2 m_codeButton = ImVec2(0, 0);
    std::string m_codeShownKey;                // the node the pane last scrolled to
    CompileResult m_compile;
    bool m_resultsOpen = false;
    ImVec2 m_compileButton = ImVec2(0, 0);
    std::map<std::string, ImVec2> m_variableRows;
    std::map<std::string, ImVec2> m_localRows;
    ImVec2 m_localsHeader = ImVec2(0, 0);
    std::vector<Problem> m_liveProblems;  // the problems in the script as it is now (Compile copies them)

    struct Getter {
        int id = 0;
        std::string variable;
        ImVec2 pos;
    };
    std::vector<Getter> m_getters;
    int m_nextGetter = 1;
    float m_componentsShare = 0.25f; // share of the left column the components panel takes
    float m_variablesShare = 0.40f;  // ...and the variables panel (References gets the rest)
    std::map<std::string, std::string> m_fieldText;  // what is being typed in each panel text field
    std::filesystem::file_time_type m_fileTime{};
    std::vector<std::function<void()>> m_pending;  // work asked for while drawing, done after
};

} // namespace Propulsion
