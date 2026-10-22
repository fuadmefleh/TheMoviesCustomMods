// Addresses and offsets were recovered from MoviesSE.exe v1.2 with Ghidra; see
// re\all_fields.txt and re\cproject_map.txt. Save-function offsets are relative to
// each class's serializable base at +0x64, so object offsets below are save offset + 0x64.

#include "game.h"
#include "mod.h"

#include <windows.h>
#include <algorithm>
#include <cstring>

namespace game {
namespace {

// Globals
constexpr uintptr_t kPlayerStudioPtr = 0x0104BCE4;   // CStudioPlayer*
constexpr uintptr_t kFinancePtr      = 0x00F87ED8;   // CFinance*
constexpr uintptr_t kGameClockDays   = 0x00E4FA4C;   // float, days
constexpr uintptr_t kAllProjectsList = 0x0104D680;   // InList<CProject>
constexpr uintptr_t kAllFacilitiesList = 0x0104ED10; // InList<CFacility>, built by Facility.cpp's static init

// Functions
constexpr uintptr_t kFinanceTransact = 0x00442B60;   // void CFinance::Transact(int64 amount, int category)
constexpr uintptr_t kProjectQuality  = 0x005B2350;   // void CProject::GetQuality(float* out)
constexpr uintptr_t kSpawnScript     = 0x005C43C0;   // CProject* __cdecl(const String* stockScript, const float xy[2])
constexpr uintptr_t kWStringAssign   = 0x004036D0;   // void __thiscall WString::Assign(const wchar_t*, int len)

// TMFixedAsset (buildings)
constexpr size_t kAssetAngle    = 0x0C4;   // float, radians
constexpr size_t kAssetPosition = 0x100;   // float[3]

// CStudio / CStudioPlayer
constexpr size_t kStudioBackCatalogue = 0x0C0;   // InList<CProject>
constexpr size_t kStudioPrestige      = 0x12C;
constexpr size_t kStudioStarRating    = 0x130;
constexpr size_t kStudioName          = 0x18C;   // String
constexpr size_t kStudioMoviesCount   = 0x1E4;
constexpr size_t kPlayerProjectList   = 0x1F0;   // InList<CProject>

// CFinance
constexpr size_t kFinanceCapital = 0x90;
constexpr size_t kFinanceDisplay = 0x98;

// CProject
constexpr size_t kProjStarRating  = 0x170;
constexpr size_t kProjPhase       = 0x228;   // CProjectPhase*
constexpr size_t kProjTitle       = 0x25C;   // String
constexpr size_t kProjReleaseDate = 0x288;
constexpr size_t kProjStartDate   = 0x2C8;
constexpr size_t kProjQuality     = 0x360;
constexpr size_t kProjIncome      = 0x3A0;   // CProjectIncome*

// CProjectIncome
constexpr size_t kIncomePool        = 0x90;  // int64 fixed point
constexpr size_t kIncomeInitialPool = 0x98;

// InList<T>: head pointer at +8, sentinel node at +0x14; node = { prev, next, T* }.
constexpr size_t kListHead     = 0x08;
constexpr size_t kListSentinel = 0x14;

bool SafeCopy(void* dst, const void* src, size_t n)
{
    __try {
        memcpy(dst, src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <typename T>
bool Read(uintptr_t addr, T& out)
{
    return addr >= 0x10000 && SafeCopy(&out, reinterpret_cast<const void*>(addr), sizeof(T));
}

template <typename T>
T ReadOr(uintptr_t addr, T fallback)
{
    T v;
    return Read(addr, v) ? v : fallback;
}

uintptr_t Ptr(uintptr_t addr) { return ReadOr<uintptr_t>(addr, 0); }

// The game's strings are { T* text; int len; int cap; T inline[...] }. Titles may be
// narrow or wide, so detect which from the bytes.
std::wstring ReadGameString(uintptr_t addr)
{
    uintptr_t text = Ptr(addr);
    int len = ReadOr<int>(addr + 4, -1);
    if (!text || len <= 0 || len > 256)
        return L"";
    char raw[512] = {};
    if (!SafeCopy(raw, reinterpret_cast<const void*>(text), static_cast<size_t>(len) * 2))
        SafeCopy(raw, reinterpret_cast<const void*>(text), static_cast<size_t>(len));
    bool wide = len > 1 && raw[1] == 0;
    if (wide)
        return std::wstring(reinterpret_cast<const wchar_t*>(raw), len);
    std::wstring out;
    for (int i = 0; i < len && raw[i]; i++)
        out += static_cast<wchar_t>(static_cast<unsigned char>(raw[i]));
    return out;
}

bool CallTransact(uintptr_t finance, int64_t amount, int category)
{
    using Fn = void(__thiscall*)(uintptr_t, uint32_t, int32_t, int);
    __try {
        reinterpret_cast<Fn>(kFinanceTransact)(finance, static_cast<uint32_t>(amount),
                                               static_cast<int32_t>(amount >> 32), category);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

float CallProjectQuality(uintptr_t project)
{
    using Fn = void(__thiscall*)(uintptr_t, float*);
    float q = -1;
    __try {
        reinterpret_cast<Fn>(kProjectQuality)(project, &q);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        q = -1;
    }
    return q;
}

// An empty stock-script name makes the game write a short script of its own.
struct GameString {
    const char* text;
    int         len;
    int         cap;
    char        inline_[20];
};

uintptr_t CallSpawnScript(float x, float y)
{
    using Fn = uintptr_t(__cdecl*)(const GameString*, const float*);
    GameString none{};
    none.text = none.inline_;
    none.cap = 0x14;
    float xy[2] = { x, y };
    __try {
        return reinterpret_cast<Fn>(kSpawnScript)(&none, xy);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

bool CallWStringAssign(uintptr_t wstring, const wchar_t* text, int len)
{
    using Fn = void(__thiscall*)(uintptr_t, const wchar_t*, int);
    __try {
        reinterpret_cast<Fn>(kWStringAssign)(wstring, text, len);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

int CallPhaseStage(uintptr_t phase)
{
    // CProjectPhase vtable +0x24 returns the stage number.
    using Fn = int(__thiscall*)(uintptr_t);
    uintptr_t vtbl = Ptr(phase);
    uintptr_t fn = vtbl ? Ptr(vtbl + 0x24) : 0;
    if (!fn)
        return -1;
    __try {
        return reinterpret_cast<Fn>(fn)(phase);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

Project ReadProject(uintptr_t p)
{
    Project pr;
    pr.address     = p;
    pr.title       = ReadGameString(p + kProjTitle);
    pr.rawQuality  = ReadOr<float>(p + kProjQuality, -1);
    pr.starRating  = ReadOr<float>(p + kProjStarRating, -1);
    pr.releaseDate = ReadOr<float>(p + kProjReleaseDate, 0);
    pr.startDate   = ReadOr<float>(p + kProjStartDate, 0);
    pr.quality     = CallProjectQuality(p);
    if (uintptr_t phase = Ptr(p + kProjPhase))
        pr.stage = CallPhaseStage(phase);
    if (uintptr_t income = Ptr(p + kProjIncome)) {
        int64_t pool, initial;
        if (Read(income + kIncomePool, pool) && Read(income + kIncomeInitialPool, initial)) {
            pr.hasIncome       = true;
            pr.incomeRemaining = pool / kMoneyScale;
            pr.incomeInitial   = initial / kMoneyScale;
        }
    }
    return pr;
}

// A rival studio's film (CProjectAI). Rival films are released the moment they're made;
// their star rating lives in their Top Movies league entry.
constexpr int kAiTitle = 0x8C, kAiStudio = 0xF0, kAiQuality = 0x110, kAiLeagueEntry = 0x130,
              kAiReleaseDate = 0x154, kAiIncome = 0x184, kLeagueMetric = 0x7C;

Project ReadRivalFilm(uintptr_t f)
{
    Project pr;
    pr.address     = f;
    pr.rival       = true;
    pr.title       = ReadGameString(f + kAiTitle);
    pr.quality     = pr.rawQuality = ReadOr<float>(f + kAiQuality, -1);
    pr.releaseDate = pr.startDate = ReadOr<float>(f + kAiReleaseDate, 0);
    pr.stage       = 7;
    if (uintptr_t entry = Ptr(f + kAiLeagueEntry))
        pr.starRating = ReadOr<float>(entry + kLeagueMetric, -1);
    if (uintptr_t studio = Ptr(f + kAiStudio))
        pr.studio = ReadGameString(studio + kStudioName);
    if (uintptr_t income = Ptr(f + kAiIncome)) {
        int64_t pool, initial;
        if (Read(income + kIncomePool, pool) && Read(income + kIncomeInitialPool, initial)) {
            pr.hasIncome       = true;
            pr.incomeRemaining = pool / kMoneyScale;
            pr.incomeInitial   = initial / kMoneyScale;
        }
    }
    return pr;
}

std::vector<uintptr_t> ReadListItems(uintptr_t list)
{
    std::vector<uintptr_t> out;
    uintptr_t sentinel = list + kListSentinel;
    uintptr_t node = Ptr(list + kListHead);
    for (int guard = 0; node && node != sentinel && guard < 4000; guard++) {
        if (uintptr_t item = Ptr(node + 8))
            out.push_back(item);
        node = Ptr(node + 4);
    }
    return out;
}

std::vector<Project> ReadProjectList(uintptr_t list)
{
    std::vector<Project> out;
    for (uintptr_t item : ReadListItems(list))
        out.push_back(ReadProject(item));
    return out;
}

// A narrow game String holding a plain identifier (letters, digits, '_', '/' and '.').
std::string ReadIdentifier(uintptr_t addr)
{
    uintptr_t text = Ptr(addr);
    int len = ReadOr<int>(addr + 4, -1);
    int cap = ReadOr<int>(addr + 8, -1);
    if (!text || len < 3 || len > 64 || cap < len)
        return "";
    char buf[65] = {};
    if (!SafeCopy(buf, reinterpret_cast<const void*>(text), static_cast<size_t>(len)))
        return "";
    for (int i = 0; i < len; i++) {
        char c = buf[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '/' || c == '.'))
            return "";
    }
    return std::string(buf, len);
}

// Where a facility's type name lives, found at runtime. A facility is created from its
// ini path ("facility/<name>"), so look for that String in the facility itself, or in an
// object it points to (e.g. its blueprint). pointerOffset -1 means "in the facility".
struct FacilityLayout {
    bool found = false;
    int  attempts = 0;
    int  pointerOffset = -1;
    int  nameOffset = -1;
} g_facLayout;

std::string NormaliseFacilityName(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    if (s.rfind("data/", 0) == 0)
        s = s.substr(5);
    if (s.rfind("facility/", 0) == 0)
        s = s.substr(9);
    if (s.size() > 4 && s.compare(s.size() - 4, 4, ".ini") == 0)
        s.resize(s.size() - 4);
    return s;
}

bool LooksLikeFacilityName(const std::string& s)
{
    std::string n = s;
    for (char& c : n)
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return n.find("facility/") != std::string::npos;
}

int FindNameIn(uintptr_t object, int size)
{
    for (int off = 4; off < size; off += 4)
        if (LooksLikeFacilityName(ReadIdentifier(object + off)))
            return off;
    return -1;
}

void DiscoverFacilityLayout(uintptr_t facility)
{
    bool verbose = g_facLayout.attempts++ == 0;
    if (int off = FindNameIn(facility, 0x800); off >= 0) {
        g_facLayout = { true, g_facLayout.attempts, -1, off };
    } else {
        for (int p = 4; p < 0x800 && !g_facLayout.found; p += 4) {
            uintptr_t target = Ptr(facility + p);
            if (target < 0x10000 || ClassName(target).empty())
                continue;
            if (int off = FindNameIn(target, 0x400); off >= 0)
                g_facLayout = { true, g_facLayout.attempts, p, off };
        }
    }
    if (g_facLayout.found) {
        mod::Log("facilities: type name at %s+0x%X (found on %s @%08X)",
                 g_facLayout.pointerOffset < 0 ? "facility" : ("[facility+0x" + std::to_string(g_facLayout.pointerOffset) + "]").c_str(),
                 g_facLayout.nameOffset, ClassName(facility).c_str(), facility);
        return;
    }
    if (!verbose)
        return;
    // Not found: log what the facility holds, to work the layout out from a tester's log.
    mod::Log("facilities: type name not found in %s @%08X; its strings and objects:", ClassName(facility).c_str(), facility);
    for (int off = 4; off < 0x800; off += 4) {
        std::string s = ReadIdentifier(facility + off);
        if (!s.empty())
            mod::Log("facilities:   +0x%X string '%s'", off, s.c_str());
        uintptr_t target = Ptr(facility + off);
        std::string cls = target >= 0x10000 ? ClassName(target) : "";
        if (!cls.empty())
            mod::Log("facilities:   +0x%X -> %s @%08X", off, cls.c_str(), target);
    }
}

std::string FacilityType(uintptr_t facility)
{
    if (!g_facLayout.found && g_facLayout.attempts < 20)
        DiscoverFacilityLayout(facility);
    if (!g_facLayout.found)
        return "";
    uintptr_t base = g_facLayout.pointerOffset < 0 ? facility : Ptr(facility + g_facLayout.pointerOffset);
    return base ? NormaliseFacilityName(ReadIdentifier(base + g_facLayout.nameOffset)) : "";
}

uintptr_t PlayerStudio() { return Ptr(kPlayerStudioPtr); }

} // namespace

Project ReadProjectAt(uintptr_t project) { return ReadProject(project); }

bool Ready() { return PlayerStudio() != 0 && Ptr(kFinancePtr) != 0; }

float DateDays() { return ReadOr<float>(kGameClockDays, -1.0f); }

Money ReadMoney()
{
    Money m;
    uintptr_t fin = Ptr(kFinancePtr);
    int64_t capital, display;
    if (fin && Read(fin + kFinanceCapital, capital) && Read(fin + kFinanceDisplay, display)) {
        m.ok      = true;
        m.capital = capital / kMoneyScale;
        m.display = display / kMoneyScale;
    }
    return m;
}

bool AddMoney(double dollars, int category)
{
    uintptr_t fin = Ptr(kFinancePtr);
    return fin && CallTransact(fin, static_cast<int64_t>(dollars * kMoneyScale), category);
}

Studio ReadPlayerStudio()
{
    Studio s;
    uintptr_t st = PlayerStudio();
    if (!st)
        return s;
    s.ok             = true;
    s.address        = st;
    s.name           = ReadGameString(st + kStudioName);
    s.prestige       = ReadOr<float>(st + kStudioPrestige, -1);
    s.starRating     = ReadOr<float>(st + kStudioStarRating, -1);
    s.moviesReleased = ReadOr<int32_t>(st + kStudioMoviesCount, -1);
    return s;
}

std::vector<Project> PlayerBackCatalogue()
{
    uintptr_t st = PlayerStudio();
    return st ? ReadProjectList(st + kStudioBackCatalogue) : std::vector<Project>{};
}

std::vector<Project> PlayerProductions()
{
    uintptr_t st = PlayerStudio();
    return st ? ReadProjectList(st + kPlayerProjectList) : std::vector<Project>{};
}

std::vector<Project> AllProjects() { return ReadProjectList(kAllProjectsList); }

std::vector<StudioFilms> AllStudios()
{
    constexpr uintptr_t kAllStudiosList    = 0x0104BCE8;   // InList<CStudio>, static init at 0x00D084B0
    constexpr uintptr_t kProjectListVtable = 0x00D214C8;   // InList<CProject>
    constexpr uintptr_t kAiProjectListVtable = 0x00D21AD8;   // InList<CProjectAI> (CStudioAI+0x230)
    std::vector<StudioFilms> out;
    uintptr_t player = PlayerStudio();
    for (uintptr_t st : ReadListItems(kAllStudiosList)) {
        StudioFilms s;
        s.address = st;
        s.name = ReadGameString(st + kStudioName);
        s.className = ClassName(st);
        s.isPlayer = st == player;
        std::vector<uintptr_t> seen;
        for (int off = 4; off < 0x800; off += 4) {
            uintptr_t vtable = Ptr(st + off);
            if (vtable != kProjectListVtable && vtable != kAiProjectListVtable)
                continue;
            s.listOffsets.push_back(off);
            for (uintptr_t item : ReadListItems(st + off)) {
                if (std::find(seen.begin(), seen.end(), item) != seen.end())
                    continue;
                seen.push_back(item);
                s.films.push_back(vtable == kProjectListVtable ? ReadProject(item) : ReadRivalFilm(item));
            }
        }
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<Project> RivalFilms()
{
    constexpr uintptr_t kAllAiProjects = 0x0104D628;   // InList<CProjectAI>, every rival film
    std::vector<Project> out;
    for (uintptr_t item : ReadListItems(kAllAiProjects)) {
        Project p = ReadRivalFilm(item);
        if (!p.title.empty())
            out.push_back(std::move(p));
    }
    return out;
}

std::vector<Facility> Facilities()
{
    std::vector<Facility> out;
    for (uintptr_t item : ReadListItems(kAllFacilitiesList))
        out.push_back({ item, FacilityType(item) });
    return out;
}

void LogBlueprints(const char* filter)
{
    // Blueprints are linked through blueprint+0xD0 from the head pointer at 0x01050878;
    // name String at +0xAC, mesh at +0xE0. +0x88 is 0 at load, later 1 on most buildings
    // (meaning unknown).
    constexpr uintptr_t kBlueprintHead = 0x01050878;
    uintptr_t node = Ptr(kBlueprintHead);
    for (int guard = 0; node && node != kBlueprintHead && guard < 500; guard++) {
        uintptr_t bp = node - 0xD0;
        std::string name = ReadIdentifier(bp + 0xAC);
        if (!filter || name.find(filter) != std::string::npos)
            mod::Log("blueprint '%s' mesh '%s' state %d", name.c_str(), ReadIdentifier(bp + 0xE0).c_str(),
                     ReadOr<int>(bp + 0x88, -1));
        node = Ptr(node);
    }
}

int CountFacilities(const char* type)
{
    int n = 0;
    for (const Facility& f : Facilities())
        n += f.type == type;
    return n;
}

bool FacilityPlacement(uintptr_t facility, float pos[3], float* angle)
{
    return Read(facility + kAssetPosition, pos[0]) && Read(facility + kAssetPosition + 4, pos[1]) &&
           Read(facility + kAssetPosition + 8, pos[2]) && Read(facility + kAssetAngle, *angle);
}

uintptr_t SpawnScript(const std::wstring& title, float x, float y)
{
    uintptr_t project = CallSpawnScript(x, y);
    if (!project || ClassName(project) != "CProject") {
        mod::Log("game: script spawn at (%.1f, %.1f) failed (%08X)", x, y, static_cast<unsigned>(project));
        return 0;
    }
    if (!title.empty())
        CallWStringAssign(project + kProjTitle, title.c_str(), static_cast<int>(title.size()));
    return project;
}

bool SetProjectTitle(uintptr_t project, const std::wstring& title)
{
    if (!project || title.empty() || ClassName(project) != "CProject")
        return false;
    return CallWStringAssign(project + kProjTitle, title.c_str(), static_cast<int>(title.size()));
}

uintptr_t FindObjectOfClass(uintptr_t object, const char* cls, int range, int* offset)
{
    for (int off = 4; off < range; off += 4) {
        uintptr_t candidate = Ptr(object + off);
        if (candidate >= 0x10000 && candidate != object && ClassName(candidate) == cls) {
            if (offset)
                *offset = off;
            return candidate;
        }
    }
    return 0;
}

std::string ClassName(uintptr_t object)
{
    // vtable[-1] -> CompleteObjectLocator { sig, offset, cdOffset, TypeDescriptor* };
    // TypeDescriptor { vtable, spare, ".?AVName@Namespace@@" }.
    uintptr_t vtbl = Ptr(object);
    if (vtbl < 0x00400000 || vtbl > 0x01200000)
        return "";
    uintptr_t col = Ptr(vtbl - 4);
    if (col < 0x00400000 || col > 0x01200000 || ReadOr<uint32_t>(col, 1) != 0)
        return "";
    uintptr_t td = Ptr(col + 12);
    if (td < 0x00400000 || td > 0x01200000)
        return "";
    char name[96] = {};
    if (!SafeCopy(name, reinterpret_cast<const void*>(td + 8), sizeof(name) - 1) || strncmp(name, ".?AV", 4) != 0)
        return "";
    std::string s(name + 4);
    size_t at = s.find('@');
    return at == std::string::npos ? s : s.substr(0, at);
}

} // namespace game
