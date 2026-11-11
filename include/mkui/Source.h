#pragma once

#include "mkui/Types.h"

#include <string>
#include <vector>

// Editable source of a .ui file (the editor [UI] panel edits this, then writes it and reads it again with parseDocument).
// Keeps the file as written: comments, blank lines, property order, $theme tokens and `use` components stay as they are, so
// writeSource(parseSource(text)) == text for files in the canonical shape (two spaces per depth, "\n" line ends).
// Only what an edit touches changes. Document (mkui/Document.h) is what the runtime reads; it inlines components and
// resolves number tokens, which is why it can't be written back.
namespace mkui {

struct Document;

// One line inside a node, in file order: a property, comment or blank line (child < 0), or the place of a child node
struct SourceItem {
    std::string line; // trimmed; empty for blank lines and children
    i32 child = -1;   // index in SourceNode::children
};

struct SourceNode {
    std::string kind; // board / frame / text / image / use; empty for the file itself (root)
    std::string name;
    std::string args; // the rest of the head line: "1920 1080" for board, the component path for use
    std::vector<SourceItem> items;
    std::vector<SourceNode> children;
};

struct Source {
    SourceNode root; // kind empty; its items are the lines around the board (comments, @format), its one child the board
    bool exported = false; // first line marks a Penpot export (open read-only, mkui/Document.h)
};

// false (with "line N: ..." in error) when nodes don't open/close properly. The node properties themselves are checked by parseDocument
bool parseSource(const std::string& text, Source& out, std::string* error = nullptr);
std::string writeSource(const Source& source);

// Paths are child indices from the board ({} = the board).
// mapDocumentNodes(source, doc)[i] = source node of doc.nodes[i] (for nodes inside a `use` component, the `use` node);
// doc is what parseDocument read from writeSource(source). Empty if the two don't line up (the text changed in between)
using SourcePath = std::vector<u32>;
std::vector<SourcePath> mapDocumentNodes(const Source& source, const Document& doc);
SourceNode* findNode(Source& source, const SourcePath& path); // nullptr if the path is gone
const SourceNode* findNode(const Source& source, const SourcePath& path);

// Properties. key is the leading words that identify the line: "pos", "fill", "on hover fill", "set label color", "span 1".
// getProperty gives the rest of the first matching line (false if none).
bool getProperty(const SourceNode& node, const std::string& key, std::string& value);
// Replaces the first matching line (and drops later duplicates) or adds the line after the node's last property line
void setProperty(SourceNode& node, const std::string& key, const std::string& value);
bool removeProperty(SourceNode& node, const std::string& key); // removes every matching line

// One node with its subtree as .ui text (clipboard), and back. false if the text isn't exactly one frame/text/image/use node
std::string writeNodeText(const SourceNode& node);
bool parseNodeText(const std::string& text, SourceNode& out);
// base if no node in the file has that name, else base2, base3 ...
std::string uniqueName(const Source& source, const std::string& base);

// Tree edits. at = position among the parent's children (clamped; the end if larger). Comment lines stay where they were
SourcePath addNode(Source& source, const SourcePath& parent, u32 at, SourceNode node);
bool removeNode(Source& source, const SourcePath& path);
// Puts frame where the node is and the node inside it. Returns the frame's path ({} and false for the board)
bool wrapNode(Source& source, const SourcePath& path, SourceNode frame, SourcePath* wrapped = nullptr);
// Moves a node under newParent at position at (after the move). Refused into its own subtree or for the board. Returns the new path
bool moveNode(Source& source, const SourcePath& path, const SourcePath& newParent, u32 at, SourcePath* moved = nullptr);

// Turns the frame at path into a component: `component` becomes a file "board <name> <w> <h>" holding the frame's lines and
// children, and the frame becomes "use <frame name> <componentPath>" that keeps only the lines saying where it sits
// (isPlacementLine), which override the board's on the use. w, h: the frame's drawn size (the board size). Comments go with
// the look. false (nothing changed) for the board, a non-frame or a bad path
bool isPlacementLine(const std::string& line); // pos size min max margin absolute constraints rotation alignSelf justifySelf cell
bool extractComponent(Source& source, const SourcePath& path, const std::string& name, const std::string& componentPath, i32 w, i32 h,
                      Source& component);

} // namespace mkui
