#include "ui/OutputLogPanel.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>

#include "platform/FileDialogs.h"
#include "ui/ImGuiTheme.h"

namespace Propulsion {

namespace {

ImVec4 colorFor(LogLevel level) {
    switch (level) {
        case LogLevel::Error: return ImVec4(1.00f, 0.38f, 0.38f, 1.0f);
        case LogLevel::Warning: return ImVec4(1.00f, 0.84f, 0.30f, 1.0f);
        case LogLevel::Verbose: return ImVec4(0.55f, 0.58f, 0.62f, 1.0f);
        case LogLevel::Info: break;
    }
    return ImGui::GetStyleColorVec4(ImGuiCol_Text);
}

std::string lowered(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

std::vector<std::string> wordsOf(const char* text) {
    std::vector<std::string> words;
    std::string current;
    for (const char* p = text; *p; ++p) {
        if (std::isspace(static_cast<unsigned char>(*p))) {
            if (!current.empty()) words.push_back(lowered(current));
            current.clear();
        } else {
            current += *p;
        }
    }
    if (!current.empty()) words.push_back(lowered(current));
    return words;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Keeping up with the log
// ---------------------------------------------------------------------------------------------

void OutputLogPanel::clear() {
    m_entries.clear();
    m_visible.clear();
    m_selected.clear();
    m_anchorId = 0;
    m_levelCounts.fill(0);
    m_baselineErrors = OutputLog::errorCount();
    m_baselineWarnings = OutputLog::warningCount();
    m_scrollToBottom = true;
}

void OutputLogPanel::sync() {
    std::vector<LogEntry> fresh;
    if (OutputLog::copySince(m_lastSeenId, fresh) == 0) return;

    // Entries were trimmed from the log while the panel wasn't looking: what it has no longer joins up with what
    // is new, so start again from what is left.
    if (!m_entries.empty() && fresh.front().id != m_lastSeenId + 1) {
        m_entries.clear();
        m_visible.clear();
        m_selected.clear();
        m_levelCounts.fill(0);
    }

    for (LogEntry& entry : fresh) {
        m_lastSeenId = entry.id;
        m_levelCounts[static_cast<size_t>(entry.level)]++;
        const bool shown = passesFilter(entry);
        m_entries.push_back(std::move(entry));
        if (shown) m_visible.push_back(m_entries.back().id);
    }

    while (m_entries.size() > OutputLog::kMaxEntries) {
        m_levelCounts[static_cast<size_t>(m_entries.front().level)]--;
        m_entries.pop_front();
    }
    while (!m_visible.empty() && !m_entries.empty() && m_visible.front() < m_entries.front().id) m_visible.pop_front();
}

const LogEntry* OutputLogPanel::entryWithId(uint64_t id) const {
    if (m_entries.empty() || id < m_entries.front().id) return nullptr;
    const uint64_t index = id - m_entries.front().id;  // ids in m_entries are consecutive
    return index < m_entries.size() ? &m_entries[static_cast<size_t>(index)] : nullptr;
}

bool OutputLogPanel::passesFilter(const LogEntry& entry) const {
    if (!m_showLevel[static_cast<size_t>(entry.level)]) return false;
    if (entry.category < m_categoryHidden.size() && m_categoryHidden[entry.category]) return false;
    if (m_searchTerms.empty()) return true;

    const std::string text = lowered(OutputLog::categoryName(entry.category)) + ' ' + lowered(entry.message);
    for (const std::string& term : m_searchTerms) {
        if (text.find(term) == std::string::npos) return false;
    }
    return true;
}

void OutputLogPanel::rebuildVisible() {
    m_filterDirty = false;
    m_visible.clear();
    for (const LogEntry& entry : m_entries) {
        if (passesFilter(entry)) m_visible.push_back(entry.id);
    }
}

size_t OutputLogPanel::visiblePositionOf(uint64_t id) const {
    auto it = std::lower_bound(m_visible.begin(), m_visible.end(), id);
    return it != m_visible.end() && *it == id ? static_cast<size_t>(it - m_visible.begin()) : m_visible.size();
}

// ---------------------------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------------------------

std::string OutputLogPanel::rowText(const LogEntry& entry) const {
    std::string text;
    if (m_showTimestamps) {
        const std::time_t seconds = static_cast<std::time_t>(entry.unixMillis / 1000);
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &seconds);
#else
        localtime_r(&seconds, &local);
#endif
        char stamp[32];
        std::snprintf(stamp, sizeof(stamp), "[%02d:%02d:%02d.%03d] ", local.tm_hour, local.tm_min, local.tm_sec,
                      static_cast<int>(entry.unixMillis % 1000));
        text += stamp;
    }
    if (m_showCategory) {
        text += OutputLog::categoryName(entry.category);
        text += ": ";
    }
    if (entry.level != LogLevel::Info) {
        text += logLevelName(entry.level);
        text += ": ";
    }
    text += entry.message;
    return text;
}

void OutputLogPanel::copyRows(bool onlySelected) const {
    std::string text;
    for (uint64_t id : m_visible) {
        if (onlySelected && !m_selected.count(id)) continue;
        if (const LogEntry* entry = entryWithId(id)) {
            text += rowText(*entry);
            text += '\n';
        }
    }
    if (!text.empty()) ImGui::SetClipboardText(text.c_str());
}

void OutputLogPanel::selectRange(size_t fromVisible, size_t toVisible) {
    if (m_visible.empty()) return;
    if (fromVisible > toVisible) std::swap(fromVisible, toVisible);
    toVisible = std::min(toVisible, m_visible.size() - 1);
    for (size_t i = fromVisible; i <= toVisible; ++i) m_selected.insert(m_visible[i]);
}

// ---------------------------------------------------------------------------------------------
// The window
// ---------------------------------------------------------------------------------------------

void OutputLogPanel::draw(bool* open) {
    if (!ImGui::Begin("Output Log", open)) {  // collapsed, or a tab behind another: nothing to draw
        ImGui::End();
        return;
    }

    sync();
    if (m_filterDirty) rebuildVisible();

    drawToolbar();
    ImGui::Separator();
    drawRows();

    ImGui::End();
}

void OutputLogPanel::drawToolbar() {
    using PropulsionMKI::UI::scaled;
    using PropulsionMKI::UI::tooltip;

    if (ImGui::Button("Filters")) ImGui::OpenPopup("##LogFilters");
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y), ImGuiCond_Appearing);
    if (ImGui::BeginPopup("##LogFilters")) {
        drawFilterMenu();
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(scaled(220.0f));
    if (ImGui::InputTextWithHint("##LogSearch", "Search log", m_searchBuffer, sizeof(m_searchBuffer))) {
        m_searchTerms = wordsOf(m_searchBuffer);
        m_filterDirty = true;
    }
    if (ImGui::IsItemHovered()) tooltip("Shows the rows that contain every word typed. The category counts too (try: vulkan error).");

    ImGui::SameLine();
    if (ImGui::Button("Clear")) clear();
    if (ImGui::IsItemHovered()) tooltip("Empty this window. The log file in the Logs folder keeps everything.");

    ImGui::SameLine();
    ImGui::BeginDisabled(m_logFilePath.empty());
    if (ImGui::Button("Open Log File")) PropulsionMKI::Platform::openWithDefaultApp(m_logFilePath);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        tooltip("%s", m_logFilePath.empty() ? "There is no log file for this session." : m_logFilePath.c_str());
    }

    ImGui::SameLine();
    if (ImGui::Button("Options")) ImGui::OpenPopup("##LogOptions");
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y), ImGuiCond_Appearing);
    if (ImGui::BeginPopup("##LogOptions")) {
        drawOptionsMenu();
        ImGui::EndPopup();
    }

    // What is in the log, at the right edge (when there is room).
    char errors[32], warnings[32];
    std::snprintf(errors, sizeof(errors), "%zu error%s", m_levelCounts[static_cast<size_t>(LogLevel::Error)],
                  m_levelCounts[static_cast<size_t>(LogLevel::Error)] == 1 ? "" : "s");
    std::snprintf(warnings, sizeof(warnings), "%zu warning%s", m_levelCounts[static_cast<size_t>(LogLevel::Warning)],
                  m_levelCounts[static_cast<size_t>(LogLevel::Warning)] == 1 ? "" : "s");
    const float gap = scaled(12.0f);
    const float width = ImGui::CalcTextSize(errors).x + gap + ImGui::CalcTextSize(warnings).x;
    const float x = ImGui::GetWindowContentRegionMax().x - width;
    ImGui::SameLine();
    if (x > ImGui::GetCursorPosX()) {
        ImGui::SetCursorPosX(x);
        ImGui::TextColored(colorFor(LogLevel::Error), "%s", errors);
        ImGui::SameLine(0.0f, gap);
        ImGui::TextColored(colorFor(LogLevel::Warning), "%s", warnings);
    } else {
        ImGui::NewLine();
    }
}

void OutputLogPanel::drawFilterMenu() {
    struct LevelRow { LogLevel level; const char* name; };
    static const LevelRow rows[] = {
        {LogLevel::Error, "Errors"}, {LogLevel::Warning, "Warnings"}, {LogLevel::Info, "Messages"}, {LogLevel::Verbose, "Verbose"}};
    for (const LevelRow& row : rows) {
        char label[48];
        std::snprintf(label, sizeof(label), "%s (%zu)", row.name, m_levelCounts[static_cast<size_t>(row.level)]);
        if (ImGui::Checkbox(label, &m_showLevel[static_cast<size_t>(row.level)])) m_filterDirty = true;
    }

    ImGui::Separator();
    if (ImGui::BeginMenu("Categories")) {
        const std::vector<std::string> names = OutputLog::categories();
        m_categoryHidden.resize(std::max(m_categoryHidden.size(), names.size()), false);

        if (ImGui::SmallButton("Show All")) {
            std::fill(m_categoryHidden.begin(), m_categoryHidden.end(), false);
            m_filterDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Hide All")) {
            std::fill(m_categoryHidden.begin(), m_categoryHidden.end(), true);
            m_filterDirty = true;
        }
        ImGui::Separator();

        std::vector<size_t> order(names.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return lowered(names[a]) < lowered(names[b]); });
        for (size_t index : order) {
            bool shown = !m_categoryHidden[index];
            if (ImGui::Checkbox(names[index].c_str(), &shown)) {
                m_categoryHidden[index] = !shown;
                m_filterDirty = true;
            }
        }
        ImGui::EndMenu();
    }

    ImGui::Separator();
    if (ImGui::MenuItem("Reset Filters")) {
        m_showLevel = {false, true, true, true};
        std::fill(m_categoryHidden.begin(), m_categoryHidden.end(), false);
        m_searchBuffer[0] = '\0';
        m_searchTerms.clear();
        m_filterDirty = true;
    }
}

void OutputLogPanel::drawOptionsMenu() {
    ImGui::Checkbox("Show Timestamps", &m_showTimestamps);
    ImGui::Checkbox("Show Categories", &m_showCategory);
    ImGui::Checkbox("Scroll to New Messages", &m_autoScroll);
    ImGui::Separator();
    if (ImGui::MenuItem("Scroll to Bottom")) m_scrollToBottom = true;
    if (ImGui::MenuItem("Copy All")) copyRows(false);
}

void OutputLogPanel::drawRows() {
    using PropulsionMKI::UI::scaled;

    ImGui::BeginChild("##LogRows", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);

    // Following new messages: only if that's wanted and the view was at the bottom (scrolling up stops it, scrolling
    // back down starts it again). This is measured before this frame's rows are added.
    const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
    const bool follow = m_scrollToBottom || (m_autoScroll && atBottom);
    m_scrollToBottom = false;

    if (m_visible.empty()) {
        ImGui::TextDisabled("%s", m_entries.empty() ? "Nothing has been logged yet." : "No messages match the filters.");
    }

    const float available = ImGui::GetContentRegionAvail().x;
    const ImGuiIO& io = ImGui::GetIO();
    bool copySelection = false;

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(m_visible.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const uint64_t id = m_visible[static_cast<size_t>(i)];
            const LogEntry* entry = entryWithId(id);
            if (!entry) continue;

            const std::string text = rowText(*entry);
            // A row as wide as its text, so a long line can be scrolled sideways.
            const float width = std::max(available, ImGui::CalcTextSize(text.c_str()).x + scaled(16.0f));

            ImGui::PushID(reinterpret_cast<const void*>(static_cast<uintptr_t>(id)));
            ImGui::PushStyleColor(ImGuiCol_Text, colorFor(entry->level));
            const bool selected = m_selected.count(id) != 0;
            const bool clicked = ImGui::Selectable(text.c_str(), selected, ImGuiSelectableFlags_None, ImVec2(width, 0));
            ImGui::PopStyleColor();

            if (clicked) {
                if (io.KeyShift && m_anchorId != 0 && visiblePositionOf(m_anchorId) < m_visible.size()) {
                    if (!io.KeyCtrl) m_selected.clear();
                    selectRange(visiblePositionOf(m_anchorId), static_cast<size_t>(i));
                } else if (io.KeyCtrl) {
                    if (!m_selected.erase(id)) m_selected.insert(id);
                    m_anchorId = id;
                } else {
                    m_selected.clear();
                    m_selected.insert(id);
                    m_anchorId = id;
                }
            }

            if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !selected) {  // right-click on a row outside the selection picks it
                m_selected.clear();
                m_selected.insert(id);
                m_anchorId = id;
            }
            if (ImGui::BeginPopupContextItem("##RowMenu")) {
                if (ImGui::MenuItem("Copy", "Ctrl+C")) copySelection = true;
                if (ImGui::MenuItem("Copy All Shown")) copyRows(false);
                if (ImGui::MenuItem("Select All", "Ctrl+A")) {
                    for (uint64_t shown : m_visible) m_selected.insert(shown);
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }
    clipper.End();

    if (follow) ImGui::SetScrollHereY(1.0f);

    // Keyboard, while the log has focus.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !ImGui::GetIO().WantTextInput) {
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) copySelection = true;
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
            for (uint64_t shown : m_visible) m_selected.insert(shown);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) m_selected.clear();
    }
    if (copySelection) copyRows(true);

    ImGui::EndChild();
}

} // namespace Propulsion
