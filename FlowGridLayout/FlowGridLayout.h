#ifndef _FlowGridLayout_FlowGridLayout_h_
#define _FlowGridLayout_FlowGridLayout_h_

#include <CtrlLib/CtrlLib.h>

namespace Upp {

// ---------- Public API --------------------------------------------------------

enum class FGLMode   : byte { Flow, Grid };
enum class FGLDir    : byte { LeftToRight, TopToBottom };
enum class FGLScroll : byte { Auto, VerticalOnly, HorizontalOnly, None };

// U++-style theme container: data-only, no dynamic allocation, read by Paint().
struct FlowGridLayoutStyle : ChStyle<FlowGridLayoutStyle> {
    // geometry
    int   padding = DPI(8);
    int   spacing = DPI(6);

    // header / segmentation (drawn above clusters when enabled)
    bool  group_header   = false;
    int   group_header_h = DPI(22);
    bool  group_divider  = false;

    // cluster box cosmetics (style-driven)
    bool  cluster_box_default = false;     // if true, clusters draw a box unless per-cluster turned off
    Color cluster_box_bg      = Blend(SColorFace(), SColorPaper(), 40);
    Color cluster_box_border  = SColorShadow();
    int   cluster_box_radius  = DPI(8);
    int   cluster_box_pad     = DPI(6);

    // control face
    Color face = SColorFace();

    static const FlowGridLayoutStyle& StyleDefault() {
        static FlowGridLayoutStyle s;
        return s;
    }
};

class FlowGridLayout : public Ctrl {
public:
    // ---- configuration
    FlowGridLayout& SetMode(FGLMode m)                 { mode = m; Reflow(); return *this; }
    FlowGridLayout& SetDirection(FGLDir d)             { dir = d; Reflow(); return *this; }
    FlowGridLayout& SetWrap(bool on = true)            { wrap = on; Reflow(); return *this; }
    FlowGridLayout& SetScrollMode(FGLScroll m)         { scroll = m; UpdateScrollbars(); return *this; }
    FlowGridLayout& SetUnifiedItemSize(Size sz, bool on = true) { unified = on; unified_sz = sz; Reflow(); return *this; }

    FlowGridLayout& SetStyle(const FlowGridLayoutStyle& s) { style = s; Refresh(); return *this; }
    const FlowGridLayoutStyle& GetStyle() const            { return style; }

    // ---- clusters
    int  NewCluster();
    FlowGridLayout& SetCurrentCluster(int id)         { cur_cluster = id; return *this; }
    FlowGridLayout& SetClusterFlow(int id, bool on);  // if false => atomic block on a line
    FlowGridLayout& SetClusterBox(int id, bool on);   // per-cluster rounded box (look is style-driven)
    FlowGridLayout& SetClusterHeader(int id, bool on = true, bool with_box = false);
    FlowGridLayout& SetClusterDecor(int id, bool header_on, bool box_on) { return SetClusterHeader(id, header_on, box_on); }

    // ---- adding items (Flow)
    int Add(Ctrl& c, int cluster_id = -1, bool scale_to_cell = false, Size fixed = Size(0,0));
    int Add(Ctrl& c, int cluster_id, int /*segment_id_unused*/, bool scale_to_cell, Size fixed) {
        // compatibility shim if an older callsite used this signature
        return Add(c, cluster_id, scale_to_cell, fixed);
    }
    int AddSpacer(int min_px = 0, int max_px = INT_MAX, int cluster_id = -1);
    int AddExpander(int weight = 1, int cluster_id = -1);
    int AddGap(int px, int cluster_id = -1);

    // ---- grid (simple MVP; rows/cols start at 0)
    int AddGrid(Ctrl& c, int row, int col, bool scale_to_cell = false, Size fixed = Size(0,0));
    int AddBlankGrid(int row, int col);

    // ---- headers (cluster labels)
    FlowGridLayout& SetGroupHeaders(bool on = true)    { default_cluster_header = on; Refresh(); return *this; }
    FlowGridLayout& WhenClusterText(Upp::Function<Upp::String(int)> fn) { when_group_text = pick(fn); Refresh(); return *this; }
    FlowGridLayout& WhenGroupText(Upp::Function<Upp::String(int)> fn)   { when_group_text = pick(fn); Refresh(); return *this; }

    // ---- selection (virtual mode – minimal surface for now)
    const Upp::Vector<int>& GetSelection() const       { return selection; }
    void ClearSelection()                              { selection.Clear(); Refresh(); }

public: // Ctrl

    virtual void Layout() override;
    virtual void Paint(Upp::Draw& w) override;
	
    // Observable content size (useful for parents stacking toolbars)
    Upp::Size GetContentSize() const { return content; }
    Upp::Function<void(Upp::Size)> WhenContentSize;

    FlowGridLayout();

private:
    // ----- internal model
    enum class Kind : byte { CtrlItem, Spacer, Expander, Gap, GridCell, BlankGrid };

    struct Item : Moveable<Item> {
        Kind  kind = Kind::CtrlItem;
        int   cluster = -1;         // physical keep-together unit
        Ctrl* ctrl = nullptr;
        bool  scale_to_cell = false;
        Size  fixed = Size(0,0);    // if non-zero, overrides min size unless unified is on
        int   min_px = 0;           // for Spacer/Gap
        int   max_px = INT_MAX;     // for Spacer
        int   weight = 0;           // for Expander
        int   row = -1, col = -1;   // for Grid
        Rect  rect;                 // computed during layout
        bool  visible = true;
    };

    struct Cluster : Moveable<Cluster> {
        bool box  = false;      // draw rounded box (look driven by style)
        bool flow = false;      // internal wrapping allowed
        int8 header = -1;       // -1 = inherit default, 0 = off, 1 = on
        Rect bounds;            // union of child rects (computed)
    };

    FGLMode  mode   = FGLMode::Flow;
    FGLDir   dir    = FGLDir::LeftToRight;
    FGLScroll scroll = FGLScroll::Auto;
    bool     wrap   = true;
    bool     unified = false;
    Size     unified_sz = Size(0,0);
    
    bool laying_out = false;     // guards Layout() re-entrancy (frames toggling)
	bool updating_sb = false;    // guards UpdateScrollbars() re-entrancy
    Upp::Size last_reported_content{0, 0};
    
    Vector<Item>    items;
    Vector<Cluster> clusters;
    int             cur_cluster = -1;

    // headers
    bool default_cluster_header = false;               // global default when Cluster.header == -1
    Function<String(int)> when_group_text;             // id -> header text

    // selection (reserved for virtual – demo uses controls so it stays empty)
    Vector<int> selection;

    // scrollbars
    ScrollBars sb;
    Point      origin = Point(0,0);
    Size       content = Size(0,0);

    FlowGridLayoutStyle style = FlowGridLayoutStyle::StyleDefault();

    // helpers
    void Reflow()                               { RefreshLayout(); }
    void UpdateScrollbars();
    void ApplyScrollbars();

    // flow layout passes
    void DoFlowLayoutH();
    void DoFlowLayoutV();
    Size NaturalItemSize(const Item& it) const;

    // paint helpers
    void PaintClusters(Upp::Draw& w);
    void PaintGroupHeader(Upp::Draw& w, const Upp::Rect& r, int cluster_id);
    void PaintClusterHeaders(Upp::Draw& w);

    int  EnsureCluster(int cluster);
};

} // namespace Upp

#endif
