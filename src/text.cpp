// The game resolves display text through one function (FUN_009b5030):
//     WString* __cdecl LookupText(WString* out, const String* key)
// It asks the string database (singleton at 0x0105CC70) for the key's index and copies
// the text; a missing key is shown as the key itself. A building's display name uses its
// file name in capitals (facility_sell -> FACILITY_SELL), so new buildings need entries.

#include "text.h"

#include <windows.h>
#include <cstdint>
#include <map>
#include <set>
#include <string>

#include "MinHook.h"
#include "mod.h"

namespace text {
namespace {

constexpr uintptr_t kLookupText     = 0x009B5030;
constexpr uintptr_t kStringDatabase = 0x0105CC70;   // CStringDatabase*
constexpr uintptr_t kFindIndex      = 0x00A35EB0;   // int __thiscall(db, const char* key), 0 = missing
constexpr uintptr_t kWStringAssign  = 0x004036D0;   // void __thiscall(WString*, const wchar_t*, int len)
// <translate> tags in rich text (the timeline's news panels) skip LookupText and resolve
// keys here instead. It returns a static record: +0x40 the text (WString), +0x64 found.
constexpr uintptr_t kTranslateKey   = 0x009B5F90;   // Record* __cdecl(const String* key, int variant, const String* exclude)
constexpr int kRecordText = 0x40, kRecordFound = 0x64;

struct GameString  { const char* text; int len; int cap; };
struct GameWString { wchar_t* text; int len; int cap; wchar_t inline_[10]; };

using LookupText_t = GameWString*(__cdecl*)(GameWString*, const GameString*);
LookupText_t g_origLookup = nullptr;
using TranslateKey_t = uint8_t*(__cdecl*)(const GameString*, int, const GameString*);
TranslateKey_t g_origTranslate = nullptr;

std::map<std::string, std::wstring> g_entries;   // upper-case key -> text
std::set<std::string> g_checked;   // keys already checked for existence

std::string Upper(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    return s;
}

std::wstring FromUtf8(const std::string& s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string Trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

void LoadEntries()
{
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\MoviesMod.ini", mod::GameDir());
    // Parsed by hand: the profile API decodes the file as ANSI, and the text is UTF-8.
    FILE* f = _wfopen(path, L"rb");
    if (!f)
        return;
    std::string content;
    char chunk[4096];
    size_t got;
    while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0)
        content.append(chunk, got);
    fclose(f);

    bool inText = false;
    size_t pos = 0;
    while (pos < content.size()) {
        size_t end = content.find('\n', pos);
        std::string line = Trim(content.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
        pos = end == std::string::npos ? content.size() : end + 1;
        if (line.empty() || line[0] == ';' || line[0] == '#')
            continue;
        if (line[0] == '[') {
            inText = Upper(line) == "[TEXT]";
            continue;
        }
        size_t eq = line.find('=');
        if (!inText || eq == std::string::npos)
            continue;
        g_entries[Upper(Trim(line.substr(0, eq)))] = FromUtf8(Trim(line.substr(eq + 1)));
    }
}

bool KeyExists(const char* key)
{
    using FindIndex_t = int(__thiscall*)(uintptr_t, const char*);
    uintptr_t db = *reinterpret_cast<uintptr_t*>(kStringDatabase);
    if (!db)
        return true;
    __try {
        return reinterpret_cast<FindIndex_t>(kFindIndex)(db, key) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return true;
    }
}

GameWString* __cdecl HookLookupText(GameWString* out, const GameString* key)
{
    if (out && key && key->text && key->len > 0 && key->len < 128) {
        std::string k = Upper(std::string(key->text, key->len));
        auto it = g_entries.find(k);
        if (it != g_entries.end()) {
            // Build the result exactly as the game does: empty inline buffer, then assign.
            using Assign_t = void(__thiscall*)(GameWString*, const wchar_t*, int);
            out->text = out->inline_;
            out->inline_[0] = 0;
            out->len = 0;
            out->cap = 10;
            reinterpret_cast<Assign_t>(kWStringAssign)(out, it->second.c_str(), static_cast<int>(it->second.size()));
            return out;
        }
        // Keys the game asks for but has no text for show what new content needs.
        if (g_checked.size() < 20000 && g_checked.insert(k).second && !KeyExists(key->text))
            mod::Log("text: missing key '%s'", k.c_str());
    }
    return g_origLookup(out, key);
}

void AssignRecord(uint8_t* record, const std::wstring& value)
{
    using Assign_t = void(__thiscall*)(GameWString*, const wchar_t*, int);
    __try {
        reinterpret_cast<Assign_t>(kWStringAssign)(reinterpret_cast<GameWString*>(record + kRecordText), value.c_str(),
                                                   static_cast<int>(value.size()));
        *reinterpret_cast<int*>(record + kRecordFound) = 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

bool RecordFound(const uint8_t* record)
{
    __try {
        return *reinterpret_cast<const int*>(record + kRecordFound) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uint8_t* __cdecl HookTranslateKey(const GameString* key, int variant, const GameString* exclude)
{
    uint8_t* record = g_origTranslate(key, variant, exclude);
    if (record && key && key->text && key->len > 0 && key->len < 128) {
        std::string k = Upper(std::string(key->text, key->len));
        auto it = g_entries.find(k);
        // A key given without its variant (SITT_DETAILS_CAREER_X for _10 / _20, the variant
        // picked by gender) takes ours when the game has none.
        if (it == g_entries.end() && !RecordFound(record)) {
            it = g_entries.find(k + "_10");
            if (it == g_entries.end())
                it = g_entries.find(k + "_20");
        }
        if (it != g_entries.end())
            AssignRecord(record, it->second);
        else if (!RecordFound(record) && g_checked.size() < 20000 && g_checked.insert(k).second)
            mod::Log("text: missing key '%s' (rich text)", k.c_str());
    }
    return record;
}

} // namespace

void Set(const std::string& key, const std::wstring& value) { g_entries[Upper(key)] = value; }
void Unset(const std::string& key) { g_entries.erase(Upper(key)); }

void Install()
{
    LoadEntries();
    void* target = reinterpret_cast<void*>(kLookupText);
    if (MH_CreateHook(target, &HookLookupText, reinterpret_cast<void**>(&g_origLookup)) == MH_OK &&
        MH_EnableHook(target) == MH_OK)
        mod::Log("text hook installed (%zu entries)", g_entries.size());
    else
        mod::Log("FAILED to hook text lookup");
    target = reinterpret_cast<void*>(kTranslateKey);
    if (MH_CreateHook(target, &HookTranslateKey, reinterpret_cast<void**>(&g_origTranslate)) != MH_OK ||
        MH_EnableHook(target) != MH_OK)
        mod::Log("FAILED to hook rich-text translation");
}

} // namespace text
