/*
  ==============================================================================
   S6000 Emulation - SAVE mode (manual p.216-218)
     VIEW MEMORY / VIEW DISK, SAVE MULTI / PROGRAM / SAMPLE, SAVE ALL,
     OPEN / CLOSE / NEW FOLDER.  Files: .AKM .AKP .WAV (S5000/S6000 format)
   (c) dominikHop  -  www.instagram.com/dominkhop
  ==============================================================================
*/
#include "Common.h"

namespace s6k::ui
{
using namespace common;

class SavePage : public Page
{
public:
    explicit SavePage (Ctx& c) : Page (c, PageId::Save, PageId::None, Mode::Save) {}
    juce::String title() const override { return "SAVE"; }
    bool listLayout() const override { return true; }

    struct Item { int type; int index; juce::String name; };   // type 2 multi, 3 program, 4 sample

    std::vector<Item> memoryItems() const
    {
        std::vector<Item> v;
        for (int i = 0; i < ctx.mem.multis.size(); ++i) v.push_back ({ 2, i, ctx.mem.multis.getObjectPointer (i)->name });
        for (int i = 0; i < ctx.mem.programs.size(); ++i) v.push_back ({ 3, i, ctx.mem.programs.getObjectPointer (i)->getName() });
        for (int i = 0; i < ctx.mem.samples.size(); ++i) v.push_back ({ 4, i, ctx.mem.samples.getObjectPointer (i)->name });
        return v;
    }

    juce::Array<juce::File> folders() const
    {
        auto f = dest.findChildFiles (juce::File::findDirectories, false);
        f.sort();
        return f;
    }

    void onShow() override
    {
        if (! dest.isDirectory()) dest = ctx.disk.getHostRootFolder();
        // preselect what you were working on
        auto items = memoryItems();
        for (int i = 0; i < (int) items.size(); ++i)
            if (items[(size_t) i].type == 2 && items[(size_t) i].index == ctx.mem.currentMulti.load()) { memSel = i; break; }
        memSel = juce::jlimit (0, juce::jmax (0, (int) items.size() - 1), memSel);
    }

    void build (Keys& k) override
    {
        k[0] = header (dest.getFileName().isEmpty() ? dest.getFullPathName() : dest.getFileName());
        const auto freeBytes = dest.getBytesFreeOnVolume();
        k[0].subtitle = "Free " + juce::String ((double) freeBytes / (1024.0 * 1024.0 * 1024.0), 1) + "GB";

        for (int i = 1; i <= 6; ++i)
        {
            const int rowIdx = scroll + geo::rowForKey (i);
            k[(size_t) i] = row ({}, {}, 0, false, [this, rowIdx]
            {
                if (viewDisk) { if (rowIdx < folders().size()) { if (rowIdx == diskSel) openFolder(); else diskSel = rowIdx; } }
                else if (rowIdx < (int) memoryItems().size()) memSel = rowIdx;
            });
        }
        k[7] = button ("SAVE ALL", [this] { saveAll(); });
        k[8] = button (viewDisk ? "VIEW MEMORY" : "VIEW DISK", [this] { viewDisk = ! viewDisk; scroll = 0; });
        if (viewDisk)
        {
            k[9] = button ("GET INFO", [this]
            {
                auto fs = folders();
                const auto f = juce::isPositiveAndBelow (diskSel, fs.size()) ? fs[diskSel] : dest;
                ctx.info ("GET INFO", { f.getFileName(), juce::String (f.getNumberOfChildFiles (juce::File::findFilesAndDirectories)) + " ITEMS",
                                        f.getFullPathName().getLastCharacters (34) });
            });
            k[10] = button ("OPEN FOLDER", [this] { openFolder(); });
            k[11] = button ("CLOSE FOLDER", [this] { if (dest.getParentDirectory() != dest) { dest = dest.getParentDirectory(); diskSel = 0; scroll = 0; } });
            k[12] = button ("NEW FOLDER", [this]
            {
                ctx.pages->pushPopup (std::make_unique<NamingPopup> (ctx, "NEW FOLDER", "NEW FOLDER", 20, [this] (const juce::String& n)
                {
                    auto f = dest.getChildFile (DiskManager::safeFileName (n));
                    if (! f.createDirectory()) ctx.error ("CANNOT CREATE FOLDER");
                }));
            });
        }
        else
        {
            k[9] = button ("CHOOSE FOLDER", [this]
            {
                chooser = std::make_unique<juce::FileChooser> ("Save to folder", dest);
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                      [this] (const juce::FileChooser& fc) { if (fc.getResult().isDirectory()) { dest = fc.getResult(); ctx.pages->refresh(); } });
            });
            k[10] = button ("SHOW IN OS", [this] { dest.revealToUser(); });
        }

        auto items = memoryItems();
        juce::String saveLabel = "SAVE";
        if (juce::isPositiveAndBelow (memSel, (int) items.size()))
            saveLabel = items[(size_t) memSel].type == 2 ? "SAVE MULTI" : items[(size_t) memSel].type == 3 ? "SAVE PROGRAM" : "SAVE SAMPLE";
        k[13] = button (saveLabel, [this] { saveSelected(); }, ! items.empty());
        k[14] = button ("SELECT " + upArrow(), [this] { wheel (-1); });
        k[15] = button ("SELECT " + downArrow(), [this] { wheel (1); });
    }

    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        const int visible = geo::listRows;
        auto head = r.removeFromTop (geo::lineH);
        if (viewDisk)
        {
            oled::text (g, "Contents of " + dest.getFileName(), head, juce::Justification::centredLeft, 10.0f, false, colours::oledDim);
            auto fs = folders();
            if (diskSel < scroll) scroll = diskSel;
            if (diskSel >= scroll + visible) scroll = diskSel - visible + 1;
            for (int i = scroll; i < juce::jmin (fs.size(), scroll + visible); ++i)
            {
                auto line = r.removeFromTop (geo::lineH);
                const bool s = i == diskSel;
                if (s) { g.setColour (colours::oledText); g.fillRect (line); }
                const auto col = s ? colours::oledBg : colours::oledText;
                oled::icon (g, 1, line.removeFromLeft (24.0f), col);
                oled::text (g, fs[i].getFileName(), line, juce::Justification::centredLeft, 11.0f, false, col);
            }
            if (fs.isEmpty()) oled::text (g, "(NO SUB-FOLDERS)", r.removeFromTop (30.0f), juce::Justification::centred, 10.5f);
            return;
        }
        oled::text (g, "Contents of ENTIRE MEMORY", head, juce::Justification::centredLeft, 10.0f, false, colours::oledDim);
        auto items = memoryItems();
        if (memSel < scroll) scroll = memSel;
        if (memSel >= scroll + visible) scroll = memSel - visible + 1;
        scroll = juce::jlimit (0, juce::jmax (0, (int) items.size() - visible), scroll);
        for (int i = scroll; i < juce::jmin ((int) items.size(), scroll + visible); ++i)
        {
            auto line = r.removeFromTop (geo::lineH);
            const bool s = i == memSel;
            if (s) { g.setColour (colours::oledText); g.fillRect (line); }
            const auto col = s ? colours::oledBg : colours::oledText;
            oled::icon (g, items[(size_t) i].type, line.removeFromLeft (24.0f), col);
            oled::text (g, items[(size_t) i].name, line, juce::Justification::centredLeft, 11.0f, false, col);
        }
        if (items.empty()) oled::text (g, "(MEMORY IS EMPTY)", r.removeFromTop (30.0f), juce::Justification::centred, 10.5f);
    }

    void wheel (int delta) override
    {
        if (viewDisk) diskSel = juce::jlimit (0, juce::jmax (0, folders().size() - 1), diskSel + delta);
        else memSel = juce::jlimit (0, juce::jmax (0, (int) memoryItems().size() - 1), memSel + delta);
    }

    void enterPlay (bool down) override { if (down) { if (viewDisk) openFolder(); else saveSelected(); } }

private:
    juce::File dest;
    bool viewDisk = false;
    int memSel = 0, diskSel = 0, scroll = 0;
    std::unique_ptr<juce::FileChooser> chooser;

    void openFolder()
    {
        auto fs = folders();
        if (juce::isPositiveAndBelow (diskSel, fs.size())) { dest = fs[diskSel]; diskSel = 0; scroll = 0; }
    }

    void saveSelected()
    {
        auto items = memoryItems();
        if (! juce::isPositiveAndBelow (memSel, (int) items.size())) return;
        const auto it = items[(size_t) memSel];
        juce::String ext = it.type == 2 ? ".AKM" : it.type == 3 ? ".AKP" : ".WAV";
        const bool exists = dest.getChildFile (DiskManager::safeFileName (it.name) + ext).existsAsFile();

        auto p = std::make_unique<PromptPopup> (ctx, it.type == 2 ? "SAVE MULTI" : it.type == 3 ? "SAVE PROGRAM" : "SAVE SAMPLE",
                                                "Save " + it.name + "\nto " + dest.getFileName() + (exists ? "\n(REPLACES EXISTING FILE)" : ""));
        if (it.type == 2) p->checks = { { "PROGS+SAMPLES", true, 6 } };
        if (it.type == 3) p->checks = { { "WITH SAMPLES", true, 6 } };
        p->onExecute = [this, it] (PromptPopup& pp)
        {
            juce::String err;
            bool ok = false;
            if (it.type == 2) ok = ctx.disk.saveMulti (*ctx.mem.multis.getObjectPointer (it.index), dest, pp.isChecked (0), err);
            else if (it.type == 3) ok = ctx.disk.saveProgram (*ctx.mem.programs.getObjectPointer (it.index), dest, pp.isChecked (0), err);
            else ok = ctx.disk.saveSample (*ctx.mem.samples.getObjectPointer (it.index), dest, err);
            if (ok) ctx.info ("SAVE", { it.name, "saved to " + dest.getFileName() });
            else ctx.error (err);
        };
        ctx.pages->pushPopup (std::move (p));
    }

    void saveAll()
    {
        ctx.confirm ("SAVE ALL", "Save everything in memory\nto " + dest.getFileName() + "?", [this]
        {
            juce::String err;
            if (ctx.disk.saveAll (dest, err)) ctx.info ("SAVE ALL", { juce::String (ctx.mem.multis.size()) + " multis, " + juce::String (ctx.mem.programs.size()) + " programs,",
                                                                      juce::String (ctx.mem.samples.size()) + " samples saved" });
            else ctx.error (err);
        });
    }
};

void registerSavePage (PageManager& pm, Ctx& c)
{
    pm.add (std::make_unique<SavePage> (c));
}

} // namespace s6k::ui
