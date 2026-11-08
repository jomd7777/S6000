/*
  ==============================================================================
   S6000 Emulation - LOAD mode  (manual p.29-36)
     LOAD page, LOAD FOLDER / MULTI / PROGRAM / SAMPLE prompts, AUDITION,
     GET INFO, OPEN / CLOSE FOLDER, DISK LIST (select / update / mount), DISK TOOLS
     Reads: host folders, .iso .img .dmg .bin .cdr .hds .toast, FAT, ISO9660,
            Akai S1000/S3000 partitions + floppies, S5000/S6000 AKP/AKM/WAV
   (c) dominikHop  -  www.instagram.com/dominikhop
  ==============================================================================
*/
#include "Common.h"

namespace s6k::ui
{
using namespace common;
using disk::VNode;
using disk::VNodePtr;

namespace
{
    int iconFor (const VNode& n)
    {
        if (n.kind == VNode::Kind::DiskImage) return 6;
        switch (n.type)
        {
            case VNode::Type::Disk:    return 5;
            case VNode::Type::Folder:  return 1;
            case VNode::Type::Multi:   return 2;
            case VNode::Type::Program: return 3;
            case VNode::Type::Sample:  return 4;
            default: return 0;
        }
    }

    juce::String sizeText (uint64_t s)
    {
        if (s >= 1024 * 1024) return juce::String ((double) s / (1024.0 * 1024.0), 1) + "M";
        if (s >= 1024) return juce::String ((int) (s / 1024)) + "K";
        return juce::String ((int) s);
    }

    /** MOUNT IMAGE / LOAD FILE: disk images are mounted, audio and Akai files are loaded straight away. */
    void chooseImage (Ctx& c, std::unique_ptr<juce::FileChooser>& holder)
    {
        holder = std::make_unique<juce::FileChooser> ("Mount a disk image or load audio / Akai files", juce::File(),
                                                      "*.iso;*.img;*.dmg;*.bin;*.cdr;*.hds;*.toast;*.ima;*.raw;*.dsk;"
                                                      "*.wav;*.aif;*.aiff;*.aifc;*.mp3;*.flac;*.ogg;*.akp;*.akm");
        holder->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::canSelectMultipleItems,
                             [&c] (const juce::FileChooser& fc)
                             {
                                 bool any = false;
                                 for (auto& f : fc.getResults())
                                 {
                                     if (! f.existsAsFile()) continue;
                                     c.disk.loadHostFile (f);      // mounts images, loads everything else
                                     any = true;
                                 }
                                 if (any) c.pages->pushPopup (std::make_unique<ProgressPopup> (c));
                             });
    }

    void chooseHdFolder (Ctx& c, std::unique_ptr<juce::FileChooser>& holder)
    {
        holder = std::make_unique<juce::FileChooser> ("Folder used as the S6000 HD", c.disk.getHostRootFolder());
        holder->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                             [&c] (const juce::FileChooser& fc)
                             {
                                 auto f = fc.getResult();
                                 if (f.isDirectory())
                                 {
                                     c.disk.setHostRootFolder (f);
                                     c.proc.savePrefs();
                                     c.pages->refresh();
                                 }
                             });
    }
}

//==============================================================================
class LoadPage : public Page
{
public:
    explicit LoadPage (Ctx& c) : Page (c, PageId::Load, PageId::None, Mode::Load) {}

    juce::String title() const override { return "LOAD"; }
    bool listLayout() const override { return true; }

    std::vector<VNodePtr> items() const { return ctx.disk.getVisibleItems (false); }

    VNodePtr current() const
    {
        auto it = items();
        return juce::isPositiveAndBelow (sel, (int) it.size()) ? it[(size_t) sel] : nullptr;
    }

    void onShow() override
    {
        // host folders may have changed (e.g. after SAVE): re-read them
        if (auto f = ctx.disk.getCurrentFolder())
            if (! f->hostPath.empty())
            {
                std::lock_guard<std::recursive_mutex> g (VNode::populateMutex());
                f->children.clear();
                f->populated = false;
            }
        sel = juce::jlimit (0, juce::jmax (0, (int) items().size() - 1), sel);
    }

    void build (Keys& k) override
    {
        auto& disks = ctx.disk.getDisks();
        const int di = ctx.disk.getCurrentDiskIndex();
        juce::String diskName = juce::isPositiveAndBelow (di, (int) disks.size()) ? disks[(size_t) di].name : juce::String ("NO DISK");
        k[0] = header (diskName);
        k[0].subtitle = juce::String (ctx.mem.percentFree()) + "% FREE";

        for (int i = 1; i <= 6; ++i)
        {
            const int rowIdx = scroll + geo::rowForKey (i);
            k[(size_t) i] = row ({}, {}, 0, false, [this, rowIdx]
            {
                if (rowIdx == sel) openOrLoad();      // touch twice = open
                else if (rowIdx < (int) items().size()) sel = rowIdx;
            });
        }
        k[7] = button ("DISK LIST", [this] { ctx.pages->show (PageId::DiskList); });
        k[8] = button ("DISK TOOLS", [this] { diskTools(); });
        k[9] = button ("GET INFO", [this] { getInfo(); });

        auto cur = current();
        const bool isImage = cur != nullptr && cur->kind == VNode::Kind::DiskImage;
        k[10] = button (isImage ? "MOUNT IMAGE" : "OPEN FOLDER", [this]
        {
            auto c = current();
            if (c == nullptr) return;
            if (c->kind == VNode::Kind::DiskImage)
            {
                ctx.disk.openFolder (c);
                ctx.pages->pushPopup (std::make_unique<ProgressPopup> (ctx));
            }
            else if (c->isContainer()) { ctx.disk.openFolder (c); sel = 0; scroll = 0; }
            else ctx.info ("OPEN FOLDER", { "This item is not a folder" });
        });
        k[11] = button ("CLOSE FOLDER", [this]
        {
            if (! ctx.disk.closeFolder()) ctx.info ("CLOSE FOLDER", { "Folder is already closed" });
            else { sel = 0; scroll = 0; }
        });
        k[12] = button ("AUDITION SAMPLE", [this] { audition(); });

        juce::String loadLabel = "LOAD FOLDER";
        if (cur != nullptr)
        {
            if (cur->type == VNode::Type::Multi) loadLabel = "LOAD MULTI";
            else if (cur->type == VNode::Type::Program) loadLabel = "LOAD PROGRAM";
            else if (cur->type == VNode::Type::Sample) loadLabel = "LOAD SAMPLE";
            else if (isImage) loadLabel = "MOUNT IMAGE";
        }
        k[13] = button (loadLabel, [this] { loadPressed(); }, cur != nullptr || ctx.disk.getCurrentFolder() != nullptr);
        k[14] = button ("SELECT " + upArrow(), [this] { wheel (-1); });
        k[15] = button ("SELECT " + downArrow(), [this] { wheel (1); });
    }

    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        auto folder = ctx.disk.getCurrentFolder();
        auto headLine = r.removeFromTop (geo::lineH);
        juce::String path = folder != nullptr ? juce::String (folder->path()) : juce::String();
        if (path.length() > 34) path = "..." + path.getLastCharacters (31);
        oled::text (g, path, headLine.reduced (2, 0), juce::Justification::centredLeft, 10.0f, false, colours::oledDim);
        g.setColour (colours::oledDim);
        g.drawHorizontalLine ((int) headLine.getBottom(), headLine.getX(), headLine.getRight());

        auto it = items();
        const int visible = geo::listRows;
        if (sel < scroll) scroll = sel;
        if (sel >= scroll + visible) scroll = sel - visible + 1;
        scroll = juce::jlimit (0, juce::jmax (0, (int) it.size() - visible), scroll);

        if (it.empty())
        {
            oled::text (g, ctx.disk.isBusy() ? "READING DISK..." : "(EMPTY)", r, juce::Justification::centred, 11.0f);
            return;
        }
        for (int i = scroll; i < juce::jmin ((int) it.size(), scroll + visible); ++i)
        {
            auto line = r.removeFromTop (geo::lineH);
            const auto& n = *it[(size_t) i];
            const bool s = i == sel;
            if (s) { g.setColour (colours::oledText); g.fillRect (line); }
            const auto col = s ? colours::oledBg : colours::oledText;
            oled::icon (g, iconFor (n), line.removeFromLeft (24.0f), col);
            juce::String info;
            if (view == 1 && ! n.isContainer()) info = sizeText (n.size);
            if (view == 2) info = juce::String (n.dateTime);
            if (info.isNotEmpty())
                oled::text (g, info, line.removeFromRight (view == 2 ? 92.0f : 52.0f), juce::Justification::centredRight, 10.0f, false, col);
            oled::text (g, juce::String (n.name), line, juce::Justification::centredLeft, 11.0f, false, col);
        }
    }

    void wheel (int delta) override
    {
        sel = juce::jlimit (0, juce::jmax (0, (int) items().size() - 1), sel + delta);
    }

    void enterPlay (bool down) override
    {
        auto c = current();
        if (c != nullptr && c->type == VNode::Type::Sample) { if (down) audition(); else ctx.engine.audition (nullptr); }
        else if (down) openOrLoad();
    }

    bool exitPressed() override
    {
        if (ctx.disk.closeFolder()) { sel = 0; scroll = 0; return true; }
        return false;
    }

private:
    int sel = 0, scroll = 0, view = 0;
    Sample::Ptr auditionHold;
    std::unique_ptr<juce::FileChooser> chooser;

    void openOrLoad()
    {
        auto c = current();
        if (c == nullptr) return;
        if (c->isContainer() || c->kind == VNode::Kind::DiskImage)
        {
            ctx.disk.openFolder (c);
            if (c->kind == VNode::Kind::DiskImage) ctx.pages->pushPopup (std::make_unique<ProgressPopup> (ctx));
            sel = 0; scroll = 0;
        }
        else loadPressed();
    }

    void audition()
    {
        auto c = current();
        if (c == nullptr || c->type != VNode::Type::Sample)
        {
            ctx.info ("AUDITION", { "You can only audition", "samples" });
            return;
        }
        ctx.disk.auditionFromDisk (c, [this] (Sample::Ptr s)
        {
            auditionHold = s;
            if (s != nullptr) ctx.engine.audition (s);
        });
    }

    void getInfo()
    {
        auto c = current();
        if (c == nullptr) { ctx.info ("GET INFO", { "Nothing selected" }); return; }
        juce::StringArray l;
        static const char* types[] = { "DISK", "FOLDER", "MULTI", "PROGRAM", "SAMPLE", "FILE" };
        l.add ("NAME: " + juce::String (c->name));
        l.add ("TYPE: " + juce::String (c->kind == VNode::Kind::DiskImage ? "DISK IMAGE" : types[(int) c->type]));
        if (c->kind == VNode::Kind::S1000Sample || c->kind == VNode::Kind::S1000Program) l.add ("FORMAT: AKAI S1000");
        if (c->kind == VNode::Kind::S3000Sample || c->kind == VNode::Kind::S3000Program) l.add ("FORMAT: AKAI S3000");
        if (c->kind == VNode::Kind::Akp || c->kind == VNode::Kind::Akm) l.add ("FORMAT: AKAI S5000/S6000");
        if (c->kind == VNode::Kind::Wav) l.add ("FORMAT: WAV");
        if (c->kind == VNode::Kind::Aiff) l.add ("FORMAT: AIFF");
        if (c->kind == VNode::Kind::Mp3) l.add ("FORMAT: MP3");
        if (c->kind == VNode::Kind::Compressed) l.add ("FORMAT: " + juce::String (c->name).fromLastOccurrenceOf (".", false, false).toUpperCase());
        if (c->isContainer()) { c->ensurePopulated(); l.add ("CONTAINS: " + juce::String ((int) c->children.size()) + " ITEMS"); }
        else l.add ("SIZE: " + sizeText (c->size) + " BYTES");
        if (! c->dateTime.empty()) l.add ("DATE: " + juce::String (c->dateTime));
        ctx.info ("GET INFO", l);
    }

    void diskTools()
    {
        std::vector<MenuPopup::Item> it;
        it.push_back ({ "DISK INFO", [this]
        {
            auto& d = ctx.disk.getDisks();
            const int i = ctx.disk.getCurrentDiskIndex();
            if (! juce::isPositiveAndBelow (i, (int) d.size())) return;
            ctx.info ("DISK INFO", { "DISK: " + d[(size_t) i].name, "FORMAT: " + d[(size_t) i].format,
                                     d[(size_t) i].writable ? "READ / WRITE" : "READ ONLY" });
        } });
        it.push_back ({ "CLEAR MEMORY", [this] { ctx.confirm ("CLEAR MEMORY", "Delete everything\nin memory?", [this] { clearMemory (ctx); }); } });
        it.push_back ({ "VIEW NAME ONLY", [this] { view = 0; } });
        it.push_back ({ "VIEW SIZE", [this] { view = 1; } });
        it.push_back ({ "VIEW DATE+TIME", [this] { view = 2; } });
        it.push_back ({ "MOUNT / LOAD FILE", [this] { chooseImage (ctx, chooser); } });
        it.push_back ({ "S6000 HD FOLDER", [this] { chooseHdFolder (ctx, chooser); } });
        ctx.pages->pushPopup (std::make_unique<MenuPopup> (ctx, "DISK TOOLS", it));
    }

    void loadPressed()
    {
        auto c = current();
        if (c == nullptr) { if (auto f = ctx.disk.getCurrentFolder()) promptFolder (f); return; }
        if (c->kind == VNode::Kind::DiskImage) { openOrLoad(); return; }
        if (c->isContainer()) { promptFolder (c); return; }

        auto p = std::make_unique<PromptPopup> (ctx, "", "");
        if (c->type == VNode::Type::Multi)
        {
            p->heading = "LOAD MULTI";
            p->question = "Load multi\n" + juce::String (c->name);
            p->checks = { { "ALL MULTIS", false, 5 }, { "PROGS+SAMPLES", true, 6 }, { "CLEAR MEMORY", false, 4 } };
        }
        else if (c->type == VNode::Type::Program)
        {
            p->heading = "LOAD PROGRAM";
            p->question = "Load program\n" + juce::String (c->name);
            p->checks = { { "ALL PROGRAMS", false, 5 }, { "WITH SAMPLES", true, 6 }, { "CLEAR MEMORY", false, 4 } };
        }
        else if (c->type == VNode::Type::Sample)
        {
            p->heading = "LOAD SAMPLE";
            p->question = "Load sample\n" + juce::String (c->name);
            p->checks = { { "ALL SAMPLES", false, 5 }, { "CLEAR MEMORY", false, 4 } };
        }
        else return;

        auto node = c;
        p->onExecute = [this, node] (PromptPopup& pp)
        {
            DiskManager::LoadOptions o;
            o.allInFolder = pp.isChecked (0);
            o.withChildren = node->type == VNode::Type::Sample ? false : pp.isChecked (1);
            const bool clear = node->type == VNode::Type::Sample ? pp.isChecked (1) : pp.isChecked (2);
            if (clear) clearMemory (ctx);
            ctx.disk.loadItem (node, o);
            ctx.pages->pushPopup (std::make_unique<ProgressPopup> (ctx));
        };
        ctx.pages->pushPopup (std::move (p));
    }

    void promptFolder (VNodePtr f)
    {
        auto p = std::make_unique<PromptPopup> (ctx, "LOAD FOLDER", "Load the contents of\n" + juce::String (f->name) + "\n(including sub-folders)");
        p->checks = { { "MULTIS", true, 5 }, { "PROGRAMS", true, 6 }, { "SAMPLES", true, 13 }, { "CLEAR MEMORY", false, 4 } };
        p->onExecute = [this, f] (PromptPopup& pp)
        {
            DiskManager::LoadOptions o;
            o.multis = pp.isChecked (0); o.programs = pp.isChecked (1); o.samples = pp.isChecked (2);
            if (pp.isChecked (3)) clearMemory (ctx);
            ctx.disk.loadFolder (f, o);
            ctx.pages->pushPopup (std::make_unique<ProgressPopup> (ctx));
        };
        ctx.pages->pushPopup (std::move (p));
    }
};

//==============================================================================
class DiskListPage : public Page
{
public:
    explicit DiskListPage (Ctx& c) : Page (c, PageId::DiskList, PageId::Load, Mode::Load) {}
    juce::String title() const override { return "DISK LIST"; }
    bool listLayout() const override { return true; }
    void onShow() override { sel = ctx.disk.getCurrentDiskIndex(); }

    void build (Keys& k) override
    {
        k[0] = header ("SELECT DISK");
        k[0].subtitle = juce::String ((int) ctx.disk.getDisks().size()) + " DISKS";
        for (int i = 1; i <= 6; ++i)
        {
            const int rowIdx = geo::rowForKey (i);
            k[(size_t) i] = row ({}, {}, 0, false, [this, rowIdx]
            {
                if (rowIdx == sel) select();
                else if (rowIdx < (int) ctx.disk.getDisks().size()) sel = rowIdx;
            });
        }
        k[7] = button ("SELECT DISK", [this] { select(); });
        k[8] = button ("CLOSE LIST", [this] { ctx.pages->show (PageId::Load); });
        k[9] = button ("GET INFO", [this]
        {
            auto& d = ctx.disk.getDisks();
            if (! juce::isPositiveAndBelow (sel, (int) d.size())) return;
            ctx.info ("GET INFO", { d[(size_t) sel].name, "FORMAT: " + d[(size_t) sel].format, d[(size_t) sel].writable ? "READ / WRITE" : "READ ONLY" });
        });
        k[10] = button ("UPDATE", [this] { ctx.disk.refreshDisks(); });
        k[11] = button ("MOUNT / LOAD FILE", [this] { chooseImage (ctx, chooser); });
        k[12] = button ("UNMOUNT", [this] { if (! ctx.disk.unmount (sel)) ctx.info ("UNMOUNT", { "This disk cannot", "be unmounted" }); });
        k[13] = button ("S6000 HD FOLDER", [this] { chooseHdFolder (ctx, chooser); });
        k[14] = button ("SELECT " + upArrow(), [this] { wheel (-1); });
        k[15] = button ("SELECT " + downArrow(), [this] { wheel (1); });
    }

    void paintCentre (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        auto head = r.removeFromTop (geo::lineH);
        oled::text (g, "DISK                        FORMAT", head, juce::Justification::centredLeft, 10.0f, false, colours::oledDim);
        auto& d = ctx.disk.getDisks();
        for (int i = 0; i < juce::jmin (geo::listRows, (int) d.size()); ++i)
        {
            auto line = r.removeFromTop (geo::lineH);
            const bool s = i == sel;
            if (s) { g.setColour (colours::oledText); g.fillRect (line); }
            const auto col = s ? colours::oledBg : colours::oledText;
            oled::icon (g, d[(size_t) i].writable || i == 1 ? 5 : 6, line.removeFromLeft (24.0f), col);
            oled::text (g, d[(size_t) i].format, line.removeFromRight (110.0f), juce::Justification::centredRight, 10.0f, false, col);
            oled::text (g, d[(size_t) i].name + (i == ctx.disk.getCurrentDiskIndex() ? " *" : ""), line, juce::Justification::centredLeft, 11.0f, false, col);
        }
    }

    void wheel (int delta) override { sel = juce::jlimit (0, juce::jmax (0, (int) ctx.disk.getDisks().size() - 1), sel + delta); }
    void enterPlay (bool down) override { if (down) select(); }

private:
    int sel = 0;
    std::unique_ptr<juce::FileChooser> chooser;
    void select() { ctx.disk.selectDisk (sel); ctx.pages->show (PageId::Load); }
};

void registerLoadPages (PageManager& pm, Ctx& c)
{
    pm.add (std::make_unique<LoadPage> (c));
    pm.add (std::make_unique<DiskListPage> (c));
}

} // namespace s6k::ui
