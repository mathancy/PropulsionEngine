#pragma once

/**
 * CodeBuffer.h
 * The text of a script while it is being typed in the code pane: its lines, a cursor and a selection,
 * the edits a text editor makes (typing, Enter with the indent kept, Backspace, Tab...) and undo.
 *
 * It knows nothing about ImGui or the script library. The pane feeds it keys and mouse positions and
 * draws what it holds; the script editor decides when the text is good enough to be saved.
 *
 * Columns are byte offsets into a line and always sit on a character boundary (the text is UTF-8).
 * PropulsionScript indents with tabs only, so Tab inserts one and Enter keeps the line's indent.
 */

#include <string>
#include <vector>

namespace Propulsion {

/// How many columns a tab is drawn as.
constexpr int kCodeTabColumns = 2;

class CodeBuffer {
public:
    struct Pos {
        int line = 0;  // 0-based
        int col = 0;   // byte offset into the line
        bool operator==(const Pos& o) const { return line == o.line && col == o.col; }
        bool operator!=(const Pos& o) const { return !(*this == o); }
        bool operator<(const Pos& o) const { return line != o.line ? line < o.line : col < o.col; }
    };

    CodeBuffer() { m_lines.emplace_back(); }

    /// The text as a file holds it once split into lines and joined again: "\n" ends every line, a
    /// byte-order mark and "\r" are dropped. What `text()` returns for a buffer holding `text`.
    static std::string normalize(const std::string& text);

    /// Replace everything (the file changed under the buffer). Undo history is dropped: it was about text that is gone.
    void setText(const std::string& text);
    std::string text() const;

    const std::vector<std::string>& lines() const { return m_lines; }
    int lineCount() const { return static_cast<int>(m_lines.size()); }
    /// Goes up with every change to the text (not with cursor moves).
    unsigned revision() const { return m_revision; }

    // ---- cursor and selection ----
    Pos cursor() const { return m_cursor; }
    Pos anchor() const { return m_anchor; }
    bool hasSelection() const { return m_cursor != m_anchor; }
    Pos selectionStart() const { return m_anchor < m_cursor ? m_anchor : m_cursor; }
    Pos selectionEnd() const { return m_anchor < m_cursor ? m_cursor : m_anchor; }
    std::string selectedText() const;
    /// The nearest real position (line and character boundary).
    Pos clamp(Pos p) const;
    /// Put the cursor there; `extend` keeps the anchor, so the text between is selected.
    void setCursor(Pos p, bool extend);
    void selectAll();
    /// Select the word (or the run of spaces, or the punctuation) at `p`.
    void selectWord(Pos p);

    void moveLeft(bool extend, bool byWord);
    void moveRight(bool extend, bool byWord);
    void moveUp(bool extend);
    void moveDown(bool extend);
    void moveLines(int lines, bool extend);  // Page Up / Page Down
    void moveHome(bool extend, bool toStart);  // Home goes to the first character, then to the column 0; `toStart`: the start of the text
    void moveEnd(bool extend, bool toEnd);     // End; `toEnd`: the end of the text

    /// Where a position is drawn, counting a leading tab as `kCodeTabColumns` columns and every other
    /// character as one; and the way back (the nearest character boundary).
    int visualColumn(Pos p) const;
    int columnForVisual(int line, int visual) const;

    // ---- edits (each is one undo step, except that a run of typing, or of deleting, is one) ----
    /// Type text at the cursor, over the selection. "\n" starts a new line; other control characters are dropped.
    void insert(const std::string& utf8);
    void newline();  // Enter: the new line keeps the indent, and gets one more after a line ending in ':'
    void backspace(bool byWord);
    void del(bool byWord);
    void deleteSelection();
    /// Tab (one tab at the cursor, or at the start of every selected line) and Shift+Tab (take one tab off).
    void indent(bool outdent);

    bool undo();
    bool redo();
    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }

private:
    struct Snapshot {
        std::vector<std::string> lines;
        Pos cursor, anchor;
    };
    enum class Kind { None, Typing, Deleting, Other };

    Snapshot snapshot() const { return {m_lines, m_cursor, m_anchor}; }
    void restore(const Snapshot& s);
    /// Note the state before an edit. Typing after typing (and deleting after deleting) joins the same step.
    void record(Kind kind);
    void changed() { m_revision++; m_wantVisual = -1; }
    void breakRun() { m_breakRun = true; }
    void deleteRange(Pos a, Pos b);
    Pos insertAt(Pos at, const std::string& text);
    Pos wordLeft(Pos p) const;
    Pos wordRight(Pos p) const;
    void moveTo(Pos p, bool extend) { setCursor(p, extend); }

    std::vector<std::string> m_lines;
    Pos m_cursor, m_anchor;
    int m_wantVisual = -1;  // the column Up and Down try to stay in
    unsigned m_revision = 0;
    std::vector<Snapshot> m_undo, m_redo;
    Kind m_lastKind = Kind::None;
    bool m_breakRun = false;
};

} // namespace Propulsion
