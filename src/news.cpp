// The game's "newspaper" is the timeline at the bottom of the screen: a list of nodules
// (TM::CNodule). World news is a CNoduleEvent, created from data/rule/timeline.csv by
//     CNoduleEvent* __cdecl AddTimelineEvent(String name /* by value */, int year, int month)  @ 0x00795AD0
// which also links it into the global list (0x0104E7BC); the timeline UI adds an icon for
// any nodule without one. Its text comes from <translate> keys built from the name:
// NAME_PAST_TITLE / _SUBTITLE / _DESCRIPTION (and _FUTURE_ before the event's date), which
// text.cpp supplies. The date is a float in days at event+0x48. Events fade out after the
// "hindsight" tuning value and are cleared when a game loads, so the mod saves its recent
// stories and puts them back after a load.

#include "news.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>

#include "game.h"
#include "mod.h"
#include "overlay.h"
#include "persist.h"
#include "text.h"

namespace news {
namespace {

constexpr uintptr_t kAddTimelineEvent = 0x00795AD0;
constexpr uintptr_t kGameLoaded       = 0x0104BCE4;   // non-null while a game is running
constexpr int       kEventDate        = 0x48;
constexpr float     kKeepDays         = 365;          // stories re-posted after a load
constexpr size_t    kMaxKept          = 24;

struct GameStringByValue {   // the game's narrow String, passed by value (0x20 bytes)
    const char* text;
    int         len;
    int         cap;        // 0x14 = inline: the callee won't free our buffer
    char        inline_[20];
};
using AddTimelineEvent_t = uint8_t*(__cdecl*)(GameStringByValue name, int year, int month);

struct Story {
    int         id = 0;
    float       day = 0;
    std::string headline, subtitle, body;
};

std::deque<Story> g_queue;    // waiting for the next frame
std::deque<Story> g_posted;   // on the timeline now (saved)
int g_nextId = 1;
bool g_enabled = false;

std::wstring Wide(const std::string& s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// Plain text for the game's rich-text renderer: line breaks as <br>, no stray tags.
std::wstring Rich(const std::string& s)
{
    std::string out;
    for (char c : s) {
        if (c == '\n')
            out += "<br>";
        else if (c == '<' || c == '>')
            out += c == '<' ? '(' : ')';
        else
            out += c;
    }
    return Wide(out);
}

std::string Name(int id)
{
    char buf[48];
    sprintf_s(buf, "timeline_moviesmod_news_%d", id);
    return buf;
}

void SetText(const Story& s)
{
    std::string base = Name(s.id);
    for (const char* when : { "_PAST", "_FUTURE" }) {
        text::Set(base + when + "_TITLE", Rich(s.headline));
        text::Set(base + when + "_SUBTITLE", Rich(s.subtitle));
        text::Set(base + when + "_DESCRIPTION", Rich(s.body));
    }
}

uint8_t* CallAddTimelineEvent(GameStringByValue name, int year, int month)
{
    __try {
        return reinterpret_cast<AddTimelineEvent_t>(kAddTimelineEvent)(name, year, month);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool AddToTimeline(const Story& s)
{
    static char name[48];
    sprintf_s(name, "timeline_moviesmod_news_%d", s.id);
    GameStringByValue str{};
    str.text = name;
    str.len  = static_cast<int>(strlen(name));
    str.cap  = 0x14;
    int year  = static_cast<int>(s.day / 365.0f);
    int month = static_cast<int>((s.day / 365.0f - year) * 12) + 1;
    uint8_t* ev = CallAddTimelineEvent(str, year, month < 1 ? 1 : month > 12 ? 12 : month);
    if (!ev)
        return false;
    *reinterpret_cast<float*>(ev + kEventDate) = s.day;
    return true;
}

void Frame()
{
    if (g_queue.empty() || !*reinterpret_cast<void**>(kGameLoaded) || !game::Ready())
        return;
    Story s = g_queue.front();
    g_queue.pop_front();
    SetText(s);
    if (!AddToTimeline(s)) {
        mod::Log("news: FAILED to add '%s' to the timeline", s.headline.c_str());
        return;
    }
    mod::Log("news: on the timeline - %s", s.headline.c_str());
    for (const Story& p : g_posted)
        if (p.id == s.id)
            return;   // a re-post after loading
    g_posted.push_back(s);
    while (g_posted.size() > kMaxKept)
        g_posted.pop_front();
}

std::string Escape(const std::string& s)
{
    std::string out;
    for (char c : s)
        out += c == '\n' ? std::string("\\n") : c == '|' ? std::string("/") : std::string(1, c);
    return out;
}

std::string Unescape(const std::string& s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') {
            out += '\n';
            i++;
        } else {
            out += s[i];
        }
    }
    return out;
}

void Save(persist::Section& out)
{
    out["next"] = std::to_string(g_nextId);
    int i = 0;
    for (const Story& s : g_posted)
        out["s" + std::to_string(i++)] = std::to_string(s.id) + "|" + std::to_string(s.day) + "|" + Escape(s.headline) +
                                         "|" + Escape(s.subtitle) + "|" + Escape(s.body);
}

void Reset()
{
    g_queue.clear();
    g_posted.clear();
    g_nextId = 1;
}

void Load(const persist::Section& in)
{
    Reset();
    if (auto it = in.find("next"); it != in.end())
        g_nextId = atoi(it->second.c_str());
    float today = game::DateDays();
    for (int i = 0;; i++) {
        auto it = in.find("s" + std::to_string(i));
        if (it == in.end())
            break;
        Story s;
        std::string v = it->second;
        size_t a = v.find('|'), b = v.find('|', a + 1), c = v.find('|', b + 1), d = v.find('|', c + 1);
        if (d == std::string::npos)
            continue;
        s.id       = atoi(v.substr(0, a).c_str());
        s.day      = static_cast<float>(atof(v.substr(a + 1, b - a - 1).c_str()));
        s.headline = Unescape(v.substr(b + 1, c - b - 1));
        s.subtitle = Unescape(v.substr(c + 1, d - c - 1));
        s.body     = Unescape(v.substr(d + 1));
        g_posted.push_back(s);
        if (today <= 0 || today - s.day < kKeepDays)
            g_queue.push_back(s);   // the game cleared its timeline while loading
    }
}

} // namespace

void Post(const std::string& headline, const std::string& subtitle, const std::string& body)
{
    if (!g_enabled)
        return;
    Story s;
    s.id       = g_nextId++;
    s.day      = game::DateDays();
    s.headline = headline;
    s.subtitle = subtitle;
    s.body     = body;
    g_queue.push_back(s);
}

void Install()
{
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\MoviesMod.ini", mod::GameDir());
    if (!GetPrivateProfileIntW(L"MoviesMod", L"TimelineNews", 1, ini)) {
        mod::Log("news: TimelineNews = 0 - stories stay in the mod's own newspaper");
        return;
    }
    g_enabled = true;
    overlay::AddFrameCallback(&Frame);
    persist::Register("news", &Save, &Load, &Reset);
    mod::Log("news: timeline stories on");
}

} // namespace news
