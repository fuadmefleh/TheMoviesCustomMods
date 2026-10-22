// Shared list files. Several of the game's data files are lists that every mod wants to add
// to - a wardrobe tab's costume names (data/costume/category_*.ini), the wardrobe tabs
// (category.ini) and similar. Shipping our own copy of such a file would wipe out another
// mod's additions (or theirs ours), depending on install order. So the mod ships its
// additions under <game>\MoviesMod\lists\data\... and merges them into <game>\Data\...
// at startup, before the game reads them:
//
//   * no file in Data yet: ours is copied in (it already holds the game's own entries);
//   * a plain list: each of our lines that is missing is appended;
//   * an ini with [sections]: each of our sections that is missing is appended.
//
// Nothing is ever removed, so other mods' entries survive, and a mod installed after ours
// that replaces a list gets our entries back on the next start.

#include <windows.h>
#include <cctype>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "mod.h"

namespace lists {
namespace {

std::string ReadFile(const std::wstring& path, bool* ok)
{
    std::string data;
    FILE* f = _wfopen(path.c_str(), L"rb");
    *ok = f != nullptr;
    if (!f)
        return data;
    char buf[8192];
    for (size_t got; (got = fread(buf, 1, sizeof(buf), f)) > 0;)
        data.append(buf, got);
    fclose(f);
    return data;
}

bool WriteFile(const std::wstring& path, const std::string& data)
{
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f)
        return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    return ok;
}

std::string Key(std::string s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    s = a == std::string::npos ? "" : s.substr(a, b - a + 1);
    for (char& c : s)
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> Lines(const std::string& text)
{
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        std::string line = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        out.push_back(line);
        pos = end == std::string::npos ? text.size() : end + 1;
    }
    return out;
}

bool IsSection(const std::string& line)
{
    std::string k = Key(line);
    return k.size() > 2 && k.front() == '[' && k.back() == ']';
}

// Appends what 'ours' has and 'theirs' lacks; returns the number of entries added.
int Merge(std::string& theirs, const std::string& ours)
{
    std::vector<std::string> mine = Lines(ours);
    bool sections = false;
    for (const std::string& l : mine)
        sections |= IsSection(l);
    std::set<std::string> have;
    for (const std::string& l : Lines(theirs))
        if (!Key(l).empty())
            have.insert(Key(l));

    std::string add;
    int added = 0;
    if (sections) {
        for (size_t i = 0; i < mine.size(); i++) {
            if (!IsSection(mine[i]) || have.count(Key(mine[i])))
                continue;
            std::string block = mine[i] + "\r\n";
            for (size_t j = i + 1; j < mine.size() && !IsSection(mine[j]); j++)
                block += mine[j] + "\r\n";
            add += "\r\n" + block;
            added++;
        }
    } else {
        for (const std::string& l : mine) {
            std::string k = Key(l);
            if (k.empty() || k[0] == ';' || have.count(k))
                continue;
            have.insert(k);
            add += l + "\r\n";
            added++;
        }
    }
    if (added) {
        if (!theirs.empty() && theirs.back() != '\n')
            theirs += "\r\n";
        theirs += add;
    }
    return added;
}

void Walk(const std::wstring& root, const std::wstring& rel, std::vector<std::wstring>& out)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((root + rel + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..")
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            Walk(root, rel + name + L"\\", out);
        else
            out.push_back(rel + name);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

} // namespace

void MergeAll()
{
    std::wstring game = mod::GameDir();
    std::wstring root = game + L"\\MoviesMod\\lists\\";
    std::vector<std::wstring> files;
    Walk(root, L"", files);
    int changed = 0, entries = 0;
    for (const std::wstring& rel : files) {
        // lists\data\costume\x.ini -> Data\costume\x.ini
        std::wstring sub = rel;
        if (_wcsnicmp(sub.c_str(), L"data\\", 5) == 0)
            sub = sub.substr(5);
        std::wstring target = game + L"\\Data\\" + sub;
        bool okOurs, okTheirs;
        std::string ours = ReadFile(root + rel, &okOurs);
        if (!okOurs)
            continue;
        std::string theirs = ReadFile(target, &okTheirs);
        int added = okTheirs ? Merge(theirs, ours) : 1;
        if (!added)
            continue;
        if (!okTheirs) {
            CreateDirectoryW((game + L"\\Data\\" + sub.substr(0, sub.find_last_of(L'\\'))).c_str(), nullptr);
            theirs = ours;
        }
        if (WriteFile(target, theirs)) {
            changed++;
            entries += okTheirs ? added : 0;
            mod::Log("lists: %ls - %s", sub.c_str(), okTheirs ? "merged our entries in" : "created");
        } else {
            mod::Log("lists: FAILED to write Data\\%ls", sub.c_str());
        }
    }
    mod::Log("lists: %d shared list files checked, %d updated (%d entries added)", static_cast<int>(files.size()),
             changed, entries);
}

} // namespace lists
