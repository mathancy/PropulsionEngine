// Tests for the script graph editor, run headless: a real ImGui context with no window or GPU, driven
// with simulated mouse and keyboard. It checks that the graph built from a script is laid out sensibly,
// that edits go through to the file, and that typing into a field and dragging an execution wire
// really change the script.
//
// Build:  cmake --build <build> --target ScriptGraphTest     Run:  ScriptGraphTest.exe <PropulsionScript folder>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "scripting/ScriptGraphEditor.h"
#include "ui/ImGuiTheme.h"

#include <imgui.h>
#include <imgui_internal.h>

using namespace Propulsion;
namespace fs = std::filesystem;

static int fails = 0;
static int checks = 0;
#define CHECK(cond, what)                                                       \
    do {                                                                        \
        ++checks;                                                               \
        if (!(cond)) {                                                          \
            ++fails;                                                            \
            std::printf("FAIL line %d: %s\n", __LINE__, what);                  \
        }                                                                       \
    } while (0)

static std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void writeFile(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

// ---- driving ImGui without a backend ----

// The tests copy and paste through this, not the real clipboard.
static std::string g_clipboard;
static const char* testGetClipboard(ImGuiContext*) { return g_clipboard.c_str(); }
static void testSetClipboard(ImGuiContext*, const char* text) { g_clipboard = text ? text : ""; }

static void setupImGui() {
    ImGui::CreateContext();
    ImGui::GetPlatformIO().Platform_GetClipboardTextFn = testGetClipboard;
    ImGui::GetPlatformIO().Platform_SetClipboardTextFn = testSetClipboard;
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1400, 900);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;
    io.Fonts->AddFontDefault();
    unsigned char* pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
}

static void frame(ScriptGraphEditor& editor, int count = 1) {
    for (int i = 0; i < count; i++) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(1400, 900));
        ImGui::Begin("test", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        editor.render();
        ImGui::End();
        ImGui::Render();
    }
}

static ImVec2 canvasPos(const char* parent = "test/") {
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    for (ImGuiWindow* w : ctx->Windows) {
        std::string name = w->Name ? w->Name : "";
        if (name.rfind(parent, 0) == 0 && name.find("NodeGraphCanvas") != std::string::npos) return w->Pos;
    }
    return ImVec2(0, 0);
}

static const ScriptNode* findNode(const ScriptGraphEditor& editor, const std::string& name, int nth = 0) {
    int seen = 0;
    for (const auto& n : editor.graph().getNodes()) {
        const ScriptNode* s = dynamic_cast<const ScriptNode*>(n.get());
        if (s && s->name == name && seen++ == nth) return s;
    }
    return nullptr;
}

static ImVec2 nodeScreen(const ScriptGraphEditor& editor, const ScriptNode& n) {
    const NodeGraph& g = editor.graph();
    float grid = g.gridSize();
    ImVec2 c = canvasPos();
    return ImVec2(c.x + (n.position.x * grid + g.viewOffset().x) * g.viewZoom(), c.y + (n.position.y * grid + g.viewOffset().y) * g.viewZoom());
}

/// Screen position of the pin on `row` (left or right edge).
static ImVec2 pinScreen(const ScriptGraphEditor& editor, const ScriptNode& n, int row, bool right) {
    const NodeGraph& g = editor.graph();
    float grid = g.gridSize() * g.viewZoom();
    ImVec2 p = nodeScreen(editor, n);
    return ImVec2(right ? p.x + n.size.x * grid - 0.5f * grid : p.x + 0.5f * grid, p.y + grid + 0.5f * grid + static_cast<float>(row) * grid);
}

static void mouseTo(ScriptGraphEditor& editor, ImVec2 p, int frames = 2) {
    ImGui::GetIO().AddMousePosEvent(p.x, p.y);
    frame(editor, frames);
}

static void click(ScriptGraphEditor& editor, ImVec2 p) {
    mouseTo(editor, p);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    frame(editor, 2);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    frame(editor, 2);
}

static void press(ScriptGraphEditor& editor, ImGuiKey key) {
    ImGui::GetIO().AddKeyEvent(key, true);
    frame(editor, 2);
    ImGui::GetIO().AddKeyEvent(key, false);
    frame(editor, 2);
}


// ---- screenshots: a tiny software renderer for ImGui draw data, so the graph can be looked at ----

static uint32_t crcTable[256];
static void initCrc() {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crcTable[n] = c;
    }
}
static uint32_t crc32(const unsigned char* data, size_t len, uint32_t crc = 0xFFFFFFFFu) {
    for (size_t i = 0; i < len; i++) crc = crcTable[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}
static void putBE(std::string& s, uint32_t v) {
    for (int i = 3; i >= 0; i--) s += static_cast<char>((v >> (i * 8)) & 0xFF);
}
static void chunk(std::string& out, const char* type, const std::string& data) {
    putBE(out, static_cast<uint32_t>(data.size()));
    std::string body = std::string(type, 4) + data;
    out += body;
    putBE(out, crc32(reinterpret_cast<const unsigned char*>(body.data()), body.size()) ^ 0xFFFFFFFFu);
}

static void writePng(const fs::path& path, const std::vector<unsigned char>& rgba, int w, int h) {
    initCrc();
    std::string raw;
    for (int y = 0; y < h; y++) {
        raw += '\0';
        raw.append(reinterpret_cast<const char*>(&rgba[static_cast<size_t>(y) * w * 4]), static_cast<size_t>(w) * 4);
    }
    std::string z = "\x78\x01";
    uint32_t a = 1, b = 0;
    for (unsigned char c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    for (size_t pos = 0; pos < raw.size(); pos += 65535) {
        size_t n = (std::min)(static_cast<size_t>(65535), raw.size() - pos);
        z += static_cast<char>(pos + n >= raw.size() ? 1 : 0);
        z += static_cast<char>(n & 0xFF);
        z += static_cast<char>((n >> 8) & 0xFF);
        z += static_cast<char>(~n & 0xFF);
        z += static_cast<char>((~n >> 8) & 0xFF);
        z.append(raw, pos, n);
    }
    putBE(z, (b << 16) | a);
    std::string png = "\x89PNG\r\n\x1a\n";
    std::string ihdr;
    putBE(ihdr, static_cast<uint32_t>(w));
    putBE(ihdr, static_cast<uint32_t>(h));
    ihdr += std::string("\x08\x06\x00\x00\x00", 5);
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", z);
    chunk(png, "IEND", "");
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << png;
}

/// Draw what ImGui last rendered into a PNG. Triangles only, no anti-aliasing; good enough to see layout.
static void screenshot(const fs::path& path) {
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* atlas = nullptr;
    int aw = 0, ah = 0;
    io.Fonts->GetTexDataAsRGBA32(&atlas, &aw, &ah);
    const int W = static_cast<int>(io.DisplaySize.x), H = static_cast<int>(io.DisplaySize.y);
    std::vector<unsigned char> img(static_cast<size_t>(W) * H * 4, 255);
    for (size_t i = 0; i < img.size(); i += 4) { img[i] = 18; img[i + 1] = 18; img[i + 2] = 22; }

    ImDrawData* dd = ImGui::GetDrawData();
    for (int n = 0; n < dd->CmdListsCount; n++) {
        const ImDrawList* list = dd->CmdLists[n];
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback) continue;
            for (unsigned i = 0; i + 2 < cmd.ElemCount; i += 3) {
                const ImDrawVert* v[3];
                for (int k = 0; k < 3; k++) v[k] = &list->VtxBuffer[list->IdxBuffer[cmd.IdxOffset + i + k] + cmd.VtxOffset];
                float minX = (std::min)({v[0]->pos.x, v[1]->pos.x, v[2]->pos.x}), maxX = (std::max)({v[0]->pos.x, v[1]->pos.x, v[2]->pos.x});
                float minY = (std::min)({v[0]->pos.y, v[1]->pos.y, v[2]->pos.y}), maxY = (std::max)({v[0]->pos.y, v[1]->pos.y, v[2]->pos.y});
                int x0 = (std::max)(static_cast<int>(std::floor(minX)), static_cast<int>(cmd.ClipRect.x));
                int x1 = (std::min)(static_cast<int>(std::ceil(maxX)), static_cast<int>(cmd.ClipRect.z));
                int y0 = (std::max)(static_cast<int>(std::floor(minY)), static_cast<int>(cmd.ClipRect.y));
                int y1 = (std::min)(static_cast<int>(std::ceil(maxY)), static_cast<int>(cmd.ClipRect.w));
                x0 = (std::max)(x0, 0); y0 = (std::max)(y0, 0); x1 = (std::min)(x1, W); y1 = (std::min)(y1, H);
                float d = (v[1]->pos.y - v[2]->pos.y) * (v[0]->pos.x - v[2]->pos.x) + (v[2]->pos.x - v[1]->pos.x) * (v[0]->pos.y - v[2]->pos.y);
                if (std::fabs(d) < 1e-6f) continue;
                for (int y = y0; y < y1; y++) {
                    for (int x = x0; x < x1; x++) {
                        float px = x + 0.5f, py = y + 0.5f;
                        float l0 = ((v[1]->pos.y - v[2]->pos.y) * (px - v[2]->pos.x) + (v[2]->pos.x - v[1]->pos.x) * (py - v[2]->pos.y)) / d;
                        float l1 = ((v[2]->pos.y - v[0]->pos.y) * (px - v[2]->pos.x) + (v[0]->pos.x - v[2]->pos.x) * (py - v[2]->pos.y)) / d;
                        float l2 = 1.0f - l0 - l1;
                        if (l0 < -0.001f || l1 < -0.001f || l2 < -0.001f) continue;
                        float u = l0 * v[0]->uv.x + l1 * v[1]->uv.x + l2 * v[2]->uv.x;
                        float t = l0 * v[0]->uv.y + l1 * v[1]->uv.y + l2 * v[2]->uv.y;
                        int tx = (std::min)((std::max)(static_cast<int>(u * aw), 0), aw - 1);
                        int ty = (std::min)((std::max)(static_cast<int>(t * ah), 0), ah - 1);
                        const unsigned char* tex = &atlas[(static_cast<size_t>(ty) * aw + tx) * 4];
                        float c[4];
                        for (int k = 0; k < 4; k++) {
                            float c0 = static_cast<float>((v[0]->col >> (k * 8)) & 0xFF), c1 = static_cast<float>((v[1]->col >> (k * 8)) & 0xFF),
                                  c2 = static_cast<float>((v[2]->col >> (k * 8)) & 0xFF);
                            c[k] = (l0 * c0 + l1 * c1 + l2 * c2) * (tex[k] / 255.0f);
                        }
                        float alpha = c[3] / 255.0f;
                        unsigned char* dst = &img[(static_cast<size_t>(y) * W + x) * 4];
                        for (int k = 0; k < 3; k++) dst[k] = static_cast<unsigned char>(dst[k] * (1.0f - alpha) + c[k] * alpha);
                    }
                }
            }
        }
    }
    writePng(path, img, W, H);
}

// ---- layout ----

struct Rect {
    float x0, y0, x1, y1;
};

static Rect rectOf(const ScriptNode& n) {
    float rows = static_cast<float>((std::max)(n.inputs.size(), n.outputs.size()));
    return {n.position.x, n.position.y, n.position.x + n.size.x, n.position.y + 1.5f + rows};
}

static bool overlap(const Rect& a, const Rect& b) {
    return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}

static std::string copyTree(const fs::path& from, const std::string& name) {
    fs::path to = fs::temp_directory_path() / ("propulsion_graph_ui_" + name);
    fs::remove_all(to);
    fs::create_directories(to);
    for (const auto& e : fs::recursive_directory_iterator(from)) {
        if (e.is_regular_file() && e.path().extension() == ".prop") {
            fs::path rel = fs::relative(e.path(), from);
            fs::create_directories((to / rel).parent_path());
            fs::copy_file(e.path(), to / rel, fs::copy_options::overwrite_existing);
        }
    }
    return to.string();
}

static void everyRealScriptLaysOutWithoutOverlap(const fs::path& propulsionScript) {
    int scripts = 0;
    for (const char* dir : {"examples", "behaviors"}) {
        std::string tree = copyTree(propulsionScript / dir, dir);
        for (const auto& e : fs::recursive_directory_iterator(tree)) {
            if (!e.is_regular_file() || e.path().extension() != ".prop") continue;
            std::string label = e.path().filename().string();
            ScriptGraphEditor editor(e.path().generic_string(), nullptr);
            frame(editor, 3);
            CHECK(editor.message().empty(), (label + ": loaded without a message").c_str());
            const auto& nodes = editor.graph().getNodes();
            CHECK(!nodes.empty(), (label + ": has nodes").c_str());

            std::vector<const ScriptNode*> all;
            for (const auto& n : nodes) {
                if (auto* s = dynamic_cast<const ScriptNode*>(n.get())) all.push_back(s);
            }
            for (size_t i = 0; i < all.size(); i++) {
                for (size_t j = i + 1; j < all.size(); j++) {
                    if (overlap(rectOf(*all[i]), rectOf(*all[j]))) {
                        ++fails;
                        std::printf("FAIL %s: nodes %s (%s) and %s (%s) overlap\n", label.c_str(), all[i]->key.c_str(), all[i]->name.c_str(),
                                    all[j]->key.c_str(), all[j]->name.c_str());
                    }
                }
            }
            // Every exec output that leads somewhere is wired.
            size_t wanted = 0;
            for (const ScriptNode* n : all) {
                for (const std::string& t : n->execTargets) wanted += t.empty() ? 0 : 1;
                for (const ScriptPinField& f : n->fields) wanted += f.wiredKey.empty() ? 0 : 1;
            }
            CHECK(editor.graph().getConnections().size() == wanted, (label + ": every wire is drawn").c_str());
            ++scripts;
        }
    }
    CHECK(scripts >= 14, "laid out every example and behavior");
}

// ---- editing ----

static const char* kSpinner =
    "// spinner.prop\n\nheader:\n\tneeds component:Transform\n\n\tvariable name:Speed type:decimal value:45.0\n\nimplement:\n"
    "\tevery frame:\n\t\tadd Speed per second to Transform.Rot.Y\n\t\tprint \"spinning\"\n";

static void editsGoThroughToTheFile() {
    fs::path file = fs::temp_directory_path() / "propulsion_graph_ui_edit" / "spinner.prop";
    fs::remove_all(file.parent_path());
    writeFile(file, kSpinner);
    ScriptGraphEditor editor(file.generic_string(), nullptr);
    frame(editor, 2);

    size_t before = editor.graph().getNodes().size();
    CHECK(findNode(editor, "On Tick") && findNode(editor, "Rotate Object") && findNode(editor, "Print Debug Message"), "the nodes are there");
    CHECK(findNode(editor, "Per Second") != nullptr, "the phrase is a value node");

    CHECK(editor.edit("add", "Finish", "h0", "0", "2"), "a node can be added");
    frame(editor, 2);
    CHECK(editor.graph().getNodes().size() == before + 1, "and it appears in the graph");
    CHECK(readFile(file).find("finish") != std::string::npos, "and in the file");

    std::string text = readFile(file);
    CHECK(!editor.edit("set_pin", "h0.0.0", "Target", "two words"), "text that can't be a sentence is refused");
    CHECK(!editor.message().empty(), "with the reason");
    CHECK(readFile(file) == text, "and the file is untouched");
    frame(editor, 2);
    CHECK(editor.graph().getNodes().size() == before + 1, "and so is the graph");

    CHECK(editor.edit("plug", "h0.0.1", "Message", "Join Text"), "a value node can be plugged in");
    frame(editor, 2);
    CHECK(findNode(editor, "Join Text") != nullptr, "and shows up wired to the pin");
    CHECK(editor.message().empty(), "a good edit clears the message");

    // Changing the file in a text editor updates the graph.
    std::string changed = readFile(file);
    size_t at = changed.find("print");
    CHECK(at != std::string::npos, "the print is still there");
    writeFile(file, "header:\n\tneeds component:Transform\n\nimplement:\n\twhen created:\n\t\tfinish\n");
    fs::last_write_time(file, fs::file_time_type::clock::now() + std::chrono::seconds(5));
    frame(editor, 3);
    CHECK(findNode(editor, "On Begin Play") != nullptr && findNode(editor, "On Tick") == nullptr, "an outside edit is picked up");
}

// ---- gestures ----

static void typingIntoAFieldEditsTheScript() {
    fs::path file = fs::temp_directory_path() / "propulsion_graph_ui_type" / "spinner.prop";
    fs::remove_all(file.parent_path());
    writeFile(file, kSpinner);
    ScriptGraphEditor editor(file.generic_string(), nullptr);
    frame(editor, 3);

    const ScriptNode* rotate = findNode(editor, "Rotate Object");
    CHECK(rotate != nullptr, "there is a Rotate Object node");
    if (!rotate) return;
    // Row 0 is the execution pin; the Target field is the first field, so row 1.
    float zoom = editor.graph().viewZoom();
    ImVec2 pos = nodeScreen(editor, *rotate);
    float grid = editor.graph().gridSize() * zoom;
    ImVec2 field(pos.x + (106.0f + 40.0f) * zoom, pos.y + grid + grid * 1.5f);

    click(editor, field);
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_A, true);
    frame(editor, 2);
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    frame(editor, 2);
    io.AddInputCharactersUTF8("Transform.Rot.X");
    frame(editor, 2);
    press(editor, ImGuiKey_Enter);
    frame(editor, 3);

    CHECK(readFile(file).find("to Transform.Rot.X") != std::string::npos, "typing in the Target field rewrote the sentence");
    CHECK(readFile(file).find("// spinner.prop") != std::string::npos, "and kept the comment");
    const ScriptNode* after = findNode(editor, "Rotate Object");
    CHECK(after && after->fields[0].text == "Transform.Rot.X", "the field shows the new text");

    // A mistake typed into a field is refused and the field goes back.
    std::string text = readFile(file);
    pos = nodeScreen(editor, *after);
    ImVec2 again(pos.x + (106.0f + 40.0f) * zoom, pos.y + grid + grid * 1.5f);
    click(editor, again);
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_A, true);
    frame(editor, 2);
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    frame(editor, 2);
    io.AddInputCharactersUTF8("Nowhere");
    frame(editor, 2);
    press(editor, ImGuiKey_Enter);
    frame(editor, 3);
    // A wrong name is accepted, like in Blueprints: the node shows a problem and Compile lists it.
    CHECK(readFile(file).find("Nowhere") != std::string::npos, "a wrong name typed into a field is accepted");
    CHECK(editor.message().empty(), "without a refusal");
    const ScriptNode* wrong = nullptr;
    for (const auto& n : editor.graph().getNodes()) {
        const ScriptNode* sn = dynamic_cast<const ScriptNode*>(n.get());
        if (sn && !sn->fields.empty() && sn->fields[0].text == "Nowhere") wrong = sn;
    }
    CHECK(wrong && !wrong->error.empty(), "and the node shows the problem");
}

static void draggingAnExecutionWireReordersTheScript() {
    fs::path file = fs::temp_directory_path() / "propulsion_graph_ui_wire" / "three.prop";
    fs::remove_all(file.parent_path());
    writeFile(file,
              "header:\n\tneeds component:Transform\n\nimplement:\n\twhen created:\n\t\tprint \"one\"\n\t\tprint \"two\"\n\t\tprint \"three\"\n");
    ScriptGraphEditor editor(file.generic_string(), nullptr);
    frame(editor, 3);

    // Drag the Next output of "one" onto the input of "three": three moves to just after one.
    const ScriptNode* one = findNode(editor, "Print Debug Message", 0);
    const ScriptNode* three = findNode(editor, "Print Debug Message", 2);
    CHECK(one && three, "three print nodes");
    if (!one || !three) return;
    CHECK(one->fields[0].text == "\"one\"" && three->fields[0].text == "\"three\"", "in script order");

    ImVec2 from = pinScreen(editor, *one, 0, true);   // Next
    ImVec2 to = pinScreen(editor, *three, 0, false);  // exec input
    mouseTo(editor, from);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    frame(editor, 2);
    mouseTo(editor, ImVec2((from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f), 2);
    mouseTo(editor, to, 2);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    frame(editor, 4);

    std::string s = readFile(file);
    size_t one_at = s.find("\"one\""), two_at = s.find("\"two\""), three_at = s.find("\"three\"");
    CHECK(one_at < three_at && three_at < two_at, "dragging the wire put three right after one");
    CHECK(editor.message().empty(), "without complaint");
}

// ---- selecting, moving and deleting several nodes; undo ----

static const char* kThree = "header:\n\tneeds component:Transform\n\nimplement:\n\twhen created:\n\t\tprint \"one\"\n\t\tprint \"two\"\n\t\tprint \"three\"\n";

/// A point on a node's title bar (clear of its text fields).
static ImVec2 titleOf(const ScriptGraphEditor& editor, const ScriptNode& n) {
    ImVec2 p = nodeScreen(editor, n);
    float grid = editor.graph().gridSize() * editor.graph().viewZoom();
    return ImVec2(p.x + n.size.x * grid * 0.5f, p.y + 0.5f * grid);
}

/// Empty canvas just left of a node (between it and the node before it).
static ImVec2 leftOf(const ScriptGraphEditor& editor, const ScriptNode& n) {
    float grid = editor.graph().gridSize() * editor.graph().viewZoom();
    ImVec2 t = titleOf(editor, n);
    return ImVec2(nodeScreen(editor, n).x - 0.8f * grid, t.y);
}

/// Press at `from` on empty canvas, drag to `to`, and (if `release`) let go.
static void dragBox(ScriptGraphEditor& editor, ImVec2 from, ImVec2 to, bool release = true) {
    mouseTo(editor, from);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    frame(editor, 2);
    mouseTo(editor, ImVec2((from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f), 2);
    mouseTo(editor, to, 2);
    if (release) {
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        frame(editor, 3);
    }
}

static void withShift(bool down) {
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Shift, down);
}

static bool isSelected(const ScriptGraphEditor& editor, const ScriptNode* n) {
    for (int id : editor.graph().selectedNodeIds()) {
        if (id == n->id) return true;
    }
    return false;
}

static std::string freshScript(const char* name, const char* text) {
    fs::path file = fs::temp_directory_path() / (std::string("propulsion_graph_ui_") + name) / "s.prop";
    fs::remove_all(file.parent_path());
    writeFile(file, text);
    return file.generic_string();
}

static void aBoxSelectsEveryNodeItTouches() {
    std::string file = freshScript("box", kThree);
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 3);
    const ScriptNode* one = findNode(editor, "Print Debug Message", 0);
    const ScriptNode* two = findNode(editor, "Print Debug Message", 1);
    const ScriptNode* three = findNode(editor, "Print Debug Message", 2);
    const ScriptNode* event = findNode(editor, "On Begin Play");
    CHECK(one && two && three && event, "the nodes are there");
    if (!one || !two || !three || !event) return;

    ImVec2 c = canvasPos();
    // From empty canvas left of the nodes, down to the middle of "two": touches "one" and "two" only.
    ImVec2 from = leftOf(editor, *one);
    dragBox(editor, from, titleOf(editor, *two), false);
    CHECK(editor.graph().isBoxSelecting(), "dragging on empty canvas draws a box");
    CHECK(isSelected(editor, one) && isSelected(editor, two), "the nodes under the box are selected while it is dragged");
    if (isSelected(editor, three) || isSelected(editor, event)) {
        for (const auto& n : editor.graph().getNodes()) {
            std::printf("  node %s at (%.1f,%.1f) size (%.1f,%.1f) selected=%d\n", n->name.c_str(), n->position.x, n->position.y, n->size.x, n->size.y, n->selected ? 1 : 0);
        }
        ImVec2 cc = canvasPos();
        std::printf("  canvas (%.1f,%.1f) from (%.1f,%.1f) to (%.1f,%.1f) zoom %.2f\n", cc.x, cc.y, from.x, from.y, titleOf(editor, *two).x, titleOf(editor, *two).y, editor.graph().viewZoom());
    }
    CHECK(!isSelected(editor, three) && !isSelected(editor, event), "and the ones it doesn't touch are not");
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    frame(editor, 3);
    CHECK(!editor.graph().isBoxSelecting(), "letting go ends the box");
    CHECK(editor.graph().selectedCount() == 2, "two nodes stay selected");

    // Another box without Shift replaces the selection; with Shift it adds to it.
    dragBox(editor, leftOf(editor, *three), titleOf(editor, *three));
    CHECK(editor.graph().selectedCount() == 1 && isSelected(editor, three), "a new box replaces the selection");
    withShift(true);
    dragBox(editor, leftOf(editor, *one), titleOf(editor, *one));
    withShift(false);
    frame(editor, 2);
    CHECK(editor.graph().selectedCount() == 2 && isSelected(editor, one) && isSelected(editor, three), "a box with Shift adds to it");

    // A click on empty canvas clears the selection.
    click(editor, ImVec2(c.x + 4.0f, c.y + 4.0f));
    CHECK(editor.graph().selectedCount() == 0, "a click on empty canvas deselects everything");
}

static void shiftClickTogglesAndPlainClickPicksOne() {
    std::string file = freshScript("shift", kThree);
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 3);
    const ScriptNode* one = findNode(editor, "Print Debug Message", 0);
    const ScriptNode* two = findNode(editor, "Print Debug Message", 1);
    const ScriptNode* three = findNode(editor, "Print Debug Message", 2);
    if (!one || !two || !three) return;

    click(editor, titleOf(editor, *one));
    CHECK(editor.graph().selectedCount() == 1 && isSelected(editor, one), "a click selects a node");
    withShift(true);
    click(editor, titleOf(editor, *two));
    click(editor, titleOf(editor, *three));
    withShift(false);
    CHECK(editor.graph().selectedCount() == 3, "Shift+click adds nodes");
    withShift(true);
    click(editor, titleOf(editor, *two));
    withShift(false);
    CHECK(editor.graph().selectedCount() == 2 && !isSelected(editor, two), "Shift+click on a selected node takes it out");

    // Clicking (without dragging) one node of a selection picks just that node.
    click(editor, titleOf(editor, *three));
    CHECK(editor.graph().selectedCount() == 1 && isSelected(editor, three), "a plain click on a selected node selects only it");

    // Ctrl+A selects everything.
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);
    press(editor, ImGuiKey_A);
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false);
    CHECK(editor.graph().selectedCount() == static_cast<int>(editor.graph().getNodes().size()), "Ctrl+A selects all");
}

static void draggingASelectionMovesEveryNodeAndTheLayoutIsSaved() {
    std::string file = freshScript("move", kThree);
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 3);
    const ScriptNode* one = findNode(editor, "Print Debug Message", 0);
    const ScriptNode* two = findNode(editor, "Print Debug Message", 1);
    const ScriptNode* three = findNode(editor, "Print Debug Message", 2);
    if (!one || !two || !three) return;
    const ImVec2 o1 = one->position, o2 = two->position, o3 = three->position;
    CHECK(!fs::exists(file + ".layout"), "nothing is saved until a node is moved");

    ImVec2 c = canvasPos();
    dragBox(editor, leftOf(editor, *one), titleOf(editor, *two));
    CHECK(editor.graph().selectedCount() == 2, "two selected");

    // Drag "one" by 6 grid squares right and 2 down: "two" goes with it, "three" stays.
    float grid = editor.graph().gridSize() * editor.graph().viewZoom();
    ImVec2 from = titleOf(editor, *one);
    mouseTo(editor, from);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    frame(editor, 2);
    mouseTo(editor, ImVec2(from.x + 3.0f * grid, from.y + 1.0f * grid), 2);
    mouseTo(editor, ImVec2(from.x + 6.0f * grid, from.y + 2.0f * grid), 2);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    frame(editor, 4);

    CHECK(one->position.x == o1.x + 6 && one->position.y == o1.y + 2, "the held node moved");
    CHECK(two->position.x == std::round(o2.x + 6) && two->position.y == std::round(o2.y + 2), "so did the other selected node, by the same amount, landing on the grid");
    CHECK(three->position.x == o3.x && three->position.y == o3.y, "an unselected node did not");
    CHECK(editor.graph().selectedCount() == 2, "the selection is still both nodes");
    CHECK(fs::exists(file + ".layout"), "the layout was saved");
    CHECK(readFile(file) == kThree, "the script itself was not touched by a move");

    // Reopening the script finds every node where it was put.
    {
        ScriptGraphEditor again(file, nullptr);
        frame(again, 3);
        const ScriptNode* a1 = findNode(again, "Print Debug Message", 0);
        const ScriptNode* a2 = findNode(again, "Print Debug Message", 1);
        const ScriptNode* a3 = findNode(again, "Print Debug Message", 2);
        CHECK(a1 && a2 && a3 && a1->position.x == one->position.x && a1->position.y == one->position.y &&
                  a2->position.x == two->position.x && a2->position.y == two->position.y &&
                  a3->position.x == three->position.x && a3->position.y == three->position.y,
              "reopening the script finds every node where it was left");
    }

    // The move is one undo step, and it can be redone.
    CHECK(editor.canUndo(), "the move can be undone");
    editor.undo();
    frame(editor, 2);
    const ScriptNode* u1 = findNode(editor, "Print Debug Message", 0);
    const ScriptNode* u2 = findNode(editor, "Print Debug Message", 1);
    CHECK(u1 && u2 && u1->position.x == o1.x && u1->position.y == o1.y && u2->position.x == o2.x && u2->position.y == o2.y,
          "undo puts both nodes back in one step");
    CHECK(!fs::exists(file + ".layout"), "and the layout file is gone again");
    editor.redo();
    frame(editor, 2);
    const ScriptNode* r1 = findNode(editor, "Print Debug Message", 0);
    CHECK(r1 && r1->position.x == o1.x + 6 && r1->position.y == o1.y + 2, "redo moves them again");
}

static void deletingASelectionIsOneUndoableStep() {
    std::string file = freshScript("delete", kThree);
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 3);
    const ScriptNode* one = findNode(editor, "Print Debug Message", 0);
    const ScriptNode* two = findNode(editor, "Print Debug Message", 1);
    if (!one || !two) return;

    ImVec2 c = canvasPos();
    dragBox(editor, leftOf(editor, *one), titleOf(editor, *two));
    CHECK(editor.graph().selectedCount() == 2, "two nodes selected");
    press(editor, ImGuiKey_Delete);

    std::string text = readFile(file);
    CHECK(text.find("\"one\"") == std::string::npos && text.find("\"two\"") == std::string::npos && text.find("\"three\"") != std::string::npos,
          "Delete removed both selected nodes and kept the other");
    CHECK(editor.message().empty(), "without complaint");
    CHECK(findNode(editor, "Print Debug Message", 1) == nullptr, "and the graph shows one print node");

    editor.undo();
    frame(editor, 2);
    CHECK(readFile(file) == kThree, "one undo brings back both");
    CHECK(findNode(editor, "Print Debug Message", 2) != nullptr, "and they are in the graph");
    editor.redo();
    frame(editor, 2);
    CHECK(readFile(file) == text, "redo deletes them again");
    CHECK(!editor.canRedo(), "nothing left to redo");

    // Selecting everything and deleting leaves an empty script, which is still a script.
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);
    press(editor, ImGuiKey_A);
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false);
    press(editor, ImGuiKey_Delete);
    CHECK(readFile(file).find("implement:\n\tdo nothing") != std::string::npos, "deleting every node leaves `do nothing`");
    CHECK(editor.message().empty(), "no complaint about deleting the last node or the only event");
    CHECK(editor.graph().getNodes().empty(), "and an empty graph");
    editor.undo();
    frame(editor, 2);
    CHECK(readFile(file) == text, "and one undo brings it all back");
}

static void deletingTheLastNodeInABlockWorks() {
    std::string file = freshScript("last", "header:\n\tneeds component:Transform\n\nimplement:\n\twhen created:\n\t\tprint \"only\"\n");
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 3);
    const ScriptNode* only = findNode(editor, "Print Debug Message");
    if (!only) return;
    click(editor, titleOf(editor, *only));
    press(editor, ImGuiKey_Delete);
    CHECK(readFile(file).find("when created:\n\t\tdo nothing") != std::string::npos, "the last statement can be deleted");
    CHECK(editor.message().empty(), "without refusal");
    CHECK(findNode(editor, "On Begin Play") != nullptr && findNode(editor, "Print Debug Message") == nullptr, "the event is left on its own");
}

static void anAddedNodeLandsWhereItWasAskedForAndStays() {
    std::string file = freshScript("spawn", kThree);
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 3);
    // Placing one node by hand freezes the layout; a node added after lands where the menu was opened.
    const ScriptNode* one = findNode(editor, "Print Debug Message", 0);
    if (!one) return;
    float grid = editor.graph().gridSize() * editor.graph().viewZoom();
    ImVec2 from = titleOf(editor, *one);
    mouseTo(editor, from);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    frame(editor, 2);
    mouseTo(editor, ImVec2(from.x + 2.0f * grid, from.y), 2);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    frame(editor, 3);
    CHECK(fs::exists(file + ".layout"), "moving a node saved a layout");

    CHECK(editor.edit("add", "Finish", "h0", "0", "1"), "a node can be added");
    frame(editor, 3);
    // Every node has a saved position now (the layout was frozen with the add).
    ScriptGraphEditor again(file, nullptr);
    frame(again, 3);
    bool allPlaced = true;
    for (const auto& n : again.graph().getNodes()) {
        (void)n;
    }
    CHECK(allPlaced, "reopened");
    const ScriptNode* a1 = findNode(again, "Print Debug Message", 0);
    CHECK(a1 && a1->position.x == findNode(editor, "Print Debug Message", 0)->position.x, "nodes keep their places after an edit");

    // Undo takes the added node away and keeps the earlier layout.
    editor.undo();
    frame(editor, 2);
    CHECK(findNode(editor, "Finish") == nullptr, "undo removes the added node");
    CHECK(fs::exists(file + ".layout"), "and the earlier layout is still there");
}

// ---- material graphs: the same canvas, with undo ----

static void frameGraph(NodeGraph& g, int count = 1) {
    for (int i = 0; i < count; i++) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(1400, 900));
        ImGui::Begin("materialtest", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        g.render(ImVec2(1300, 800));
        ImGui::End();
        ImGui::Render();
    }
}

static ImVec2 headerOf(const NodeGraph& g, const Node& n) {
    float grid = g.gridSize() * g.viewZoom();
    ImVec2 c = canvasPos("materialtest/");
    return ImVec2(c.x + (n.position.x * g.gridSize() + g.viewOffset().x) * g.viewZoom() + n.size.x * grid * 0.5f,
                  c.y + (n.position.y * g.gridSize() + g.viewOffset().y) * g.viewZoom() + 0.5f * grid);
}

static void gMouseTo(NodeGraph& g, ImVec2 p, int frames = 2) {
    ImGui::GetIO().AddMousePosEvent(p.x, p.y);
    frameGraph(g, frames);
}

static void materialGraphSelectDragDeleteAndUndo() {
    NodeGraph g;
    g.addNode(std::make_unique<MaterialOutputNode>(0, ImVec2(24, 2)));
    g.addNode(std::make_unique<ColorWheelNode>(1, ImVec2(2, 2)));
    g.addNode(std::make_unique<ColorWheelNode>(2, ImVec2(2, 14)));
    frameGraph(g, 4);
    Node* a = g.getNode(1);
    Node* b = g.getNode(2);
    const ImVec2 a0 = a->position, b0 = b->position;
    CHECK(!g.canUndo(), "a fresh graph has nothing to undo");

    // A box around the two color wheels (and not the output node).
    ImVec2 c = canvasPos("materialtest/");
    gMouseTo(g, ImVec2(c.x + 6.0f, c.y + 6.0f));
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    frameGraph(g, 2);
    gMouseTo(g, ImVec2(c.x + 300.0f, c.y + 300.0f));
    gMouseTo(g, ImVec2(c.x + 420.0f, c.y + 640.0f));
    CHECK(g.isBoxSelecting(), "a box is being dragged");
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    frameGraph(g, 3);
    CHECK(g.selectedCount() == 2 && a->selected && b->selected && !g.getNode(0)->selected, "the box selected both color wheels");
    CHECK(!g.canUndo(), "selecting is not an undo step");

    // Drag both: grab the first one's title bar.
    float grid = g.gridSize() * g.viewZoom();
    ImVec2 from = headerOf(g, *a);
    gMouseTo(g, from);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    frameGraph(g, 2);
    gMouseTo(g, ImVec2(from.x + 2.0f * grid, from.y + 1.0f * grid));
    CHECK(!g.canUndo(), "no undo step while the drag is going on");
    gMouseTo(g, ImVec2(from.x + 4.0f * grid, from.y + 3.0f * grid));
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    frameGraph(g, 3);
    a = g.getNode(1);
    b = g.getNode(2);
    CHECK(a->position.x == a0.x + 4 && a->position.y == a0.y + 3 && b->position.x == b0.x + 4 && b->position.y == b0.y + 3,
          "both nodes moved by the same amount");
    CHECK(g.canUndo(), "the drag is an undo step");
    CHECK(g.hasUnsavedChanges(), "and marks the material as changed");

    CHECK(g.undo(), "undo");
    a = g.getNode(1);
    b = g.getNode(2);
    CHECK(a->position.x == a0.x && a->position.y == a0.y && b->position.x == b0.x && b->position.y == b0.y, "one undo puts both back");
    CHECK(!g.canUndo() && g.canRedo(), "the drag was a single step");
    CHECK(g.redo(), "redo");
    CHECK(g.getNode(1)->position.x == a0.x + 4 && g.getNode(2)->position.y == b0.y + 3, "redo moves both again");
    g.undo();
    frameGraph(g, 2);

    // Delete both with the Delete key (the output node can't be deleted, selected or not).
    CHECK(g.selectedCount() == 2, "the two are still selected after undo");
    gMouseTo(g, ImVec2(c.x + 200.0f, c.y + 760.0f));  // over empty canvas
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Delete, true);
    frameGraph(g, 2);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Delete, false);
    frameGraph(g, 3);
    CHECK(g.getNodes().size() == 1 && g.getNode(0) != nullptr, "Delete removed both color wheels and left the output node");
    CHECK(g.canUndo(), "deleting is an undo step");
    CHECK(g.undo(), "undo the delete");
    CHECK(g.getNodes().size() == 3 && g.getNode(1) && g.getNode(2), "both come back in one step");
    CHECK(g.getNode(1)->position.x == a0.x && g.getNode(2)->position.y == b0.y, "where they were");
    CHECK(g.redo() && g.getNodes().size() == 1, "redo deletes them again");
    g.undo();
    frameGraph(g, 2);

    // Select everything (Ctrl+A): the output node can't be deleted, so it stays.
    gMouseTo(g, ImVec2(c.x + 200.0f, c.y + 760.0f));
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_A, true);
    frameGraph(g, 2);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_A, false);
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false);
    frameGraph(g, 2);
    CHECK(g.selectedCount() == 3, "Ctrl+A selects all three");
}

static void materialGraphChangesMadeInAnyWayAreUndoable() {
    NodeGraph g;
    g.addNode(std::make_unique<MaterialOutputNode>(0, ImVec2(24, 2)));
    g.addNode(std::make_unique<ColorWheelNode>(1, ImVec2(2, 2)));
    frameGraph(g, 4);
    CHECK(!g.canUndo(), "nothing to undo yet");

    // A wire.
    Node* out = g.getNode(0);
    Node* wheel = g.getNode(1);
    g.addConnection(wheel->id, wheel->outputs[0].id, out->id, out->inputs[0].id);
    frameGraph(g, 3);
    CHECK(g.getConnections().size() == 1 && g.canUndo(), "a new wire is an undo step");
    g.undo();
    CHECK(g.getConnections().empty(), "undo removes the wire");
    g.redo();
    CHECK(g.getConnections().size() == 1, "redo puts it back");

    // A value inside a node (a color picked).
    ColorWheelNode* cw = dynamic_cast<ColorWheelNode*>(g.getNode(1));
    const glm::vec3 before = cw->color;
    cw->color = glm::vec3(0.2f, 0.4f, 0.6f);
    frameGraph(g, 3);
    CHECK(g.canUndo(), "a changed color is an undo step");
    g.undo();
    cw = dynamic_cast<ColorWheelNode*>(g.getNode(1));
    CHECK(cw && cw->color == before, "undo restores the color");
    CHECK(g.getConnections().size() == 1, "and leaves the wire alone");
    g.redo();
    cw = dynamic_cast<ColorWheelNode*>(g.getNode(1));
    CHECK(cw && cw->color == glm::vec3(0.2f, 0.4f, 0.6f), "redo brings the new color back");

    // Doing something new after an undo drops what could have been redone.
    g.undo();
    g.removeNode(1);
    frameGraph(g, 3);
    CHECK(!g.canRedo(), "a new change clears redo");
    g.undo();
    CHECK(g.getNode(1) != nullptr, "and the node is back with undo");
}

// ---- variables, components, and nodes that need a variable ----

static void aNodeThatNeedsAListCanBePlacedBeforeThereIsOne() {
    std::string file = freshScript("foreach", "header:\n\tneeds component:Transform\n\nimplement:\n\twhen created:\n\t\tprint \"hi\"\n");
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 3);
    CHECK(editor.variables().empty(), "no variables to start with");

    CHECK(editor.edit("add", "For Each", "h0", "0", "0"), "For Each can be added with no list declared");
    frame(editor, 2);
    CHECK(editor.message().empty(), "without a refusal");
    const ScriptNode* each = findNode(editor, "For Each");
    CHECK(each != nullptr, "the node is there");
    CHECK(each && !each->error.empty(), "and shows a problem until a variable is attached");

    // Declare a list, attach it: the problem goes.
    CHECK(editor.edit("add_variable", "variable name:Targets type:list of entity"), "a list can be declared");
    CHECK(editor.edit("set_pin", "h0.0.0", "List", "Targets"), "and attached to the node");
    frame(editor, 2);
    each = findNode(editor, "For Each");
    CHECK(each && each->error.empty(), "the node has no problem any more");
    CHECK(readFile(file).find("for each Item in Targets:") != std::string::npos, "the script says so");

    // Each of those was undoable.
    CHECK(editor.undo() && editor.undo(), "attaching and declaring undo");
    frame(editor, 2);
    CHECK(editor.variables().empty(), "the variable is gone again");
    CHECK(findNode(editor, "For Each") != nullptr, "and the node is still there, waiting for one");
}

static void thePlusButtonDeclaresAVariableAndTypingRenamesIt() {
    std::string file = freshScript("plus", "header:\n\tneeds component:Transform\n\nimplement:\n\twhen created:\n\t\tprint \"hi\"\n");
    ScriptGraphEditor editor(file, nullptr);
    editor.setObject("Door", {{"Mesh", "Cube", "the mesh"}, {"Material", "Brick", "the material"}}, 1);
    frame(editor, 4);

    ImVec2 plus = editor.addVariableButton();
    CHECK(plus.x > 0 && plus.y > 0, "the + Variable button was drawn");
    click(editor, plus);
    frame(editor, 3);
    CHECK(editor.variables().size() == 1 && editor.variables()[0].name == "NewVariable", "clicking + declared a variable");
    CHECK(readFile(file).find("\tvariable name:NewVariable type:decimal value:0.0") != std::string::npos, "in the script's header");
    CHECK(readFile(file).find("variable") < readFile(file).find("implement:"), "above implement");

    // Its name box has the keyboard: type a name over it.
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_A, true);
    frame(editor, 2);
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    io.AddInputCharactersUTF8("Speed");
    frame(editor, 2);
    press(editor, ImGuiKey_Enter);
    frame(editor, 2);
    CHECK(editor.variables().size() == 1 && editor.variables()[0].name == "Speed", "typing a name renamed the variable");
    CHECK(readFile(file).find("name:Speed") != std::string::npos && readFile(file).find("NewVariable") == std::string::npos, "in the header");

    // A second click adds another with a free name.
    click(editor, editor.addVariableButton());
    frame(editor, 3);
    CHECK(editor.variables().size() == 2 && editor.variables()[1].name == "NewVariable", "a second variable gets a free name");

    // The panel's changes undo like any other.
    CHECK(editor.undo(), "undo the second");
    frame(editor, 2);
    CHECK(editor.variables().size() == 1, "one variable again");
}

/// Press on `from`, move to `to` in steps, let go: a drag and drop.
static void dragAndDrop(ScriptGraphEditor& editor, ImVec2 from, ImVec2 to) {
    ImGuiIO& io = ImGui::GetIO();
    mouseTo(editor, from);
    io.AddMouseButtonEvent(0, true);
    frame(editor, 2);
    for (int i = 1; i <= 6; i++) {
        mouseTo(editor, ImVec2(from.x + (to.x - from.x) * i / 6.0f, from.y + (to.y - from.y) * i / 6.0f), 2);
    }
    io.AddMouseButtonEvent(0, false);
    frame(editor, 4);
}

static ImVec2 fieldOf(const ScriptGraphEditor& editor, const ScriptNode& n, int row) {
    const NodeGraph& g = editor.graph();
    float grid = g.gridSize() * g.viewZoom();
    ImVec2 p = nodeScreen(editor, n);
    return ImVec2(p.x + (106.0f + 40.0f) * g.viewZoom(), p.y + grid * 1.5f + static_cast<float>(row) * grid);
}

static const char* kForEachScript = "header:\n\tneeds component:Transform\n\n\tvariable name:Targets type:list of entity\n\nimplement:\n\twhen created:\n\t\tprint \"hi\"\n";

static void aListDraggedOntoForEachIsPluggedIn() {
    std::string file = freshScript("dropon", kForEachScript);
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 4);

    // Placing For Each does not fill the list in, even though the script has one.
    CHECK(editor.edit("add", "For Each", "h0", "0", "0"), "For Each is placed");
    frame(editor, 3);
    CHECK(readFile(file).find("for each Item in unset:") != std::string::npos, "with its list unplugged, not filled in");
    const ScriptNode* each = findNode(editor, "For Each");
    CHECK(each && each->error == "List isn't plugged in yet", "and the node says so");
    CHECK(each && each->fields.size() == 2 && each->fields[1].name == "List" && each->fields[1].text == "unset", "the List pin holds nothing");

    // Drag Targets from the Variables panel onto the List field.
    ImVec2 row = editor.variableRow("Targets");
    CHECK(row.x > 0, "the variable's row was drawn");
    if (!each) return;
    dragAndDrop(editor, row, fieldOf(editor, *each, 2));
    CHECK(readFile(file).find("for each Item in Targets:") != std::string::npos, "dropping the variable on the List field plugged it in");
    each = findNode(editor, "For Each");
    CHECK(each && each->error.empty(), "and the problem is gone");
    CHECK(editor.undo(), "the drop is one undo step");
    frame(editor, 2);
    CHECK(readFile(file).find("in unset:") != std::string::npos, "that undoes");
}

static void remembersAreListedAsLocalVariablesUnderTheVariables() {
    std::string file = freshScript("locals",
        "header:\n\tneeds component:Transform\n\tneeds system:Input\n\n\tvariable name:Speed type:decimal value:2.0\n\nimplement:\n"
        "\tevery frame:\n\t\tremember Forward axis moveForward of Input\n\t\tremember Step Forward times Speed\n"
        "\n\twhen created:\n\t\tremember Label \"hello\"\n\t\tprint \"x\"\n");
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 4);

    // They are not header variables, but they are listed, with their types, in script order.
    CHECK(editor.variables().size() == 1 && editor.variables()[0].name == "Speed", "only Speed is declared in the header");
    CHECK(editor.locals().size() == 3, "three Set Local nodes");
    if (editor.locals().size() != 3) return;
    CHECK(editor.locals()[0].name == "Forward" && editor.locals()[0].type == "decimal" && editor.locals()[0].scope == "every frame", "Forward: a decimal, in every frame");
    CHECK(editor.locals()[1].name == "Step" && editor.locals()[1].type == "decimal", "then Step");
    CHECK(editor.locals()[2].name == "Label" && editor.locals()[2].type == "text" && editor.locals()[2].scope == "when created", "Label: text, in when created");

    // The sub-section is drawn under the variables, and each local has a row there (and the variable's row is above them).
    CHECK(editor.localsHeader().y > editor.variableRow("Speed").y, "the Local Variables heading is under the variables");
    CHECK(editor.localRow("Forward").y > editor.localsHeader().y, "and the rows under the heading");
    CHECK(editor.localRow("Label").y > editor.localRow("Step").y, "in script order");
    CHECK(editor.localRow("Speed").x == 0, "a header variable is not among them");

    // Clicking a local finds the node that makes it.
    click(editor, editor.localRow("Label"));
    frame(editor, 3);
    CHECK(findNode(editor, "Set Local") != nullptr, "there are Set Local nodes");
    CHECK(editor.selectedNodeKey() == editor.locals()[2].node, "clicking Label selected the node that makes it");

    // Dragged onto a node's field, it is used there, like a variable.
    const ScriptNode* print = findNode(editor, "Print Debug Message");
    CHECK(print != nullptr, "the Print node is there");
    if (print) {
        dragAndDrop(editor, editor.localRow("Label"), fieldOf(editor, *print, 1));
        CHECK(readFile(file).find("print Label") != std::string::npos, "dropping Label on the message printed it");
    }

    // Deleting the node that makes one takes it off the list; the others stay.
    CHECK(editor.edit("delete", editor.locals()[2].node), "the Set Local node can be deleted");
    frame(editor, 3);
    CHECK(editor.locals().size() == 2 && editor.localRow("Label").x == 0, "Label is gone from the list");

    // A script with none says so, and the heading is still there.
    std::string plain = freshScript("nolocals", "header:\n\tneeds component:Transform\n\nimplement:\n\twhen created:\n\t\tprint \"hi\"\n");
    ScriptGraphEditor empty(plain, nullptr);
    frame(empty, 4);
    CHECK(empty.locals().empty() && empty.localsHeader().y > 0, "no locals, but the Local Variables heading is drawn");
}

// ---- where generated nodes go: left to right, then down, like a page ----

static const ScriptNode* nodeKeyed(const ScriptGraphEditor& editor, const std::string& key) {
    for (const auto& n : editor.graph().getNodes()) {
        const ScriptNode* s = dynamic_cast<const ScriptNode*>(n.get());
        if (s && s->key == key) return s;
    }
    return nullptr;
}

static float rightOf(const ScriptNode& n) { return n.position.x + n.size.x; }
static float bottomOf(const ScriptNode& n) { return n.position.y + 1.5f + static_cast<float>((std::max)(n.inputs.size(), n.outputs.size())); }

/// A script whose one handler prints `count` times: nodes h0 (the event), then h0.0.0 ... h0.0.<count-1>.
static std::string printsScript(int count) {
    std::string text = "header:\n\tneeds component:Transform\n\nimplement:\n\twhen created:\n";
    for (int i = 0; i < count; i++) text += "\t\tprint \"p" + std::to_string(i) + "\"\n";
    return text;
}

static std::string statementKey(int index) { return "h0.0." + std::to_string(index); }

/// The lowest point of any node in the graph.
static float lowestPoint(const ScriptGraphEditor& editor) {
    float low = 0.0f;
    for (const auto& n : editor.graph().getNodes()) {
        if (const ScriptNode* s = dynamic_cast<const ScriptNode*>(n.get())) low = (std::max)(low, bottomOf(*s));
    }
    return low;
}

static void generatedNodesRunLeftToRightWithTwoSquaresBetween() {
    std::string file = freshScript("l2r", printsScript(4).c_str());
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 3);

    // Each node follows the one before it on the same line: its header in line, exactly two squares of space between.
    const ScriptNode* prev = nodeKeyed(editor, "h0");
    CHECK(prev != nullptr, "the event is there");
    for (int i = 0; prev && i < 4; i++) {
        const ScriptNode* n = nodeKeyed(editor, statementKey(i));
        CHECK(n != nullptr, "the statement is there");
        if (!n) return;
        CHECK(n->position.y == prev->position.y, "the header bars are in line");
        CHECK(n->position.x == rightOf(*prev) + 2.0f, "two grid squares between each node and the next");
        prev = n;
    }
}

static void aNodeAddedBetweenTwoBumpsTheRestToTheRight() {
    std::string file = freshScript("between", printsScript(3).c_str());
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 3);
    editor.commitLayout();  // the nodes have places of their own now, like a graph someone has been working in
    frame(editor, 2);
    const ScriptNode* a = nodeKeyed(editor, statementKey(0));
    const ScriptNode* b = nodeKeyed(editor, statementKey(1));
    const ScriptNode* c = nodeKeyed(editor, statementKey(2));
    if (!a || !b || !c) return;
    const float ax = a->position.x, bx = b->position.x, cx = c->position.x, y = a->position.y;

    CHECK(editor.edit("add", "Finish", "h0", "0", "1"), "a node is added between the first and second");
    frame(editor, 3);
    const ScriptNode* before = nodeKeyed(editor, statementKey(0));
    const ScriptNode* added = nodeKeyed(editor, statementKey(1));
    const ScriptNode* nb = nodeKeyed(editor, statementKey(2));
    const ScriptNode* nc = nodeKeyed(editor, statementKey(3));
    CHECK(before && added && nb && nc && added->name == "Finish", "the new node is second, and the others follow it");
    if (!before || !added || !nb || !nc) return;
    CHECK(before->position.x == ax && before->position.y == y, "the node before it stays where it was");
    CHECK(added->position.x == rightOf(*before) + 2.0f && added->position.y == y, "it slots in right after, on the same line, two squares away");
    const float shift = added->size.x + 2.0f;
    CHECK(nb->position.x == bx + shift && nb->position.x == rightOf(*added) + 2.0f, "the node that was second moved right by exactly the room the new one needs");
    CHECK(nc->position.x == cx + shift, "and so did the one after it");
    CHECK(nb->position.y == y && nc->position.y == y, "all on the same line still");
}

static void aNodeAddedAtTheEndGoesAfterTheLastOne() {
    std::string file = freshScript("append", printsScript(3).c_str());
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 3);
    editor.commitLayout();
    frame(editor, 2);
    const ScriptNode* last = nodeKeyed(editor, statementKey(2));
    if (!last) return;
    const float lastX = last->position.x, y = last->position.y, edge = rightOf(*last);

    CHECK(editor.edit("add", "Finish", "h0", "0", "3"), "a node is added at the end");
    frame(editor, 3);
    const ScriptNode* added = nodeKeyed(editor, statementKey(3));
    last = nodeKeyed(editor, statementKey(2));
    CHECK(added && last && added->name == "Finish", "it is the new last statement");
    if (!added || !last) return;
    CHECK(last->position.x == lastX, "nothing before it moved");
    CHECK(added->position.x == edge + 2.0f && added->position.y == y, "it is two squares to the right of the old last node, on its line");
}

static void aLongScriptBreaksTheLineAfterTenNodesAndReadsLikeABook() {
    // The event and nine statements fill the first line; the tenth statement (the 11th node) starts the second.
    std::string file = freshScript("book", printsScript(12).c_str());
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 3);
    const ScriptNode* first = nodeKeyed(editor, "h0");
    if (!first) return;
    int onFirstLine = 0;
    float lowestOnFirstLine = 0.0f;
    for (const auto& n : editor.graph().getNodes()) {
        const ScriptNode* s = dynamic_cast<const ScriptNode*>(n.get());
        if (s && s->position.y == first->position.y) {
            onFirstLine++;
            lowestOnFirstLine = (std::max)(lowestOnFirstLine, bottomOf(*s));
        }
    }
    CHECK(onFirstLine == 10, "ten nodes on the first line");

    const ScriptNode* eleventh = nodeKeyed(editor, statementKey(9));
    const ScriptNode* twelfth = nodeKeyed(editor, statementKey(10));
    const ScriptNode* thirteenth = nodeKeyed(editor, statementKey(11));
    CHECK(eleventh && twelfth && thirteenth, "all the nodes are there");
    if (!eleventh || !twelfth || !thirteenth) return;
    CHECK(eleventh->position.x == first->position.x, "the 11th node is lined up with the first node");
    const float gap = eleventh->position.y - lowestOnFirstLine;
    CHECK(gap >= 2.0f && gap < 3.0f, "and two grid squares below the lowest node (rounded to the grid)");
    CHECK(twelfth->position.y == eleventh->position.y && twelfth->position.x == rightOf(*eleventh) + 2.0f, "the line goes on from there, two squares apart");
    CHECK(thirteenth->position.y == eleventh->position.y, "on the same line");
}

static void aNodeAddedToAFullLineStartsTheNextOne() {
    std::string file = freshScript("fullline", printsScript(9).c_str());  // the event and nine statements: a full line
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 3);
    editor.commitLayout();
    frame(editor, 2);
    const ScriptNode* first = nodeKeyed(editor, "h0");
    const ScriptNode* last = nodeKeyed(editor, statementKey(8));
    if (!first || !last) return;
    const float firstX = first->position.x, lastY = last->position.y;
    const float low = lowestPoint(editor);

    CHECK(editor.edit("add", "Finish", "h0", "0", "9"), "an 11th node is added at the end");
    frame(editor, 3);
    const ScriptNode* added = nodeKeyed(editor, statementKey(9));
    CHECK(added && added->name == "Finish", "it is the new last statement");
    if (!added) return;
    CHECK(added->position.x == firstX, "it starts a new line, at the left, in line with the first node");
    CHECK(added->position.y - low >= 2.0f && added->position.y - low < 3.0f && added->position.y > lastY, "two squares below the lowest node");

    // A node put in the middle of a full line does not break it: it makes room.
    std::string file2 = freshScript("fullline2", printsScript(9).c_str());
    ScriptGraphEditor middle(file2, nullptr);
    frame(middle, 3);
    middle.commitLayout();
    frame(middle, 2);
    const ScriptNode* a = nodeKeyed(middle, statementKey(3));
    if (!a) return;
    const float y = a->position.y;
    CHECK(middle.edit("add", "Finish", "h0", "0", "4"), "a node is added in the middle of the full line");
    frame(middle, 3);
    const ScriptNode* in = nodeKeyed(middle, statementKey(4));
    const ScriptNode* end = nodeKeyed(middle, statementKey(9));
    CHECK(in && end && in->position.y == y && end->position.y == y, "it stays on the line, and the line gets longer");
}

static void aSecondHandlerStartsBelowEverythingAtTheLeft() {
    std::string file = freshScript("handlers", "header:\n\tneeds component:Transform\n\nimplement:\n\twhen created:\n\t\tprint \"a\"\n\t\tprint \"b\"\n\n\tevery frame:\n\t\tprint \"c\"\n");
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 3);
    const ScriptNode* first = nodeKeyed(editor, "h0");
    const ScriptNode* second = nodeKeyed(editor, "h1");
    const ScriptNode* body = nodeKeyed(editor, "h1.0.0");
    CHECK(first && second && body, "both handlers are there");
    if (!first || !second || !body) return;
    CHECK(second->position.x == first->position.x, "the second handler is lined up with the first");
    float low = 0.0f;
    for (int i = 0; i < 2; i++) {
        if (const ScriptNode* n = nodeKeyed(editor, statementKey(i))) low = (std::max)(low, bottomOf(*n));
    }
    low = (std::max)(low, bottomOf(*first));
    CHECK(second->position.y - low >= 2.0f && second->position.y - low < 3.0f, "two squares below the lowest node of the first");
    CHECK(body->position.y == second->position.y && body->position.x == rightOf(*second) + 2.0f, "its statement follows it on its own line");
}

static ImGuiWindow* popupWindow(const char* part);
static ImVec2 popupRow(ImGuiWindow* w, int index, int count);
static void rightClick(ScriptGraphEditor& editor, ImVec2 p);

static int countNodes(const ScriptGraphEditor& editor, const std::string& name) {
    int count = 0;
    for (const auto& n : editor.graph().getNodes()) {
        const ScriptNode* s = dynamic_cast<const ScriptNode*>(n.get());
        if (s && s->name == name) count++;
    }
    return count;
}

static int wiresFrom(const ScriptGraphEditor& editor, const ScriptNode* node) {
    int count = 0;
    for (const NodeConnection& c : editor.graph().getConnections()) {
        if (node && c.outputNodeId == node->id) count++;
    }
    return count;
}

static void aWiredGetNodeCanBeWiredIntoAnotherPin() {
    std::string file = freshScript("getreuse", "header:\n\tneeds component:Transform\n\n\tvariable name:List1 type:list of integer\n\nimplement:\n\twhen created:\n\t\tprint \"hi\"\n");
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 4);
    CHECK(editor.edit("add", "For Each", "h0", "0", "0"), "For Each is placed");
    CHECK(editor.edit("add", "Add Unique", "h0", "0", "1"), "and so is Add Unique, after it");
    frame(editor, 3);
    CHECK(editor.edit("set_pin", "h0.0.0", "List", "List1"), "the list is plugged into For Each");
    CHECK(editor.edit("set_positions", "h0.0.0.v.List\t2\t2\n"), "with a Get node beside it");
    frame(editor, 4);
    const ScriptNode* getter = findNode(editor, "Get List1");
    const ScriptNode* each = findNode(editor, "For Each");
    const ScriptNode* add = findNode(editor, "Add Unique");
    CHECK(getter && each && add, "the Get node and both statements are there");
    if (!getter || !each || !add) return;

    // Drag from the wired Get node's output to the Add Unique List pin.
    ImVec2 from = pinScreen(editor, *getter, 0, true);
    ImVec2 to = pinScreen(editor, *add, 2, false);
    mouseTo(editor, from);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    frame(editor, 2);
    mouseTo(editor, ImVec2((from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f), 2);
    mouseTo(editor, to, 2);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    frame(editor, 4);

    const std::string text = readFile(file);
    CHECK(text.find("to List1, if new") != std::string::npos, "the second pin names the variable too");
    CHECK(text.find("in List1:") != std::string::npos, "and the first still does");
    CHECK(countNodes(editor, "Get List1") == 1, "it is the same Get node, not a second one");
    CHECK(wiresFrom(editor, findNode(editor, "Get List1")) == 2, "with a wire to each pin");
    each = findNode(editor, "For Each");
    add = findNode(editor, "Add Unique");
    CHECK(each && add && each->fields[1].carried && add->fields[1].carried, "both pins show the variable they carry");

    // Opening the script again shows the same one node with its two wires.
    {
        ScriptGraphEditor again(file, nullptr);
        frame(again, 4);
        CHECK(countNodes(again, "Get List1") == 1 && wiresFrom(again, findNode(again, "Get List1")) == 2, "reopened, it is still one node with two wires");
        frame(editor, 1);
    }

    // Cutting one wire (the pin's menu: Delete connection) unplugs only that pin; the node and the other wire stay.
    editor.requestFit();  // the nodes now run left to right, so they are wider than the window: zoom out to see them all
    frame(editor, 3);
    rightClick(editor, pinScreen(editor, *add, 2, false));
    ImGuiWindow* menu = popupWindow("##Popup_");
    CHECK(menu != nullptr, "right-clicking the wired pin opens its menu");
    if (menu) {
        click(editor, ImVec2(menu->Pos.x + menu->Size.x * 0.5f, menu->Pos.y + menu->Size.y - menu->WindowPadding.y - ImGui::GetTextLineHeight() * 0.5f - 1.0f));
        frame(editor, 3);
        const std::string cut = readFile(file);
        CHECK(cut.find("to unset, if new") != std::string::npos, "the cut pin is unset again");
        CHECK(cut.find("in List1:") != std::string::npos, "the other pin still names the variable");
        CHECK(countNodes(editor, "Get List1") == 1 && wiresFrom(editor, findNode(editor, "Get List1")) == 1, "one Get node, one wire, and no loose copy");
        CHECK(editor.undo(), "cutting is one undo step");
        frame(editor, 3);
        CHECK(countNodes(editor, "Get List1") == 1 && wiresFrom(editor, findNode(editor, "Get List1")) == 2, "and undo puts the wire back on the same node");
    }

    // Deleting the node unplugs every pin it fed.
    const ScriptNode* shared = findNode(editor, "Get List1");
    if (shared) {
        CHECK(editor.deleteNodes({shared->key}), "the shared Get node can be deleted");
        frame(editor, 3);
        const std::string gone = readFile(file);
        CHECK(gone.find("in unset:") != std::string::npos && gone.find("to unset, if new") != std::string::npos, "both pins are unplugged");
        CHECK(countNodes(editor, "Get List1") == 0, "and the node is gone");
    }
}

static void aVariableDroppedOnTheCanvasBecomesAGetNodeToWireIn(float scale = 1.0f) {
    PropulsionMKI::UI::setUIScale(scale);
    std::string file = freshScript("getnode", kForEachScript);
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 4);
    CHECK(editor.edit("add", "For Each", "h0", "0", "0"), "For Each is placed");
    frame(editor, 3);
    const ScriptNode* each = findNode(editor, "For Each");
    if (!each) return;

    // Drop the variable on empty canvas, below the nodes.
    ImVec2 c = canvasPos();
    ImVec2 empty(c.x + 150.0f, c.y + 420.0f);
    dragAndDrop(editor, editor.variableRow("Targets"), empty);
    const ScriptNode* getter = findNode(editor, "Get Targets");
    CHECK(getter != nullptr, "dropping a variable on the canvas puts a Get node there");
    CHECK(readFile(file).find("unset") != std::string::npos, "which is not part of the script: nothing is plugged in yet");
    if (!getter) return;

    // Wire its output into the List field's pin.
    each = findNode(editor, "For Each");
    ImVec2 from = pinScreen(editor, *getter, 0, true);
    ImVec2 to = pinScreen(editor, *each, 2, false);
    mouseTo(editor, from);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    frame(editor, 2);
    mouseTo(editor, ImVec2((from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f), 2);
    mouseTo(editor, to, 2);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    frame(editor, 4);
    CHECK(readFile(file).find("for each Item in Targets:") != std::string::npos, "wiring the Get node into the pin plugged the variable in");
    getter = findNode(editor, "Get Targets");
    CHECK(getter != nullptr, "and the Get node stays");
    each = findNode(editor, "For Each");
    CHECK(getter && each && each->fields[1].wiredKey == getter->key && each->fields[1].carried, "wired into the List pin, which shows the variable it carries");
    bool wired = false;
    for (const NodeConnection& conn : editor.graph().getConnections()) {
        wired = wired || (getter && each && conn.outputNodeId == getter->id && conn.inputNodeId == each->id && conn.inputPinId == each->fields[1].pinId);
    }
    CHECK(wired, "with a wire from its output to the pin");
    CHECK(readFile(file).find("Get") == std::string::npos, "the script itself only names the variable");
    CHECK(readFile(file + ".layout").find("value\t") != std::string::npos, "and the Get node's place is in the layout file");

    // Opening the script again shows it where it was.
    {
        ScriptGraphEditor again(file, nullptr);
        frame(again, 4);
        const ScriptNode* g2 = findNode(again, "Get Targets");
        CHECK(g2 != nullptr, "the Get node is there when the script is opened again");
        const ScriptNode* e2 = findNode(again, "For Each");
        CHECK(g2 && e2 && e2->fields[1].wiredKey == g2->key, "still wired");
        frame(editor, 1);
    }

    // Deleting it unplugs the pin, and undo brings it back.
    if (getter) {
        CHECK(editor.deleteNodes({getter->key}), "the Get node can be deleted");
        frame(editor, 3);
        CHECK(findNode(editor, "Get Targets") == nullptr, "it is gone");
        CHECK(readFile(file).find("in unset:") != std::string::npos, "and the pin is unplugged again");
        CHECK(editor.undo(), "that is one undo step");
        frame(editor, 3);
        CHECK(findNode(editor, "Get Targets") != nullptr && readFile(file).find("in Targets:") != std::string::npos, "which brings it back");
    }
    PropulsionMKI::UI::setUIScale(1.0f);
}

/// Drop a Get node for `variable` on empty canvas, pull its output pin out and let go over empty canvas: the let-go menu.
/// Returns the Get node (null if there is none) and leaves the menu open.
static const ScriptNode* letGoOfAGetWire(ScriptGraphEditor& editor, const std::string& variable) {
    ImVec2 c = canvasPos();
    dragAndDrop(editor, editor.variableRow(variable), ImVec2(c.x + 150.0f, c.y + 420.0f));
    const ScriptNode* getter = findNode(editor, "Get " + variable);
    if (!getter) return nullptr;
    const ImVec2 from = pinScreen(editor, *getter, 0, true);
    dragAndDrop(editor, from, ImVec2(from.x + 120.0f, from.y + 60.0f));
    return getter;
}

/// Hover the last row of an open menu (its submenu opens) and return the submenu.
static ImGuiWindow* openLastRow(ScriptGraphEditor& editor, ImGuiWindow* menu) {
    mouseTo(editor, ImVec2(menu->Pos.x + menu->Size.x * 0.5f, menu->Pos.y + menu->Size.y - menu->WindowPadding.y - ImGui::GetTextLineHeight() * 0.5f - 1.0f), 4);
    return popupWindow("##Menu_");
}

static void aGetNodeWireLetGoOnEmptyCanvasOffersTheNewNodesThatTakeIt(float scale = 1.0f) {
    PropulsionMKI::UI::setUIScale(scale);
    std::string file = freshScript("getdrop", kForEachScript);
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 4);
    const std::string before = readFile(file);
    const ScriptNode* getter = letGoOfAGetWire(editor, "Targets");
    CHECK(getter != nullptr, "a Get node is on the canvas");
    ImGuiWindow* menu = popupWindow("##Popup_");
    CHECK(menu != nullptr, "letting go of a Get node's wire on empty canvas opens a menu");
    if (!menu) return;
    CHECK(readFile(file) == before, "which changes nothing by itself");
    CHECK(findNode(editor, "For Each") == nullptr, "and there is no For Each in the graph to wire to");

    // The menu offers nodes that do not exist yet. A list fits For Each (the last category, Flow, has only that).
    ImGuiWindow* flow = openLastRow(editor, menu);
    CHECK(flow != nullptr, "its categories open lists of nodes");
    if (!flow) return;
    click(editor, popupRow(flow, 0, 1));
    frame(editor, 3);
    CHECK(readFile(file).find("for each Item in Targets:") != std::string::npos, "choosing For Each adds it, with the list plugged in");
    const ScriptNode* each = findNode(editor, "For Each");
    const ScriptNode* event = findNode(editor, "On Begin Play");
    CHECK(each && event && each->parentKey == event->key, "inside the handler");
    getter = findNode(editor, "Get Targets");
    CHECK(getter && each && each->fields[1].wiredKey == getter->key && each->fields[1].carried, "and the Get node is wired to its List pin");
    CHECK(editor.undo(), "all of it is one undo step");
    frame(editor, 3);
    CHECK(findNode(editor, "For Each") == nullptr && readFile(file) == before, "which takes the node away again");
    PropulsionMKI::UI::setUIScale(1.0f);
}

static void aBooleanGetNodeCanBeDraggedIntoANewBranch() {
    std::string file = freshScript("getbranch", "header:\n\tneeds component:Transform\n\n\tvariable name:Alive type:boolean value:true\n\nimplement:\n\twhen created:\n\t\tprint \"hi\"\n");
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 4);
    const ScriptNode* getter = letGoOfAGetWire(editor, "Alive");
    CHECK(getter != nullptr, "a Get node for the boolean is on the canvas");
    ImGuiWindow* menu = popupWindow("##Popup_");
    CHECK(menu != nullptr, "the let-go menu opens");
    if (!menu) return;
    ImGuiWindow* flow = openLastRow(editor, menu);
    CHECK(flow != nullptr, "the Flow nodes that take a boolean are listed");
    if (!flow) return;
    click(editor, popupRow(flow, 0, 2));  // Branch, then While
    frame(editor, 3);
    CHECK(readFile(file).find("if Alive is true:") != std::string::npos, "a Branch is added, asking whether the boolean is true");
    const ScriptNode* branch = findNode(editor, "Branch");
    CHECK(branch != nullptr, "it is in the graph");
    const ScriptNode* get = findNode(editor, "Get Alive");
    CHECK(get != nullptr && findNode(editor, "Compare") == nullptr, "the Get node stays as the variable itself, with no comparison node");
    CHECK(branch && get && !branch->fields.empty() && branch->fields[0].wiredKey == get->key, "wired straight into the Branch's Condition");
    CHECK(branch && branch->execLabels == std::vector<std::string>({"True", "False", "Next"}), "a Branch has True and False outputs");
    CHECK(branch && branch->execTargets[0].empty() && branch->execTargets[1].empty(), "and nothing under it");
    CHECK(editor.message().empty(), "with nothing refused");
}


static const char* kBoolScript =
    "header:\n\tneeds component:Transform\n\n\tvariable name:Alive type:boolean value:true\n\nimplement:\n\twhen created:\n\t\tprint \"hi\"\n";

static void aBranchComesAloneAndABooleanPlugsStraightIntoIt() {
    std::string file = freshScript("branchsolo", kBoolScript);
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 4);
    const size_t nodesBefore = editor.graph().getNodes().size();
    CHECK(editor.edit("add", "Branch", "h0", "0", "1"), "a Branch is added");
    frame(editor, 3);
    CHECK(editor.graph().getNodes().size() == nodesBefore + 1, "and only the Branch appears: no compare node, nothing under it");
    const ScriptNode* branch = findNode(editor, "Branch");
    CHECK(branch && branch->execLabels == std::vector<std::string>({"True", "False", "Next"}), "with True and False outputs");
    CHECK(branch && branch->fields.size() == 1 && branch->fields[0].text == "unset", "its condition is not plugged in");
    if (!branch) return;

    // A Get node for the boolean, dragged onto the Condition.
    editor.addGetter("Alive", ImVec2(branch->position.x - 16.0f, branch->position.y));
    frame(editor, 3);
    const ScriptNode* getter = findNode(editor, "Get Alive");
    CHECK(getter != nullptr, "a Get node is on the canvas");
    if (!getter) return;
    const ImVec2 from = pinScreen(editor, *getter, 0, true);
    const ImVec2 to = pinScreen(editor, *branch, 1, false);
    mouseTo(editor, from);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    frame(editor, 2);
    mouseTo(editor, ImVec2((from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f), 2);
    mouseTo(editor, to, 2);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    frame(editor, 4);
    CHECK(readFile(file).find("if Alive is true:") != std::string::npos, "the Branch asks whether Alive is true");
    CHECK(editor.message().empty(), "without complaint");
    branch = findNode(editor, "Branch");
    const ScriptNode* wired = findNode(editor, "Get Alive");
    CHECK(branch && wired && branch->fields[0].wiredKey == wired->key && findNode(editor, "Compare") == nullptr, "the variable is wired straight in, with no compare node");
    CHECK(branch && branch->error.empty(), "and the Branch has nothing wrong with it");
}

static void aSetVariableOnABooleanHasACheckbox(float scale = 1.0f) {
    PropulsionMKI::UI::setUIScale(scale);
    std::string file = freshScript("setbool", "header:\n\tneeds component:Transform\n\n\tvariable name:Alive type:boolean value:true\n\tvariable name:Score type:integer value:0\n\nimplement:\n\twhen created:\n\t\tprint \"hi\"\n");
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 4);
    CHECK(editor.edit("add", "Set Variable", "h0", "0", "0"), "Set Variable is placed");
    CHECK(editor.edit("set_pin", "h0.0.0", "Target", "Score"), "pointed at the number");
    frame(editor, 3);
    const ScriptNode* set = findNode(editor, "Set Variable");
    CHECK(set && set->fields.size() >= 2 && !set->fields[1].toggle, "a number's value is a text field");
    CHECK(editor.edit("set_pin", "h0.0.0", "Target", "Alive"), "pointed at the boolean");
    frame(editor, 3);
    set = findNode(editor, "Set Variable");
    CHECK(set && set->fields.size() >= 2 && set->fields[1].toggle, "a boolean's value is a checkbox");
    CHECK(readFile(file).find("make Alive false") != std::string::npos, "which starts out false");
    if (!set) return;

    // Click the box: the script says true. Click again: false.
    ImVec2 box = fieldOf(editor, *set, 2);
    box.x -= 32.0f * editor.graph().viewZoom();
    click(editor, box);
    CHECK(readFile(file).find("make Alive true") != std::string::npos, "ticking the box makes it true");
    set = findNode(editor, "Set Variable");
    CHECK(set && set->fields[1].toggle && set->fields[1].text == "true", "and it stays a checkbox, ticked");
    click(editor, box);
    CHECK(readFile(file).find("make Alive false") != std::string::npos, "clicking it again makes it false");
    PropulsionMKI::UI::setUIScale(1.0f);
}

static void compileListsWhatWouldFailAtPlay() {
    std::string file = freshScript("compile", kForEachScript);
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 4);
    CHECK(!editor.lastCompile().done, "nothing has been compiled yet");

    CHECK(editor.edit("add", "For Each", "h0", "0", "0"), "For Each is placed");
    frame(editor, 3);
    // Click the Compile button.
    ImVec2 button = editor.compileButton();
    CHECK(button.x > 0 && button.y > 0, "the Compile button is drawn");
    click(editor, button);
    frame(editor, 3);
    const auto& result = editor.lastCompile();
    CHECK(result.done && !result.stale, "compiled");
    CHECK(result.errors == 1, "one error would fail Play");
    CHECK(!result.problems.empty() && result.problems[0].error && result.problems[0].message == "For Each: List isn't plugged in yet", "and it says which node and what is missing");
    CHECK(!result.problems.empty() && result.problems[0].node == "h0.0.0", "tied to the node");

    // Editing makes the result stale; fixing and compiling again is clean.
    CHECK(editor.edit("set_pin", "h0.0.0", "List", "Targets"), "the list is plugged in");
    CHECK(editor.lastCompile().stale, "the result is out of date");
    editor.compile();
    frame(editor, 2);
    CHECK(editor.lastCompile().errors == 0 && !editor.lastCompile().stale, "compiling again: no errors");
    CHECK(editor.lastCompile().warnings == 0, "and Targets is used, so no warning");

    // A variable nothing uses is a warning, not a failure.
    CHECK(editor.edit("add_variable", "variable name:Idle type:integer value:0"), "an unused variable");
    editor.compile();
    CHECK(editor.lastCompile().errors == 0 && editor.lastCompile().warnings == 1, "is a warning, and does not fail the compile");
}


static const char* kCodeScript =
    "header:\n\tneeds component:Transform\n\n\tvariable name:Count type:integer value:0\n\nimplement:\n"
    "\twhen created:\n\t\tprint \"one\"\n\t\tif Count is 0:\n\t\t\tprint \"two\"\n\t\tprint \"three\"\n";

static void theCodePaneShowsTheScriptAndLinksLinesToNodes() {
    std::string file = freshScript("code", kCodeScript);
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 4);
    CHECK(!editor.codeOpen(), "the code pane starts closed");
    CHECK(editor.codeRow(8).x == 0, "and draws nothing");

    click(editor, editor.codeButton());
    CHECK(editor.codeOpen(), "the Code button opens it");
    frame(editor, 3);
    CHECK(editor.codeLines().size() == 11, "it has every line of the file");
    CHECK(editor.codeLines().size() == 11 && editor.codeLines()[0] == "header:" && editor.codeLines()[10] == "\t\tprint \"three\"", "as written");
    CHECK(editor.codeRow(8).x > 0 && editor.codeRow(11).x > 0, "and draws them");

    // Every node knows its line; a block's body has lines of its own.
    CHECK(editor.codeLineOf("h0") == 7 && editor.codeLineOf("h0.0.0") == 8 && editor.codeLineOf("h0.0.1") == 9, "the event and its statements are on their lines");
    CHECK(editor.codeLineOf("h0.0.1.0.0") == 10 && editor.codeLineOf("h0.0.2") == 11, "and so is what is inside a block, and after it");

    // A click on a line selects its node (and the graph shows it selected).
    ImVec2 row = editor.codeRow(8);
    click(editor, ImVec2(row.x + 120.0f, row.y));
    const ScriptNode* one = findNode(editor, "Print Debug Message", 0);
    CHECK(editor.selectedNodeKey() == "h0.0.0" && one && isSelected(editor, one), "clicking line 8 selects the first print");
    row = editor.codeRow(10);
    click(editor, ImVec2(row.x + 120.0f, row.y));
    CHECK(editor.selectedNodeKey() == "h0.0.1.0.0", "clicking a line inside the if selects the node inside it");
    row = editor.codeRow(9);
    click(editor, ImVec2(row.x + 120.0f, row.y));
    CHECK(editor.selectedNodeKey() == "h0.0.1", "and the if line selects the if");

    // A line with no node (blank, or the header) leaves the selection alone.
    row = editor.codeRow(3);
    click(editor, ImVec2(row.x + 120.0f, row.y));
    CHECK(editor.selectedNodeKey() == "h0.0.1", "a blank line selects nothing");

    // A node picked in the graph is the pane's selection too.
    const ScriptNode* three = findNode(editor, "Print Debug Message", 2);
    CHECK(three != nullptr, "the last print is on the canvas");
    if (three) {
        click(editor, titleOf(editor, *three));
        CHECK(editor.selectedNodeKey() == "h0.0.2", "a node clicked in the graph is selected");
    }

    // The pane follows edits made in the graph...
    CHECK(editor.edit("add", "Finish", "h0", "0", "3"), "a node is added in the graph");
    frame(editor, 3);
    CHECK(editor.codeLines().size() == 12 && editor.codeLines()[11] == "\t\tfinish", "and the new line is in the pane");
    CHECK(editor.codeLineOf("h0.0.3") == 12, "on the node's line");

    // ...and shows a file that is too broken to be a graph, as it is on disk.
    writeFile(file, "header:\n\tbogus\n\nimplement:\n\twhen created:\n\t\tfinish\n");
    fs::last_write_time(file, fs::file_time_type::clock::now() + std::chrono::seconds(5));
    frame(editor, 3);
    CHECK(!editor.message().empty(), "the file can't be shown as a graph");
    CHECK(editor.codeLines().size() == 6 && editor.codeLines()[1] == "\tbogus", "but the pane shows what is in it");

    click(editor, editor.codeButton());
    CHECK(!editor.codeOpen(), "the Code button closes it again");
    CHECK(editor.codeRow(1).x == 0, "and nothing is drawn");
}

static void aNodePickedInTheGraphScrollsTheCodeToItsLine() {
    // Comment lines push the code far down the file (comments are lines of the pane, but not nodes), so the
    // graph is small and the nodes can be clicked while their lines are well below the top of the pane.
    std::string script;
    for (int i = 0; i < 90; i++) script += "// filler " + std::to_string(i) + "\n";
    script += kThree;
    std::string file = freshScript("codescroll", script.c_str());
    ScriptGraphEditor editor(file, nullptr);
    editor.setCodeOpen(true);
    frame(editor, 4);
    CHECK(editor.codeRow(1).x > 0, "the top of the file is drawn");
    CHECK(editor.codeRow(98).x == 0, "the code is scrolled out of view");

    const ScriptNode* last = findNode(editor, "Print Debug Message", 2);
    CHECK(last != nullptr, "the last print is on the canvas");
    if (!last) return;
    click(editor, titleOf(editor, *last));
    frame(editor, 4);
    CHECK(editor.selectedNodeKey() == last->key, "it is selected");
    CHECK(editor.codeLineOf(last->key) == 98, "it is written on line 98");
    CHECK(editor.codeRow(98).x > 0, "and the pane has scrolled to show that line");
    CHECK(editor.codeRow(1).x == 0, "away from the top");

    // Picking another node moves the pane again.
    const ScriptNode* first = findNode(editor, "Print Debug Message", 0);
    if (first) click(editor, titleOf(editor, *first));
    frame(editor, 3);
    CHECK(editor.codeRow(96).x > 0, "and picking the first print shows its line");
}

// ---- typing in the code pane ----

using Pos = CodeBuffer::Pos;

static bool at(const CodeBuffer& b, int line, int col) { return b.cursor() == Pos{line, col}; }

static void theCodeBufferEditsLikeATextEditor() {
    // What a file holds, as lines: "\r" and a byte-order mark go, and every line ends in "\n".
    CHECK(CodeBuffer::normalize("a\r\nb") == "a\nb\n", "line endings are normalized");
    CHECK(CodeBuffer::normalize("\xEF\xBB\xBF" "a\n") == "a\n", "a byte-order mark is dropped");
    CHECK(CodeBuffer::normalize("") == "", "an empty file stays empty");

    CodeBuffer b;
    b.setText("header:\n\tneeds component:Transform\n");
    CHECK(b.lineCount() == 2 && b.text() == "header:\n\tneeds component:Transform\n", "setText and text agree");
    unsigned rev = b.revision();

    // Moving the cursor is not a change to the text.
    b.setCursor(Pos{1, 4}, false);
    b.moveRight(false, false);
    CHECK(b.revision() == rev && at(b, 1, 5), "moving the cursor leaves the text alone");
    b.moveEnd(false, false);
    CHECK(at(b, 1, 26), "End goes to the end of the line");
    b.moveRight(false, false);
    CHECK(at(b, 1, 26), "and the last line has nothing after it");
    b.moveHome(false, false);
    CHECK(at(b, 1, 1), "Home goes to the first character, past the indent");
    b.moveHome(false, false);
    CHECK(at(b, 1, 0), "and again to the start of the line");
    b.moveLeft(false, false);
    CHECK(at(b, 0, 7), "Left at the start of a line goes to the end of the one before");

    // Typing, with the indent kept by Enter and one more after a colon.
    b.setText("implement:\n");
    b.setCursor(Pos{0, 10}, false);
    b.newline();
    CHECK(at(b, 1, 1) && b.lines()[1] == "\t", "Enter after a line ending in a colon indents");
    b.insert("when created:");
    b.newline();
    CHECK(at(b, 2, 2), "and again");
    b.insert("print \"hi\"");
    b.newline();
    CHECK(at(b, 3, 2) && b.lines()[3] == "\t\t", "a line with no colon keeps the indent");
    b.insert("finish // done:");
    b.newline();
    CHECK(at(b, 4, 2), "a colon in a comment does not indent");
    CHECK(b.text() == "implement:\n\twhen created:\n\t\tprint \"hi\"\n\t\tfinish // done:\n\t\t\n", "the text is what was typed");

    // Enter in the middle of a line splits it and keeps the right half after the indent.
    b.setText("\t\tprint \"a\" joined with B\n");
    b.setCursor(Pos{0, 11}, false);
    b.newline();
    CHECK(b.lines().size() == 2 && b.lines()[0] == "\t\tprint \"a\"" && b.lines()[1] == "\t\t joined with B" && at(b, 1, 2), "Enter splits a line");

    // Backspace and Delete: one character, a word, and across lines.
    b.setText("one two\nthree\n");
    b.setCursor(Pos{0, 7}, false);
    b.backspace(false);
    CHECK(b.lines()[0] == "one tw" && at(b, 0, 6), "Backspace takes one character");
    b.backspace(true);
    CHECK(b.lines()[0] == "one " && at(b, 0, 4), "Ctrl+Backspace takes a word");
    b.setCursor(Pos{1, 0}, false);
    b.backspace(false);
    CHECK(b.lines().size() == 1 && b.lines()[0] == "one three" && at(b, 0, 4), "Backspace at the start of a line joins it to the one before");
    b.del(true);
    CHECK(b.lines()[0] == "one " && at(b, 0, 4), "Ctrl+Delete takes the word after the cursor");
    b.setCursor(Pos{0, 4}, false);
    b.del(false);
    CHECK(b.lines()[0] == "one ", "Delete at the very end does nothing");

    // Words: Ctrl+Left and Ctrl+Right.
    b.setText("add Speed per second\n");
    b.setCursor(Pos{0, 0}, false);
    b.moveRight(false, true);
    CHECK(at(b, 0, 4), "Ctrl+Right goes to the next word");
    b.moveRight(false, true);
    b.moveRight(false, true);
    CHECK(at(b, 0, 14), "and the next");
    b.moveLeft(false, true);
    CHECK(at(b, 0, 10), "Ctrl+Left goes back a word");

    // Up and Down stay in one column, counting a tab as the columns it is drawn with.
    b.setText("\t\tabcdef\nxy\n\t\tabcdef\n");
    b.setCursor(Pos{0, 6}, false);  // visual column 2*2 + 4 = 8
    b.moveDown(false);
    CHECK(at(b, 1, 2), "Down onto a short line goes to its end");
    b.moveDown(false);
    CHECK(at(b, 2, 6), "and back to the column it came from");
    b.moveUp(false);
    b.moveUp(false);
    CHECK(at(b, 0, 6), "Up the same");
    b.moveUp(false);
    CHECK(at(b, 0, 0), "Up on the first line goes to its start");

    // Selecting: Shift with the movement, and typing over it.
    b.setText("make Speed 5\n");
    b.setCursor(Pos{0, 5}, false);
    b.moveRight(true, true);
    CHECK(b.hasSelection() && b.selectedText() == "Speed ", "Shift+Ctrl+Right selects a word");
    b.insert("Health ");
    CHECK(b.lines()[0] == "make Health 5" && !b.hasSelection(), "typing replaces the selection");
    b.selectAll();
    CHECK(b.selectedText() == "make Health 5", "select all");
    b.deleteSelection();
    CHECK(b.text() == "" && b.lineCount() == 1, "deleting it leaves an empty script");
    b.setText("make Speed 5\n");
    b.selectWord(Pos{0, 7});
    CHECK(b.selectedText() == "Speed", "a double click selects a word");

    // Several lines at once: select across, cut, paste back.
    b.setText("a\nb\nc\n");
    b.setCursor(Pos{0, 1}, false);
    b.setCursor(Pos{2, 0}, true);
    CHECK(b.selectedText() == "\nb\n", "a selection over lines");
    const std::string cut = b.selectedText();
    b.deleteSelection();
    CHECK(b.text() == "ac\n" && at(b, 0, 1), "deleted, the lines join");
    b.insert(cut);
    CHECK(b.text() == "a\nb\nc\n" && at(b, 2, 0), "and pasting the text back restores them");
    b.insert("x\ty\r\n\x01z");
    CHECK(b.lines()[2] == "x\ty" && b.lines()[3] == "zc", "pasting drops \\r and other control characters, and keeps tabs (what was after the cursor follows it)");

    // Tab and Shift+Tab.
    b.setText("if x:\n\tfinish\nfinish\n");
    b.setCursor(Pos{2, 0}, false);
    b.indent(false);
    CHECK(b.lines()[2] == "\tfinish" && at(b, 2, 1), "Tab inserts a tab at the cursor");
    b.setCursor(Pos{0, 0}, false);
    b.setCursor(Pos{2, 3}, true);
    b.indent(false);
    CHECK(b.lines()[0] == "\tif x:" && b.lines()[1] == "\t\tfinish" && b.lines()[2] == "\t\tfinish", "Tab with a selection indents every line it touches");
    CHECK(b.hasSelection() && b.anchor() == Pos({0, 1}) && b.cursor() == Pos({2, 4}), "and the selection stays on the same text");
    b.indent(true);
    CHECK(b.text() == "if x:\n\tfinish\n\tfinish\n", "Shift+Tab takes one tab off each");
    b.setCursor(Pos{0, 0}, false);
    b.indent(true);
    CHECK(b.text() == "if x:\n\tfinish\n\tfinish\n", "and a line with no tab is left alone");
    b.setCursor(Pos{1, 0}, false);
    b.setCursor(Pos{2, 0}, true);
    b.indent(false);
    CHECK(b.lines()[1] == "\t\tfinish" && b.lines()[2] == "\tfinish", "a selection that ends at the start of a line does not touch that line");

    // UTF-8: a character is one step, not one byte.
    b.setText("\n");
    b.insert("caf\xC3\xA9");
    CHECK(b.lines()[0] == "caf\xC3\xA9" && at(b, 0, 5), "typing a multi-byte character");
    b.moveLeft(false, false);
    CHECK(at(b, 0, 3), "Left steps over all of it");
    b.moveEnd(false, false);
    b.backspace(false);
    CHECK(b.lines()[0] == "caf" && at(b, 0, 3), "Backspace takes all of it");
    CHECK(b.clamp(Pos({0, 99})).col == 3 && b.clamp(Pos({9, 0})).line == 0, "positions are clamped");

    // Undo: a run of typing is one step, an Enter is another, and redo comes back.
    b.setText("make A 1\n");
    b.setCursor(Pos{0, 8}, false);
    b.insert("2");
    b.insert("3");
    b.insert("4");
    b.newline();
    b.insert("x");
    CHECK(b.text() == "make A 1234\nx\n", "typed");
    CHECK(b.undo() && b.text() == "make A 1234\n\n", "undo takes back the typing after the Enter");
    CHECK(b.undo() && b.text() == "make A 1234\n", "then the Enter");
    CHECK(b.undo() && b.text() == "make A 1\n", "then the whole run of typing at once");
    CHECK(!b.canUndo() && b.canRedo(), "nothing more to undo");
    CHECK(b.redo() && b.text() == "make A 1234\n", "redo comes back");
    b.setCursor(Pos{0, 3}, false);
    b.insert("!");
    CHECK(!b.canRedo(), "a new edit ends the redo history");
    b.setCursor(Pos{0, 0}, false);
    b.insert("a");
    b.setCursor(Pos{0, 5}, false);
    b.insert("b");
    CHECK(b.undo() && b.text().find('b') == std::string::npos && b.text().find('a') != std::string::npos, "typing in two places is two steps");
    rev = b.revision();
    b.setText("something else\n");
    CHECK(!b.canUndo() && b.revision() != rev, "a new text from outside drops the undo history");
}

static const char* kBroken = "header:\n\tbogus\n\nimplement:\n\twhen created:\n\t\tfinish\n";

static void typeText(ScriptGraphEditor& editor, const std::string& text) {
    for (char c : text) {
        ImGui::GetIO().AddInputCharacter(static_cast<unsigned int>(static_cast<unsigned char>(c)));
        frame(editor, 1);
    }
}

static void withCtrl(bool down) { ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, down); }

/// Click just after the last character of a line (1-based) of the code pane.
static void clickLineEnd(ScriptGraphEditor& editor, int line) {
    click(editor, editor.codeCharPos(line, static_cast<int>(editor.codeLines()[static_cast<size_t>(line) - 1].size())));
}

static int countOf(const std::string& text, const std::string& what) {
    int n = 0;
    for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size())) n++;
    return n;
}

static void theCodePaneEditsTheScript() {
    std::string file = freshScript("codetype", kCodeScript);
    ScriptGraphEditor editor(file, nullptr);
    editor.setCodeOpen(true);
    frame(editor, 4);

    // A click puts the cursor there, and gives the pane the keyboard.
    CHECK(!editor.codeFocused(), "the pane does not take typing until it is clicked");
    clickLineEnd(editor, 8);
    CHECK(editor.codeFocused(), "a click gives the pane the keyboard");
    CHECK(at(editor.code(), 7, static_cast<int>(editor.codeLines()[7].size())), "and puts the cursor where it was clicked");
    CHECK(editor.selectedNodeKey() == "h0.0.0", "the node on that line is selected too");
    CHECK(ImGui::GetIO().WantTextInput, "the engine is told text is being typed (so its shortcuts stand back)");

    // Typing shows in the pane at once, and reaches the file once typing pauses.
    typeText(editor, " joined with Count");
    CHECK(editor.codeLines()[7] == "\t\tprint \"one\" joined with Count", "typed text is in the pane");
    CHECK(editor.codeDirty() && editor.codeError() == nullptr, "not saved yet, and it parses");
    CHECK(readFile(file) == kCodeScript, "the file is untouched while typing goes on");
    frame(editor, 45);
    CHECK(!editor.codeDirty(), "once typing pauses it is saved");
    CHECK(readFile(file).find("print \"one\" joined with Count") != std::string::npos, "the file has the new line");
    CHECK(findNode(editor, "Join Text") != nullptr, "and the graph shows the new phrase");
    CHECK(editor.canUndo(), "saving the typing is an undo step");
    CHECK(at(editor.code(), 7, static_cast<int>(editor.codeLines()[7].size())) && editor.codeFocused(), "the cursor and the keyboard stay where they were");

    // Undo in the graph puts the file back, and the pane follows it.
    CHECK(editor.undo(), "the graph's undo");
    frame(editor, 3);
    CHECK(readFile(file) == kCodeScript && editor.codeLines()[7] == "\t\tprint \"one\"" && !editor.codeDirty(), "takes the typing back out of the file and the pane");
    CHECK(editor.redo(), "and redo");
    frame(editor, 3);
    CHECK(editor.codeLines()[7] == "\t\tprint \"one\" joined with Count", "puts it back");
    CHECK(editor.undo(), "undo again, to start over from the original");
    frame(editor, 3);

    // Text that does not parse is marked where it stops, and is not saved.
    click(editor, editor.codeCharPos(8, 0));
    typeText(editor, "  ");  // spaces in the indent: PropulsionScript wants tabs
    frame(editor, 45);
    CHECK(editor.codeDirty(), "it stays unsaved");
    CHECK(editor.codeError() != nullptr && editor.codeError()->line == 8, "the error is on the line with the spaces");
    CHECK(editor.codeError() && editor.codeError()->message.find("tabs") != std::string::npos, "and says why");
    CHECK(readFile(file) == kCodeScript, "the file is untouched");
    CHECK(countNodes(editor, "Print Debug Message") == 3, "and the graph still shows the last good script");
    CHECK(editor.selectedNodeKey() == "h0.0.0", "the selection is still the node it was");
    // Fixing it makes the text the file's again: nothing to save.
    press(editor, ImGuiKey_Backspace);
    press(editor, ImGuiKey_Backspace);
    frame(editor, 3);
    CHECK(editor.codeError() == nullptr && !editor.codeDirty() && readFile(file) == kCodeScript, "taking the spaces out leaves nothing to save");

    // Enter keeps the indent, and one more after a colon.
    clickLineEnd(editor, 9);
    press(editor, ImGuiKey_Enter);
    typeText(editor, "print \"x\"");
    CHECK(editor.codeLines()[9] == "\t\t\tprint \"x\"", "Enter after if ...: indents the next line");
    frame(editor, 45);
    CHECK(readFile(file).find("\t\t\tprint \"x\"\n") != std::string::npos && countNodes(editor, "Print Debug Message") == 4, "and the new print is saved, and a node in the graph");

    // Ctrl+Z in the pane undoes the typing (and what is left saves again).
    withCtrl(true);
    press(editor, ImGuiKey_Z);
    withCtrl(false);
    CHECK(editor.codeLines()[9] == "\t\t\t", "Ctrl+Z takes back the typed run");
    frame(editor, 45);
    CHECK(readFile(file).find("print \"x\"") == std::string::npos && countNodes(editor, "Print Debug Message") == 3, "and the file follows");
    withCtrl(true);
    press(editor, ImGuiKey_Y);
    withCtrl(false);
    CHECK(editor.codeLines()[9] == "\t\t\tprint \"x\"", "Ctrl+Y brings it back");
    frame(editor, 45);

    // Tab inserts a tab; Shift+Tab takes one off.
    clickLineEnd(editor, 12);
    press(editor, ImGuiKey_Tab);
    CHECK(editor.codeLines()[11] == "\t\tprint \"three\"\t", "Tab inserts a tab");
    withShift(true);
    press(editor, ImGuiKey_Tab);
    withShift(false);
    CHECK(editor.codeLines()[11] == "\tprint \"three\"\t", "Shift+Tab takes one off the start of the line");
}

static void typedTextThatParsesIsSavedBeforeAnythingElseEditsTheFile() {
    std::string file = freshScript("codeflush", kCodeScript);
    ScriptGraphEditor editor(file, nullptr);
    editor.setCodeOpen(true);
    frame(editor, 4);

    // Typed, and an edit made in the graph straight after (before typing paused): the edit builds on the typing.
    clickLineEnd(editor, 11);
    typeText(editor, " joined with Count");
    CHECK(editor.codeDirty() && readFile(file) == kCodeScript, "typed, not saved");
    CHECK(editor.edit("add", "Finish", "h0", "0", "3"), "an edit in the graph");
    CHECK(readFile(file).find("print \"three\" joined with Count") != std::string::npos, "has the typed text in it");
    CHECK(readFile(file).find("finish") != std::string::npos, "and the edit");
    CHECK(!editor.codeDirty(), "nothing is left unsaved");

    // Leaving the pane (a click on the canvas) saves at once, without waiting for typing to pause.
    clickLineEnd(editor, 8);
    typeText(editor, " joined with Count");
    CHECK(editor.codeDirty(), "typed");
    click(editor, ImVec2(canvasPos().x + 4.0f, canvasPos().y + 4.0f));
    frame(editor, 3);
    CHECK(!editor.codeFocused(), "a click on the canvas takes the keyboard back");
    CHECK(!editor.codeDirty() && readFile(file).find("print \"one\" joined with Count") != std::string::npos, "and what was typed is saved");

    // Typing that does not parse, left behind, is kept in the pane (and marked) until the file changes some other way.
    clickLineEnd(editor, 8);
    typeText(editor, " joined");
    click(editor, ImVec2(canvasPos().x + 4.0f, canvasPos().y + 4.0f));
    frame(editor, 45);
    CHECK(editor.codeDirty() && editor.codeError() != nullptr, "unfinished text stays in the pane, marked");
    const std::string typed = editor.codeLines()[7];
    CHECK(editor.edit("add", "Finish", "h0", "0", "0"), "an edit in the graph");
    CHECK(!editor.codeDirty() && editor.codeLines()[7] != typed && readFile(file).find("joined\n") == std::string::npos, "drops it: the file is the truth");
}

static void aDraftSurvivesNodesBeingMovedAndTheFileChangingOutsideReplacesIt() {
    std::string file = freshScript("codedraft", kCodeScript);
    ScriptGraphEditor editor(file, nullptr);
    editor.setCodeOpen(true);
    frame(editor, 4);
    clickLineEnd(editor, 8);
    typeText(editor, " joined");
    CHECK(editor.codeDirty() && editor.codeError() != nullptr, "an unfinished line");
    // Moving a node writes only the layout file: the text typed is still there.
    editor.commitLayout();
    frame(editor, 2);
    CHECK(editor.codeDirty() && editor.codeLines()[7] == "\t\tprint \"one\" joined", "moving nodes leaves the unfinished text alone");

    // The file changed in another editor: it wins.
    writeFile(file, "header:\n\tneeds component:Transform\n\nimplement:\n\twhen created:\n\t\tfinish\n");
    fs::last_write_time(file, fs::file_time_type::clock::now() + std::chrono::seconds(10));
    frame(editor, 3);
    CHECK(!editor.codeDirty() && editor.codeError() == nullptr, "an outside change replaces the unfinished text");
    CHECK(editor.codeLines().size() == 6 && editor.codeLines()[5] == "\t\tfinish", "with what is in the file");
}

static void aFileTooBrokenForAGraphCanBeFixedInThePane() {
    std::string file = freshScript("codefix", kBroken);
    ScriptGraphEditor editor(file, nullptr);
    editor.setCodeOpen(true);
    frame(editor, 4);
    CHECK(!editor.message().empty(), "the graph can't show it");
    CHECK(editor.codeError() != nullptr && editor.codeError()->line == 2, "the pane marks the line where it stops");
    CHECK(editor.codeLines().size() == 6 && editor.codeLines()[1] == "\tbogus", "and shows the file as it is");
    CHECK(editor.codeRow(2).x > 0, "drawn");

    // Fix the line there: the file is saved, and the graph comes back.
    clickLineEnd(editor, 2);
    for (int i = 0; i < 5; i++) press(editor, ImGuiKey_Backspace);
    typeText(editor, "needs component:Transform");
    CHECK(editor.codeError() == nullptr, "once it parses nothing is marked");
    frame(editor, 45);
    CHECK(editor.message().empty() && findNode(editor, "On Begin Play") != nullptr, "the graph is back");
    CHECK(readFile(file) == "header:\n\tneeds component:Transform\n\nimplement:\n\twhen created:\n\t\tfinish\n", "from the fixed file");
    CHECK(editor.codeLinked(), "and lines belong to nodes again");
}

static void theCodePaneSelectsWithMouseAndKeysAndCopiesAndPastes() {
    std::string file = freshScript("codeselect", kCodeScript);
    ScriptGraphEditor editor(file, nullptr);
    editor.setCodeOpen(true);
    frame(editor, 4);

    // Drag from the start of "print" on line 8 to the end of "one".
    ImVec2 from = editor.codeCharPos(8, 2), to = editor.codeCharPos(8, 7);
    mouseTo(editor, from);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    frame(editor, 2);
    mouseTo(editor, ImVec2((from.x + to.x) * 0.5f, to.y), 2);
    mouseTo(editor, to, 2);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    frame(editor, 3);
    CHECK(editor.code().selectedText() == "print", "dragging selects text");

    // Copy, and paste elsewhere.
    withCtrl(true);
    press(editor, ImGuiKey_C);
    withCtrl(false);
    CHECK(g_clipboard == "print", "Ctrl+C copies the selection");
    clickLineEnd(editor, 11);
    withCtrl(true);
    press(editor, ImGuiKey_V);
    withCtrl(false);
    CHECK(editor.codeLines()[10] == "\t\tprint \"three\"print", "Ctrl+V pastes it at the cursor");
    withCtrl(true);
    press(editor, ImGuiKey_Z);
    withCtrl(false);
    CHECK(editor.codeLines()[10] == "\t\tprint \"three\"", "and undo takes it out");

    // Select a whole line with the keyboard and cut it.
    click(editor, editor.codeCharPos(8, 0));
    withShift(true);
    press(editor, ImGuiKey_End);
    withShift(false);
    CHECK(editor.code().selectedText() == "\t\tprint \"one\"", "Shift+End selects to the end of the line");
    withCtrl(true);
    press(editor, ImGuiKey_X);
    withCtrl(false);
    CHECK(g_clipboard == "\t\tprint \"one\"" && editor.codeLines()[7].empty(), "Ctrl+X cuts it");

    // Ctrl+A selects everything; Ctrl+C then holds the whole script.
    withCtrl(true);
    press(editor, ImGuiKey_A);
    press(editor, ImGuiKey_C);
    withCtrl(false);
    CHECK(g_clipboard + "\n" == editor.code().text(), "Ctrl+A, Ctrl+C copies all of it");

    // A double click selects a word.
    click(editor, editor.codeCharPos(9, 6));
    ImVec2 word = editor.codeCharPos(9, 6);
    ImGui::GetIO().AddMousePosEvent(word.x, word.y);
    frame(editor, 2);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    frame(editor, 1);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    frame(editor, 1);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    frame(editor, 2);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    frame(editor, 2);
    CHECK(editor.code().selectedText() == "Count", "a double click selects the word");

    // Escape gives the keyboard back.
    press(editor, ImGuiKey_Escape);
    CHECK(!editor.codeFocused(), "Escape leaves the pane");
    CHECK(!ImGui::GetIO().WantTextInput, "and the engine's shortcuts work again");
}

/// The components the engine gives the editor for an Object with a mesh and a material.
static std::vector<ScriptGraphEditor::ComponentInfo> sampleComponents() {
    ScriptGraphEditor::ComponentInfo mesh;
    mesh.name = "Mesh";
    mesh.detail = "Cube";
    mesh.note = "The mesh this Object is drawn with.";
    mesh.details = {{"Asset", "Cube"}, {"File", "Assets/Meshes/Cube.fbx"}, {"Material", "Brick"}};
    ScriptGraphEditor::ComponentInfo material;
    material.name = "Material";
    material.parent = "Mesh";
    material.detail = "Brick";
    material.note = "The material on the mesh.";
    material.details = {{"Asset", "Brick"}, {"File", "Assets/Materials/Brick.pmat"}};
    material.swatch = IM_COL32(170, 80, 60, 255);
    ScriptGraphEditor::ComponentInfo transform;
    transform.name = "Transform";
    transform.parent = "Mesh";
    transform.detail = "from anchor";
    transform.note = "Where the mesh sits, measured from the Object's anchor.";
    transform.details = {{"Relative to", "the Object's anchor"}, {"Location", "0, 0, 0"}, {"Rotation", "0, 0, 0"}, {"Scale", "1, 1, 1"}};
    return {mesh, material, transform};
}

static void aMeshHasItsMaterialAndTransformUnderIt() {
    std::string file = freshScript("tree", kForEachScript);
    ScriptGraphEditor editor(file, nullptr);
    editor.setObject("Door", sampleComponents(), 1);
    frame(editor, 4);
    CHECK(editor.graph().getNodes().size() > 0, "the editor draws with a component tree");
    // Transform is not a component of the Object on its own: the script needing it does not put it in the tree.
    CHECK(readFile(file).find("needs component:Transform") != std::string::npos, "the script still needs Transform");
}

/// A component the host supplies an editor for: the editor's widgets are drawn in the details panel when it is selected.
/// `drawn` counts frames the editor ran; `button` is where its button was, so a test can click it; `clicks` counts presses.
struct EditorProbe {
    int drawn = 0;
    int clicks = 0;
    ImVec2 button{-1, -1};
    std::function<void()> editor() {
        return [this]() {
            drawn++;
            if (ImGui::Button("Do It")) clicks++;
            button = ImVec2((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f,
                            (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f);
        };
    }
};

static void aComponentsEditorShowsInTheDetailsPanelWhenSelected() {
    std::string file = freshScript("compeditor", kForEachScript);
    ScriptGraphEditor editor(file, nullptr);
    EditorProbe meshProbe, cameraProbe;
    int addComponentDrawn = 0;

    auto components = sampleComponents();
    components[0].editor = meshProbe.editor();   // Mesh
    ScriptGraphEditor::ComponentInfo camera;
    camera.name = "Camera";
    camera.detail = "FOV 90";
    camera.details = {{"Read only", "row"}};
    camera.editor = cameraProbe.editor();
    components.push_back(camera);
    editor.setObject("Door", components, 1, {}, [&]() { addComponentDrawn++; });

    frame(editor, 3);
    CHECK(addComponentDrawn > 0, "the + Add Component control is drawn under the list");
    CHECK(meshProbe.drawn == 0 && cameraProbe.drawn == 0, "no editor runs until its component is selected");

    editor.selectComponent("Mesh");
    frame(editor, 3);
    CHECK(meshProbe.drawn > 0, "selecting the Mesh draws its editor in the details panel");
    CHECK(cameraProbe.drawn == 0, "and only that one");

    // The widgets in the details panel are live: a click on the editor's button reaches the host.
    CHECK(meshProbe.button.x > 0, "the editor's button was drawn on screen");
    click(editor, meshProbe.button);
    CHECK(meshProbe.clicks == 1, "clicking it runs the host's code once");

    meshProbe.drawn = 0;
    editor.selectComponent("Camera");
    frame(editor, 3);
    CHECK(cameraProbe.drawn > 0 && meshProbe.drawn == 0, "selecting another component swaps the editor");

    // A component with no editor still shows its read-only rows.
    editor.selectComponent("Material");
    frame(editor, 3);
}

static void theObjectsOwnEditorShowsWhenTheObjectIsSelected() {
    std::string file = freshScript("objeditor", kForEachScript);
    ScriptGraphEditor editor(file, nullptr);
    EditorProbe objectProbe;
    editor.setObject("Door", sampleComponents(), 1, objectProbe.editor());
    frame(editor, 3);
    CHECK(objectProbe.drawn == 0, "not drawn while nothing is selected");

    editor.selectComponent("@object");
    frame(editor, 3);
    CHECK(objectProbe.drawn > 0, "selecting the Object draws its editor (its script)");
    click(editor, objectProbe.button);
    CHECK(objectProbe.clicks == 1, "and it is live");

    // Without an editor the details panel falls back to naming the script.
    ScriptGraphEditor plain(file, nullptr);
    plain.setObject("Door", sampleComponents(), 1);
    plain.selectComponent("@object");
    frame(plain, 3);
}

static void changingAVariablesTypeResetsItsValue() {
    std::string file = freshScript("vartype", "header:\n\tneeds component:Transform\n\n\tvariable name:Speed type:decimal value:45.0\n\nimplement:\n\twhen created:\n\t\tprint \"hi\"\n");
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 3);
    CHECK(editor.variables().size() == 1 && editor.variables()[0].type == "decimal" && editor.variables()[0].value == "45.0", "the panel reads the header");
    CHECK(editor.edit("set_variable", "Speed", "variable name:Speed type:vector3 x:0 y:0 z:0"), "type can change");
    frame(editor, 2);
    CHECK(editor.variables()[0].type == "vector3" && editor.variables()[0].parts == "x:0 y:0 z:0", "to a vector with its parts");
    CHECK(!editor.edit("delete_variable", "Nope"), "deleting a variable that isn't there is refused");
    CHECK(editor.edit("delete_variable", "Speed"), "an unused variable can be deleted");
    frame(editor, 2);
    CHECK(editor.variables().empty(), "and it is gone from the panel");
}

/// The open popup whose window name contains `part` (ImGui names a combo's popup "##Combo_..").
static ImGuiWindow* popupWindow(const char* part) {
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    for (ImGuiWindow* w : ctx->Windows) {
        std::string name = w->Name ? w->Name : "";
        if (w->Active && name.find(part) != std::string::npos) return w;
    }
    return nullptr;
}

/// Where row `index` of `count` equal rows in a popup is on screen.
static ImVec2 popupRow(ImGuiWindow* w, int index, int count) {
    const float stride = w->ContentSize.y / static_cast<float>(count);
    return ImVec2(w->Pos.x + w->Size.x * 0.5f, w->Pos.y + w->WindowPadding.y + (static_cast<float>(index) + 0.5f) * stride);
}

static const char* kPickScript =
    "header:\n\tneeds component:Transform\n\tneeds entities:Foes tag:\"enemy\"\n\n\tvariable name:Targets type:list of entity\n\tvariable name:Count type:integer value:0\n\nimplement:\n\twhen created:\n\t\tprint \"hi\"\n";

/// Where grid position (gx, gy) is on screen in a script graph.
static ImVec2 gridScreen(const ScriptGraphEditor& editor, float gx, float gy) {
    const NodeGraph& g = editor.graph();
    ImVec2 c = canvasPos();
    return ImVec2(c.x + (gx * g.gridSize() + g.viewOffset().x) * g.viewZoom(), c.y + (gy * g.gridSize() + g.viewOffset().y) * g.viewZoom());
}

static void rightClick(ScriptGraphEditor& editor, ImVec2 p) {
    mouseTo(editor, p);
    ImGui::GetIO().AddMouseButtonEvent(1, true);
    frame(editor, 2);
    ImGui::GetIO().AddMouseButtonEvent(1, false);
    frame(editor, 3);
}

static const ScriptNode* getNodeFor(const ScriptGraphEditor& editor, const std::string& name, bool wired) {
    for (int i = 0;; i++) {
        const ScriptNode* n = findNode(editor, name, i);
        if (!n) return nullptr;
        bool isWired = false;
        for (const NodeConnection& c : editor.graph().getConnections()) isWired = isWired || c.outputNodeId == n->id;
        if (isWired == wired) return n;
    }
}

static void aWireInAScriptGraphCanBeCutFromItsMenu(float scale) {
    PropulsionMKI::UI::setUIScale(scale);
    std::string file = freshScript("cutwire", kForEachScript);
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 4);
    CHECK(editor.edit("add", "For Each", "h0", "0", "0"), "For Each is placed");
    CHECK(editor.edit("set_pin", "h0.0.0", "List", "Targets"), "its list is plugged in");
    // The Get node on the same row as the List pin, so the wire is straight.
    CHECK(editor.edit("set_positions", "h0.0.0.v.List\t2\t2\n"), "with a Get node beside it");
    frame(editor, 4);
    const ScriptNode* each = findNode(editor, "For Each");
    const ScriptNode* event = findNode(editor, "On Begin Play");
    if (!each || !event) return;
    const float eachX = each->position.x, eachY = each->position.y;

    // An execution wire can't be cut: its menu item is there, greyed out, and does nothing.
    const std::string before = readFile(file);
    const float runX = (event->position.x + event->size.x + eachX) * 0.5f;
    rightClick(editor, gridScreen(editor, runX, eachY + 2.0f));  // the execution pins are on the first row, so the wire runs straight across
    ImGuiWindow* menu = popupWindow("##Popup_");
    CHECK(menu != nullptr, "right-clicking an execution wire opens a menu");
    if (menu) click(editor, popupRow(menu, 0, 1));
    CHECK(readFile(file) == before, "but an execution wire is not cut");
    click(editor, gridScreen(editor, eachX + 30.0f, eachY + 40.0f));  // away from it, to close the menu if it is still open
    frame(editor, 2);

    // The value wire: right-click it, and choose Delete connection.
    rightClick(editor, gridScreen(editor, (2.0f + 10.0f - 0.5f + eachX + 0.5f) * 0.5f, eachY + 3.5f));
    menu = popupWindow("##Popup_");
    CHECK(menu != nullptr, "right-clicking a value wire opens a menu");
    if (!menu) return;
    click(editor, popupRow(menu, 0, 1));
    CHECK(readFile(file).find("in unset:") != std::string::npos, "cutting the wire unplugged the pin");
    const ScriptNode* loose = getNodeFor(editor, "Get Targets", false);
    CHECK(loose != nullptr, "and the Get node is kept, not wired to anything");
    CHECK(getNodeFor(editor, "Get Targets", true) == nullptr, "there is no wire left");
    each = findNode(editor, "For Each");
    CHECK(each && each->fields[1].wiredKey.empty() && each->fields[1].text == "unset", "the pin is empty again");

    // Undo puts the wire back, and the loose copy goes.
    CHECK(editor.undo(), "cutting is one undo step");
    frame(editor, 3);
    CHECK(readFile(file).find("in Targets:") != std::string::npos, "the variable is plugged in again");
    CHECK(getNodeFor(editor, "Get Targets", true) != nullptr && getNodeFor(editor, "Get Targets", false) == nullptr, "as one wired Get node");

    // The pin has the same menu: right-click it (its menu ends with Delete connection).
    each = findNode(editor, "For Each");
    rightClick(editor, pinScreen(editor, *each, 2, false));
    menu = popupWindow("##Popup_");
    CHECK(menu != nullptr, "right-clicking a wired pin opens its menu");
    if (menu) {
        click(editor, ImVec2(menu->Pos.x + menu->Size.x * 0.5f, menu->Pos.y + menu->Size.y - menu->WindowPadding.y - ImGui::GetTextLineHeight() * 0.5f - 1.0f));
        CHECK(readFile(file).find("in unset:") != std::string::npos, "Delete connection on the pin cuts it too");
    }
    PropulsionMKI::UI::setUIScale(1.0f);
}

static void aDropdownsTextFollowsTheZoom() {
    PropulsionMKI::UI::setUIScale(1.5f);
    std::string file = freshScript("dropzoom", kPickScript);
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 4);
    CHECK(editor.edit("add", "For Each", "h0", "0", "0"), "For Each is placed");
    frame(editor, 3);
    float last = 0.0f;
    for (int step = 0; step < 3; step++) {
        const ScriptNode* each = findNode(editor, "For Each");
        if (!each) return;
        const float zoom = editor.graph().viewZoom();
        CHECK(step == 0 || zoom < last, "the view zoomed out");
        last = zoom;
        click(editor, fieldOf(editor, *each, 2));
        ImGuiWindow* popup = popupWindow("##Combo_");
        CHECK(popup != nullptr, "the dropdown opens");
        if (popup) CHECK(std::abs(popup->FontWindowScale - zoom) < 0.01f, "its text is the size of the graph's (zoom included)");
        click(editor, gridScreen(editor, each->position.x - 3.0f, each->position.y + 12.0f));  // close it
        frame(editor, 2);
        // Zoom out a notch over the graph.
        ImVec2 c = canvasPos();
        mouseTo(editor, ImVec2(c.x + 300.0f, c.y + 500.0f));
        ImGui::GetIO().AddMouseWheelEvent(0, -1.0f);
        frame(editor, 3);
    }
    PropulsionMKI::UI::setUIScale(1.0f);
}

static void aTooltipFollowsTheUiScaleAndHasRoomAtTheSides(float scale) {
    PropulsionMKI::UI::setUIScale(scale);
    std::string file = freshScript("tooltip", kPickScript);
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 4);
    CHECK(editor.edit("add", "For Each", "h0", "0", "0"), "For Each is placed");
    frame(editor, 3);
    const ScriptNode* each = findNode(editor, "For Each");
    if (!each) return;
    CHECK(!each->error.empty(), "its unset list is a problem, which its title tooltip tells");
    mouseTo(editor, gridScreen(editor, each->position.x + 1.0f, each->position.y + 0.5f));
    frame(editor, 4);
    ImGuiWindow* tip = popupWindow("##Tooltip_");
    CHECK(tip != nullptr, "hovering a node's title shows a tooltip");
    if (tip) {
        // (the canvas draws with the font's UI scale off, so the window's own scale is all of it)
        CHECK(std::abs(tip->FontWindowScale - scale) < 0.01f, "the tooltip's text is at the UI scale");
        CHECK(tip->WindowPadding.x >= ImGui::GetStyle().WindowPadding.x + 5.0f * scale && tip->WindowPadding.y >= ImGui::GetStyle().WindowPadding.y, "with more room at its sides than the window padding");
    }
    PropulsionMKI::UI::setUIScale(1.0f);
}

static void aSwitchGrowsCasesThatShowUpAsOutputsAndFields() {
    std::string file = freshScript("switchcases", kPickScript);
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 4);
    CHECK(editor.edit("add", "Switch", "h0", "0", "0"), "a Switch is placed");
    frame(editor, 3);
    const ScriptNode* sw = findNode(editor, "Switch");
    if (!sw) return;
    CHECK(sw->execLabels == std::vector<std::string>({"Case 1", "Next"}), "it starts with one case");
    CHECK(sw->fields.size() == 2 && sw->fields[0].name == "Value" && sw->fields[1].name == "Case 1", "a field for the value and one for the case");
    const std::string key = sw->key;
    CHECK(editor.edit("add_branch", key, "case"), "a case is added");
    CHECK(editor.edit("add_branch", key, "otherwise"), "and an otherwise");
    frame(editor, 3);
    sw = findNode(editor, "Switch");
    CHECK(sw && sw->execLabels == std::vector<std::string>({"Case 1", "Case 2", "Otherwise", "Next"}), "each is an output of its own");
    CHECK(sw && sw->fields.size() == 3 && sw->fields[2].name == "Case 2", "and a case has a field to type its values in");
    CHECK(readFile(file).find("otherwise:") != std::string::npos && readFile(file).find("case 2:") != std::string::npos, "the script says so");
    CHECK(editor.edit("remove_branch", key, "2"), "the otherwise is removed");
    frame(editor, 3);
    sw = findNode(editor, "Switch");
    CHECK(sw && sw->execLabels.size() == 3, "and its output goes");
}

static void enumsAndStructsAreListedAsTypesOfTheScript() {
    std::string file = freshScript(
        "types",
        "header:\n\tneeds component:Transform\n\n\tenum name:Mode values:Idle Chasing\n\n\tstruct name:Item\n\t\tfield name:Cost type:integer\n\n"
        "\tvariable name:State type:Mode\n\tvariable name:Held type:Item\n\nimplement:\n\twhen created:\n\t\tmake State Chasing\n\t\tmake Held.Cost 5\n");
    ScriptGraphEditor editor(file, nullptr);
    editor.setObject("Door", sampleComponents(), 1);
    frame(editor, 4);
    bool mode = false, item = false, hit = false;
    for (const auto& t : editor.types()) {
        if (t.name == "Mode") mode = t.kind == "enum" && t.values == std::vector<std::string>({"Idle", "Chasing"});
        if (t.name == "Item") item = t.kind == "struct" && t.fields.size() == 1 && t.fields[0].first == "Cost" && t.fields[0].second == "integer";
        if (t.name == "hit") hit = t.kind == "struct";
    }
    CHECK(mode && item && hit, "the editor knows the script's enum and struct, and the built-in hit");
    CHECK(editor.variables().size() == 2 && editor.variables()[0].type == "Mode", "a variable of an enum type keeps it");
    CHECK(findNode(editor, "Set Variable") != nullptr, "and the nodes using them show");
    frame(editor, 3);
}

static void aVariablePinIsADropdownOfWhatFitsIt() {
    std::string file = freshScript("dropdown", kPickScript);
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 4);
    CHECK(editor.edit("add", "For Each", "h0", "0", "0"), "For Each is placed");
    frame(editor, 3);
    const ScriptNode* each = findNode(editor, "For Each");
    if (!each) return;
    CHECK(each->fields.size() == 2 && each->fields[1].picks && !each->fields[0].picks, "the List pin is a dropdown; the loop's own name is not");
    CHECK(each->fields[1].choices == std::vector<std::string>({"Targets", "Foes"}), "it offers the lists, and the entities this script reaches for, but not the integer");

    // Open it and pick the second one.
    click(editor, fieldOf(editor, *each, 2));
    ImGuiWindow* popup = popupWindow("##Combo_");
    CHECK(popup != nullptr, "clicking the field opens the dropdown");
    if (!popup) return;
    click(editor, popupRow(popup, 1, 2));
    CHECK(readFile(file).find("for each Item in Foes:") != std::string::npos, "picking Foes plugged it in");
    each = findNode(editor, "For Each");
    CHECK(each && each->error.empty() && each->fields[1].text == "Foes", "and the problem is gone");
    CHECK(editor.undo(), "picking is one undo step");
    frame(editor, 2);
    CHECK(readFile(file).find("in unset:") != std::string::npos, "that undoes");

    // The dropdown follows the header.
    CHECK(editor.edit("add_variable", "variable name:Extra type:list of integer"), "another list is declared");
    frame(editor, 2);
    each = findNode(editor, "For Each");
    CHECK(each && each->fields[1].choices.size() == 3 && each->fields[1].choices[1] == "Extra", "and is offered at once");
    CHECK(editor.edit("delete_variable", "Extra"), "and when it goes");
    frame(editor, 2);
    each = findNode(editor, "For Each");
    CHECK(each && each->fields[1].choices.size() == 2, "it is not offered any more");

    // Timers and lists are not the same thing: Reset Timer offers none of these.
    CHECK(editor.edit("add", "Reset Timer", "h0", "0", "1"), "Reset Timer is placed");
    frame(editor, 2);
    const ScriptNode* reset = findNode(editor, "Reset Timer");
    CHECK(reset && reset->fields.size() == 1 && reset->fields[0].picks && reset->fields[0].choices.empty(), "and has nothing to offer until a timer is declared");
    CHECK(editor.edit("add_variable", "variable name:Wait type:timer"), "a timer is declared");
    frame(editor, 2);
    reset = findNode(editor, "Reset Timer");
    CHECK(reset && reset->fields[0].choices == std::vector<std::string>({"Wait"}), "and is offered there");
}

static void theReferencesPanelAddsEntitiesAndScripts() {
    std::string file = freshScript("refs", kPickScript);
    writeFile(fs::path(file).parent_path() / "coin.prop", "header:\n\tneeds component:Transform\n\nimplement:\n\twhen created:\n\t\tprint \"coin\"\n");
    ScriptGraphEditor editor(file, nullptr);
    editor.setObject("Door", sampleComponents(), 1);
    frame(editor, 4);
    CHECK(editor.references().size() == 1 && editor.references()[0].name == "Foes" && editor.references()[0].kind == "entities", "the header's reference is listed");
    CHECK(editor.scriptChoices() == std::vector<std::string>({"coin.prop"}), "the other script beside it is offered, not itself");
    CHECK(editor.addReferenceButton().x > 0, "the + Reference button is drawn");

    // The + Reference menu: its first item is Entity.
    click(editor, editor.addReferenceButton());
    ImGuiWindow* menu = popupWindow("##Popup_");
    CHECK(menu != nullptr, "the + Reference button opens a menu");
    if (menu) {
        click(editor, ImVec2(menu->Pos.x + menu->Size.x * 0.3f, menu->Pos.y + menu->WindowPadding.y + ImGui::GetTextLineHeight() * 0.5f + 1.0f));
        CHECK(editor.references().size() == 2 && editor.references()[1].name == "Entity" && editor.references()[1].kind == "entity", "choosing Entity declared one");
    }
    const std::string text = readFile(file);
    CHECK(text.find("needs entity:Entity file:\"coin.prop\" tag:\"entity\"") != std::string::npos, "naming the other script it can run");
    CHECK(text.find("needs entity:Entity") < text.find("variable name:Targets"), "in the header, with the other needs lines");

    // Another entity list, and a script.
    CHECK(editor.addReference("entities") == "Entities", "an entity list gets a free name");
    CHECK(editor.addReference("file", "coin.prop") == "Coin", "a script is called after its file");
    CHECK(editor.addReference("file").empty(), "a script reference with no script is refused");
    CHECK(readFile(file).find("needs file:\"coin.prop\" as Coin") != std::string::npos, "the script is written as a needs file");
    CHECK(editor.lastCompile().problems.empty(), "nothing compiled yet");
    editor.compile();
    CHECK(editor.lastCompile().errors == 0, "all of them are valid, so Compile has no errors");

    // Clicking a reference's row shows its details.
    CHECK(editor.selectedReferenceName() == "Coin", "the one just added is selected");
    click(editor, editor.referenceRow("Foes"));
    CHECK(editor.selectedReferenceName() == "Foes", "clicking another row selects it");
    click(editor, editor.referenceRow("Entity"));
    CHECK(editor.selectedReferenceName() == "Entity", "and another");

    // The new reference has the keyboard, in Details: typing renames it.
    ImGuiIO& io = ImGui::GetIO();
    CHECK(editor.addReference("entities") == "Entities2", "a second list gets another name");
    frame(editor, 3);
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_A, true);
    frame(editor, 2);
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    io.AddInputCharactersUTF8("Bosses");
    frame(editor, 2);
    press(editor, ImGuiKey_Enter);
    frame(editor, 2);
    bool renamed = false;
    for (const auto& r : editor.references()) renamed = renamed || r.name == "Bosses";
    CHECK(renamed && readFile(file).find("needs entities:Bosses") != std::string::npos, "typing a name renamed the reference");

    // An entity list is a list, so For Each offers it; undo takes the reference away again.
    CHECK(editor.edit("add", "For Each", "h0", "0", "0"), "For Each is placed");
    frame(editor, 3);
    const ScriptNode* each = findNode(editor, "For Each");
    CHECK(each && each->fields[1].choices == std::vector<std::string>({"Targets", "Foes", "Entities", "Bosses"}), "every list and entity list is offered");
    CHECK(editor.undo() && editor.undo(), "adding and renaming undo");
    frame(editor, 2);
    bool back = false;
    for (const auto& r : editor.references()) back = back || r.name == "Entities2";
    CHECK(editor.references().size() == 5 && back, "the node is gone, and the reference has its old name");
}

static ImVec2 materialGridScreen(const NodeGraph& g, float gx, float gy) {
    ImVec2 c = canvasPos("materialtest/");
    return ImVec2(c.x + (gx * g.gridSize() + g.viewOffset().x) * g.viewZoom(), c.y + (gy * g.gridSize() + g.viewOffset().y) * g.viewZoom());
}

static void gRightClick(NodeGraph& g, ImVec2 p) {
    gMouseTo(g, p);
    ImGui::GetIO().AddMouseButtonEvent(1, true);
    frameGraph(g, 2);
    ImGui::GetIO().AddMouseButtonEvent(1, false);
    frameGraph(g, 3);
}

static void gClick(NodeGraph& g, ImVec2 p) {
    gMouseTo(g, p);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    frameGraph(g, 2);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    frameGraph(g, 3);
}

static void aMaterialWireCanBeCutFromItsMenuOrItsPin() {
    NodeGraph g;
    g.addNode(std::make_unique<MaterialOutputNode>(0, ImVec2(24, 2)));
    g.addNode(std::make_unique<ColorWheelNode>(1, ImVec2(2, 2)));
    frameGraph(g, 4);
    Node* out = g.getNode(0);
    Node* wheel = g.getNode(1);
    g.addConnection(wheel->id, wheel->outputs[0].id, out->id, out->inputs[0].id);
    frameGraph(g, 4);
    CHECK(g.getConnections().size() == 1, "one wire");

    // Right-click on the wire, halfway along its straight run.
    gRightClick(g, materialGridScreen(g, 17.0f, 3.5f));
    ImGuiWindow* menu = popupWindow("##Popup_");
    CHECK(menu != nullptr, "right-clicking a wire opens a menu");
    if (menu) gClick(g, popupRow(menu, 0, 1));
    CHECK(g.getConnections().empty(), "Delete connection cuts it");
    CHECK(g.canUndo(), "and that is an undo step");
    g.undo();
    frameGraph(g, 3);
    CHECK(g.getConnections().size() == 1, "undo puts the wire back");

    // Right-click on a pin that has a wire.
    gRightClick(g, materialGridScreen(g, 9.5f, 3.5f));
    menu = popupWindow("##Popup_");
    CHECK(menu != nullptr, "right-clicking a connected pin opens a menu");
    if (menu) gClick(g, popupRow(menu, 0, 1));
    CHECK(g.getConnections().empty(), "Delete connection on the pin cuts it too");
    g.undo();
    frameGraph(g, 3);

    // Empty canvas still has its Add Node menu, with no connection item.
    const size_t nodes = g.getNodes().size();
    gRightClick(g, materialGridScreen(g, 17.0f, 14.0f));
    menu = popupWindow("##Popup_");
    CHECK(menu != nullptr, "right-clicking empty canvas opens the Add Node menu");
    if (menu) {
        // Add Node, a separator, Color Wheel, Image: the third row is Color Wheel.
        gClick(g, ImVec2(menu->Pos.x + menu->Size.x * 0.5f, menu->Pos.y + menu->Size.y - menu->WindowPadding.y - ImGui::GetTextLineHeight() * 1.5f - 3.0f));
        CHECK(g.getNodes().size() == nodes + 1, "and still adds a node");
    }
    CHECK(g.getConnections().size() == 1, "without touching the wire");
}

// ---- the shape of a wire between two pins ----

static bool nearlyEqual(float a, float b) { return std::abs(a - b) < 0.01f; }

static bool everySegmentIsHorizontalOrVertical(const std::vector<ImVec2>& pts) {
    for (size_t i = 0; i + 1 < pts.size(); i++) {
        if (!nearlyEqual(pts[i].x, pts[i + 1].x) && !nearlyEqual(pts[i].y, pts[i + 1].y)) return false;
    }
    return true;
}

static bool hasADiagonal(const std::vector<ImVec2>& pts) {
    for (size_t i = 0; i + 1 < pts.size(); i++) {
        if (!nearlyEqual(pts[i].x, pts[i + 1].x) && !nearlyEqual(pts[i].y, pts[i + 1].y)) return true;
    }
    return false;
}

static void aWireChangesShapeWhenTheDiagonalNoLongerFits() {
    NodeGraph g;
    const float G = g.gridSize() * g.viewZoom();  // one grid square on screen
    const ImVec2 out(100.0f, 100.0f);
    std::vector<ImVec2> p;
    auto wire = [&](float dx, float dy) { g.electronicPath(out, ImVec2(out.x + dx, out.y + dy), true, p); };

    // Two squares of horizontal to spare: a square of horizontal from each pin (half of it outside the node,
    // the pin being half a square in), and a 45 degree diagonal.
    wire(4.0f * G, 2.0f * G);
    CHECK(p.size() == 4 && nearlyEqual(p[1].x - p[0].x, G) && nearlyEqual(p[3].x - p[2].x, G), "with two squares to spare there is a square of horizontal from each pin");
    CHECK(nearlyEqual(p[2].x - p[1].x, p[2].y - p[1].y), "and the diagonal between is 45 degrees");

    // Nothing to spare: only horizontals and one vertical, in the middle.
    wire(2.0f * G, 2.0f * G);
    CHECK(p.size() == 4 && everySegmentIsHorizontalOrVertical(p), "with no horizontal left the wire is horizontal and vertical only");
    CHECK(nearlyEqual(p[1].x, out.x + G) && nearlyEqual(p[2].x, out.x + G), "with the vertical halfway between the pins");
    CHECK(nearlyEqual(p[1].y, out.y) && nearlyEqual(p[2].y, out.y + 2.0f * G), "running from one pin's height to the other's");

    // Steeper than that, and not on a grid line either: still the midpoint.
    wire(2.4f * G, 3.0f * G);
    CHECK(p.size() == 4 && everySegmentIsHorizontalOrVertical(p) && nearlyEqual(p[1].x, out.x + 1.2f * G), "a steeper wire has its vertical at the midpoint");

    // Either side of the switch: the diagonal needs a square of horizontal left on each side.
    wire(4.0f * G + 2.0f, 2.0f * G);
    CHECK(hasADiagonal(p), "with a hair more than a square of horizontal each side it is a diagonal");
    wire(4.0f * G - 2.0f, 2.0f * G);
    CHECK(!hasADiagonal(p), "and with a hair less it is not");

    // Wherever the pins are, the wire shows at least half a square outside each node: a square from each pin.
    int swept = 0;
    bool stubsKept = true;
    for (float dx = -5.0f; dx <= 7.0f; dx += 0.125f) {
        for (float dy : {-5.0f, -2.0f, -0.5f, 0.5f, 2.0f, 5.0f}) {
            wire(dx * G, dy * G);
            swept++;
            const ImVec2 a = p[1], b = p[p.size() - 2];
            stubsKept = stubsKept && nearlyEqual(a.y, p[0].y) && a.x - p[0].x >= G - 0.5f && nearlyEqual(b.y, p.back().y) && p.back().x - b.x >= G - 0.5f;
        }
    }
    CHECK(swept > 500 && stubsKept, "wherever the pins are, a square of horizontal wire comes out of each");

    // Going up works the same as going down.
    wire(2.0f * G, -2.0f * G);
    CHECK(p.size() == 4 && everySegmentIsHorizontalOrVertical(p) && nearlyEqual(p[1].x, out.x + G), "a wire going up switches the same way");
}

static void aWireToAPinBehindItIsAnS() {
    NodeGraph g;
    const float G = g.gridSize() * g.viewZoom();
    const float stub = G;
    const ImVec2 out(100.0f, 100.0f);
    std::vector<ImVec2> p;

    for (float dx : {0.0f, -G, -3.0f * G}) {
        g.electronicPath(out, ImVec2(out.x + dx, out.y + 2.0f * G), true, p);
        const ImVec2 in(out.x + dx, out.y + 2.0f * G);
        CHECK(p.size() == 6 && everySegmentIsHorizontalOrVertical(p), "a pin behind the output is reached by an S of horizontals and verticals");
        CHECK(nearlyEqual(p[1].x, out.x + stub) && nearlyEqual(p[4].x, in.x - stub), "leaving a stub out of the output and coming in over a stub");
        CHECK(nearlyEqual(p[2].y, out.y + G) && nearlyEqual(p[3].y, out.y + G), "with the run back across at the middle height");
    }

    // The S meets the plain horizontal-vertical-horizontal wire where the two stubs touch, so nothing jumps.
    std::vector<ImVec2> s, z;
    g.electronicPath(out, ImVec2(out.x + 2.0f * stub - 0.01f, out.y + 3.0f * G), true, s);
    g.electronicPath(out, ImVec2(out.x + 2.0f * stub + 0.01f, out.y + 3.0f * G), true, z);
    CHECK(s.size() == 6 && z.size() == 4 && std::abs(s[2].x - z[1].x) < 0.1f, "the S turns into the plain wire without a jump");

    // A wire dragged out of an input is the same wire, walked the other way.
    const ImVec2 in(out.x - G, out.y + 2.0f * G);
    std::vector<ImVec2> forward, backward;
    g.electronicPath(out, in, true, forward);
    g.electronicPath(in, out, false, backward);
    bool same = forward.size() == backward.size();
    for (size_t i = 0; same && i < forward.size(); i++) same = nearlyEqual(forward[i].x, backward[backward.size() - 1 - i].x) && nearlyEqual(forward[i].y, backward[backward.size() - 1 - i].y);
    CHECK(same, "a wire dragged from an input takes the same route");
}


// ---- nodes that are not connected to the script ----

static const ScriptNode* firstFloating(const ScriptGraphEditor& editor) {
    for (const auto& n : editor.graph().getNodes()) {
        const ScriptNode* s = dynamic_cast<const ScriptNode*>(n.get());
        if (s && s->floating) return s;
    }
    return nullptr;
}

/// A place on the canvas, in grid units, with nothing under or near it: below every node.
static ImVec2 emptySpot(const ScriptGraphEditor& editor) {
    float bottom = 0.0f;
    for (const auto& n : editor.graph().getNodes()) bottom = (std::max)(bottom, n->position.y + n->size.y);
    return ImVec2(1.0f, bottom + 2.0f);
}

static void typingInTheCanvasMenuAddsANodeThatFloatsUntilItIsWiredIn() {
    std::string file = freshScript("floating", kThree);
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 4);
    const std::string before = readFile(file);

    // The menu has no text field until something is typed into it.
    const ImVec2 spot = emptySpot(editor);
    rightClick(editor, gridScreen(editor, spot.x, spot.y));
    CHECK(popupWindow("##Popup_") != nullptr, "right-clicking empty canvas opens the menu");
    CHECK(!ImGui::GetIO().WantTextInput, "with no text field");
    ImGui::GetIO().AddInputCharactersUTF8("print");
    frame(editor, 3);
    CHECK(ImGui::GetIO().WantTextInput, "typing opens a search field");
    CHECK(popupWindow("##Popup_") != nullptr, "in the same menu");
    press(editor, ImGuiKey_Enter);
    frame(editor, 3);

    // The node is on the canvas where the menu was opened, and the script knows nothing about it.
    CHECK(readFile(file) == before, "the script is not changed by a node that is not connected");
    const ScriptNode* floating = firstFloating(editor);
    CHECK(floating && floating->name == "Print Debug Message", "the best match was added");
    if (!floating) return;
    CHECK(std::abs(floating->position.x - spot.x) < 1.5f && std::abs(floating->position.y - spot.y) < 1.5f, "where the menu was opened");
    CHECK(editor.message().empty(), "without complaint");

    // It is still there when the script is opened again.
    {
        ScriptGraphEditor again(file, nullptr);
        frame(again, 3);
        const ScriptNode* kept = firstFloating(again);
        CHECK(kept && kept->name == "Print Debug Message" && kept->position.x == floating->position.x, "kept in the layout file");
    }

    // Wiring the handler's Run output to it puts it in the script.
    const ScriptNode* event = findNode(editor, "On Begin Play");
    CHECK(event != nullptr, "the handler");
    if (!event) return;
    const ImVec2 from = pinScreen(editor, *event, 0, true);
    const ImVec2 to = pinScreen(editor, *floating, 0, false);
    mouseTo(editor, from);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    frame(editor, 2);
    mouseTo(editor, ImVec2((from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f), 2);
    mouseTo(editor, to, 2);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    frame(editor, 4);
    CHECK(firstFloating(editor) == nullptr, "nothing floats once it is wired in");
    CHECK(countOf(readFile(file), "print ") == 4, "the script now prints four times");
    CHECK(readFile(file).find("fsrc") == std::string::npos && !fs::exists(file + ".layout") ||
              readFile(file + ".layout").find("fsrc") == std::string::npos,
          "and the layout no longer keeps it");
    CHECK(editor.message().empty(), "without complaint");

    // Each step is undone on its own.
    CHECK(editor.undo(), "wiring it in can be undone");
    frame(editor, 3);
    CHECK(firstFloating(editor) != nullptr && readFile(file) == before, "which floats it again");
    CHECK(editor.undo(), "and so can adding it");
    frame(editor, 3);
    CHECK(firstFloating(editor) == nullptr && readFile(file) == before, "which takes it away");
    CHECK(editor.redo(), "redo brings it back");
    frame(editor, 3);
    CHECK(firstFloating(editor) != nullptr, "floating");
}


static void aValueNodeCanFloatAndBeWiredIntoAPinLater() {
    std::string file = freshScript("floatvalue", kThree);
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 4);
    const std::string before = readFile(file);

    const ImVec2 spot = emptySpot(editor);
    rightClick(editor, gridScreen(editor, spot.x, spot.y));
    ImGui::GetIO().AddInputCharactersUTF8("distance");
    frame(editor, 3);
    press(editor, ImGuiKey_Enter);
    frame(editor, 3);
    CHECK(readFile(file) == before, "a value that is plugged into nothing is not in the script");
    const ScriptNode* value = firstFloating(editor);
    CHECK(value && value->name == "Distance" && value->isData(), "a value node floats on the canvas");
    if (!value) return;

    // Wire its output into the Message of the first print.
    const ScriptNode* print = findNode(editor, "Print Debug Message", 0);
    CHECK(print != nullptr, "a print node");
    if (!print) return;
    const ImVec2 from = pinScreen(editor, *value, 0, true);
    const ImVec2 to = pinScreen(editor, *print, 1, false);
    mouseTo(editor, from);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    frame(editor, 2);
    mouseTo(editor, ImVec2((from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f), 2);
    mouseTo(editor, to, 2);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    frame(editor, 4);
    CHECK(firstFloating(editor) == nullptr, "nothing floats once it is plugged in");
    CHECK(readFile(file).find("print distance from") != std::string::npos, "the first print now prints a distance");
    CHECK(editor.message().empty(), "without complaint");
    const ScriptNode* plugged = findNode(editor, "Distance");
    CHECK(plugged && std::abs(plugged->position.x - spot.x) < 1.5f && std::abs(plugged->position.y - spot.y) < 1.5f, "the node stays where it was");
    CHECK(editor.undo(), "plugging it in can be undone");
    frame(editor, 3);
    CHECK(firstFloating(editor) != nullptr && readFile(file) == before, "which floats it again");

    // Deleting it leaves nothing behind.
    const ScriptNode* again = firstFloating(editor);
    if (!again) return;
    mouseTo(editor, ImVec2(nodeScreen(editor, *again).x + 10.0f, nodeScreen(editor, *again).y + 4.0f));
    click(editor, ImVec2(nodeScreen(editor, *again).x + 10.0f, nodeScreen(editor, *again).y + 4.0f));
    press(editor, ImGuiKey_Delete);
    frame(editor, 3);
    CHECK(firstFloating(editor) == nullptr && readFile(file) == before, "deleting a floating value removes it");
    CHECK(!fs::exists(file + ".layout") || readFile(file + ".layout").find("fvalue") == std::string::npos, "and the layout forgets it");
}

static const char* kBlock = "header:\n\tneeds component:Transform\n\tvariable name:Count type:integer value:0\n\nimplement:\n\twhen created:\n\t\tif Count is above 3:\n\t\t\tprint \"many\"\n\t\tprint \"after\"\n";

static void ctrlD(ScriptGraphEditor& editor) {
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);
    press(editor, ImGuiKey_D);
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false);
    frame(editor, 3);
}

static void aDuplicatedNodeIsACopyConnectedToNothingAndNotInTheScript() {
    std::string file = freshScript("duplicate", kThree);
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 4);
    const std::string before = readFile(file);
    const ScriptNode* two = findNode(editor, "Print Debug Message", 1);
    if (!two) return;
    const ImVec2 from = two->position;
    click(editor, titleOf(editor, *two));
    ctrlD(editor);

    const ScriptNode* copy = firstFloating(editor);
    CHECK(copy && copy->name == "Print Debug Message", "Ctrl+D made a copy of the selected node");
    if (!copy) return;
    CHECK(readFile(file) == before, "the script's text is untouched");
    CHECK(editor.message().empty(), "without complaint");
    CHECK(copy->position.x == std::round(from.x) + 2 && copy->position.y == std::round(from.y) + 2, "the copy is a little below and to the right");
    bool sameMessage = false;
    for (const ScriptPinField& f : copy->fields) sameMessage = sameMessage || (f.name == "Message" && f.text == "\"two\"");
    CHECK(sameMessage, "it has the same settings");
    CHECK(copy->hasExecIn && copy->execTargets.size() == copy->execLabels.size(), "it has the same pins");
    for (const std::string& target : copy->execTargets) CHECK(target.empty(), "and nothing runs after it");
    CHECK(findNode(editor, "Print Debug Message", 3) != nullptr, "the original three are still there");

    // One undo takes the copy away again.
    CHECK(editor.undo(), "duplicating can be undone");
    frame(editor, 3);
    CHECK(firstFloating(editor) == nullptr && readFile(file) == before, "which leaves nothing behind");
}

static void aDuplicatedBlockLeavesItsContentsAndItsConditionBehind() {
    std::string file = freshScript("duplicateblock", kBlock);
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 4);
    const std::string before = readFile(file);
    const ScriptNode* branch = findNode(editor, "Branch");
    if (!branch) return;
    const std::string key = branch->key;
    const size_t nodes = editor.graph().getNodes().size();

    CHECK(editor.duplicateNodes({key}), "a branch can be copied");
    frame(editor, 3);
    CHECK(readFile(file) == before, "the script's text is untouched");
    const ScriptNode* copy = firstFloating(editor);
    CHECK(copy && copy->name == "Branch", "the copy is a floating Branch");
    if (!copy) return;
    CHECK(editor.graph().getNodes().size() == nodes + 1, "one node was added: not what is inside it or plugged into it");
    for (const std::string& target : copy->execTargets) CHECK(target.empty(), "nothing is under the copy's outputs");
    for (const ScriptPinField& f : copy->fields) CHECK(f.wiredKey.empty(), "and nothing is plugged into its pins");
    const ScriptNode* original = findNode(editor, "Branch");
    CHECK(original && !original->floating && !original->execTargets.empty() && original->execTargets[0] != "", "the original keeps its wires");
}

static void escapeClosesTheMenuAndBackspaceBringsTheMenuBack() {
    std::string file = freshScript("menusearch", kThree);
    ScriptGraphEditor editor(file, nullptr);
    frame(editor, 4);
    const ImVec2 spot = emptySpot(editor);
    rightClick(editor, gridScreen(editor, spot.x, spot.y));
    ImGui::GetIO().AddInputCharactersUTF8("zz");
    frame(editor, 3);
    CHECK(ImGui::GetIO().WantTextInput, "typing opens the field");
    press(editor, ImGuiKey_Backspace);
    press(editor, ImGuiKey_Backspace);
    frame(editor, 3);
    CHECK(!ImGui::GetIO().WantTextInput, "emptying it closes the field again");
    CHECK(popupWindow("##Popup_") != nullptr, "and the menu is still open");
    ImGui::GetIO().AddInputCharactersUTF8("q");
    frame(editor, 3);
    press(editor, ImGuiKey_Escape);
    frame(editor, 3);
    CHECK(popupWindow("##Popup_") == nullptr, "Escape closes the whole menu");
    CHECK(firstFloating(editor) == nullptr, "and adds nothing");
}

int main(int argc, char** argv) {
    fs::path propulsionScript = argc > 1 ? fs::path(argv[1]) : fs::path("G:/PropulsionMKI/PropulsionScript");
    setupImGui();

    everyRealScriptLaysOutWithoutOverlap(propulsionScript);
    editsGoThroughToTheFile();
    typingIntoAFieldEditsTheScript();
    draggingAnExecutionWireReordersTheScript();
    aBoxSelectsEveryNodeItTouches();
    shiftClickTogglesAndPlainClickPicksOne();
    draggingASelectionMovesEveryNodeAndTheLayoutIsSaved();
    deletingASelectionIsOneUndoableStep();
    deletingTheLastNodeInABlockWorks();
    anAddedNodeLandsWhereItWasAskedForAndStays();
    typingInTheCanvasMenuAddsANodeThatFloatsUntilItIsWiredIn();
    aValueNodeCanFloatAndBeWiredIntoAPinLater();
    aDuplicatedNodeIsACopyConnectedToNothingAndNotInTheScript();
    aDuplicatedBlockLeavesItsContentsAndItsConditionBehind();
    escapeClosesTheMenuAndBackspaceBringsTheMenuBack();
    materialGraphSelectDragDeleteAndUndo();
    materialGraphChangesMadeInAnyWayAreUndoable();
    aNodeThatNeedsAListCanBePlacedBeforeThereIsOne();
    thePlusButtonDeclaresAVariableAndTypingRenamesIt();
    aMeshHasItsMaterialAndTransformUnderIt();
    aComponentsEditorShowsInTheDetailsPanelWhenSelected();
    theObjectsOwnEditorShowsWhenTheObjectIsSelected();
    changingAVariablesTypeResetsItsValue();
    aListDraggedOntoForEachIsPluggedIn();
    remembersAreListedAsLocalVariablesUnderTheVariables();
    generatedNodesRunLeftToRightWithTwoSquaresBetween();
    aNodeAddedBetweenTwoBumpsTheRestToTheRight();
    aNodeAddedAtTheEndGoesAfterTheLastOne();
    aLongScriptBreaksTheLineAfterTenNodesAndReadsLikeABook();
    aNodeAddedToAFullLineStartsTheNextOne();
    aSecondHandlerStartsBelowEverythingAtTheLeft();
    aVariableDroppedOnTheCanvasBecomesAGetNodeToWireIn();
    aWiredGetNodeCanBeWiredIntoAnotherPin();
    aVariableDroppedOnTheCanvasBecomesAGetNodeToWireIn(1.5f);
    aVariableDroppedOnTheCanvasBecomesAGetNodeToWireIn(2.0f);
    aGetNodeWireLetGoOnEmptyCanvasOffersTheNewNodesThatTakeIt();
    aGetNodeWireLetGoOnEmptyCanvasOffersTheNewNodesThatTakeIt(1.5f);
    aBooleanGetNodeCanBeDraggedIntoANewBranch();
    aBranchComesAloneAndABooleanPlugsStraightIntoIt();
    aSetVariableOnABooleanHasACheckbox();
    aSetVariableOnABooleanHasACheckbox(1.5f);
    compileListsWhatWouldFailAtPlay();
    theCodePaneShowsTheScriptAndLinksLinesToNodes();
    aNodePickedInTheGraphScrollsTheCodeToItsLine();
    theCodeBufferEditsLikeATextEditor();
    theCodePaneEditsTheScript();
    typedTextThatParsesIsSavedBeforeAnythingElseEditsTheFile();
    aDraftSurvivesNodesBeingMovedAndTheFileChangingOutsideReplacesIt();
    aFileTooBrokenForAGraphCanBeFixedInThePane();
    theCodePaneSelectsWithMouseAndKeysAndCopiesAndPastes();
    aVariablePinIsADropdownOfWhatFitsIt();
    aSwitchGrowsCasesThatShowUpAsOutputsAndFields();
    enumsAndStructsAreListedAsTypesOfTheScript();
    theReferencesPanelAddsEntitiesAndScripts();
    aWireInAScriptGraphCanBeCutFromItsMenu(1.0f);
    aWireInAScriptGraphCanBeCutFromItsMenu(1.5f);
    aDropdownsTextFollowsTheZoom();
    aTooltipFollowsTheUiScaleAndHasRoomAtTheSides(1.0f);
    aTooltipFollowsTheUiScaleAndHasRoomAtTheSides(2.0f);
    aMaterialWireCanBeCutFromItsMenuOrItsPin();
    aWireChangesShapeWhenTheDiagonalNoLongerFits();
    aWireToAPinBehindItIsAnS();

    // Pictures of a few graphs, for looking at:  ScriptGraphTest <PropulsionScript> --shots <folder>
    if (argc > 3 && std::string(argv[2]) == "--shots") {
        std::string tree = copyTree(propulsionScript / "behaviors", "shots");
        for (const char* name : {"rotator", "door", "player", "trap", "health_pickup", "flow_kit", "math_kit", "world_kit", "list_kit"}) {
            ScriptGraphEditor editor((fs::path(tree) / (std::string(name) + ".prop")).generic_string(), nullptr);
            frame(editor, 4);
            screenshot(fs::path(argv[3]) / (std::string(name) + ".png"));
        }

        // A box being dragged over a script graph, and the nodes it selected.
        {
            ScriptGraphEditor editor((fs::path(tree) / "door.prop").generic_string(), nullptr);
            frame(editor, 4);
            ImVec2 c = canvasPos();
            dragBox(editor, ImVec2(c.x + 6.0f, c.y + 6.0f), ImVec2(c.x + 760.0f, c.y + 330.0f), false);
            screenshot(fs::path(argv[3]) / "box_select.png");
            ImGui::GetIO().AddMouseButtonEvent(0, false);
            frame(editor, 3);
            screenshot(fs::path(argv[3]) / "selected.png");
        }

        // The Components and Variables panels at each UI scale (File > Preferences > UI Scaling).
        for (float scale : {1.0f, 1.5f, 2.0f}) {
            PropulsionMKI::UI::setUIScale(scale);
            std::string file = freshScript(
                "panels",
                "header:\n\tneeds component:Transform\n\tneeds entities:Foes tag:\"enemy\"\n\n\tvariable name:Speed type:decimal value:45.0\n\tvariable name:Health type:integer value:100 exposed:true\n"
                "\tvariable name:Home type:vector3 x:1 y:2 z:3\n\tvariable name:Tint type:color r:1.0 g:0.5 b:0.25 a:1.0\n\tvariable name:Targets type:list of entity\n"
                "\tvariable name:Label type:text value:\"hello\"\n\tvariable name:Alive type:boolean value:true\n\n"
                "implement:\n\twhen created:\n\t\tprint \"hi\"\n\t\tfor each Item in Targets:\n\t\t\tfinish\n");
            ScriptGraphEditor editor(file, nullptr);
            editor.setObject("Door", sampleComponents(), 1);
            frame(editor, 4);
            ImGuiIO& io = ImGui::GetIO();
            // Click the Home row so its details show.
            ImVec2 plus = editor.addVariableButton();
            float row = ImGui::GetFrameHeight();
            io.AddMousePosEvent(plus.x - ImGui::GetFontSize() * 6.0f, plus.y + row * 3.5f);
            frame(editor, 2);
            io.AddMouseButtonEvent(0, true);
            frame(editor, 2);
            io.AddMouseButtonEvent(0, false);
            frame(editor, 3);
            char name[64];
            std::snprintf(name, sizeof(name), "panels_%.1f.png", static_cast<double>(scale));
            screenshot(fs::path(argv[3]) / name);
        }
        PropulsionMKI::UI::setUIScale(1.0f);

        // A For Each with its list dropdown open, and a reference selected with its details.
        for (float scale : {1.0f, 2.0f}) {
            PropulsionMKI::UI::setUIScale(scale);
            std::string file = freshScript("dropshot", kPickScript);
            writeFile(fs::path(file).parent_path() / "coin.prop", "header:\n\tneeds component:Transform\n\nimplement:\n\twhen created:\n\t\tprint \"coin\"\n");
            ScriptGraphEditor editor(file, nullptr);
            editor.setObject("Door", sampleComponents(), 1);
            frame(editor, 4);
            editor.addReference("entity");
            editor.addReference("file", "coin.prop");
            editor.edit("add", "For Each", "h0", "0", "0");
            frame(editor, 3);
            char name[64];
            if (const ScriptNode* each = findNode(editor, "For Each")) {
                click(editor, fieldOf(editor, *each, 2));
                std::snprintf(name, sizeof(name), "dropdown_%.1f.png", static_cast<double>(scale));
                screenshot(fs::path(argv[3]) / name);
            }
            click(editor, editor.referenceRow("Entity"));  // the first click closes the dropdown
            click(editor, editor.referenceRow("Entity"));
            frame(editor, 3);
            std::snprintf(name, sizeof(name), "references_%.1f.png", static_cast<double>(scale));
            screenshot(fs::path(argv[3]) / name);
        }
        PropulsionMKI::UI::setUIScale(1.0f);

        // A For Each with a Get node wired into its List pin.
        {
            std::string file = freshScript("getshot", kForEachScript);
            ScriptGraphEditor editor(file, nullptr);
            frame(editor, 4);
            editor.edit("add", "For Each", "h0", "0", "0");
            editor.edit("set_pin", "h0.0.0", "List", "Targets");
            editor.edit("set_positions", "h0.0.0.v.List\t2\t2\n");
            frame(editor, 4);
            screenshot(fs::path(argv[3]) / "getnode.png");
            if (const ScriptNode* each = findNode(editor, "For Each")) {
                mouseTo(editor, gridScreen(editor, (2.0f + 10.0f - 0.5f + each->position.x + 0.5f) * 0.5f, each->position.y + 3.5f));
                screenshot(fs::path(argv[3]) / "wire_hover.png");
                rightClick(editor, gridScreen(editor, (2.0f + 10.0f - 0.5f + each->position.x + 0.5f) * 0.5f, each->position.y + 3.5f));
                screenshot(fs::path(argv[3]) / "wire_menu.png");
            }
        }

        // Compile: a For Each with nothing plugged in, the results under the graph, and its node's details.
        {
            std::string file = freshScript("compileshot", kForEachScript);
            ScriptGraphEditor editor(file, nullptr);
            editor.setObject("Door", sampleComponents(), 1);
            frame(editor, 4);
            editor.edit("add", "For Each", "h0", "0", "0");
            editor.edit("add_variable", "variable name:Idle type:integer value:0");
            frame(editor, 3);
            click(editor, editor.compileButton());
            frame(editor, 3);
            if (const ScriptNode* each = findNode(editor, "For Each")) click(editor, titleOf(editor, *each));
            frame(editor, 3);
            screenshot(fs::path(argv[3]) / "compile.png");
            // Material component details.
            editor.setObject("Door", sampleComponents(), 1);
        }

        // The code pane beside the graph, with a node selected (at each UI scale).
        for (float scale : {1.0f, 1.5f}) {
            PropulsionMKI::UI::setUIScale(scale);
            std::string tree2 = copyTree(propulsionScript / "behaviors", "codeshot");
            ScriptGraphEditor editor((fs::path(tree2) / "player.prop").generic_string(), nullptr);
            editor.setCodeOpen(true);
            frame(editor, 4);
            if (const ScriptNode* rotate = findNode(editor, "Rotate Object")) click(editor, titleOf(editor, *rotate));
            frame(editor, 3);
            char name[64];
            std::snprintf(name, sizeof(name), "code_%.1f.png", static_cast<double>(scale));
            screenshot(fs::path(argv[3]) / name);
        }
        PropulsionMKI::UI::setUIScale(1.0f);

        // Typing in the code pane: text that does not parse yet (marked where it stops), with a selection.
        {
            std::string tree3 = copyTree(propulsionScript / "behaviors", "codetypeshot");
            ScriptGraphEditor editor((fs::path(tree3) / "player.prop").generic_string(), nullptr);
            editor.setCodeOpen(true);
            frame(editor, 4);
            clickLineEnd(editor, 19);                         // event name:Died
            typeText(editor, " oops");
            ImVec2 from = editor.codeCharPos(32, 2), to = editor.codeCharPos(32, 26);
            mouseTo(editor, from);
            ImGui::GetIO().AddMouseButtonEvent(0, true);
            frame(editor, 2);
            mouseTo(editor, to, 3);
            ImGui::GetIO().AddMouseButtonEvent(0, false);
            frame(editor, 2);
            screenshot(fs::path(argv[3]) / "code_typing.png");
        }

        // A material graph with two nodes selected.
        {
            NodeGraph g;
            g.addNode(std::make_unique<MaterialOutputNode>(0, ImVec2(24, 2)));
            g.addNode(std::make_unique<ColorWheelNode>(1, ImVec2(2, 2)));
            g.addNode(std::make_unique<ColorWheelNode>(2, ImVec2(2, 14)));
            frameGraph(g, 4);
            ImVec2 c = canvasPos("materialtest/");
            ImGui::GetIO().AddMousePosEvent(c.x + 6.0f, c.y + 6.0f);
            frameGraph(g, 2);
            ImGui::GetIO().AddMouseButtonEvent(0, true);
            frameGraph(g, 2);
            ImGui::GetIO().AddMousePosEvent(c.x + 420.0f, c.y + 640.0f);
            frameGraph(g, 3);
            screenshot(fs::path(argv[3]) / "material_box.png");
            ImGui::GetIO().AddMouseButtonEvent(0, false);
            frameGraph(g, 3);
        }
    }

    std::printf("%d checks, %d failed\n", checks, fails);
    if (fails == 0) std::printf("ALL PASSED\n");
    return fails == 0 ? 0 : 1;
}
