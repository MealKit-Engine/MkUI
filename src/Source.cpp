#include "mkui/Source.h"

#include "mkui/Document.h"

#include <algorithm>
#include <cstddef>

namespace mkui {
namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        size_t j = i;
        while (j < s.size() && s[j] != ' ' && s[j] != '\t') ++j;
        if (j > i) out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

// The rest after the first words (text as is)
std::string rest(const std::string& line, size_t words) {
    size_t i = 0;
    for (size_t w = 0; w < words; ++w) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t') ++i;
    }
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    return line.substr(i);
}

bool isHead(const std::string& word) {
    return word == "board" || word == "frame" || word == "text" || word == "image" || word == "use";
}

bool isProperty(const SourceItem& item) {
    return item.child < 0 && !item.line.empty() && item.line[0] != '#';
}

// Line starts with the key's words (and nothing else may follow the key without a space)
bool matches(const std::string& line, const std::vector<std::string>& key) {
    if (line.empty() || line[0] == '#' || key.empty()) return false;
    const std::vector<std::string> words = split(line);
    if (words.size() < key.size()) return false;
    for (size_t i = 0; i < key.size(); ++i)
        if (words[i] != key[i]) return false;
    return true;
}

std::string joinLine(const std::string& key, const std::string& value) {
    return value.empty() ? key : key + " " + value;
}

void writeNode(const SourceNode& n, u32 depth, std::string& out) {
    const std::string indent(depth * 2, ' ');
    out += indent + n.kind + " " + n.name;
    if (!n.args.empty()) out += " " + n.args;
    out += "\n";
    const std::string inner((depth + 1) * 2, ' ');
    for (const SourceItem& item : n.items) {
        if (item.child >= 0) writeNode(n.children[static_cast<size_t>(item.child)], depth + 1, out);
        else if (item.line.empty()) out += "\n";
        else out += inner + item.line + "\n";
    }
    out += indent + "end\n";
}

// Inserts child at position at (clamped) among the parent's children, with its item before the child that was there
u32 insertChild(SourceNode& parent, u32 at, SourceNode child) {
    const u32 count = static_cast<u32>(parent.children.size());
    if (at > count) at = count;
    size_t itemPos = parent.items.size();
    for (size_t i = 0; i < parent.items.size(); ++i) {
        const i32 c = parent.items[i].child;
        if (at < count && c == static_cast<i32>(at)) {
            itemPos = i;
            break;
        }
        if (at == count && count > 0 && c == static_cast<i32>(count - 1)) itemPos = i + 1;
    }
    for (SourceItem& item : parent.items)
        if (item.child >= static_cast<i32>(at)) ++item.child;
    parent.children.insert(parent.children.begin() + at, std::move(child));
    parent.items.insert(parent.items.begin() + static_cast<std::ptrdiff_t>(itemPos), SourceItem{{}, static_cast<i32>(at)});
    return at;
}

SourceNode eraseChild(SourceNode& parent, u32 index) {
    SourceNode taken = std::move(parent.children[index]);
    parent.children.erase(parent.children.begin() + index);
    for (size_t i = 0; i < parent.items.size();) {
        SourceItem& item = parent.items[i];
        if (item.child == static_cast<i32>(index)) {
            parent.items.erase(parent.items.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        if (item.child > static_cast<i32>(index)) --item.child;
        ++i;
    }
    return taken;
}

bool mapNode(const SourceNode& n, SourcePath& path, const Document& doc, size_t& i, std::vector<SourcePath>& out) {
    if (i >= doc.nodes.size() || doc.nodes[i].name != n.name) return false;
    if (n.kind == "use") {
        const size_t end = doc.nodes[i].end;
        for (; i < end; ++i) out[i] = path;
        return true;
    }
    out[i++] = path;
    for (u32 c = 0; c < n.children.size(); ++c) {
        path.push_back(c);
        if (!mapNode(n.children[c], path, doc, i, out)) return false;
        path.pop_back();
    }
    return true;
}

} // namespace

bool parseSource(const std::string& text, Source& out, std::string* error) {
    out = {};
    std::vector<SourceNode*> stack{&out.root};
    u32 lineNo = 0;
    size_t pos = 0;
    auto fail = [&](const std::string& message) {
        if (error) *error = "line " + std::to_string(lineNo) + ": " + message;
        return false;
    };
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = trim(text.substr(pos, end - pos));
        pos = end + 1;
        ++lineNo;
        if (lineNo == 1 && line.rfind("# MealKit UI exported from Penpot", 0) == 0) out.exported = true;
        SourceNode& top = *stack.back();
        const std::vector<std::string> words = line.empty() || line[0] == '#' ? std::vector<std::string>{} : split(line);
        if (!words.empty() && isHead(words[0])) {
            if (words.size() < 2) return fail("expected \"" + words[0] + " <name>\"");
            if ((words[0] == "board") != (stack.size() == 1)) return fail(words[0] == "board" ? "board inside a node" : "node outside the board");
            if (words[0] == "board" && !out.root.children.empty()) return fail("only one board per file");
            SourceNode n;
            n.kind = words[0];
            n.name = words[1];
            n.args = rest(line, 2);
            top.items.push_back({{}, static_cast<i32>(top.children.size())});
            top.children.push_back(std::move(n));
            stack.push_back(&top.children.back()); // children of top aren't added again until this node closes
        } else if (!words.empty() && words[0] == "end") {
            if (stack.size() == 1) return fail("end without a node");
            stack.pop_back();
        } else {
            top.items.push_back({line, -1});
        }
    }
    if (stack.size() != 1) return fail("missing end (" + std::to_string(stack.size() - 1) + " node(s) open)");
    if (out.root.children.empty()) return fail("no board");
    return true;
}

std::string writeSource(const Source& source) {
    std::string out;
    for (const SourceItem& item : source.root.items) {
        if (item.child >= 0) writeNode(source.root.children[static_cast<size_t>(item.child)], 0, out);
        else out += item.line + "\n";
    }
    return out;
}

std::vector<SourcePath> mapDocumentNodes(const Source& source, const Document& doc) {
    if (source.root.children.empty()) return {};
    std::vector<SourcePath> out(doc.nodes.size());
    SourcePath path;
    size_t i = 0;
    if (!mapNode(source.root.children[0], path, doc, i, out) || i != doc.nodes.size()) return {};
    return out;
}

SourceNode* findNode(Source& source, const SourcePath& path) {
    return const_cast<SourceNode*>(findNode(static_cast<const Source&>(source), path));
}

const SourceNode* findNode(const Source& source, const SourcePath& path) {
    if (source.root.children.empty()) return nullptr;
    const SourceNode* n = &source.root.children[0];
    for (u32 c : path) {
        if (c >= n->children.size()) return nullptr;
        n = &n->children[c];
    }
    return n;
}

bool getProperty(const SourceNode& node, const std::string& key, std::string& value) {
    const std::vector<std::string> k = split(key);
    for (const SourceItem& item : node.items)
        if (item.child < 0 && matches(item.line, k)) {
            value = rest(item.line, k.size());
            return true;
        }
    return false;
}

void setProperty(SourceNode& node, const std::string& key, const std::string& value) {
    const std::vector<std::string> k = split(key);
    const std::string line = joinLine(key, value);
    bool done = false;
    for (size_t i = 0; i < node.items.size();) {
        SourceItem& item = node.items[i];
        if (item.child < 0 && matches(item.line, k)) {
            if (done) {
                node.items.erase(node.items.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            item.line = line;
            done = true;
        }
        ++i;
    }
    if (done) return;
    // After the last property line; without one, before the first child
    size_t at = node.items.size();
    bool hasProperty = false;
    for (size_t i = 0; i < node.items.size(); ++i)
        if (isProperty(node.items[i])) {
            at = i + 1;
            hasProperty = true;
        }
    if (!hasProperty)
        for (size_t i = 0; i < node.items.size(); ++i)
            if (node.items[i].child >= 0) {
                at = i;
                break;
            }
    node.items.insert(node.items.begin() + static_cast<std::ptrdiff_t>(at), SourceItem{line, -1});
}

bool removeProperty(SourceNode& node, const std::string& key) {
    const std::vector<std::string> k = split(key);
    bool removed = false;
    for (size_t i = 0; i < node.items.size();) {
        if (node.items[i].child < 0 && matches(node.items[i].line, k)) {
            node.items.erase(node.items.begin() + static_cast<std::ptrdiff_t>(i));
            removed = true;
            continue;
        }
        ++i;
    }
    return removed;
}

std::string writeNodeText(const SourceNode& node) {
    std::string out;
    writeNode(node, 0, out);
    return out;
}

bool parseNodeText(const std::string& text, SourceNode& out) {
    Source s;
    if (!parseSource("board clip 1 1\n" + text + "\nend\n", s)) return false;
    const SourceNode& board = s.root.children[0];
    if (board.children.size() != 1) return false;
    for (const SourceItem& item : board.items)
        if (item.child < 0 && !item.line.empty()) return false; // anything beside the node
    out = board.children[0];
    return true;
}

namespace {
bool hasName(const SourceNode& n, const std::string& name) {
    if (n.name == name) return true;
    for (const SourceNode& c : n.children)
        if (hasName(c, name)) return true;
    return false;
}
} // namespace

std::string uniqueName(const Source& source, const std::string& base) {
    if (!hasName(source.root, base)) return base;
    for (u32 i = 2;; ++i) {
        const std::string name = base + std::to_string(i);
        if (!hasName(source.root, name)) return name;
    }
}

bool wrapNode(Source& source, const SourcePath& path, SourceNode frame, SourcePath* wrapped) {
    if (path.empty() || !findNode(source, path)) return false;
    const SourcePath parent(path.begin(), path.end() - 1);
    const SourcePath frameAt = addNode(source, parent, path.back(), std::move(frame));
    SourcePath node = path;
    ++node.back(); // the frame went in front of it
    if (!moveNode(source, node, frameAt, 0)) return false;
    if (wrapped) *wrapped = frameAt;
    return true;
}

SourcePath addNode(Source& source, const SourcePath& parent, u32 at, SourceNode node) {
    SourceNode* p = findNode(source, parent);
    if (!p) return {};
    SourcePath path = parent;
    path.push_back(insertChild(*p, at, std::move(node)));
    return path;
}

bool removeNode(Source& source, const SourcePath& path) {
    if (path.empty()) return false; // the board
    SourceNode* p = findNode(source, SourcePath(path.begin(), path.end() - 1));
    if (!p || path.back() >= p->children.size()) return false;
    eraseChild(*p, path.back());
    return true;
}

bool moveNode(Source& source, const SourcePath& path, const SourcePath& newParent, u32 at, SourcePath* moved) {
    if (path.empty() || !findNode(source, path) || !findNode(source, newParent)) return false;
    if (newParent.size() >= path.size() && std::equal(path.begin(), path.end(), newParent.begin())) return false; // into itself
    const SourcePath oldParent(path.begin(), path.end() - 1);
    SourceNode taken = eraseChild(*findNode(source, oldParent), path.back());
    // Removing shifts later siblings (and paths through them) down by one
    SourcePath target = newParent;
    const size_t level = oldParent.size();
    if (target.size() > level && std::equal(oldParent.begin(), oldParent.end(), target.begin()) && target[level] > path.back())
        --target[level];
    SourceNode* p = findNode(source, target);
    target.push_back(insertChild(*p, at, std::move(taken)));
    if (moved) *moved = target;
    return true;
}

bool isPlacementLine(const std::string& line) {
    const size_t space = line.find(' ');
    const std::string k = line.substr(0, space);
    for (const char* p : {"pos", "size", "min", "max", "margin", "absolute", "constraints", "rotation", "alignSelf", "justifySelf", "cell"})
        if (k == p) return true;
    return false;
}

bool extractComponent(Source& source, const SourcePath& path, const std::string& name, const std::string& componentPath, i32 w, i32 h,
                      Source& component) {
    if (path.empty() || name.empty() || componentPath.empty()) return false;
    SourceNode* n = findNode(source, path);
    if (!n || n->kind != "frame") return false;
    Source out;
    if (!parseSource("# MealKit UI (hand-written): component " + name + "\n@format ui 1\nboard " + name + " " + std::to_string(w) + " " +
                         std::to_string(h) + "\nend\n",
                     out))
        return false;
    SourceNode& board = out.root.children[0];
    SourceNode use;
    use.kind = "use";
    use.name = n->name;
    use.args = componentPath;
    for (SourceItem& item : n->items) {
        if (item.child < 0 && isPlacementLine(item.line)) use.items.push_back(std::move(item));
        else board.items.push_back(std::move(item)); // look, behaviour, comments, children (indices unchanged)
    }
    board.children = std::move(n->children);
    *n = std::move(use);
    component = std::move(out);
    return true;
}

} // namespace mkui
