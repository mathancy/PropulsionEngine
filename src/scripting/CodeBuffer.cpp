#include "scripting/CodeBuffer.h"

#include <algorithm>
#include <cctype>

namespace Propulsion {

namespace {

constexpr size_t kMaxUndoSteps = 200;

bool isContinuation(unsigned char c) { return (c & 0xC0) == 0x80; }

int prevBoundary(const std::string& s, int col) {
    if (col <= 0) return 0;
    col--;
    while (col > 0 && isContinuation(static_cast<unsigned char>(s[col]))) col--;
    return col;
}

int nextBoundary(const std::string& s, int col) {
    const int n = static_cast<int>(s.size());
    if (col >= n) return n;
    col++;
    while (col < n && isContinuation(static_cast<unsigned char>(s[col]))) col++;
    return col;
}

enum class CharClass { Space, Word, Other };

CharClass classOf(unsigned char c) {
    if (c == ' ' || c == '\t') return CharClass::Space;
    if (std::isalnum(c) || c == '_' || c >= 0x80) return CharClass::Word;
    return CharClass::Other;
}

int leadingTabs(const std::string& s) {
    int n = 0;
    while (n < static_cast<int>(s.size()) && s[n] == '\t') n++;
    return n;
}

/// Whether a line, up to where `left` ends, ends in a colon (not counting a comment after it).
bool endsWithColon(const std::string& left) {
    size_t end = left.size();
    bool inText = false;
    for (size_t i = 0; i < left.size(); i++) {
        if (left[i] == '"') inText = !inText;
        else if (!inText && left[i] == '/' && i + 1 < left.size() && left[i + 1] == '/') {
            end = i;
            break;
        }
    }
    while (end > 0 && (left[end - 1] == ' ' || left[end - 1] == '\t')) end--;
    return end > 0 && left[end - 1] == ':';
}

std::vector<std::string> splitLines(const std::string& text) {
    std::string t = text;
    if (t.compare(0, 3, "\xEF\xBB\xBF") == 0) t.erase(0, 3);
    std::vector<std::string> lines;
    size_t at = 0;
    while (at < t.size()) {
        size_t end = t.find('\n', at);
        if (end == std::string::npos) end = t.size();
        std::string line = t.substr(at, end - at);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        at = end + 1;
    }
    if (lines.empty()) lines.emplace_back();
    return lines;
}

std::string joinLines(const std::vector<std::string>& lines) {
    if (lines.size() == 1 && lines[0].empty()) return std::string();
    std::string out;
    for (const std::string& l : lines) {
        out += l;
        out += '\n';
    }
    return out;
}

} // namespace

std::string CodeBuffer::normalize(const std::string& text) { return joinLines(splitLines(text)); }

void CodeBuffer::setText(const std::string& text) {
    m_lines = splitLines(text);
    m_cursor = clamp(m_cursor);
    m_anchor = clamp(m_anchor);
    m_undo.clear();
    m_redo.clear();
    m_lastKind = Kind::None;
    changed();
}

std::string CodeBuffer::text() const { return joinLines(m_lines); }

// ---------------------------------------------------------------------------------------------
// Cursor and selection
// ---------------------------------------------------------------------------------------------

CodeBuffer::Pos CodeBuffer::clamp(Pos p) const {
    p.line = std::clamp(p.line, 0, lineCount() - 1);
    const std::string& s = m_lines[p.line];
    p.col = std::clamp(p.col, 0, static_cast<int>(s.size()));
    while (p.col > 0 && p.col < static_cast<int>(s.size()) && isContinuation(static_cast<unsigned char>(s[p.col]))) p.col--;
    return p;
}

void CodeBuffer::setCursor(Pos p, bool extend) {
    m_cursor = clamp(p);
    if (!extend) m_anchor = m_cursor;
    m_wantVisual = -1;
    breakRun();
}

void CodeBuffer::selectAll() {
    m_anchor = Pos{0, 0};
    m_cursor = Pos{lineCount() - 1, static_cast<int>(m_lines.back().size())};
    m_wantVisual = -1;
    breakRun();
}

void CodeBuffer::selectWord(Pos p) {
    p = clamp(p);
    const std::string& s = m_lines[p.line];
    if (s.empty()) {
        setCursor(p, false);
        return;
    }
    int at = std::min(p.col, static_cast<int>(s.size()) - 1);
    // On the end of the line, take the word before it.
    if (p.col >= static_cast<int>(s.size())) at = prevBoundary(s, static_cast<int>(s.size()));
    const CharClass cls = classOf(static_cast<unsigned char>(s[at]));
    int a = at, b = nextBoundary(s, at);
    while (a > 0 && classOf(static_cast<unsigned char>(s[prevBoundary(s, a)])) == cls) a = prevBoundary(s, a);
    while (b < static_cast<int>(s.size()) && classOf(static_cast<unsigned char>(s[b])) == cls) b = nextBoundary(s, b);
    m_anchor = Pos{p.line, a};
    m_cursor = Pos{p.line, b};
    m_wantVisual = -1;
    breakRun();
}

std::string CodeBuffer::selectedText() const {
    const Pos a = selectionStart(), b = selectionEnd();
    if (a == b) return std::string();
    if (a.line == b.line) return m_lines[a.line].substr(a.col, b.col - a.col);
    std::string out = m_lines[a.line].substr(a.col) + "\n";
    for (int l = a.line + 1; l < b.line; l++) out += m_lines[l] + "\n";
    out += m_lines[b.line].substr(0, b.col);
    return out;
}

CodeBuffer::Pos CodeBuffer::wordLeft(Pos p) const {
    if (p.col == 0) return p.line > 0 ? Pos{p.line - 1, static_cast<int>(m_lines[p.line - 1].size())} : p;
    const std::string& s = m_lines[p.line];
    int c = p.col;
    while (c > 0 && classOf(static_cast<unsigned char>(s[prevBoundary(s, c)])) == CharClass::Space) c = prevBoundary(s, c);
    if (c > 0) {
        const CharClass cls = classOf(static_cast<unsigned char>(s[prevBoundary(s, c)]));
        while (c > 0 && classOf(static_cast<unsigned char>(s[prevBoundary(s, c)])) == cls) c = prevBoundary(s, c);
    }
    return Pos{p.line, c};
}

CodeBuffer::Pos CodeBuffer::wordRight(Pos p) const {
    const std::string& s = m_lines[p.line];
    const int n = static_cast<int>(s.size());
    if (p.col >= n) return p.line + 1 < lineCount() ? Pos{p.line + 1, 0} : p;
    int c = p.col;
    const CharClass cls = classOf(static_cast<unsigned char>(s[c]));
    if (cls != CharClass::Space) {
        while (c < n && classOf(static_cast<unsigned char>(s[c])) == cls) c = nextBoundary(s, c);
    }
    while (c < n && classOf(static_cast<unsigned char>(s[c])) == CharClass::Space) c = nextBoundary(s, c);
    return Pos{p.line, c};
}

void CodeBuffer::moveLeft(bool extend, bool byWord) {
    // With a selection and no Shift, Left goes to the start of it.
    if (hasSelection() && !extend) {
        setCursor(selectionStart(), false);
        return;
    }
    Pos p = m_cursor;
    if (byWord) p = wordLeft(p);
    else if (p.col > 0) p.col = prevBoundary(m_lines[p.line], p.col);
    else if (p.line > 0) p = Pos{p.line - 1, static_cast<int>(m_lines[p.line - 1].size())};
    setCursor(p, extend);
}

void CodeBuffer::moveRight(bool extend, bool byWord) {
    if (hasSelection() && !extend) {
        setCursor(selectionEnd(), false);
        return;
    }
    Pos p = m_cursor;
    if (byWord) p = wordRight(p);
    else if (p.col < static_cast<int>(m_lines[p.line].size())) p.col = nextBoundary(m_lines[p.line], p.col);
    else if (p.line + 1 < lineCount()) p = Pos{p.line + 1, 0};
    setCursor(p, extend);
}

void CodeBuffer::moveUp(bool extend) { moveLines(-1, extend); }
void CodeBuffer::moveDown(bool extend) { moveLines(1, extend); }

void CodeBuffer::moveLines(int lines, bool extend) {
    const int want = m_wantVisual >= 0 ? m_wantVisual : visualColumn(m_cursor);
    Pos p = m_cursor;
    const int target = p.line + lines;
    if (target < 0) {
        p = Pos{0, 0};
    } else if (target >= lineCount()) {
        p = Pos{lineCount() - 1, static_cast<int>(m_lines.back().size())};
    } else {
        p.line = target;
        p.col = columnForVisual(target, want);
    }
    setCursor(p, extend);
    m_wantVisual = want;  // a run of Up and Down stays in one column, even over short lines
}

void CodeBuffer::moveHome(bool extend, bool toStart) {
    if (toStart) {
        setCursor(Pos{0, 0}, extend);
        return;
    }
    const std::string& s = m_lines[m_cursor.line];
    int first = 0;
    while (first < static_cast<int>(s.size()) && (s[first] == '\t' || s[first] == ' ')) first++;
    setCursor(Pos{m_cursor.line, m_cursor.col == first ? 0 : first}, extend);
}

void CodeBuffer::moveEnd(bool extend, bool toEnd) {
    const int line = toEnd ? lineCount() - 1 : m_cursor.line;
    setCursor(Pos{line, static_cast<int>(m_lines[line].size())}, extend);
}

int CodeBuffer::visualColumn(Pos p) const {
    p = clamp(p);
    const std::string& s = m_lines[p.line];
    const int tabs = std::min(leadingTabs(s), p.col);
    int column = tabs * kCodeTabColumns;
    for (int i = tabs; i < p.col; i = nextBoundary(s, i)) column++;
    return column;
}

int CodeBuffer::columnForVisual(int line, int visual) const {
    line = std::clamp(line, 0, lineCount() - 1);
    const std::string& s = m_lines[line];
    const int tabs = leadingTabs(s);
    if (visual < tabs * kCodeTabColumns) return std::min(tabs, (visual + kCodeTabColumns / 2) / kCodeTabColumns);
    int remaining = visual - tabs * kCodeTabColumns;
    int col = tabs;
    while (remaining > 0 && col < static_cast<int>(s.size())) {
        col = nextBoundary(s, col);
        remaining--;
    }
    return col;
}

// ---------------------------------------------------------------------------------------------
// Edits
// ---------------------------------------------------------------------------------------------

void CodeBuffer::restore(const Snapshot& s) {
    m_lines = s.lines;
    m_cursor = clamp(s.cursor);
    m_anchor = clamp(s.anchor);
    changed();
}

void CodeBuffer::record(Kind kind) {
    const bool join = (kind == Kind::Typing || kind == Kind::Deleting) && m_lastKind == kind && !m_breakRun && !m_undo.empty();
    if (!join) {
        m_undo.push_back(snapshot());
        if (m_undo.size() > kMaxUndoSteps) m_undo.erase(m_undo.begin());
    }
    m_redo.clear();
    m_lastKind = kind;
    m_breakRun = false;
}

void CodeBuffer::deleteRange(Pos a, Pos b) {
    if (b < a) std::swap(a, b);
    if (a == b) return;
    if (a.line == b.line) {
        m_lines[a.line].erase(a.col, b.col - a.col);
    } else {
        m_lines[a.line] = m_lines[a.line].substr(0, a.col) + m_lines[b.line].substr(b.col);
        m_lines.erase(m_lines.begin() + a.line + 1, m_lines.begin() + b.line + 1);
    }
    m_cursor = m_anchor = a;
}

CodeBuffer::Pos CodeBuffer::insertAt(Pos at, const std::string& text) {
    const std::string tail = m_lines[at.line].substr(at.col);
    m_lines[at.line].erase(at.col);
    size_t start = 0;
    Pos end = at;
    int line = at.line;
    while (true) {
        const size_t nl = text.find('\n', start);
        const std::string piece = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        m_lines[line] += piece;
        end = Pos{line, static_cast<int>(m_lines[line].size())};
        if (nl == std::string::npos) break;
        m_lines.insert(m_lines.begin() + line + 1, std::string());
        line++;
        start = nl + 1;
    }
    m_lines[end.line] += tail;
    return end;
}

void CodeBuffer::insert(const std::string& utf8) {
    std::string clean;
    for (unsigned char c : utf8) {
        if (c == '\n' || c == '\t' || (c >= 0x20 && c != 0x7F)) clean += static_cast<char>(c);
    }
    if (clean.empty()) return;
    record(clean.find('\n') == std::string::npos && !hasSelection() ? Kind::Typing : Kind::Other);
    if (hasSelection()) deleteRange(m_anchor, m_cursor);
    m_cursor = m_anchor = insertAt(m_cursor, clean);
    changed();
}

void CodeBuffer::newline() {
    record(Kind::Other);
    if (hasSelection()) deleteRange(m_anchor, m_cursor);
    const std::string line = m_lines[m_cursor.line];
    const std::string left = line.substr(0, m_cursor.col), right = line.substr(m_cursor.col);
    const int keep = std::min(leadingTabs(line), m_cursor.col);
    const int indent = keep + (endsWithColon(left) ? 1 : 0);
    m_lines[m_cursor.line] = left;
    m_lines.insert(m_lines.begin() + m_cursor.line + 1, std::string(static_cast<size_t>(indent), '\t') + right);
    m_cursor = m_anchor = Pos{m_cursor.line + 1, indent};
    changed();
}

void CodeBuffer::deleteSelection() {
    if (!hasSelection()) return;
    record(Kind::Other);
    deleteRange(m_anchor, m_cursor);
    changed();
}

void CodeBuffer::backspace(bool byWord) {
    if (hasSelection()) {
        deleteSelection();
        return;
    }
    if (m_cursor.line == 0 && m_cursor.col == 0) return;
    Pos target = m_cursor;
    if (m_cursor.col == 0) target = Pos{m_cursor.line - 1, static_cast<int>(m_lines[m_cursor.line - 1].size())};
    else if (byWord) target = wordLeft(m_cursor);
    else target.col = prevBoundary(m_lines[m_cursor.line], m_cursor.col);
    record(Kind::Deleting);
    deleteRange(target, m_cursor);
    changed();
}

void CodeBuffer::del(bool byWord) {
    if (hasSelection()) {
        deleteSelection();
        return;
    }
    const std::string& s = m_lines[m_cursor.line];
    Pos target = m_cursor;
    if (m_cursor.col >= static_cast<int>(s.size())) {
        if (m_cursor.line + 1 >= lineCount()) return;
        target = Pos{m_cursor.line + 1, 0};
    } else if (byWord) {
        target = wordRight(m_cursor);
    } else {
        target.col = nextBoundary(s, m_cursor.col);
    }
    record(Kind::Deleting);
    deleteRange(m_cursor, target);
    changed();
}

void CodeBuffer::indent(bool outdent) {
    if (!outdent && !hasSelection()) {
        insert("\t");
        return;
    }
    Pos a = selectionStart(), b = selectionEnd();
    // Without Shift, the lines touched by the selection; a selection that ends at the start of a line does not touch that line.
    int last = b.line;
    if (hasSelection() && b.col == 0 && b.line > a.line) last--;
    record(Kind::Other);
    bool any = false;
    for (int l = a.line; l <= last; l++) {
        std::string& s = m_lines[l];
        if (!outdent) {
            s.insert(0, "\t");
        } else if (!s.empty() && s[0] == '\t') {
            s.erase(0, 1);
        } else {
            continue;
        }
        any = true;
        auto shift = [&](Pos& p) {
            if (p.line == l) p.col = std::max(0, p.col + (outdent ? -1 : 1));
        };
        shift(m_cursor);
        shift(m_anchor);
    }
    if (!any) {
        // Nothing changed: take the step back out.
        m_undo.pop_back();
        return;
    }
    changed();
}

bool CodeBuffer::undo() {
    if (m_undo.empty()) return false;
    m_redo.push_back(snapshot());
    const Snapshot s = std::move(m_undo.back());
    m_undo.pop_back();
    restore(s);
    m_lastKind = Kind::None;
    return true;
}

bool CodeBuffer::redo() {
    if (m_redo.empty()) return false;
    m_undo.push_back(snapshot());
    const Snapshot s = std::move(m_redo.back());
    m_redo.pop_back();
    restore(s);
    m_lastKind = Kind::None;
    return true;
}

} // namespace Propulsion
