#pragma once

/*
 * OutputLogPanel.h
 *
 * The Output Log window (View -> Output Log): everything in OutputLog, newest at the bottom, the way Unreal's
 * Output Log works.
 *   - errors are red and warnings yellow; Filters picks which levels and which categories are shown
 *   - the search box keeps rows that contain every word typed (it looks at the category too)
 *   - click selects a row, Shift-click a range, Ctrl-click adds one; Ctrl+C copies them (Ctrl+A selects all)
 *   - it follows new messages while scrolled to the bottom, and stops following when you scroll up
 *
 * The panel keeps its own copy of the entries it has seen, so Clear only empties the panel: the session's log
 * file still has everything.
 */

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_set>
#include <vector>

#include "core/OutputLog.h"

namespace Propulsion {

class OutputLogPanel {
public:
    /// Draw the window. `open` is set to false when the user closes it.
    void draw(bool* open);

    /// Forget what the panel has shown so far (new messages still arrive).
    void clear();

    /// Errors and warnings logged since the panel was last cleared (what the footer shows).
    size_t errorsSinceClear() const { return OutputLog::errorCount() - m_baselineErrors; }
    size_t warningsSinceClear() const { return OutputLog::warningCount() - m_baselineWarnings; }

    /// The session's log file, for the "Open Log File" button.
    void setLogFilePath(std::string path) { m_logFilePath = std::move(path); }

private:
    void sync();
    void rebuildVisible();
    bool passesFilter(const LogEntry& entry) const;
    const LogEntry* entryWithId(uint64_t id) const;
    std::string rowText(const LogEntry& entry) const;
    void copyRows(bool onlySelected) const;
    void selectRange(size_t fromVisible, size_t toVisible);
    size_t visiblePositionOf(uint64_t id) const;  // where `id` is in m_visible (it is sorted), or m_visible.size()

    void drawToolbar();
    void drawFilterMenu();
    void drawOptionsMenu();
    void drawRows();

    // What the panel has been told about so far (consecutive ids, oldest first).
    std::deque<LogEntry> m_entries;
    std::deque<uint64_t> m_visible;  // ids of the entries that pass the filter, ascending
    uint64_t m_lastSeenId = 0;
    std::array<size_t, 4> m_levelCounts{};  // by LogLevel, over m_entries

    // Filter
    std::array<bool, 4> m_showLevel{false, true, true, true};  // Verbose is off until asked for
    std::vector<bool> m_categoryHidden;                       // by category id; a category nobody hid is shown
    char m_searchBuffer[256] = "";
    std::vector<std::string> m_searchTerms;                   // lower case words from the search box
    bool m_filterDirty = true;

    // Look
    bool m_showTimestamps = false;
    bool m_showCategory = true;
    bool m_autoScroll = true;
    bool m_wasAtBottom = true;
    bool m_scrollToBottom = false;

    // Selection
    std::unordered_set<uint64_t> m_selected;
    uint64_t m_anchorId = 0;

    std::string m_logFilePath;
    size_t m_baselineErrors = 0;    // OutputLog's totals when the panel was cleared
    size_t m_baselineWarnings = 0;
};

} // namespace Propulsion
