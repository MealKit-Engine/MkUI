#pragma once

#include "mkui/Host.h"
#include "mkui/Types.h"

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// A view model without reflection: named values set at run time, nested models and lists of models.
//   mkui::ValueModel hud;
//   hud.set("score", 120);
//   hud.set("name", "Player 1");
//   hud.child("stats").set("hp", 0.8f);                 // "stats.hp" in the .ui
//   auto& rows = hud.list("players"); rows.resize(3);    // "list players"; each row is a ValueModel
//   rows[0].set("name", "A");
//   ui.bind(screen, hud.model());                        // the System is made with mkui::valueBinder()
// Input widgets write back into the values they are bound to (bool, int, float, string).
// The runtime keeps pointers into the values while the screen is open: setting an existing name only changes the value,
// but adding names or resizing a list after bind() needs bind() again.
namespace mkui {

class ValueModel {
public:
    ValueModel();
    ~ValueModel();
    ValueModel(ValueModel&&) noexcept;
    ValueModel& operator=(ValueModel&&) noexcept;
    ValueModel(const ValueModel&) = delete;
    ValueModel& operator=(const ValueModel&) = delete;

    void set(std::string_view name, bool value);
    void set(std::string_view name, i32 value);
    void set(std::string_view name, f32 value);
    void set(std::string_view name, const char* value);
    void set(std::string_view name, std::string value);
    void setColor(std::string_view name, u32 rgba); // rgba() value
    // Integer shown by name: names[value] ("enum" in a select widget)
    void setEnum(std::string_view name, i32 value, std::vector<std::string> names);

    ValueModel& child(std::string_view name);               // created on first use
    std::vector<ValueModel>& list(std::string_view name);   // created empty on first use

    bool getBool(std::string_view name, bool fallback = false) const;
    i32 getInt(std::string_view name, i32 fallback = 0) const;
    f32 getFloat(std::string_view name, f32 fallback = 0.0f) const;
    const std::string& getString(std::string_view name) const; // empty if missing
    bool has(std::string_view name) const;

    Model model(); // to System::bind

    struct Entry;

private:
    friend class ValueBinder;
    Entry& entry(std::string_view name, ValueKind kind);
    const Entry* find(std::string_view name) const;
    std::map<std::string, std::unique_ptr<Entry>, std::less<>> entries_; // Entry in Values.cpp
};

// The Binder for ValueModel (only binds models made by model())
class ValueBinder : public Binder {
public:
    bool resolve(const Model& model, const std::string& path, Value& out) const override;
    const char* typeName(const Model& model) const override;
    void format(const Value& value, std::string& out) const override;
};

const ValueBinder& valueBinder(); // one shared instance

} // namespace mkui
