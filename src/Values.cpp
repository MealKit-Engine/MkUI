#include "mkui/Values.h"

#include <cstdlib>

namespace mkui {

namespace {
const char kValueModelType = 0; // Model::type of every ValueModel (its address is the tag)
const std::string kEmpty;
} // namespace

struct ValueModel::Entry {
    ValueKind kind = ValueKind::Other;
    bool b = false;
    i32 i = 0;
    f32 f = 0.0f;
    u32 color = 0;
    std::string s;
    std::vector<std::string> enumNames;
    std::vector<ValueModel> items;      // list
    std::unique_ptr<ValueModel> child;  // nested model
};

ValueModel::ValueModel() = default;
ValueModel::~ValueModel() = default;
ValueModel::ValueModel(ValueModel&&) noexcept = default;
ValueModel& ValueModel::operator=(ValueModel&&) noexcept = default;

ValueModel::Entry& ValueModel::entry(std::string_view name, ValueKind kind) {
    auto it = entries_.find(name);
    if (it == entries_.end()) it = entries_.emplace(std::string(name), std::make_unique<Entry>()).first;
    Entry& e = *it->second;
    if (e.kind != kind) {
        e = Entry{};
        e.kind = kind;
    }
    return e;
}

const ValueModel::Entry* ValueModel::find(std::string_view name) const {
    auto it = entries_.find(name);
    return it == entries_.end() ? nullptr : it->second.get();
}

void ValueModel::set(std::string_view name, bool value) { entry(name, ValueKind::Bool).b = value; }
void ValueModel::set(std::string_view name, i32 value) { entry(name, ValueKind::Int).i = value; }
void ValueModel::set(std::string_view name, f32 value) { entry(name, ValueKind::Float).f = value; }
void ValueModel::set(std::string_view name, const char* value) { entry(name, ValueKind::String).s = value ? value : ""; }
void ValueModel::set(std::string_view name, std::string value) { entry(name, ValueKind::String).s = std::move(value); }
void ValueModel::setColor(std::string_view name, u32 rgbaValue) { entry(name, ValueKind::Color).color = rgbaValue; }
void ValueModel::setEnum(std::string_view name, i32 value, std::vector<std::string> names) {
    Entry& e = entry(name, ValueKind::Int);
    e.i = value;
    e.enumNames = std::move(names);
}

ValueModel& ValueModel::child(std::string_view name) {
    Entry& e = entry(name, ValueKind::Struct);
    if (!e.child) e.child = std::make_unique<ValueModel>();
    return *e.child;
}

std::vector<ValueModel>& ValueModel::list(std::string_view name) { return entry(name, ValueKind::Array).items; }

bool ValueModel::getBool(std::string_view name, bool fallback) const {
    const Entry* e = find(name);
    return e && e->kind == ValueKind::Bool ? e->b : fallback;
}
i32 ValueModel::getInt(std::string_view name, i32 fallback) const {
    const Entry* e = find(name);
    return e && e->kind == ValueKind::Int ? e->i : fallback;
}
f32 ValueModel::getFloat(std::string_view name, f32 fallback) const {
    const Entry* e = find(name);
    return e && e->kind == ValueKind::Float ? e->f : fallback;
}
const std::string& ValueModel::getString(std::string_view name) const {
    const Entry* e = find(name);
    return e && e->kind == ValueKind::String ? e->s : kEmpty;
}
bool ValueModel::has(std::string_view name) const { return find(name) != nullptr; }

Model ValueModel::model() { return Model{&kValueModelType, this}; }

// ---------------------------------------------------------------- Binder

bool ValueBinder::resolve(const Model& model, const std::string& path, Value& out) const {
    if (model.type != &kValueModelType || !model.data) return false;
    ValueModel* m = static_cast<ValueModel*>(model.data);
    std::string_view rest = path;
    while (true) {
        // one segment: name, name[index]
        const size_t end = rest.find_first_of(".[");
        const std::string_view name = rest.substr(0, end);
        auto it = m->entries_.find(name);
        if (it == m->entries_.end()) return false;
        ValueModel::Entry& e = *it->second;
        rest = end == std::string_view::npos ? std::string_view() : rest.substr(end);
        if (!rest.empty() && rest[0] == '[') {
            const size_t close = rest.find(']');
            if (close == std::string_view::npos || e.kind != ValueKind::Array) return false;
            const size_t index = static_cast<size_t>(std::strtoul(std::string(rest.substr(1, close - 1)).c_str(), nullptr, 10));
            if (index >= e.items.size()) return false;
            rest = rest.substr(close + 1);
            if (rest.empty() || rest[0] != '.') return false; // a row itself isn't a value
            m = &e.items[index];
            rest = rest.substr(1);
            continue;
        }
        if (!rest.empty()) { // '.'
            if (e.kind != ValueKind::Struct || !e.child) return false;
            m = e.child.get();
            rest = rest.substr(1);
            continue;
        }
        out = Value{};
        out.kind = e.kind;
        switch (e.kind) {
        case ValueKind::Bool: out.data = &e.b, out.size = 1; break;
        case ValueKind::Int:
            out.data = &e.i, out.size = 4;
            if (!e.enumNames.empty()) out.enumNames = &e.enumNames;
            break;
        case ValueKind::Float: out.data = &e.f, out.size = 4; break;
        case ValueKind::Color: out.data = &e.color, out.size = 4; break;
        case ValueKind::String: out.data = &e.s, out.size = static_cast<u32>(sizeof(std::string)); break;
        case ValueKind::Array:
            out.data = e.items.data();
            out.count = static_cast<u32>(e.items.size());
            out.elementSize = static_cast<u32>(sizeof(ValueModel));
            out.elementKind = ValueKind::Struct;
            out.elementType = &kValueModelType;
            break;
        case ValueKind::Struct: out.data = e.child.get(), out.size = static_cast<u32>(sizeof(ValueModel)); break;
        default: return false;
        }
        return true;
    }
}

const char* ValueBinder::typeName(const Model& model) const { return model.type == &kValueModelType ? "ValueModel" : "?"; }

void ValueBinder::format(const Value&, std::string& out) const { out.clear(); }

const ValueBinder& valueBinder() {
    static const ValueBinder binder;
    return binder;
}

} // namespace mkui
