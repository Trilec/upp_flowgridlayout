#include <CtrlLib/CtrlLib.h>
//#include <Painter/DrawPainter.h> 
#include <FlowGridLayout/FlowGridLayout.h>


using namespace Upp;

using namespace Upp;

// ---------- small helpers ----------
static inline int  DPIx(int px) { return DPI(px); }
static inline int  Maxi(int a, int b) { return a > b ? a : b; }

static inline Label& MakeHeader(Array<Label>& store, const char *txt) {
    Label& h = store.Create();
    h.SetFont(StdFont().Bold());
    h.SetInk(SColorText());
    h.SetText(txt);
    return h;
}

// =====================================================
// ShowcaseCard  (RoundedRectangle via BufferPainter)
// =====================================================
class ShowcaseCard : public Ctrl {
public:
    ShowcaseCard& SetTitle(const String& s, Font f = StdFont().Bold().Height(22)) { title = s; titleFont = f; Refresh(); return *this; }
    ShowcaseCard& SetBody (const String& s, Font f = StdFont().Height(12))        { body  = s; bodyFont  = f; Refresh(); return *this; }
    ShowcaseCard& SetBadge(const String& s, Font f = StdFont().Height(12))        { badge = s; badgeFont = f; Refresh(); return *this; }

    ShowcaseCard& EnableFrame(bool on = true)  { showFrame = on; Refresh(); return *this; }
    ShowcaseCard& EnableFill(bool on = true)   { fillBody  = on; Refresh(); return *this; }
    ShowcaseCard& SetRadius(int px)            { radius    = max(2, px); Refresh(); return *this; }
    ShowcaseCard& SetPadding(int x, int y)     { padx = x; pady = y; Refresh(); return *this; }
    ShowcaseCard& SetColors(Color face, Color stroke,
                            Color tInk = SColorText(),
                            Color bInk = SColorText(),
                            Color gInk = SColorDisabled())
    { bg = face; border = stroke; titleInk = tInk; bodyInk = bInk; badgeInk = gInk; Refresh(); return *this; }

    virtual Size GetMinSize() const override { return Size(DPI(220), DPI(90)); }

private:
    // content
    String title, body, badge;

    // style
    int   radius   = DPI(12);
    int   padx     = DPI(12);
    int   pady     = DPI(10);
    int   gapTitle = DPI(4);
    bool  showFrame = true;
    bool  fillBody  = true;

    // fonts/inks
    Font  titleFont = StdFont().Bold().Height(22);
    Font  bodyFont  = StdFont().Height(12);
    Font  badgeFont = StdFont().Height(12);
    Color titleInk  = SColorText();
    Color bodyInk   = SColorText();
    Color badgeInk  = SColorDisabled();
    Color bg        = Blend(SColorPaper(), SColorFace(), 32);
    Color border    = SColorShadow();

    static String WrapToWidth(const String& s, Font f, int w) {
        if(IsNull(s) || w <= 8) return s;
        Vector<String> words = Split(s, CharFilterWhitespace);
        String out, line;
        for(const String& wrd : words) {
            String probe = line.IsEmpty() ? wrd : line + ' ' + wrd;
            if(GetTextSize(probe, f).cx <= w || line.IsEmpty())
                line = probe;
            else {
                if(!out.IsEmpty()) out << '\n';
                out << line;
                line = wrd;
            }
        }
        if(!line.IsEmpty()) {
            if(!out.IsEmpty()) out << '\n';
            out << line;
        }
        return out;
    }

    virtual void Paint(Draw& w) override {
        Size sz = GetSize();
        
        
        // Antialiased rounded rect using DrawPainter
        {
            DrawPainter p(w, sz);
            p.DrawRect(sz, SColorFace());
            if(fillBody) {
                p.Begin();
                p.RoundedRectangle(0.5, 0.5, sz.cx - 1.0, sz.cy - 1.0, (double)radius, (double)radius);
                p.End();
                p.Fill(bg);
            }
            if(showFrame) {
                p.Begin();
                p.RoundedRectangle(1.0, 1.0, sz.cx - 2.0, sz.cy - 2.0, (double)radius, (double)radius);
                p.End();
                p.Stroke(2.0, border);
            }
        }

        // Foreground text (title left, badge right, wrapped body)
        Rect c = Rect(sz).Deflated(padx, pady);

        const int titleLH = titleFont.GetLineHeight();
        const int badgeLH = badgeFont.GetLineHeight();
        const int lineH   = max(titleLH, badgeLH);

        int y = c.top;

        if(!IsNull(badge)) {
            Size bs = GetTextSize(badge, badgeFont);
            int by  = y + (lineH - badgeLH) / 2;
            w.DrawText(c.right - bs.cx, by, badge, badgeFont, badgeInk);
        }

        int ty = y + (lineH - titleLH) / 2;
        w.DrawText(c.left, ty, title, titleFont, titleInk);
        y += lineH + gapTitle;

        if(!IsNull(body)) {
            String wrapped = WrapToWidth(body, bodyFont, c.GetWidth());
            int lh = bodyFont.GetLineHeight();
            for(const String& line : Split(wrapped, '\n')) {
                w.DrawText(c.left, y, line, bodyFont, bodyInk);
                y += lh;
                if(y > c.bottom) break;
            }
        }
    }
};


// =====================================================
// A very small StackView (one child visible at a time)
// =====================================================
class StackView : public Ctrl {
public:
    StackView& AddPage(Ctrl& c) { Add(c); pages.Add(&c); c.Hide(); return *this; }
    void Set(int i) {
        if(i < 0 || i >= pages.GetCount() || i == current) return;
        if(current >= 0) pages[current]->Hide();
        current = i;
        pages[current]->Show();
        Layout();
    }
    virtual void Layout() override {
        Rect v = GetView();
        for(Ctrl* p : pages) p->SetRect(v);
    }
private:
    Vector<Ctrl*> pages;
    int current = -1;
};

// =====================================================
// Demo views (minimal but visible content)
// =====================================================
class WidgetsView : public Ctrl {
public:
    WidgetsView() {
        Add(topbar);
        Add(mid);
        Add(area.HSizePos().VSizePos());
        area.Horz(flow_left, grid_right).SetPos(4500);

        // Topbar (wraps, non-scrolling)
        topbar.SetMode(FGLMode::Flow).SetDirection(FGLDir::LeftToRight).SetWrap(true)
              .SetScrollMode(FGLScroll::None).SetGroupHeaders(false);

        {
            int c = topbar.NewCluster();
            topbar.SetClusterBox(c, true);

            topbar.Add(MakeHeader(labels, "TOP: Chrome demo (wraps; pushes content)"),
                       c, false, Size(DPIx(300), DPIx(22)));
            topbar.AddGap(DPIx(6), c);

            Label& title = labels.Create();
            title.SetText("Untitled — Icon Builder");
            topbar.Add(title, c, false, Size(DPIx(220), DPIx(28)));

            DropList& preview = drops.Create();
            preview.Add("Preview 16").Add("Preview 32").Add("Preview 64").Add("Preview 128");
            preview.SetIndex(3);
            topbar.Add(preview, c, false, Size(DPIx(160), DPIx(28)));

            search.AddList("Search…");
            search.SetText("Search…");
            topbar.Add(search, c, false, Size(DPIx(260), DPIx(28)));

            topbar.AddExpander(1, c);

            auto quick = [&](const char *txt)->Button& { Button& b = buttons.Create(); b.SetLabel(txt); return b; };
            for(const char *s : { "⏺", "⏱", "🖫", "📄" })
                topbar.Add(quick(s), c, false, Size(DPIx(34), DPIx(28)));

            topbar.AddExpander(1, c);
            for(const char *s : { "—", "▢", "✕" })
                topbar.Add(quick(s), c, false, Size(DPIx(34), DPIx(28)));
        }

        // Middle (clusters + edge cases)
        mid.SetMode(FGLMode::Flow).SetWrap(true).SetScrollMode(FGLScroll::None).SetGroupHeaders(true);
        mid.WhenClusterText([&](int gid){
            switch(gid) { case 0: return String("Tools"); case 1: return String("Style"); case 2: return String("Controls"); }
            return Format("Cluster %d", gid);
        });

        // Tools
        {
            int g = mid.NewCluster(); mid.SetClusterHeader(g, true, false);
            auto mk = [&](const char *lab)->Button& { Button& b = buttons.Create(); b.SetLabel(lab); return b; };
            for(const char* s : { "Select", "◻", "◯", "◼", "●", "▱", "△", "T" })
                mid.Add(mk(s), g, false, Size(DPIx(64), DPIx(28)));
        }
        // Style centered
        {
            int g = mid.NewCluster(); mid.SetClusterHeader(g, true, false);
            mid.AddExpander(1, g);
            auto mk = [&](const char *lab)->Button& { Button& b = buttons.Create(); b.SetLabel(lab); return b; };
            for(const char* s : { "⎺_", "◻", "⬛" })
                mid.Add(mk(s), g, false, Size(DPIx(48), DPIx(28)));
            mid.AddExpander(1, g);
        }
        // Controls sampler
        {
            int g = mid.NewCluster(); mid.SetClusterHeader(g, true, false);

            Button& longbtn = buttons.Create();
            longbtn.SetLabel("Very long action that forces wrap");
            mid.Add(longbtn, g, false, Size(DPIx(240), DPIx(28)));

            EditString& ed = edits.Create(); ed.SetText("EditString");
            mid.Add(ed, g, false, Size(DPIx(120), DPIx(28)));

            EditIntSpin& is = ints.Create();  is <<= 42;
            mid.Add(is, g, false, Size(DPIx(80), DPIx(28)));

            EditDoubleSpin& ds = doubles.Create(); ds <<= 3.14;
            mid.Add(ds, g, false, Size(DPIx(100), DPIx(28)));

            DropList& dl = drops.Create(); dl.Add("One").Add("Two").Add("Three").SetIndex(0);
            mid.Add(dl, g, false, Size(DPIx(120), DPIx(28)));

            Option& oa = opts.Create(); oa.SetLabel("Option A"); oa <<= true;  mid.Add(oa, g);
            Option& ob = opts.Create(); ob.SetLabel("Option B");               mid.Add(ob, g);

            // slider style with ScrollBar (horz + vert)
            ScrollBar& hb = bars.Create(); hb.Horz(); hb.SetTotal(100); hb.SetPage(10); hb.Set(50);
            mid.Add(hb, g, true, Size(DPIx(180), DPIx(18)));

            ScrollBar& vb = bars.Create(); vb.Vert(); vb.SetTotal(100); vb.SetPage(10); vb.Set(50);
            mid.Add(vb, g, true, Size(DPIx(18), DPIx(80)));
        }

        // Bottom: flow left + grid right in a Splitter
        flow_left.SetMode(FGLMode::Flow).SetWrap(true).SetUnifiedItemSize(Size(DPIx(92), DPIx(28)), true)
                  .SetScrollMode(FGLScroll::Auto);
        flow_left.Add(MakeHeader(labels, "Bottom Left — Flow with unified item size & wrap"));
        flow_left.AddGap(DPIx(8));
        for(const char* s : { "Reset", "Duplicate", "Delete", "Clear", "Flip X", "Flip Y", "Copy" }) {
            Button& b = buttons.Create(); b.SetLabel(s);
            flow_left.Add(b);
        }
        flow_left.AddGap(DPIx(6));
        Option& oa = opts.Create(); oa.SetLabel("Option A"); flow_left.Add(oa);
        Option& ob = opts.Create(); ob.SetLabel("Option B"); flow_left.Add(ob);
        Option& oc = opts.Create(); oc.SetLabel("Option C"); flow_left.Add(oc);

        grid_right.SetMode(FGLMode::Grid).SetScrollMode(FGLScroll::Auto);
        grid_right.AddGrid(MakeHeader(labels, "Bottom Right — Grid 3x3"), 0, 0, false, Size(DPIx(220), DPIx(24)));
        for(int r = 1; r <= 3; ++r)
            for(int c = 0; c < 3; ++c) {
                Button& b = buttons.Create();
                b.SetLabel(Format("R%dC%d", r, c));
                grid_right.AddGrid(b, r, c, true, Size(DPIx(96), DPIx(28)));
            }

        topbar.WhenContentSize = [&](Size){ RefreshLayout(); };
        mid.WhenContentSize    = [&](Size){ RefreshLayout(); };
    }

    virtual void Layout() override {
        Rect v = GetView();
        int y = v.top;

        int top_h = Maxi(DPIx(44), topbar.GetContentSize().cy);
        int mid_h = Maxi(DPIx(64), mid.GetContentSize().cy);

        topbar.SetRect(v.left, y, v.GetWidth(), top_h); y += top_h + DPIx(6);
        mid   .SetRect(v.left, y, v.GetWidth(), mid_h); y += mid_h + DPIx(8);
        area  .SetRect(v.left, y, v.GetWidth(), v.bottom - y);
    }

private:
    FlowGridLayout topbar, mid;
    Splitter       area;
    FlowGridLayout flow_left, grid_right;

    Array<Label>          labels;
    Array<Button>         buttons;
    Array<Option>         opts;
    Array<DropList>       drops;
    Array<EditString>     edits;
    Array<EditIntSpin>    ints;
    Array<EditDoubleSpin> doubles;
    Array<ScrollBar>      bars;
    WithDropChoice<EditString> search;
};

class PanelsView : public Ctrl {
public:
    PanelsView() {
        Add(hs.HSizePos().VSizePos());
        vs.Vert(center, right).SetPos(7000);
        hs.Horz(left, vs).SetPos(3000);

        center.SetFrame(ThinInsetFrame());

        left.SetMode(FGLMode::Flow).SetWrap(true).SetScrollMode(FGLScroll::Auto);
        right.SetMode(FGLMode::Flow).SetWrap(true).SetScrollMode(FGLScroll::Auto);

        left.Add(MakeHeader(labels, "Left Panel"));
        left.AddGap(DPIx(6));
        for(const char* s : { "Tool Palette", "Layers", "Assets" }) {
            Button& b = buttons.Create(); b.SetLabel(s);
            left.Add(b, -1, true, Size(DPIx(200), DPIx(28)));
        }

        right.Add(MakeHeader(labels, "Right Panel"));
        right.AddGap(DPIx(6));
        for(const char* s : { "Inspector", "Properties" }) {
            Button& b = buttons.Create(); b.SetLabel(s);
            right.Add(b, -1, true, Size(DPIx(200), DPIx(28)));
        }
    }
private:
    Splitter hs, vs;
    Ctrl     center;
    FlowGridLayout left, right;
    Array<Label>  labels;
    Array<Button> buttons;
};

class PositioningView : public Ctrl {
public:
    PositioningView() {
        Add(sticky);
        sticky.SetText("Sticky Subheader (Fixed)");
        sticky.SetFrame(ThinInsetFrame());
        sticky.SetAlign(ALIGN_CENTER);

        Add(body);
        body.SetMode(FGLMode::Flow).SetWrap(true).SetScrollMode(FGLScroll::Auto);

        body.Add(MakeHeader(labels, "Tiles"));
        body.AddGap(DPIx(6));
        for(int i = 1; i <= 8; i++) {
            Label& x = labels.Create();
            x.SetText(Format("Tile %d", i));
            x.SetFrame(ThinInsetFrame());
            x.SetAlign(ALIGN_CENTER);
            body.Add(x, -1, true, Size(DPIx(120), DPIx(64)));
        }
    }
    virtual void Layout() override {
        const int header_h = DPIx(24);
        Rect v = GetView();
        sticky.SetRect(v.left, v.top, v.GetWidth(), header_h);
        body  .SetRect(v.left, v.top + header_h, v.GetWidth(), v.GetHeight() - header_h);
    }
private:
    Label          sticky;
    FlowGridLayout body;
    Array<Label>   labels;
};

class GridView : public Ctrl {
public:
    GridView() {
        Add(fg);
        fg.SetMode(FGLMode::Flow).SetWrap(true).SetScrollMode(FGLScroll::Auto);
        fg.Add(MakeHeader(labels, "Responsive Reflow — shrink window to see wrap"));
        fg.AddGap(DPIx(8));
        for(const char *t : { "Left", "Center", "Right" }) {
            Label& c = labels.Create();
            c.SetText(t);
            c.SetFrame(ThinInsetFrame());
            c.SetAlign(ALIGN_CENTER);
            fg.Add(c, -1, true, Size(DPIx(220), DPIx(80)));
        }
    }
    virtual void Layout() override { fg.SetRect(GetView()); }
private:
    FlowGridLayout fg;
    Array<Label>   labels;
};

// =====================================================
// Playground window
// =====================================================
class Playground : public TopWindow {
public:
	
	Playground() {
	    Title("FlowGrid — Demo");
	    Sizeable().Zoomable();
	    Add(root.SizePos());
	
	    // header + stack rows
	    root.Add(header, DPI(80), false, DPI(0));
	    root.Add(stack,  DPI(0),  true,  DPI(8));
	
	    header.SetMode(FGLMode::Flow)
	          .SetWrap(true)
	          .SetScrollMode(FGLScroll::None)
	          .SetGroupHeaders(false);
	    header.WhenContentSize = [&](Size){ RefreshLayout(); };
	
	    // Title card (text only)
	    int cTitle = header.NewCluster();
	    header.SetClusterBox(cTitle, false);
	    cardTitle
	        .SetTitle("Widget Layout Showcase", StdFont().Bold().Height(28))
	        .SetBody("Default view is Sash Cards. Left buttons switch to Panels, Positioning, and Grid demos.",
	                 StdFont().Height(12))
	        .SetBadge("")
	        .EnableFrame(false).EnableFill(false)
	        .SetRadius(DPI(12));
	    header.Add(cardTitle, cTitle, true, Size(DPI(560), DPI(110)));
	
	    // Info card (≈30% smaller than 280 -> 196)
	    int cInfo = header.NewCluster();
	    header.SetClusterBox(cInfo, false);
	    cardInfo
	        .SetTitle("Showcase", StdFont().Bold().Height(12))
	        .SetBadge("")
	        .SetBody("Default loads Sash Cards under the title. Buttons switch the main section.",
	                 StdFont().Height(12))
	        .EnableFrame(true).EnableFill(true)
	        .SetRadius(DPI(12));
	    header.Add(cardInfo, cInfo, true, Size(DPI(196), DPI(110)));
	
	    // *** spacer between info card and nav buttons ***
	    header.AddGap(DPI(12));
	
	    // Nav buttons (wrap)
	    int cNav = header.NewCluster();
	    header.SetClusterBox(cNav, false);
	    auto add_nav = [&](const char *glyph, const char *label, int idx) {
	        Button& b = nav.Create();
	        b.SetLabel(Format("%s\n%s", glyph, label));
	        b.SetFont(StdFont().Bold());
	        b.WhenAction = [=]{ Go(idx); };
	        header.Add(b, cNav, true, Size(DPI(96), DPI(64)));
	    };
	    add_nav("➤", "Widgets",     0);
	    add_nav("▥", "Panels",      1);
	    add_nav("▤", "Positioning", 2);
	    add_nav("▦", "Grid",        3);
	
	    // stack pages
	    stack.AddPage(view_widgets);
	    stack.AddPage(view_panels);
	    stack.AddPage(view_pos);
	    stack.AddPage(view_grid);
	
	    Go(0);
	}


    virtual void Layout() override {
        int hh = max(DPI(72), header.GetContentSize().cy);
        root.At(0).minh = hh;          // ensure first row (header) takes reported height
        root.SetRect(GetView());
    }

private:
    struct VBox : Ctrl {
        struct Row { Ctrl* ctrl=nullptr; int minh=0; int gap=0; bool fill=false; };
        Vector<Row> rows; int default_gap = DPI(6);

        VBox& Add(Ctrl& c, int min_h, bool fill_row=false, int top_gap=-1) {
            Row r; r.ctrl=&c; r.minh=min_h; r.fill=fill_row; r.gap = top_gap < 0 ? default_gap : top_gap;
            Ctrl::Add(c); rows.Add(r); Layout(); return *this;
        }
        Row& At(int i) { return rows[i]; }

        virtual void Layout() override {
            Rect v = GetView();
            int y = v.top, rest=0, fixed=0;
            for(const Row& r : rows) { fixed += r.gap; if(r.fill) rest++; else fixed += r.minh; }
            int avail = v.GetHeight() - fixed;
            int each  = rest ? max(0, avail / rest) : 0;
            for(Row& r : rows) {
                y += r.gap; int h = r.fill ? each : r.minh;
                if(r.ctrl) r.ctrl->SetRect(v.left, y, v.GetWidth(), h);
                y += h;
            }
        }
    };

    VBox           root;
    FlowGridLayout header;
    StackView      stack;

    ShowcaseCard   cardTitle;
    ShowcaseCard   cardInfo;

    Array<Button>  nav;

    WidgetsView     view_widgets;
    PanelsView      view_panels;
    PositioningView view_pos;
    GridView        view_grid;

	void Go(int i) {
	    int idx = ::clamp(i, 0, 3);
	    stack.Set(idx);
	
	    switch(idx) {
	    case 0:
	        cardInfo
	            .SetTitle("Widgets", StdFont().Bold().Height(12))
	            .SetBadge("➤")
	            .SetBody("Toolbar-like flows, wrapping groups, common controls, and edge cases.",
	                     StdFont().Height(12))
	            .EnableFrame(true).EnableFill(true);
	        break;
	    case 1:
	        cardInfo
	            .SetTitle("Panels", StdFont().Bold().Height(12))
	            .SetBadge("▥")
	            .SetBody("Dockable / resizable composition with splitters; left and right flows.",
	                     StdFont().Height(12))
	            .EnableFrame(true).EnableFill(true);
	        break;
	    case 2:
	        cardInfo
	            .SetTitle("Positioning", StdFont().Bold().Height(12))
	            .SetBadge("▤")
	            .SetBody("Fixed subheader plus scrollable flow content with tiles.",
	                     StdFont().Height(12))
	            .EnableFrame(true).EnableFill(true);
	        break;
	    case 3:
	        cardInfo
	            .SetTitle("Grid / Reflow", StdFont().Bold().Height(12))
	            .SetBadge("▦")
	            .SetBody("Three cards that reflow vertically as width shrinks, using flow wraps.",
	                     StdFont().Height(12))
	            .EnableFrame(true).EnableFill(true);
	        break;
	    }
	    header.Refresh();
	}

};

// ----------------------------------
// GUI entry
// ----------------------------------
GUI_APP_MAIN
{
    Playground().Run();
}
