//======================================================================
//  GivRenderer.cc - Paint the giv data through the painter class
//
//  Dov Grobgeld <dov.grobgeld@gmail.com>
//  Tue Nov  6 22:55:53 2007
//----------------------------------------------------------------------

#include "GivRenderer.h"
#include <math.h>

#define COLOR_NONE 0xfffe 

static gboolean
clip_line_to_rectangle(double x0, double y0, double x1, double y1,
                       double rect_x0, double rect_y0, double rect_x1, double rect_y1,
                       /* output */
                       double *cx0, double *cy0, double *cx1, double *cy1);

GivRenderer::GivRenderer(GPtrArray *_datasets,
                         GivPainter& _painter,
                         double _scale_x,
                         double _scale_y,
                         double _shift_x,
                         double _shift_y,
                         double _width,
                         double _height
                         ) : 
    datasets(_datasets),
    painter(_painter),
    scale_x(_scale_x),
    scale_y(_scale_y),
    shift_x(_shift_x),
    shift_y(_shift_y),
    width(_width),
    height(_height)
{
}

void GivRenderer::paint()
{
    const double cs = 1.0/65535;
    
    for (int ds_idx=0; ds_idx<(int)datasets->len; ds_idx++) {
        giv_dataset_t *dataset = (giv_dataset_t*)g_ptr_array_index(datasets, ds_idx);
        if (!dataset->is_visible)
            continue;
        
        painter.set_set_idx(ds_idx);

        // Create negative color values for "invisible" datasets
        double rr = cs*dataset->color.red;
        double gg = cs*dataset->color.green;
        double bb = cs*dataset->color.blue;
        double alpha = cs*dataset->color.alpha;

#if 0
        printf("datasets[%d]->color.pixel = %d\n",
               ds_idx, dataset->color.pixel);
#endif
        if (dataset->color.alpha == COLOR_NONE) 
            painter.set_color(-1,-1,-1);
        else
            painter.set_color(rr,gg,bb,alpha);

        double line_width = dataset->line_width;
        painter.set_line_width(line_width);
        painter.set_dashes(dataset->num_dashes,
                           dataset->dashes);
        GivArrowType arrow = dataset->arrow_type;
        painter.set_arrow(arrow & ARROW_TYPE_START,
                          arrow & ARROW_TYPE_END);
        double old_x=-1, old_y=-1;
        bool need_paint = false;
        bool has_text = false; // Assume by default we don't have text

        // Whether we need a a separate sweep for text
        bool need_check_for_text = !(dataset->do_draw_lines
                                     || dataset->do_draw_marks);

        // Loop three times and draw as follows:
        //    It 0: Draw filled area of polygons.
        //    It 1: Draw contours of polygons and other line graphs
        //    It 2: Draw quiver
        for (int i=0; i<3; i++) {

            if ((i==0 && dataset->do_draw_polygon)
                || (i==1 && dataset->do_draw_lines)
                || (i==2 && dataset->has_quiver)
                ) {
                // Set properties for quiver
                if (i==2) {
                    painter.set_arrow(false, true);
                    painter.set_line_width(2);
                }

                if (i==1 && dataset->do_draw_polygon
                    && !dataset->do_draw_polygon_outline)
                    continue;
                for (int p_idx=0; p_idx<(int)dataset->points->len; p_idx++) {
                    point_t p = g_array_index(dataset->points, point_t, p_idx);

                    double m_x = p.data.point.x * scale_x - shift_x;
                    double m_y = p.data.point.y * scale_y - shift_y;

                    if (i < 2 && p.op == OP_DRAW) {
                        double cx0=old_x, cy0=old_y, cx1=m_x, cy1=m_y;
                        double margin = line_width * 20;

                        // Don't clip polygons
                        if (i==0
                            || clip_line_to_rectangle(old_x, old_y, m_x, m_y,
                                                   -margin,-margin,
                                                   width+margin,height+margin,
                                                   // output
                                                   &cx0, &cy0, &cx1, &cy1)
                            ) {
                            painter.add_line_segment(cx0, cy0, cx1, cy1,
                                                     i==0);
                            need_paint = true;
                        }
                    }
                    else if (p.op == OP_QUIVER) {
                        double qscale = dataset->quiver_scale;
                        double q_x = old_x + p.data.point.x * scale_x * qscale;
                        double q_y = old_y + p.data.point.y * scale_y * qscale;
                        painter.add_line_segment(old_x, old_y, q_x, q_y,
                                                 false);
                        need_paint = true;
                    }
                    else if (p.op == OP_TEXT) 
                        has_text = true;
                    old_x = m_x;
                    old_y = m_y;
                }

                if (i==0) // draw polygon
                    painter.fill();
                if (i==1 && dataset->do_draw_polygon) {
                    if (dataset->outline_color.alpha == COLOR_NONE) 
                        painter.set_color(-1,-1,-1);
                    else {
                        double rr_s = cs*dataset->outline_color.red;
                        double gg_s = cs*dataset->outline_color.green;
                        double bb_s = cs*dataset->outline_color.blue;
                        double alpha_s = cs*dataset->outline_color.alpha;

                        painter.set_color(rr_s,gg_s,bb_s,alpha_s);
                    }
                }
                if (i==2) {
                    // Todo: extract quiver color
                    double rr_s = cs*dataset->quiver_color.red;
                    double gg_s = cs*dataset->quiver_color.green;
                    double bb_s = cs*dataset->quiver_color.blue;
                    double alpha = cs*dataset->quiver_color.alpha;

                    painter.set_color(rr_s,gg_s,bb_s,alpha);
                }
                if (i>=1)
                    painter.stroke();
            }
        }
        if (dataset->do_draw_marks) {
            GivMarkType mark_type = GivMarkType(dataset->mark_type);
            double mark_size_x = dataset->mark_size;
            double mark_size_y = mark_size_x;
            if (dataset->do_scale_marks) {
                mark_size_x *= fabs(scale_x);
                mark_size_y *= fabs(scale_y);
            }
            painter.set_color(rr,gg,bb,alpha);
            // Reset line width as it may have been changed for quiver
            painter.set_line_width(line_width);
            
            for (int p_idx=0; p_idx<(int)dataset->points->len; p_idx++) {
                point_t p = g_array_index(dataset->points, point_t, p_idx);

                if (p.op == OP_QUIVER)
                    continue;
                if (p.op == OP_TEXT) {
                    has_text = true;
                }
                else {
                    double m_x = p.data.point.x * scale_x - shift_x;
                    double m_y = p.data.point.y * scale_y - shift_y;

                    // Crop marks 
                    if (m_x < -mark_size_x || m_x > width+mark_size_x
                        || m_y < -mark_size_y || m_y > height+mark_size_y)
                        continue;
                    painter.add_mark(mark_type,
                                     mark_size_x, mark_size_y,
                                     m_x, m_y);
                    need_paint = true;
                }
                if (need_paint)
                    painter.draw_marks();
            }
        }
        if (need_check_for_text || has_text) {
            if (dataset->font_name)
                painter.set_font(dataset->font_name);
            if (dataset->text_size > 0 || dataset->do_scale_fonts) {
                double scale = 1.0;
                if (dataset->do_scale_fonts)
                    scale = scale_x;
                double font_size = dataset->text_size * scale;
                // A hack when font size has not been set for a scalable
                // font!
                double epsilon = 1e-9;
                if (font_size < epsilon)
                    font_size = 14 * scale;
                painter.set_text_size(font_size);
            }
            painter.set_color(rr,gg,bb,alpha);
            for (int p_idx=0; p_idx<(int)dataset->points->len; p_idx++) {
                point_t p = g_array_index(dataset->points, point_t, p_idx);

                if (p.op == OP_TEXT) {
                    double m_x = p.data.point.x * scale_x - shift_x;
                    double m_y = p.data.point.y * scale_y - shift_y;
                    const char *text = p.data.text_object->string;
                    int text_align = p.data.text_object->text_align;
                    painter.add_text(text, m_x, m_y, text_align, dataset->do_pango_markup);
                }
            }
            painter.fill();
        }
    }
}

static inline gboolean
line_hor_line_intersect(double x0, double y0, double x1, double y1,
                        double line_x0, double line_x1, double line_y,
                        /* output */
                        double *x_cross, double *y_cross)
{
    if (y1 == y0) {
        *y_cross = x0;
        *x_cross = 0; /* Any x is a crossing */
        if (y1 == line_y)
            return TRUE;
        return FALSE;
    }
    
    *y_cross = line_y; /* Obviously! */
    *x_cross = x0 + (x1 - x0)*(line_y - y0)/(y1-y0);
    
    if (y1<y0) {
        double tmp = y0;
        y0=y1;
        y1=tmp;
    }
    
    return (*x_cross >= line_x0 && *x_cross <= line_x1 && *y_cross >= y0 && *y_cross <= y1);
}

static inline gboolean
line_ver_line_intersect(double x0, double y0, double x1, double y1,
                        double line_y0, double line_y1, double line_x,
                        /* output */
                        double *x_cross, double *y_cross)
{
    if (x1 == x0) {
        *x_cross = x0;
        *y_cross = 0; /* Any y is a crossing */
        if (x1 == line_x)
            return TRUE;
        return FALSE;
    }

    
    *x_cross = line_x; /* Obviously! */
    *y_cross = y0 + (y1 - y0)*(line_x - x0)/(x1-x0);

    if (x1<x0) {
        double tmp = x0;
        x0=x1;
        x1=tmp;
    }
    return (*y_cross >= line_y0 && *y_cross <= line_y1 && *x_cross >= x0 && *x_cross <= x1);
}

gboolean
clip_line_to_rectangle(double x0, double y0, double x1, double y1,
                       double rect_x0, double rect_y0, double rect_x1, double rect_y1,
                       /* output */
                       double *cx0, double *cy0, double *cx1, double *cy1)
{
    gboolean z0_inside, z1_inside;
    int num_crosses = 0;
    double cross_x[4], cross_y[4];
    
    /* Trivial tests if the point is outside the window on the same side*/
    if (x0 < rect_x0 && x1 < rect_x0)     return FALSE;
    if (y0 < rect_y0 && y1 < rect_y0)     return FALSE;
    if (x0 > rect_x1 && x1 > rect_x1)     return FALSE;
    if (y0 > rect_y1 && y1 > rect_y1)     return FALSE;

    /* Test if p1 is inside the window */
    z0_inside = (   x0 >= rect_x0 && x0 <= rect_x1
                    && y0 >= rect_y0 && y0 <= rect_y1);

    /* Test if p2 is inside the window */
    z1_inside = (   x1 >= rect_x0 && x1 <= rect_x1
                    && y1 >= rect_y0 && y1 <= rect_y1);

#ifdef DEBUG_CLIP
    printf("z0_inside z1_inside = %d %d  z0=(%d %d)  z1=(%d %d) rect=(%d %d %d %d)\n", z0_inside, z1_inside, (int)x0, (int)y0, (int)x1, (int)y1, (int)rect_x0, (int)rect_y0, (int)rect_x1, (int)rect_y1);
#endif
    
    /* Check if both are inside */
    if (z0_inside && z1_inside) {
        *cx0 = x0; *cy0 = y0;
        *cx1 = x1; *cy1 = y1;
        return TRUE;
    }

    /* Check for line intersection with the four edges */
    if (line_hor_line_intersect(x0, y0, x1, y1,
                                rect_x0, rect_x1, rect_y0,
                                &cross_x[num_crosses], &cross_y[num_crosses]))
        ++num_crosses;

    if (line_hor_line_intersect(x0, y0, x1, y1,
                                rect_x0, rect_x1, rect_y1,
                                &cross_x[num_crosses], &cross_y[num_crosses]))
        ++num_crosses;

    if (line_ver_line_intersect(x0, y0, x1, y1,
                                rect_y0, rect_y1, rect_x0, 
                                &cross_x[num_crosses], &cross_y[num_crosses]))
        ++num_crosses;

    if (line_ver_line_intersect(x0, y0, x1, y1,
                                rect_y0, rect_y1, rect_x1, 
                                &cross_x[num_crosses], &cross_y[num_crosses]))
        ++num_crosses;

#ifdef DEBUG_CLIP
    {
        int i;
        printf("num_crosses = %d\n", num_crosses);
        for (i=0; i<num_crosses; i++) {
            printf("   crossing[%d] = (%.2f %.2f)\n", i, cross_x[i], cross_y[i]);
        }
    }
#endif
    
    if (num_crosses == 0)
        return FALSE;
    else if (num_crosses == 1) {
        if (z0_inside) {
            *cx1 = cross_x[0];
            *cy1 = cross_y[0];
            *cx0 = x0;
            *cy0 = y0;
        } else {
            *cx0 = cross_x[0];
            *cy0 = cross_y[0];
            *cx1 = x1;
            *cy1 = y1;
        }
        return TRUE;
    }
    else {
        *cx0 = cross_x[0];
        *cy0 = cross_y[0];
        *cx1 = cross_x[1];
        *cy1 = cross_y[1];
        return TRUE;
    }
}
