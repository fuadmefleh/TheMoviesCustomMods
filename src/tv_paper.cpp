// The Season Finale newspaper: a front page for every finale of your shows, and the
// Press tab that keeps the old editions.

#define NOMINMAX
#include <windows.h>
#include <algorithm>

#include "tv_internal.h"

namespace tv {
namespace {

const ImVec4 kPaper(0.95f, 0.92f, 0.84f, 1.0f);
const ImVec4 kInk(0.12f, 0.10f, 0.08f, 1.0f);
const ImVec4 kFaded(0.38f, 0.34f, 0.30f, 1.0f);
const ImVec4 kRedInk(0.62f, 0.12f, 0.10f, 1.0f);

const char* Price(float year)
{
    return year < 1950 ? "THREE CENTS" : year < 1970 ? "FIVE CENTS" : year < 1990 ? "25 CENTS" : "ONE DOLLAR";
}

// Text centred in the window, wrapped to lines no wider than 'width'.
void Centred(const std::string& text, float width, const ImVec4& colour)
{
    std::vector<std::string> lines;
    std::string line;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t space = text.find(' ', pos);
        std::string word = text.substr(pos, space == std::string::npos ? std::string::npos : space - pos);
        std::string tryLine = line.empty() ? word : line + " " + word;
        if (!line.empty() && ImGui::CalcTextSize(tryLine.c_str()).x > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = tryLine;
        }
        if (space == std::string::npos)
            break;
        pos = space + 1;
    }
    if (!line.empty())
        lines.push_back(line);
    float full = ImGui::GetContentRegionAvail().x;
    for (const std::string& l : lines) {
        float w = ImGui::CalcTextSize(l.c_str()).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (full - w) * 0.5f));
        ImGui::TextColored(colour, "%s", l.c_str());
    }
}

void Rule(float thickness)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + w, p.y + thickness), ImGui::GetColorU32(kInk));
    ImGui::Dummy(ImVec2(w, thickness + 4.0f));
}

} // namespace

void DrawNewspaper()
{
    if (g_paperOpen < 0 || g_paperOpen >= static_cast<int>(g_papers.size()))
        return;
    const Newspaper& p = g_papers[g_paperOpen];
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(720.0f, io.DisplaySize.x - 40), 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kPaper);
    ImGui::PushStyleColor(ImGuiCol_Text, kInk);
    ImGui::PushStyleColor(ImGuiCol_Border, kInk);
    ImGui::PushStyleColor(ImGuiCol_Separator, kInk);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.81f, 0.70f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.74f, 0.60f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.72f, 0.66f, 0.52f, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(26, 18));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 2.0f);
    bool open = true;
    if (ImGui::Begin("##newspaper", &open, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
                                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
        float width = ImGui::GetContentRegionAvail().x;
        float year = p.day / kDaysPerYear;

        ImGui::SetWindowFontScale(0.85f);
        ImGui::TextColored(kFaded, "%s", DateText(p.day).c_str());
        ImGui::SameLine(width - ImGui::CalcTextSize(Price(year)).x);
        ImGui::TextColored(kFaded, "%s", Price(year));
        ImGui::SetWindowFontScale(2.4f);
        Centred(p.paper, width, kInk);
        ImGui::SetWindowFontScale(1.0f);
        Rule(3.0f);
        Rule(1.0f);

        ImGui::SetWindowFontScale(0.95f);
        Centred(p.kicker, width, kRedInk);
        ImGui::SetWindowFontScale(1.75f);
        Centred(p.headline, width * 0.96f, kInk);
        ImGui::SetWindowFontScale(1.15f);
        Centred(p.subhead, width * 0.9f, kFaded);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Spacing();
        Rule(1.0f);

        if (ImGui::BeginTable("paper_cols", 2, ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("story", ImGuiTableColumnFlags_WidthStretch, 0.64f);
            ImGui::TableSetupColumn("side", ImGuiTableColumnFlags_WidthStretch, 0.36f);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(p.body.c_str());
            if (!p.verdict.empty()) {
                ImGui::Spacing();
                ImGui::TextUnformatted(p.verdict.c_str());
            }
            ImGui::PopTextWrapPos();
            ImGui::TableNextColumn();
            ImGui::TextColored(kRedInk, "THE CRITICS");
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(p.quote.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            if (!p.sidebar.empty()) {
                ImGui::TextColored(kRedInk, "LAST WEEK'S TOP SHOWS");
                ImGui::SetWindowFontScale(0.9f);
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(p.sidebar.c_str());
                ImGui::PopTextWrapPos();
                ImGui::SetWindowFontScale(1.0f);
            }
            ImGui::EndTable();
        }

        Rule(1.0f);
        if (ImGui::Button("Put the paper down"))
            open = false;
        if (g_paperOpen > 0) {
            ImGui::SameLine();
            if (ImGui::Button("Older edition"))
                g_paperOpen--;
        }
        if (g_paperOpen + 1 < static_cast<int>(g_papers.size())) {
            ImGui::SameLine();
            if (ImGui::Button("Newer edition"))
                g_paperOpen++;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(7);
    if (!open)
        g_paperOpen = -1;
}

void DrawPressTab()
{
    ImGui::Checkbox("Bring me the paper after every season finale", &g_paperAuto);
    if (g_papers.empty()) {
        ImGui::TextDisabled("No finales yet. When a season of one of your shows ends, the papers will have their say.");
        return;
    }
    constexpr ImGuiTableFlags flags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("press", 3, flags, ImVec2(0, std::max(360.0f, ImGui::GetContentRegionAvail().y - 10)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Date");
        ImGui::TableSetupColumn("Edition");
        ImGui::TableSetupColumn("Headline", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (int i = static_cast<int>(g_papers.size()) - 1; i >= 0; i--) {
            const Newspaper& p = g_papers[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(DateText(p.day).c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", p.kicker.c_str());
            ImGui::TableNextColumn();
            if (ImGui::Selectable(Format("%s##paper%d", p.headline.c_str(), i).c_str(), g_paperOpen == i))
                g_paperOpen = i;
        }
        ImGui::EndTable();
    }
}

} // namespace tv
