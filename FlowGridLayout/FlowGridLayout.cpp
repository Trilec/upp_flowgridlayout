#include "FlowGridLayout.h"

namespace Upp {

// ---------------------- Public surface ---------------------------------------

FlowGridLayout::FlowGridLayout() {
    Transparent(false);
    AddFrame(sb);
    sb.WhenScroll << [=]{
        // avoid re-entrancy from inside layout/frames
        PostCallback([=]{ ApplyScrollbars(); });
    };
    sb.WhenLeftClick << [=]{ SetFocus(); };
}


int FlowGridLayout::NewCluster() {
    int id = clusters.GetCount();
    clusters.Add(Cluster());
    return id;
}

int FlowGridLayout::EnsureCluster(int id) {
    if(id < 0) return -1;
    while(id >= clusters.GetCount())
        clusters.Add(Cluster());
    return id;
}

FlowGridLayout& FlowGridLayout::SetClusterFlow(int id, bool on) {
    id = EnsureCluster(id);
    if(id >= 0) { clusters[id].flow = on; Reflow(); }
    return *this;
}

FlowGridLayout& FlowGridLayout::SetClusterBox(int id, bool on) {
    id = EnsureCluster(id);
    if(id >= 0) { clusters[id].box = on; Refresh(); }
    return *this;
}

FlowGridLayout& FlowGridLayout::SetClusterHeader(int id, bool on, bool with_box) {
    id = EnsureCluster(id);
    if(id >= 0) {
        clusters[id].header = on ? 1 : 0;   // per-cluster override
        if(with_box)
            clusters[id].box = true;        // convenience toggle
        Refresh();
    }
    return *this;
}

int FlowGridLayout::Add(Ctrl& c, int cluster_id, bool scale_to_cell, Size fixed) {
    Item& it = items.Add();
    it.kind         = Kind::CtrlItem;
    it.ctrl         = &c;
    it.cluster      = EnsureCluster(cluster_id);
    it.scale_to_cell= scale_to_cell;
    it.fixed        = fixed;

    // IMPORTANT: add as child control using base-class Add, not our overload
    Ctrl::Add(c);

    Reflow();
    return items.GetCount() - 1;
}


int FlowGridLayout::AddSpacer(int min_px, int max_px, int cluster_id) {
    Item& it = items.Add();
    it.kind = Kind::Spacer;
    it.cluster = EnsureCluster(cluster_id);
    it.min_px = min_px;
    it.max_px = max_px;
    Reflow();
    return items.GetCount()-1;
}

int FlowGridLayout::AddExpander(int weight, int cluster_id) {
    Item& it = items.Add();
    it.kind = Kind::Expander;
    it.cluster = EnsureCluster(cluster_id);
    it.weight = max(1, weight);
    Reflow();
    return items.GetCount()-1;
}

int FlowGridLayout::AddGap(int px, int cluster_id) {
    Item& it = items.Add();
    it.kind = Kind::Gap;
    it.cluster = EnsureCluster(cluster_id);
    it.min_px = it.max_px = max(0, px);
    Reflow();
    return items.GetCount()-1;
}

int FlowGridLayout::AddGrid(Ctrl& c, int row, int col, bool scale_to_cell, Size fixed) {
    Item& it = items.Add();
    it.kind         = Kind::GridCell;
    it.ctrl         = &c;
    it.row          = row;
    it.col          = col;
    it.scale_to_cell= scale_to_cell;
    it.fixed        = fixed;

    // IMPORTANT: add as child control using base-class Add, not our overload
    Ctrl::Add(c);

    Reflow();
    return items.GetCount() - 1;
}


int FlowGridLayout::AddBlankGrid(int row, int col) {
    Item& it = items.Add();
    it.kind = Kind::BlankGrid;
    it.row = row;
    it.col = col;
    Reflow();
    return items.GetCount()-1;
}

// ----------------------- Layout & Paint --------------------------------------

Size FlowGridLayout::NaturalItemSize(const Item& it) const {
    if(unified)
        return unified_sz;
    if(it.kind == Kind::CtrlItem || it.kind == Kind::GridCell) {
        Size ms = it.ctrl ? it.ctrl->GetMinSize() : Size(0,0);
        if(it.fixed.cx > 0 || it.fixed.cy > 0) // use fixed if provided
            ms = it.fixed;
        return ms;
    }
    if(it.kind == Kind::Spacer || it.kind == Kind::Gap) {
        return dir == FGLDir::LeftToRight ? Size(it.min_px, DPI(1)) : Size(DPI(1), it.min_px);
    }
    if(it.kind == Kind::Expander) {
        return dir == FGLDir::LeftToRight ? Size(0, DPI(1)) : Size(DPI(1), 0);
    }
    return Size(0,0);
}

void FlowGridLayout::UpdateScrollbars() {
    if(updating_sb)
        return;
    updating_sb = true;

    // Helper: decide desired visibility from a given page size
    auto Decide = [&](const Size& page, bool& wantx, bool& wanty) {
        switch(scroll) {
        case FGLScroll::None:
            wantx = false; wanty = false;
            break;
        case FGLScroll::VerticalOnly:
            wantx = false; wanty = true;
            break;
        case FGLScroll::HorizontalOnly:
            wantx = true;  wanty = false;
            break;
        case FGLScroll::Auto:
        default:
            wantx = content.cx > page.cx;
            wanty = content.cy > page.cy;
            break;
        }
    };

    // Try to reach a stable X/Y visibility in at most two passes.
    for(int pass = 0; pass < 2; ++pass) {
        Size page = GetView().GetSize();
        bool wantx = false, wanty = false;
        Decide(page, wantx, wanty);
        sb.ShowX(wantx);
        sb.ShowY(wanty);
        // After ShowX/ShowY, the frame layout changes -> GetView() changes.
        // Loop once more so visibility can settle using the new page size.
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

    sb.Set(p, page, content);

    updating_sb = false;
}


void FlowGridLayout::ApplyScrollbars() {
    // Read current thumb, clamp to content/page, repaint only (no relayout here).
    const Size page = GetView().GetSize();
    Point p = sb.Get();

    const int maxx = max(0, content.cx - page.cx);
    const int maxy = max(0, content.cy - page.cy);
    p.x = minmax(p.x, 0, maxx);
    p.y = minmax(p.y, 0, maxy);

    if(p != origin) {
        origin = p;
        Refresh(); // repaint with new origin
    }
}


void FlowGridLayout::Layout() {
    if(laying_out)
        return;                 // guard against re-entrancy from frame changes
    laying_out = true;

    // Work in client/view area (ScrollBars are a frame and shrink GetView()).
    Rect r = GetView();
    r.Deflate(style.padding);

    if(mode == FGLMode::Grid) {
        // ---- Grid pass: measure column widths / row heights, then place cells
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

            // Effective cell size without the leading spacing on this row/col
            Size cell( colw[it.col] - (it.col ? style.spacing : 0),
                       rowh[it.row] - (it.row ? style.spacing : 0) );

            it.rect = RectC(px, py, cell.cx, cell.cy);

            // Control size: either scaled to cell or natural clamped to cell
            Size want = it.scale_to_cell ? cell : NaturalItemSize(it);
            want.cx = min(want.cx, cell.cx);
            want.cy = min(want.cy, cell.cy);

            if(it.ctrl)
                it.ctrl->SetRect(px - origin.x, py - origin.y, want.cx, want.cy);
        }

        // Content size = sum of column widths / row heights + padding on both sides
        int totalw = 0, totalh = 0;
        for(int c = 0; c < colw.GetCount(); ++c) totalw += colw[c];
        for(int rr = 0; rr < rowh.GetCount(); ++rr) totalh += rowh[rr];
        content = Size(totalw + 2 * style.padding, totalh + 2 * style.padding);
    }
    else {
        // ---- Flow pass (delegates to horizontal/vertical)
        if(dir == FGLDir::LeftToRight)
            DoFlowLayoutH();
        else
            DoFlowLayoutV();
        // NOTE: DoFlowLayoutH/V compute 'content' based on laid-out items.
    }

    // ---- Notify content change (for parents stacking toolbars, etc.)
    if(content != last_reported_content) {
        last_reported_content = content;
        if(WhenContentSize)
            PostCallback([=]{ WhenContentSize(content); });
    }

    laying_out = false;
    UpdateScrollbars();
}




void FlowGridLayout::DoFlowLayoutH() {
    Rect vr = GetView();  
    vr.Deflate(style.padding);
    int x = vr.left, y = vr.top;
    int line_h = 0;

    for(Cluster& cl : clusters) cl.bounds = Rect(0,0,0,0);

    auto CommitLine = [&](int from, int to, int free_px) {
        // spacers to max
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
        // place
        int lx = vr.left;
        for(int i=from;i<to;i++) {
            Item& it = items[i];
            Size ns = it.rect.GetSize();
            if(ns.cx == 0 || ns.cy == 0) {
                ns = it.rect.GetSize().IsEmpty() ? Size(NaturalItemSize(it).cx, line_h) : ns;
            }
            Rect rr = RectC(lx, y, ns.cx, line_h);
            if(ns.cy < line_h) rr.top += (line_h - ns.cy)/2, rr.bottom = rr.top + ns.cy;
            it.rect = rr;
            if(it.ctrl) {
                Size want = it.scale_to_cell ? rr.GetSize() : NaturalItemSize(it);
                want.cx = min(want.cx, rr.GetWidth());
                want.cy = min(want.cy, rr.GetHeight());
                it.ctrl->SetRect(rr.left - origin.x, rr.top - origin.y, want.cx, want.cy);
            }
            if(it.cluster >= 0) {
                Cluster& cl = clusters[it.cluster];
                cl.bounds = cl.bounds.IsEmpty() ? it.rect : (cl.bounds | it.rect);
            }
            lx += ns.cx + style.spacing;
        }
    };

    int line_start = 0;
    int used_w = 0;
    line_h = 0;

    auto NaturalW = [&](const Item& it)->int {
        Size ns = NaturalItemSize(it);
        if(it.kind==Kind::Spacer) return it.min_px;
        if(it.kind==Kind::Gap)    return it.min_px;
        if(it.kind==Kind::Expander) return 0;
        return ns.cx;
    };

    for(int i=0;i<items.GetCount();++i) {
        Item& it = items[i];
        if(it.kind==Kind::GridCell || it.kind==Kind::BlankGrid) continue;

        // atomic cluster?
        if(it.cluster >= 0 && !clusters[it.cluster].flow) {
            int j=i, cw=0, ch=0;
            while(j<items.GetCount() && items[j].cluster==it.cluster &&
                 !(items[j].kind==Kind::GridCell||items[j].kind==Kind::BlankGrid)) {
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
        it.rect = RectC(0,0, needw, ns.cy);
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

void FlowGridLayout::DoFlowLayoutV() {
    Rect vr = GetView();
    vr.Deflate(style.padding);
    int x = vr.left, y = vr.top;
    int line_w = 0;

    for(Cluster& cl : clusters) cl.bounds = Rect(0,0,0,0);

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
            Size ns = it.rect.GetSize();
            if(ns.cx == 0 || ns.cy == 0) {
                ns = it.rect.GetSize().IsEmpty() ? Size(line_w, NaturalItemSize(it).cy) : ns;
            }
            Rect rr = RectC(x, ly, line_w, ns.cy);
            if(ns.cx < line_w) rr.left += (line_w - ns.cx)/2, rr.right = rr.left + ns.cx;
            it.rect = rr;
            if(it.ctrl) {
                Size want = it.scale_to_cell ? rr.GetSize() : NaturalItemSize(it);
                want.cx = min(want.cx, rr.GetWidth());
                want.cy = min(want.cy, rr.GetHeight());
                it.ctrl->SetRect(rr.left - origin.x, rr.top - origin.y, want.cx, want.cy);
            }
            if(it.cluster >= 0) {
                Cluster& cl = clusters[it.cluster];
                cl.bounds = cl.bounds.IsEmpty() ? it.rect : (cl.bounds | it.rect);
            }
            ly += ns.cy + style.spacing;
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

        if(it.cluster >= 0 && !clusters[it.cluster].flow) {
            int j=i, ch=0, cw=0;
            while(j<items.GetCount() && items[j].cluster==it.cluster &&
                 !(items[j].kind==Kind::GridCell||items[j].kind==Kind::BlankGrid)) {
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


// --- Rounded rect fill using Draw only (no Painter dependency) ----------------

static inline void FillRoundedRect(Draw& w, Rect r, int rad, Color col) {
    if(rad <= 0) { w.DrawRect(r, col); return; }
    int rx = min(rad, r.GetWidth()  / 2);
    int ry = min(rad, r.GetHeight() / 2);

    // center and side rectangles
    w.DrawRect(Rect(r.left + rx, r.top, r.right - rx, r.bottom), col);
    w.DrawRect(Rect(r.left, r.top + ry, r.left + rx, r.bottom - ry), col);
    w.DrawRect(Rect(r.right - rx, r.top + ry, r.right, r.bottom - ry), col);

    // four quarter-ellipses (filled)
    w.DrawEllipse(Rect(r.left,           r.top,            2*rx, 2*ry), col); // TL
    w.DrawEllipse(Rect(r.right - 2*rx,   r.top,            2*rx, 2*ry), col); // TR
    w.DrawEllipse(Rect(r.left,           r.bottom - 2*ry,  2*rx, 2*ry), col); // BL
    w.DrawEllipse(Rect(r.right - 2*rx,   r.bottom - 2*ry,  2*rx, 2*ry), col); // BR
}

static inline void PaintClusterBox(Draw& w, const Rect& r, const FlowGridLayoutStyle& st) {
    // Simulate a 1px border: draw border-colored rounded rect, then inset fill
    FillRoundedRect(w, r, st.cluster_box_radius, st.cluster_box_border);
    Rect inner = r.Deflated(1);
    int  inner_rad = max(0, st.cluster_box_radius - 1);
    FillRoundedRect(w, inner, inner_rad, st.cluster_box_bg);
}

void FlowGridLayout::PaintClusters(Draw& w) {
    for(const Cluster& c : clusters) {
        // if cluster requests a box OR default says boxes are on, and bounds exist
        if(!(c.box || style.cluster_box_default) || c.bounds.IsEmpty()) continue;
        Rect r = c.bounds.Inflated(style.cluster_box_pad);
        r.Offset(-origin);
        PaintClusterBox(w, r, style);
    }
}

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

void FlowGridLayout::PaintClusterHeaders(Draw& w)
{
    if(!style.group_header || style.group_header_h <= 0)
        return;

    for(int i = 0; i < clusters.GetCount(); ++i) {
        const Cluster& c = clusters[i];
        if(c.bounds.IsEmpty())
            continue;

        // Resolve effective header flag: per-cluster override or default
        bool show = (c.header >= 0) ? (c.header != 0) : default_cluster_header;
        if(!show)
            continue;

        Rect r = c.bounds;
        r.top   -= style.group_header_h + DPI(2);
        r.bottom = r.top + style.group_header_h;

        PaintGroupHeader(w, r, i);
    }
}

void FlowGridLayout::Paint(Draw& w) {
    w.DrawRect(GetSize(), style.face);

    // Underlays (rounded boxes) – style-driven
    PaintClusters(w);

    // Headers – per-cluster or default flag decides visibility
    PaintClusterHeaders(w);
}

} // namespace Upp
