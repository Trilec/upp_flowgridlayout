#ifndef _FlowGridLayout_FlowGridLayout_h_
#define _FlowGridLayout_FlowGridLayout_h_

#include <CtrlLib/CtrlLib.h>
#include <limits.h>

namespace Upp {

//==============================================================================
// FlowGridLayout: Flow / Grid hybrid with lightweight clustering and headers.
// - Modes: Flow (wrap-aware) or Grid (row/col).
// - Direction: LeftToRight / TopToBottom.
// - Cluster features: keep items together, optional rounded boxes, headers.
// - API parity: Inset/Gap, AlignItems, SetFixedColumn/Row via unified sizing.
// - Sizing helpers: GetContentSize(), MeasureHeightForWidth(int).
//==============================================================================



class FlowGridLayout : public Ctrl {
public:
    /// Cross-axis alignment (semantics similar to FlowBoxLayout).
    enum Align : byte { Auto, Stretch, Start, Center, End };
    
    /// Primary flow direction.
    enum Direction { H, V };
    
    /// Flow vs. Grid mode.
	enum FGLMode   : byte { Flow, Grid };
	/// Scrolling policy for internal ScrollBars frame.
	enum FGLScroll : byte { AutoScroll, VerticalOnly, HorizontalOnly, None };
	

    //-------------------------------------------------------------------------
    // Style (theme data-only; no heap; read by Paint)
    //-------------------------------------------------------------------------
    struct Style : ChStyle<Style> {
        // Container geometry.
        int   padding = DPI(8);     ///< Inner padding on all sides.
        int   spacing = DPI(6);     ///< Gap between neighboring items/lines.

        // Group headers (drawn above clusters when enabled).
        bool  group_header   = false;
        int   group_header_h = DPI(22);
        bool  group_divider  = false;

        // Cluster rounded box cosmetics.
        bool  cluster_box_default = false;     ///< Draw a box for clusters by default.
        Color cluster_box_bg      = Blend(SColorFace(), SColorPaper(), 40);
        Color cluster_box_border  = SColorShadow();
        int   cluster_box_radius  = DPI(8);
        int   cluster_box_pad     = DPI(6);

        // Control face.
        Color face = SColorFace();

        static const Style& StyleDefault() {
            static Style s;
            return s;
        }
    };

    //-------------------------------------------------------------------------
    // Construction / style
    //-------------------------------------------------------------------------

    /** Create the layout; installs ScrollBars as a frame. */
    FlowGridLayout();

    /** Set Flow vs Grid mode. Triggers relayout. */
    FlowGridLayout& SetMode(FGLMode m)                 { mode = m; Reflow(); return *this; }
    /** Set primary direction. Triggers relayout. */
    FlowGridLayout& SetDirection(Direction d)             { dir = d; Reflow(); return *this; }
    /** Enable/disable wrapping (Flow mode). Triggers relayout. */
    FlowGridLayout& SetWrap(bool on = true)            { wrap = on; Reflow(); return *this; }
    /** Configure automatic vs fixed scroll policy. Updates scrollbars. */
    FlowGridLayout& SetScrollMode(FGLScroll m)         { scroll = m; UpdateScrollbars(); return *this; }
    /** Force a unified (fixed) cell size for all items. Triggers relayout. */
    FlowGridLayout& SetUnifiedItemSize(Size sz, bool on = true) { unified = on; unified_sz = sz; Reflow(); return *this; }

    /** Assign visual style (padding/spacing, headers, cluster boxes). */
    FlowGridLayout& SetStyle(const Style& s)           { style = s; Refresh(); return *this; }
    /** Read current style. */
    const Style&    GetStyle() const                   { return style; }

    //-------------------------------------------------------------------------
    // Flow-like API parity (Inset/Gap/Align/Fixed row/col/debug)
    //-------------------------------------------------------------------------

    /** Set inter-item gap (both axes). */
    FlowGridLayout& SetGap(int px)                     { style.spacing = max(0, px); Reflow(); return *this; }
    /** Set uniform inner padding. */
    FlowGridLayout& SetInset(int all)                  { style.padding = max(0, all); Reflow(); return *this; }
    /** Set symmetric padding (largest wins as an approximation). */
    FlowGridLayout& SetInset(int w, int h)             { style.padding = max(0, max(w, h)); Reflow(); return *this; }
    /** Set per-edge padding (largest wins as an approximation). */
    FlowGridLayout& SetInset(int l, int t, int r, int b){ style.padding = max(0, max(max(l, r), max(t, b))); Reflow(); return *this; }
    /** Force fixed column width (Flow LTR) via unified sizing. */
    FlowGridLayout& SetFixedColumn(int px)             { unified = true; unified_sz.cx = max(1, px); Reflow(); return *this; }
    /** Force fixed row height (Flow TTB) via unified sizing. */
    FlowGridLayout& SetFixedRow(int px)                { unified = true; unified_sz.cy = max(1, px); Reflow(); return *this; }
    /** Set default cross-axis alignment for items. */
    FlowGridLayout& SetAlignItems(Align a)             { align_items = a; Reflow(); return *this; }
    /** Toggle debug overlay. */
    FlowGridLayout& SetDebug(bool on = true)           { debug = on; Refresh(); return *this; }

    //-------------------------------------------------------------------------
    // Throttling (batch inserts)
    //-------------------------------------------------------------------------

    /** Pause automatic relayout (nestable). */
    FlowGridLayout& PauseLayout()                      { ++layout_pause; return *this; }
    /** Resume auto relayout; optionally relayout immediately. */
    FlowGridLayout& ResumeLayout(bool relayout = true) {
        if(layout_pause > 0) --layout_pause;
        if(layout_pause == 0 && (relayout || pending_layout)) { pending_layout = false; RefreshLayout(); }
        return *this;
    }
    /** RAII helper to pause/resume layout while batching. */
    struct PauseScope {
        FlowGridLayout& L; bool relayout;
        PauseScope(FlowGridLayout& l, bool r=true) : L(l), relayout(r) { L.PauseLayout(); }
        ~PauseScope() { L.ResumeLayout(relayout); }
    };

    //-------------------------------------------------------------------------
    // Clusters
    //-------------------------------------------------------------------------

    /** Create a new cluster, returning its id. */
    int  NewCluster();
    /** Change the "current" cluster used by subsequent Add* calls. */
    FlowGridLayout& SetCurrentCluster(int id)         { cur_cluster = id; return *this; }
    /** Allow/forbid wrapping *within* a cluster (false => atomic block). */
    FlowGridLayout& SetClusterFlow(int id, bool on);
    /** Toggle a rounded box for a cluster (style drives look). */
    FlowGridLayout& SetClusterBox(int id, bool on);
    /** Toggle a cluster header; optionally force a box as well. */
    FlowGridLayout& SetClusterHeader(int id, bool on = true, bool with_box = false);
    /** Convenience to set header + box in one call. */
    FlowGridLayout& SetClusterDecor(int id, bool header_on, bool box_on) { return SetClusterHeader(id, header_on, box_on); }

    //-------------------------------------------------------------------------
    // Flow additions
    //-------------------------------------------------------------------------

    /**
     * Add a control to the flow.
     * @param c Control to insert.
     * @param cluster_id Cluster id (-1 = none).
     * @param scale_to_cell If true, control fills its assigned cell.
     * @param fixed If non-zero, overrides min-size unless unified sizing is on.
     * @return Item index.
     */
    int Add(Ctrl& c, int cluster_id = -1, bool scale_to_cell = false, Size fixed = Size(0,0));

    /** Compatibility overload (older signature). */
    int Add(Ctrl& c, int cluster_id, int /*segment_id_unused*/, bool scale_to_cell, Size fixed) {
        return Add(c, cluster_id, scale_to_cell, fixed);
    }

    /** Add a spacer with min/max pixels on the main axis. */
    int AddSpacer(int min_px = 0, int max_px = INT_MAX, int cluster_id = -1);
    /** Add an expanding gap (weight shares leftover on the main axis). */
    int AddExpand(int weight = 1, int cluster_id = -1);
    /** Add a fixed-pixel gap on the main axis. */
    int AddGap(int px, int cluster_id = -1);
    /** Insert a hard line/column break (Flow mode). */
    int AddBreak(int cluster_id = -1);

    //-------------------------------------------------------------------------
    // Grid additions (row/col addressing; simple MVP)
    //-------------------------------------------------------------------------

    /** Add a control to a grid cell (row, col). */
    int AddGrid(Ctrl& c, int row, int col, bool scale_to_cell = false, Size fixed = Size(0,0));
    /** Reserve a blank grid cell (affects row/col measurement). */
    int AddBlankGrid(int row, int col);

    //-------------------------------------------------------------------------
    // Headers and selection
    //-------------------------------------------------------------------------

    /** Enable group headers globally (per-cluster can override). */
    FlowGridLayout& SetGroupHeaders(bool on = true)    { default_cluster_header = on; Refresh(); return *this; }
    /** Provide header text callback (cluster id -> text). */
    FlowGridLayout& WhenClusterText(Upp::Function<Upp::String(int)> fn) { when_group_text = pick(fn); Refresh(); return *this; }
    /** Alias for WhenClusterText. */
    FlowGridLayout& WhenGroupText(Upp::Function<Upp::String(int)> fn)   { when_group_text = pick(fn); Refresh(); return *this; }

    /** Return current selection (virtual mode stub). */
    const Upp::Vector<int>& GetSelection() const       { return selection; }
    /** Clear selection and repaint. */
    void ClearSelection()                              { selection.Clear(); Refresh(); }

    //-------------------------------------------------------------------------
    // Ctrl overrides and sizing helpers
    //-------------------------------------------------------------------------

    /** Perform layout; dispatches Grid vs Flow and computes content size. */
    void Layout() override;

    /** Paint background, clusters, headers, and optional debug overlay. */
    void Paint(Upp::Draw& w) override;


	/** Conservative natural size.
	    - Flow LTR + wrap: reports height-for-width using a conservative width.
	    - Flow TTB: sums item heights; width is max child width.
	    - Flow LTR (no wrap): sums item widths; height is max child height.
	    - Grid: uses measured rows/cols envelope.
	    Includes inner padding on both axes. */
	Upp::Size GetMinSize() const override;

    /** Observable content size (useful for parents). */
    Upp::Size GetContentSize() const { return content; }

    /** Optional height-for-width probe (includes padding). */
    int MeasureHeightForWidth(int total_width);

    /** Notifies on content size changes. */
    Upp::Function<void(Upp::Size)> WhenContentSize;
    Upp::String ToString() const;


private:
    //----- Internal model -----------------------------------------------------

    enum class Kind : byte { CtrlItem, Spacer, Expander, Gap, GridCell, BlankGrid, Break };

    struct Item : Moveable<Item> {
        Kind  kind = Kind::CtrlItem;
        int   cluster = -1;         // cluster id (keep-together unit)
        Ctrl* ctrl = nullptr;
        bool  scale_to_cell = false;
        Size  fixed = Size(0,0);    // overrides min size unless unified is on
        int   min_px = 0;           // spacer/gap min
        int   max_px = INT_MAX;     // spacer max
        int   weight = 0;           // expander weight
        int   row = -1, col = -1;   // grid addressing
        Rect  rect;                 // computed cell area
        bool  visible = true;
    };

    struct Cluster : Moveable<Cluster> {
        bool box  = false;      // draw rounded box (style-driven)
        bool flow = false;      // allow wrapping inside cluster
        int8 header = -1;       // -1 inherit, 0 off, 1 on
        Rect bounds;            // union of child cell rects
    };

    static inline bool IsBreak   (const Item& it) { return it.kind == Kind::Break; }
    static inline bool IsSpacer  (const Item& it) { return it.kind == Kind::Spacer; }
    static inline bool IsGap     (const Item& it) { return it.kind == Kind::Gap; }
    static inline bool IsExpander(const Item& it) { return it.kind == Kind::Expander; }
    static inline bool IsCtrl    (const Item& it) { return it.kind == Kind::CtrlItem || it.kind == Kind::GridCell; }
    static inline bool IsGridLike(const Item& it) { return it.kind == Kind::GridCell || it.kind == Kind::BlankGrid; }
    static inline bool IsFlowRenderable(const Item& it) { return !(IsGridLike(it) || IsBreak(it)); }
    
    // Config/state
    FGLMode  mode   = FGLMode::Flow;
    Direction   dir    = Direction::H;
    FGLScroll scroll = FGLScroll::AutoScroll;
    bool     wrap   = true;
    bool     unified = false;
    Size     unified_sz = Size(0,0);

    Align    align_items = Stretch;
    bool     debug = false;

    // Throttling / reentrancy guards
    bool laying_out = false;
    bool updating_sb = false;
    int  layout_pause = 0;
    bool pending_layout = false;

    // Content reporting
    Upp::Size last_reported_content{0, 0};

    Vector<Item>    items;
    Vector<Cluster> clusters;
    int             cur_cluster = -1;

    // Headers
    bool default_cluster_header = false;
    Function<String(int)> when_group_text;

    // Selection
    Vector<int> selection;

    // Scrollbars and geometry
    ScrollBars sb;
    Point      origin = Point(0,0);
    Size       content = Size(0,0);

    Style style = Style::StyleDefault();

    // Helpers
    void Reflow()                  { if(layout_pause == 0) RefreshLayout(); else pending_layout = true; }
    void UpdateScrollbars();
    void ApplyScrollbars();

    // Flow passes
    void LayoutHorizontal();
    void LayoutVertical();

    // Measurement helpers
    Size NaturalItemSize(const Item& it) const;
    int  EnsureCluster(int cluster);

    // Painting helpers
    void PaintClusters(Upp::Draw& w);
    void PaintGroupHeader(Upp::Draw& w, const Upp::Rect& r, int cluster_id);
    void PaintClusterHeaders(Upp::Draw& w);
    void DebugPaint(Upp::Draw& w);
};

} // namespace Upp

#endif // _FlowGridLayout_FlowGridLayout_h_
