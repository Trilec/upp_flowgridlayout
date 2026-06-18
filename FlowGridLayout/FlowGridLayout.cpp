#include "FlowGridLayout.h"

namespace Upp {

//==============================================================================
// Construction / public surface
//==============================================================================

/** Constructor: sets face, installs ScrollBars frame, wires callbacks. */
FlowGridLayout::FlowGridLayout() {
    Transparent(false);
    AddFrame(sb);
    sb.WhenScroll << [=]{
        // Avoid re-entrancy while frames are recalculating.
        PostCallback([=]{ ApplyScrollbars(); });
    };
    sb.WhenLeftClick << [=]{ SetFocus(); };
}

/** Create and return a new cluster id. */
int FlowGridLayout::NewCluster() {
    int id = clusters.GetCount();
    clusters.Add(Cluster());
    return id;
}

/** Ensure cluster index exists; return normalized id or -1 for "none". */
int FlowGridLayout::EnsureCluster(int id) {
    if(id < 0) return -1;
    while(id >= clusters.GetCount())
        clusters.Add(Cluster());
    return id;
}

/** Allow or forbid wrapping inside a specific cluster. */
FlowGridLayout& FlowGridLayout::SetClusterFlow(int id, bool on) {
    id = EnsureCluster(id);
    if(id >= 0) { clusters[id].flow = on; Reflow(); }
    return *this;
}

/** Toggle rounded box for a cluster (style drives look). */
FlowGridLayout& FlowGridLayout::SetClusterBox(int id, bool on) {
    id = EnsureCluster(id);
    if(id >= 0) { clusters[id].box = on; Refresh(); }
    return *this;
}

/** Toggle a cluster header and optionally a box as a convenience. */
FlowGridLayout& FlowGridLayout::SetClusterHeader(int id, bool on, bool with_box) {
    id = EnsureCluster(id);
    if(id >= 0) {
        clusters[id].header = on ? 1 : 0;
        if(with_box)
            clusters[id].box = true;
        Refresh();
    }
    return *this;
}

/** Add a control to the flow, optionally bound to a cluster. */
int FlowGridLayout::Add(Ctrl& c, int cluster_id, bool scale_to_cell, Size fixed) {
    Item& it = items.Add();
    it.kind         = Kind::CtrlItem;
    it.ctrl         = &c;
    it.cluster      = EnsureCluster(cluster_id);
    it.scale_to_cell= scale_to_cell;
    it.fixed        = fixed;

    // Add as child control via base class to avoid our overload.
    Ctrl::Add(c);

    Reflow();
    return items.GetCount() - 1;
}

/** Add a spacer with min/max pixels along the main axis. */
int FlowGridLayout::AddSpacer(int min_px, int max_px, int cluster_id) {
    Item& it = items.Add();
    it.kind = Kind::Spacer;
    it.cluster = EnsureCluster(cluster_id);
    it.min_px = min_px;
    it.max_px = max_px;
    Reflow();
    return items.GetCount()-1;
}

/** Add an expanding gap (weight shares leftover main-axis space). */
int FlowGridLayout::AddExpand(int weight, int cluster_id) {
    Item& it = items.Add();
    it.kind = Kind::Expander;
    it.cluster = EnsureCluster(cluster_id);
    it.weight = max(1, weight);
    Reflow();
    return items.GetCount()-1;
}

/** Add a fixed-pixel gap along the main axis. */
int FlowGridLayout::AddGap(int px, int cluster_id) {
    Item& it = items.Add();
    it.kind = Kind::Gap;
    it.cluster = EnsureCluster(cluster_id);
    it.min_px = it.max_px = max(0, px);
    Reflow();
    return items.GetCount()-1;
}

/** Insert a hard line/column break (Flow mode). */
int FlowGridLayout::AddBreak(int cluster_id) {
    Item& it = items.Add();
    it.kind    = Kind::Break;
    it.cluster = EnsureCluster(cluster_id);
    Reflow();
    return items.GetCount() - 1;
}

/** Add a control to the grid at (row, col). */
int FlowGridLayout::AddGrid(Ctrl& c, int row, int col, bool scale_to_cell, Size fixed) {
    Item& it = items.Add();
    it.kind         = Kind::GridCell;
    it.ctrl         = &c;
    it.row          = row;
    it.col          = col;
    it.scale_to_cell= scale_to_cell;
    it.fixed        = fixed;

    Ctrl::Add(c);

    Reflow();
    return items.GetCount() - 1;
}

/** Reserve a blank grid cell (affects row/col measurement). */
int FlowGridLayout::AddBlankGrid(int row, int col) {
    Item& it = items.Add();
    it.kind = Kind::BlankGrid;
    it.row = row;
    it.col = col;
    Reflow();
    return items.GetCount()-1;
}

//==============================================================================
// Layout and scrollbars
//==============================================================================


/**
 * Conservative natural size.
 * In Flow LTR + wrap: compute height-for-width using current width, or a
 * DPI(240) fallback if width is not yet known.
 * In Flow TTB: sum child heights (+gaps), width = max child width.
 * In Flow LTR (no wrap): sum child widths (+gaps), height = max child height.
 * In Grid: envelope of measured row heights and column widths.
 * Always includes padding.
 */
Size FlowGridLayout::GetMinSize() const {
    // ---------- Grid envelope ----------
    if(mode == FGLMode::Grid) {
        // Measure rows/cols as Layout() does (but without touching children).
        int maxrow = -1, maxcol = -1;
        for(const Item& it : items)
            if(it.kind == Kind::GridCell || it.kind == Kind::BlankGrid) {
                maxrow = max(maxrow, it.row);
                maxcol = max(maxcol, it.col);
            }

        const int rows = maxrow + 1;
        const int cols = max(maxcol + 1, 0);

        Vector<int> colw, rowh;
        colw.SetCount(cols, 0);
        rowh.SetCount(max(0, rows), 0);

        for(const Item& it : items)
            if(it.kind == Kind::GridCell) {
                Size ns = NaturalItemSize(it);
                colw[it.col] = max(colw[it.col], ns.cx);
                rowh[it.row] = max(rowh[it.row], ns.cy);
            }

        int sumw = 0, sumh = 0;
        for(int c = 0; c < colw.GetCount(); ++c) {
            if(c) sumw += style.spacing;
            sumw += colw[c];
        }
        for(int r = 0; r < rowh.GetCount(); ++r) {
            if(r) sumh += style.spacing;
            sumh += rowh[r];
        }

        return Size(sumw + 2*style.padding, sumh + 2*style.padding);
    }

    // ---------- Flow envelope ----------
    const int gap = style.spacing;

    auto IsFlowRenderable = [&](const Item& it)->bool {
        // skip Grid/BlankGrid entirely; Break is not a renderable item
        return !(it.kind == Kind::GridCell || it.kind == Kind::BlankGrid || it.kind == Kind::Break);
    };

    auto NaturalW = [&](const Item& it)->int {
        if(it.kind == Kind::Spacer || it.kind == Kind::Gap)   return it.min_px;
        if(it.kind == Kind::Expander)                          return 0;
        return NaturalItemSize(it).cx;
    };
    auto NaturalH = [&](const Item& it)->int {
        if(it.kind == Kind::Spacer || it.kind == Kind::Gap)   return it.min_px;
        if(it.kind == Kind::Expander)                          return 0;
        return NaturalItemSize(it).cy;
    };

    // Flow, Left-to-right, wrapping: height-for-width probe like FlowBox.
    if(dir == Direction::H && wrap) {
        int eff_total_w = GetSize().cx;
        if(eff_total_w <= 0) {
            // fallback: a conservative width that avoids silly tall estimates
            eff_total_w = DPI(240);
        }
        // MeasureHeightForWidth is non-const; mirror FlowBox with const_cast.
        int h = const_cast<FlowGridLayout*>(this)->MeasureHeightForWidth(eff_total_w);
        if(h < 0) h = 0;

        // Width baseline: report current width if any, else the conservative fallback.
        int baseline_w = GetSize().cx;
        if(baseline_w <= 0) baseline_w = DPI(240);
        return Size(baseline_w, h);
    }

    // Flow, Top-to-bottom (stack): sum heights (+gaps), width = max width.
    if(dir == Direction::V) {
        int sumh = 0, maxw = 0, count = 0;
        for(const Item& it : items) {
            if(!IsFlowRenderable(it)) continue;
            if(count) sumh += gap;
            sumh += NaturalH(it);
            maxw = max(maxw, NaturalW(it));
            ++count;
        }
        return Size(maxw + 2*style.padding, sumh + 2*style.padding);
    }

    // Flow, Left-to-right, no wrap: sum widths (+gaps), height = max height.
    {
        int sumw = 0, maxh = 0, count = 0;
        for(const Item& it : items) {
            if(!IsFlowRenderable(it)) continue;
            if(count) sumw += gap;
            sumw += NaturalW(it);
            maxh = max(maxh, NaturalH(it));
            ++count;
        }
        return Size(sumw + 2*style.padding, maxh + 2*style.padding);
    }
}

/** Compute a natural size for an item (unified or control min / fixed). */
Size FlowGridLayout::NaturalItemSize(const Item& it) const {
    if(unified)
        return unified_sz;
    if(it.kind == Kind::CtrlItem || it.kind == Kind::GridCell) {
        Size ms = it.ctrl ? it.ctrl->GetMinSize() : Size(0,0);
        if(it.fixed.cx > 0 || it.fixed.cy > 0)
            ms = it.fixed;
        return ms;
    }
    if(it.kind == Kind::Spacer || it.kind == Kind::Gap) {
        return dir == Direction::H ? Size(it.min_px, DPI(1)) : Size(DPI(1), it.min_px);
    }
    if(it.kind == Kind::Expander) {
        return dir == Direction::H ? Size(0, DPI(1)) : Size(DPI(1), 0);
    }
    return Size(0,0);
}

/** Show/hide and configure scrollbars based on content vs view. */
void FlowGridLayout::UpdateScrollbars() {
    if(updating_sb)
        return;
    updating_sb = true;

    // Decide target visibility for X/Y given a page size.
    auto Decide = [&](const Size& page, bool& wantx, bool& wanty) {
        switch(scroll) {
        case FGLScroll::None:          wantx = false; wanty = false; break;
        case FGLScroll::VerticalOnly:  wantx = false; wanty = true;  break;
        case FGLScroll::HorizontalOnly:wantx = true;  wanty = false; break;
        case FGLScroll::AutoScroll:
        default:
            wantx = content.cx > page.cx;
            wanty = content.cy > page.cy;
            break;
        }
    };

    // Seek a stable visibility in at most two passes (frame affects view).
    for(int pass = 0; pass < 2; ++pass) {
        Size page = GetView().GetSize();
        bool wantx = false, wanty = false;
        Decide(page, wantx, wanty);
        sb.ShowX(wantx);
        sb.ShowY(wanty);
    }

    // Final page after visibility settles
    Size page = GetView().GetSize();

    // Clamp origin and set bars
    Point p = origin;
    const int maxx = max(0, content.cx - page.cx);
    const int maxy = max(0, content.cy - page.cy);
    p.x = minmax(p.x, 0, maxx);
    p.y = minmax(p.y, 0, maxy);
    origin = p;

    if(scroll == FGLScroll::None) {
        sb.HideX(); sb.HideY();
        origin = Point(0,0);
        updating_sb = false;
        return;
    }

    // ScrollBars::Set expects (pos, page, total) in newer U++.
    sb.Set(p, page, content);

    updating_sb = false;
}

/** Apply scrollbar thumbs to origin; repaint only (no relayout). */
void FlowGridLayout::ApplyScrollbars() {
    const Size page = GetView().GetSize();
    Point p = sb.Get();

    const int maxx = max(0, content.cx - page.cx);
    const int maxy = max(0, content.cy - page.cy);
    p.x = minmax(p.x, 0, maxx);
    p.y = minmax(p.y, 0, maxy);

    if(p != origin) {
        origin = p;
        Refresh();
    }
}

/** Layout dispatcher: Grid vs Flow; computes content and updates scrollbars. */
void FlowGridLayout::Layout() {
    if(laying_out)
        return;
    laying_out = true;

    Rect r = GetView();
    r.Deflate(style.padding);

    if(mode == FGLMode::Grid) {
        //----- Grid: measure columns/rows, then place cells -------------------
        int maxrow = -1, maxcol = -1;
        for(const Item& it : items)
            if(it.kind == Kind::GridCell || it.kind == Kind::BlankGrid) {
                maxrow = max(maxrow, it.row);
                maxcol = max(maxcol, it.col);
            }

        const int rows = maxrow + 1, cols = max(maxcol + 1, 0);
        Vector<int> colw, rowh;
        colw.SetCount(cols, 0);
        rowh.SetCount(max(0, rows), 0);

        // Natural widths/heights per column/row
        for(const Item& it : items)
            if(it.kind == Kind::GridCell) {
                Size ns = NaturalItemSize(it);
                colw[it.col] = max(colw[it.col], ns.cx);
                rowh[it.row] = max(rowh[it.row], ns.cy);
            }

        // Add spacing between columns/rows
        for(int c = 0; c < colw.GetCount(); ++c) if(c) colw[c] += style.spacing;
        for(int rr = 0; rr < rowh.GetCount(); ++rr) if(rr) rowh[rr] += style.spacing;

        // Place cells
        for(int i = 0; i < items.GetCount(); ++i) {
            Item& it = items[i];
            if(it.kind != Kind::GridCell)
                continue;

            int px = r.left;  for(int c = 0; c < it.col; ++c) px += colw[c];
            int py = r.top;   for(int rr = 0; rr < it.row; ++rr) py += rowh[rr];

            Size cell( colw[it.col] - (it.col ? style.spacing : 0),
                       rowh[it.row] - (it.row ? style.spacing : 0) );

            it.rect = RectC(px, py, cell.cx, cell.cy); // cell area

            // Control size: either scaled to cell or natural clamped to cell
            Size want = it.scale_to_cell ? cell : NaturalItemSize(it);
            want.cx = min(want.cx, cell.cx);
            want.cy = min(want.cy, cell.cy);

            if(it.ctrl)
                it.ctrl->SetRect(px - origin.x, py - origin.y, want.cx, want.cy);
        }

        // Content size: sum columns/rows + padding on both sides
        int totalw = 0, totalh = 0;
        for(int c = 0; c < colw.GetCount(); ++c) totalw += colw[c];
        for(int rr = 0; rr < rowh.GetCount(); ++rr) totalh += rowh[rr];
        content = Size(totalw + 2 * style.padding, totalh + 2 * style.padding);
    }
    else {
        //----- Flow -----------------------------------------------------------
        if(dir == Direction::H)
            LayoutHorizontal();
        else
            LayoutVertical();
        // content is set in the flow passes
    }

    // Notify on content change
    if(content != last_reported_content) {
        last_reported_content = content;
        if(WhenContentSize)
            PostCallback([=]{ WhenContentSize(content); });
    }

    laying_out = false;
    UpdateScrollbars();
}

//==============================================================================
// Painting
//==============================================================================

/** Fill rounded rect, approximated (fast; no anti-alias). */
static inline void FillRoundedRect(Draw& w, Rect r, int rad, Color col) {
    if(rad <= 0) { w.DrawRect(r, col); return; }
    int rx = min(rad, r.GetWidth()  / 2);
    int ry = min(rad, r.GetHeight() / 2);

    // center and sides
    w.DrawRect(Rect(r.left + rx, r.top, r.right - rx, r.bottom), col);
    w.DrawRect(Rect(r.left, r.top + ry, r.left + rx, r.bottom - ry), col);
    w.DrawRect(Rect(r.right - rx, r.top + ry, r.right, r.bottom - ry), col);

    // corners
    w.DrawEllipse(Rect(r.left,           r.top,            2*rx, 2*ry), col);
    w.DrawEllipse(Rect(r.right - 2*rx,   r.top,            2*rx, 2*ry), col);
    w.DrawEllipse(Rect(r.left,           r.bottom - 2*ry,  2*rx, 2*ry), col);
    w.DrawEllipse(Rect(r.right - 2*rx,   r.bottom - 2*ry,  2*rx, 2*ry), col);
}

/** Paint a cluster rounded box using Style colors. */
static inline void PaintClusterBox(Draw& w, const Rect& r, const FlowGridLayout::Style& st) {
    FillRoundedRect(w, r, st.cluster_box_radius, st.cluster_box_border);
    Rect inner = r.Deflated(1);
    int  inner_rad = max(0, st.cluster_box_radius - 1);
    FillRoundedRect(w, inner, inner_rad, st.cluster_box_bg);
}

/** Paint rounded boxes for clusters that request one. */
void FlowGridLayout::PaintClusters(Draw& w) {
    for(const Cluster& c : clusters) {
        if(!(c.box || style.cluster_box_default) || c.bounds.IsEmpty()) continue;
        Rect r = c.bounds.Inflated(style.cluster_box_pad);
        r.Offset(-origin);
        PaintClusterBox(w, r, style);
    }
}

/** Paint a single cluster header band and optional divider. */
void FlowGridLayout::PaintGroupHeader(Draw& w, const Rect& r, int cluster_id) {
    if(!style.group_header) return;
    String txt = when_group_text ? when_group_text(cluster_id)
                                 : String().Cat() << "Cluster " << cluster_id;
    Rect hr = r;
    hr.bottom = hr.top + style.group_header_h;
    hr.Offset(-origin);
    w.DrawRect(hr, Blend(style.face, SColorHighlight(), 10));
    w.DrawText(hr.left + DPI(8), hr.top + (hr.GetHeight() - GetStdFontCy())/2,
               txt, StdFont(), SColorText());
    if(style.group_divider) {
        Rect dl = hr;
        dl.top = dl.bottom - DPI(1);
        w.DrawRect(dl, SColorShadow());
    }
}

/** Walk clusters and paint headers (if enabled by style/cluster override). */
void FlowGridLayout::PaintClusterHeaders(Draw& w) {
    if(!style.group_header || style.group_header_h <= 0)
        return;

    for(int i = 0; i < clusters.GetCount(); ++i) {
        const Cluster& c = clusters[i];
        if(c.bounds.IsEmpty())
            continue;

        // Effective header: per-cluster or default
        bool show = (c.header >= 0) ? (c.header != 0) : default_cluster_header;
        if(!show)
            continue;

        Rect r = c.bounds;
        r.top   -= style.group_header_h + DPI(2);
        r.bottom = r.top + style.group_header_h;

        PaintGroupHeader(w, r, i);
    }
}


/** Paint face, cluster boxes, headers, and (optional) debug overlay. */
void FlowGridLayout::Paint(Draw& w) {
    w.DrawRect(GetSize(), style.face);
    PaintClusters(w);
    PaintClusterHeaders(w);
    DebugPaint(w);
}

//==============================================================================
// Flow passes (LeftToRight / TopToBottom)
//==============================================================================

/** Flow pass for LeftToRight direction (wrap-aware). Computes content size. */
void FlowGridLayout::LayoutHorizontal() {
    Rect vr = GetView();  
    vr.Deflate(style.padding);
    int x = vr.left, y = vr.top;
    int line_h = 0;

    for(Cluster& cl : clusters) cl.bounds = Rect(0,0,0,0);

    // Commit a laid-out line [from, to)
    auto CommitLine = [&](int from, int to, int free_px) {
        // distribute to spacers
        int count_sp = 0; for(int i=from;i<to;i++) if(items[i].kind==Kind::Spacer) count_sp++;
        if(count_sp) {
            for(int i=from;i<to;i++) if(items[i].kind==Kind::Spacer) {
                int grow = min(items[i].max_px - items[i].min_px, free_px / max(count_sp,1));
                items[i].rect.SetSize(Size(items[i].min_px + max(0,grow), line_h));
                free_px -= max(0,grow);
            }
        }
        // expanders proportionally
        int wsum = 0; for(int i=from;i<to;i++) if(items[i].kind==Kind::Expander) wsum += max(1, items[i].weight);
        if(wsum > 0 && free_px > 0) {
            for(int i=from;i<to;i++) if(items[i].kind==Kind::Expander) {
                int got = free_px * max(1, items[i].weight) / wsum;
                items[i].rect.SetSize(Size(got, line_h));
            }
        }
        // place cells and controls
        int lx = vr.left;
        for(int i=from;i<to;i++) {
            Item& it = items[i];
            if(it.kind == Kind::Break) continue; // nothing to render

            // Cell width (pre-sized by Spacer/Expander SetSize or natural)
            Size ns_base = it.rect.GetSize();
            if(ns_base.cx == 0 || ns_base.cy == 0) {
                Size nat = NaturalItemSize(it);
                ns_base = Size((ns_base.cx ? ns_base.cx : nat.cx), line_h);
            }
            Rect cell = RectC(lx, y, ns_base.cx, line_h);
            it.rect = cell; // keep union basis for cluster bounds

            // Control rectangle within 'cell' based on cross-axis alignment
            if(it.ctrl) {
                Size want = it.scale_to_cell ? cell.GetSize() : NaturalItemSize(it);
                want.cx = min(want.cx, cell.GetWidth());
                want.cy = min(want.cy, cell.GetHeight());

                Rect cr = cell;
                if(it.scale_to_cell) {
                    cr = cell;
                } else {
                    switch(align_items) {
                        case Stretch:
                            cr.top = cell.top;
                            cr.bottom = cell.bottom;
                            cr.left = cell.left;
                            cr.right = cell.left + want.cx;
                            break;
                        case Start:
                            cr.top = cell.top;
                            cr.bottom = cell.top + want.cy;
                            cr.left = cell.left;
                            cr.right = cell.left + want.cx;
                            break;
                        case Center:
                            cr.top = cell.top + (cell.GetHeight() - want.cy)/2;
                            cr.bottom = cr.top + want.cy;
                            cr.left = cell.left;
                            cr.right = cell.left + want.cx;
                            break;
                        case End:
                            cr.bottom = cell.bottom;
                            cr.top = cr.bottom - want.cy;
                            cr.left = cell.left;
                            cr.right = cell.left + want.cx;
                            break;
                        case Auto:
                        default:
                            cr.top = cell.top + (cell.GetHeight() - want.cy)/2;
                            cr.bottom = cr.top + want.cy;
                            cr.left = cell.left;
                            cr.right = cell.left + want.cx;
                            break;
                    }
                }
                it.ctrl->SetRect(cr.left - origin.x, cr.top - origin.y,
                                 cr.GetWidth(), cr.GetHeight());
            }

            if(it.cluster >= 0) {
                Cluster& cl = clusters[it.cluster];
                cl.bounds = cl.bounds.IsEmpty() ? cell : (cl.bounds | cell);
            }
            lx += cell.GetWidth() + style.spacing;
        }
    };

    int line_start = 0;
    int used_w = 0;
    line_h = 0;

    auto NaturalW = [&](const Item& it)->int {
        Size ns = NaturalItemSize(it);
        if(it.kind==Kind::Spacer)   return it.min_px;
        if(it.kind==Kind::Gap)      return it.min_px;
        if(it.kind==Kind::Expander) return 0;
        return ns.cx;
    };

    for(int i=0;i<items.GetCount();++i) {
        Item& it = items[i];
        if(it.kind==Kind::GridCell || it.kind==Kind::BlankGrid) continue;

        // Hard break commits current line if there's content.
        if(it.kind == Kind::Break) {
            if(i > line_start) {
                int free_px = (vr.right - vr.left) - (used_w ? (used_w - style.spacing) : 0);
                CommitLine(line_start, i, max(0, free_px));
                y += line_h + style.spacing;
                x = vr.left;
                line_h = 0;
                line_start = i + 1;
                used_w = 0;
            } else {
                line_start = i + 1;
            }
            continue;
        }

        // Atomic cluster (no internal wrap)
        if(it.cluster >= 0 && !clusters[it.cluster].flow) {
            int j=i, cw=0, ch=0;
            while(j<items.GetCount() && items[j].cluster==it.cluster &&
                 !(items[j].kind==Kind::GridCell||items[j].kind==Kind::BlankGrid||items[j].kind==Kind::Break)) {
                cw += NaturalW(items[j]);
                ch = max(ch, NaturalItemSize(items[j]).cy);
                if(j>i) cw += style.spacing;
                j++;
            }
            if(wrap && (x + cw > vr.right+1) && i>line_start) {
                int free_px = (vr.right - vr.left) - (used_w ? (used_w - style.spacing) : 0);
                CommitLine(line_start, i, max(0, free_px));
                y += line_h + style.spacing;
                x = vr.left;
                line_h = 0;
                line_start = i;
                used_w = 0;
            }
            for(int k=i;k<j;k++) {
                Size ns = NaturalItemSize(items[k]);
                items[k].rect = RectC(0,0, ns.cx, max(ns.cy, ch));
                used_w += ns.cx + (k>i?style.spacing:0);
                line_h = max(line_h, ns.cy);
            }
            x += cw + style.spacing;
            i = j-1;
            continue;
        }

        Size ns = NaturalItemSize(it);
        int needw = NaturalW(it);
        if(wrap && x != vr.left && (x + needw > vr.right+1)) {
            int free_px = (vr.right - vr.left) - (used_w ? (used_w - style.spacing) : 0);
            CommitLine(line_start, i, max(0, free_px));
            y += line_h + style.spacing;
            x = vr.left;
            line_h = 0;
            line_start = i;
            used_w = 0;
        }
        it.rect = RectC(0,0, needw, ns.cy); // temp; finalized in CommitLine
        used_w += needw + (i>line_start?style.spacing:0);
        line_h = max(line_h, ns.cy);
        x += needw + style.spacing;
    }

    if(line_start < items.GetCount()) {
        int free_px = (vr.right - vr.left) - (used_w ? (used_w - style.spacing) : 0);
        CommitLine(line_start, items.GetCount(), max(0, free_px));
    }

    Rect cb = Rect(vr.left, vr.top, vr.left, vr.top);
    bool first = true;
    for(const Item& it : items) {
        if(it.kind==Kind::GridCell||it.kind==Kind::BlankGrid) continue;
        if(first) { cb = it.rect; first=false; }
        else cb |= it.rect;
    }
    if(first) cb = RectC(vr.left, vr.top, 0, 0);
    cb.Inflate(style.padding);
    content = cb.GetSize();
}

/** Flow pass for TopToBottom direction (wrap-aware). Computes content size. */
void FlowGridLayout::LayoutVertical() {
    Rect vr = GetView();
    vr.Deflate(style.padding);
    int x = vr.left, y = vr.top;
    int line_w = 0;

    for(Cluster& cl : clusters) cl.bounds = Rect(0,0,0,0);

    // Commit a laid-out column [from, to)
    auto CommitCol = [&](int from, int to, int free_px) {
        int count_sp = 0; for(int i=from;i<to;i++) if(items[i].kind==Kind::Spacer) count_sp++;
        if(count_sp) {
            for(int i=from;i<to;i++) if(items[i].kind==Kind::Spacer) {
                int grow = min(items[i].max_px - items[i].min_px, free_px / max(count_sp,1));
                items[i].rect.SetSize(Size(line_w, items[i].min_px + max(0,grow)));
                free_px -= max(0,grow);
            }
        }
        int wsum = 0; for(int i=from;i<to;i++) if(items[i].kind==Kind::Expander) wsum += max(1, items[i].weight);
        if(wsum > 0 && free_px > 0) {
            for(int i=from;i<to;i++) if(items[i].kind==Kind::Expander) {
                int got = free_px * max(1, items[i].weight) / wsum;
                items[i].rect.SetSize(Size(line_w, got));
            }
        }
        int ly = vr.top;
        for(int i=from;i<to;i++) {
            Item& it = items[i];
            if(it.kind == Kind::Break) continue;

            Size ns_base = it.rect.GetSize();
            if(ns_base.cx == 0 || ns_base.cy == 0) {
                Size nat = NaturalItemSize(it);
                ns_base = Size(line_w, (ns_base.cy ? ns_base.cy : nat.cy));
            }
            Rect cell = RectC(x, ly, line_w, ns_base.cy);
            it.rect = cell;

            if(it.ctrl) {
                Size want = it.scale_to_cell ? cell.GetSize() : NaturalItemSize(it);
                want.cx = min(want.cx, cell.GetWidth());
                want.cy = min(want.cy, cell.GetHeight());

                Rect cr = cell;
                if(it.scale_to_cell) {
                    cr = cell;
                } else {
                    switch(align_items) {
                        case Stretch:
                            cr.left = cell.left;
                            cr.right= cell.right;
                            cr.top  = cell.top;
                            cr.bottom = cell.top + want.cy;
                            break;
                        case Start:
                            cr.left = cell.left;
                            cr.right= cell.left + want.cx;
                            cr.top  = cell.top;
                            cr.bottom = cell.top + want.cy;
                            break;
                        case Center:
                            cr.left = cell.left + (cell.GetWidth() - want.cx)/2;
                            cr.right= cr.left + want.cx;
                            cr.top  = cell.top;
                            cr.bottom = cell.top + want.cy;
                            break;
                        case End:
                            cr.right = cell.right;
                            cr.left  = cr.right - want.cx;
                            cr.top   = cell.top;
                            cr.bottom= cell.top + want.cy;
                            break;
                        case Auto:
                        default:
                            cr.left = cell.left + (cell.GetWidth() - want.cx)/2;
                            cr.right= cr.left + want.cx;
                            cr.top  = cell.top;
                            cr.bottom = cell.top + want.cy;
                            break;
                    }
                }

                it.ctrl->SetRect(cr.left - origin.x, cr.top - origin.y,
                                 cr.GetWidth(), cr.GetHeight());
            }

            if(it.cluster >= 0) {
                Cluster& cl = clusters[it.cluster];
                cl.bounds = cl.bounds.IsEmpty() ? cell : (cl.bounds | cell);
            }
            ly += cell.GetHeight() + style.spacing;
        }
    };

    int col_start = 0;
    int used_h = 0;
    line_w = 0;

    auto NaturalH = [&](const Item& it)->int {
        Size ns = NaturalItemSize(it);
        if(it.kind==Kind::Spacer)   return it.min_px;
        if(it.kind==Kind::Gap)      return it.min_px;
        if(it.kind==Kind::Expander) return 0;
        return ns.cy;
    };

    for(int i=0;i<items.GetCount();++i) {
        Item& it = items[i];
        if(it.kind==Kind::GridCell || it.kind==Kind::BlankGrid) continue;

        if(it.kind == Kind::Break) {
            if(i > col_start) {
                int free_px = (vr.bottom - vr.top) - (used_h ? (used_h - style.spacing) : 0);
                CommitCol(col_start, i, max(0, free_px));
                x += line_w + style.spacing;
                y = vr.top;
                col_start = i + 1;
                used_h = 0;
                line_w = 0;
            } else {
                col_start = i + 1;
            }
            continue;
        }

        if(it.cluster >= 0 && !clusters[it.cluster].flow) {
            int j=i, ch=0, cw=0;
            while(j<items.GetCount() && items[j].cluster==it.cluster &&
                 !(items[j].kind==Kind::GridCell||items[j].kind==Kind::BlankGrid||items[j].kind==Kind::Break)) {
                ch += NaturalH(items[j]);
                cw = max(cw, NaturalItemSize(items[j]).cx);
                if(j>i) ch += style.spacing;
                j++;
            }
            if(wrap && (y + ch > vr.bottom+1) && i>col_start) {
                int free_px = (vr.bottom - vr.top) - (used_h ? (used_h - style.spacing) : 0);
                CommitCol(col_start, i, max(0, free_px));
                x += line_w + style.spacing;
                y = vr.top;
                col_start = i;
                used_h = 0;
                line_w = 0;
            }
            for(int k=i;k<j;k++) {
                Size ns = NaturalItemSize(items[k]);
                items[k].rect = RectC(0,0, max(cw, ns.cx), ns.cy);
                used_h += ns.cy + (k>i?style.spacing:0);
                line_w = max(line_w, ns.cx);
            }
            y += ch + style.spacing;
            i = j-1;
            continue;
        }

        Size ns = NaturalItemSize(it);
        int needh = NaturalH(it);
        if(wrap && y != vr.top && (y + needh > vr.bottom+1)) {
            int free_px = (vr.bottom - vr.top) - (used_h ? (used_h - style.spacing) : 0);
            CommitCol(col_start, i, max(0, free_px));
            x += line_w + style.spacing;
            y = vr.top;
            col_start = i;
            used_h = 0;
            line_w = 0;
        }
        it.rect = RectC(0,0, ns.cx, needh);
        used_h += needh + (i>col_start?style.spacing:0);
        line_w = max(line_w, ns.cx);
        y += needh + style.spacing;
    }

    if(col_start < items.GetCount()) {
        int free_px = (GetView().GetHeight() - 2*style.padding) - (used_h ? (used_h - style.spacing) : 0);
        CommitCol(col_start, items.GetCount(), max(0, free_px));
    }

    Rect cb(0,0,0,0); bool first=true;
    for(const Item& it : items) {
        if(it.kind==Kind::GridCell||it.kind==Kind::BlankGrid) continue;
        if(first) { cb = it.rect; first=false; }
        else cb |= it.rect;
    }
    if(first) cb = RectC(style.padding, style.padding, 0, 0);
    cb.Inflate(style.padding);
    content = cb.GetSize();
}

//==============================================================================
// Height-for-width probe
//==============================================================================

/**
 * Compute natural total height for a given total width (including padding).
 * - Flow LTR: simulate wrapping using NaturalItemSize and spacing/padding.
 * - Flow TTB: width has little effect; returns content height for current data.
 * - Grid: independent of width; returns measured grid height for current items.
 * This method is a *probe*: it does not change child rects or scroll state.
 */
int FlowGridLayout::MeasureHeightForWidth(int total_width) {
    if(total_width <= 0)
        return 0;

    const int inner_w = max(0, total_width - 2*style.padding);

    // Grid: height is just the grid measurement, independent of width
    if(mode == FGLMode::Grid) {
        // emulate the grid measurement part of Layout()
        int maxrow = -1, maxcol = -1;
        for(const Item& it : items)
            if(it.kind == Kind::GridCell || it.kind == Kind::BlankGrid) {
                maxrow = max(maxrow, it.row);
                maxcol = max(maxcol, it.col);
            }
        const int rows = maxrow + 1;
        Vector<int> rowh; rowh.SetCount(max(0, rows), 0);
        for(const Item& it : items)
            if(it.kind == Kind::GridCell) {
                Size ns = NaturalItemSize(it);
                rowh[it.row] = max(rowh[it.row], ns.cy);
            }
        int totalh = 0;
        for(int rr = 0; rr < rowh.GetCount(); ++rr) totalh += rowh[rr] + (rr ? style.spacing : 0);
        return totalh + 2*style.padding;
    }

    // Flow TopToBottom: width does not affect vertical packing much; approximate
    if(dir == Direction::V) {
        // Stack vertically until height sum (with spacing) – ignore column wraps
        int hsum = 0, count = 0;
        for(const Item& it : items) {
            if(it.kind==Kind::GridCell || it.kind==Kind::BlankGrid || it.kind==Kind::Break) continue;
            if(it.kind==Kind::Expander) continue; // expanders need container height
            if(it.kind==Kind::Spacer || it.kind==Kind::Gap) { hsum += it.min_px; if(count) hsum += style.spacing; ++count; continue; }
            Size ns = NaturalItemSize(it);
            if(count) hsum += style.spacing;
            hsum += ns.cy;
            ++count;
        }
        return hsum + 2*style.padding;
    }

    // Flow LeftToRight: simulate rows
    int x = 0, y = 0, line_h = 0;
    auto NaturalW = [&](const Item& it)->int {
        Size ns = NaturalItemSize(it);
        if(it.kind==Kind::Spacer)   return it.min_px;
        if(it.kind==Kind::Gap)      return it.min_px;
        if(it.kind==Kind::Expander) return 0;
        return ns.cx;
    };

    auto Newline = [&](){
        y += line_h;
        if(y > 0) y += style.spacing;
        x = 0;
        line_h = 0;
    };

    for(int i=0;i<items.GetCount();++i) {
        const Item& it = items[i];
        if(it.kind==Kind::GridCell || it.kind==Kind::BlankGrid) continue;

        if(it.kind == Kind::Break) {
            if(x > 0 || line_h > 0) Newline();
            continue;
        }

        // Atomic cluster
        if(it.cluster >= 0 && !clusters[it.cluster].flow) {
            int j=i, cw=0, ch=0; bool first=false;
            while(j<items.GetCount() && items[j].cluster==it.cluster &&
                 !(items[j].kind==Kind::GridCell||items[j].kind==Kind::BlankGrid||items[j].kind==Kind::Break)) {
                if(first) cw += style.spacing;
                first = true;
                cw += NaturalW(items[j]);
                ch = max(ch, NaturalItemSize(items[j]).cy);
                j++;
            }
            if(wrap && x>0 && x + cw > inner_w) Newline();
            x += cw;
            line_h = max(line_h, ch);
            i = j-1;
            if(x < inner_w) x += style.spacing;
            continue;
        }

        const int wneed = NaturalW(it);
        const int hneed = (it.kind==Kind::Spacer || it.kind==Kind::Gap) ? 0 : NaturalItemSize(it).cy;

        if(wrap && x>0 && x + wneed > inner_w) Newline();
        x += wneed;
        line_h = max(line_h, hneed);
        if(x < inner_w) x += style.spacing;
    }
    if(line_h > 0) y += line_h;
    return y + 2*style.padding;
}
// FlowGridLayout.cpp — Debug overlay now uses predicates (no behavior change)

void FlowGridLayout::DebugPaint(Upp::Draw& w) {
    if(!debug) return;

    // View and inner content rect
    Rect view = GetView();
    Rect inner = view.Deflated(style.padding);
    w.DrawRect(view.left,  view.top,      view.GetWidth(),    1, SColorDisabled());
    w.DrawRect(view.left,  view.bottom-1, view.GetWidth(),    1, SColorDisabled());
    w.DrawRect(view.left,  view.top,      1,                  view.GetHeight(), SColorDisabled());
    w.DrawRect(view.right-1, view.top,    1,                  view.GetHeight(), SColorDisabled());

    w.DrawRect(inner.left, inner.top,     inner.GetWidth(),   1, SColorShadow());
    w.DrawRect(inner.left, inner.bottom-1,inner.GetWidth(),   1, SColorShadow());
    w.DrawRect(inner.left, inner.top,     1,                  inner.GetHeight(), SColorShadow());
    w.DrawRect(inner.right-1, inner.top,  1,                  inner.GetHeight(), SColorShadow());

    // Item cell rects (skip Grid-like cells and Break markers)
    for(const Item& it : items) {
        if(IsGridLike(it) || IsBreak(it)) continue;
        Rect r = it.rect; r.Offset(-origin);
        Color c = SColorHighlight();
        w.DrawRect(r.left, r.top, r.GetWidth(), 1, c);
        w.DrawRect(r.left, r.bottom-1, r.GetWidth(), 1, c);
        w.DrawRect(r.left, r.top, 1, r.GetHeight(), c);
        w.DrawRect(r.right-1, r.top, 1, r.GetHeight(), c);
    }
}

String FlowGridLayout::ToString() const {
    String s;
    s << "FlowGridLayout{"
      << "mode=" << (mode == FGLMode::Flow ? "Flow" : "Grid")
      << ", dir=" << (dir == Direction::H ? "H" : "V")
      << ", wrap=" << (wrap ? "true" : "false")
      << ", gap=" << style.spacing
      << ", padding=" << style.padding
      << ", unified=" << (unified ? AsString(unified_sz) : String("off"))
      << ", items=" << items.GetCount()
      << ", clusters=" << clusters.GetCount()
      << ", content=(" << content.cx << "x" << content.cy << ")"
      << ", debug=" << (debug ? "on" : "off")
      << "}";
    return s;
}

} // namespace Upp
