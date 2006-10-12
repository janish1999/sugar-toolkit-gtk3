/* 
 * Copyright (C) 2006 Red Hat, Inc
 *
 * Sugar is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Sugar is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#include <gtk/gtkentry.h>

#include "sugar-address-entry.h"

enum {
	PROP_0,
	PROP_PROGRESS
};

typedef enum {
  CURSOR_STANDARD,
  CURSOR_DND
} CursorType;

static void _gtk_entry_effective_inner_border (GtkEntry  *entry,
                                   			   GtkBorder *border);
static void  get_text_area_size				  (GtkEntry *entry,
                    						   gint     *x,
							                   gint     *y,
						                       gint     *width,
						                       gint     *height);

G_DEFINE_TYPE(SugarAddressEntry, sugar_address_entry, GTK_TYPE_ENTRY)

static GQuark quark_inner_border   = 0;
static const GtkBorder default_inner_border = { 2, 2, 2, 2 };

static void
draw_insertion_cursor (GtkEntry      *entry,
		       GdkRectangle  *cursor_location,
		       gboolean       is_primary,
		       PangoDirection direction,
		       gboolean       draw_arrow)
{
  GtkWidget *widget = GTK_WIDGET (entry);
  GtkTextDirection text_dir;

  if (direction == PANGO_DIRECTION_LTR)
    text_dir = GTK_TEXT_DIR_LTR;
  else
    text_dir = GTK_TEXT_DIR_RTL;

  gtk_draw_insertion_cursor (widget, entry->text_area, NULL,
			     cursor_location,
			     is_primary, text_dir, draw_arrow);
}

static void
gtk_entry_get_pixel_ranges (GtkEntry  *entry,
			    gint     **ranges,
			    gint      *n_ranges)
{
  gint start_char, end_char;

  if (gtk_editable_get_selection_bounds (GTK_EDITABLE (entry), &start_char, &end_char))
    {
      //PangoLayout *layout = gtk_entry_ensure_layout (entry, TRUE);
      PangoLayout *layout = gtk_entry_get_layout (entry);
      PangoLayoutLine *line = pango_layout_get_lines (layout)->data;
      const char *text = pango_layout_get_text (layout);
      gint start_index = g_utf8_offset_to_pointer (text, start_char) - text;
      gint end_index = g_utf8_offset_to_pointer (text, end_char) - text;
      gint real_n_ranges, i;

      pango_layout_line_get_x_ranges (line, start_index, end_index, ranges, &real_n_ranges);

      if (ranges)
	{
	  gint *r = *ranges;
	  
	  for (i = 0; i < real_n_ranges; ++i)
	    {
	      r[2 * i + 1] = (r[2 * i + 1] - r[2 * i]) / PANGO_SCALE;
	      r[2 * i] = r[2 * i] / PANGO_SCALE;
	    }
	}
      
      if (n_ranges)
	*n_ranges = real_n_ranges;
    }
  else
    {
      if (n_ranges)
	*n_ranges = 0;
      if (ranges)
	*ranges = NULL;
    }
}

static void
gtk_entry_get_cursor_locations (GtkEntry   *entry,
				CursorType  type,
				gint       *strong_x,
				gint       *weak_x)
{
  if (!entry->visible && !entry->invisible_char)
    {
      if (strong_x)
	*strong_x = 0;
      
      if (weak_x)
	*weak_x = 0;
    }
  else
    {
      //PangoLayout *layout = gtk_entry_ensure_layout (entry, TRUE);
      PangoLayout *layout = gtk_entry_get_layout (entry);
      const gchar *text = pango_layout_get_text (layout);
      PangoRectangle strong_pos, weak_pos;
      gint index;
  
      if (type == CURSOR_STANDARD)
	{
	  index = g_utf8_offset_to_pointer (text, entry->current_pos + entry->preedit_cursor) - text;
	}
      else /* type == CURSOR_DND */
	{
	  index = g_utf8_offset_to_pointer (text, entry->dnd_position) - text;

	  if (entry->dnd_position > entry->current_pos)
	    {
	      if (entry->visible)
		index += entry->preedit_length;
	      else
		{
		  gint preedit_len_chars = g_utf8_strlen (text, -1) - entry->text_length;
		  index += preedit_len_chars * g_unichar_to_utf8 (entry->invisible_char, NULL);
		}
	    }
	}
      
      pango_layout_get_cursor_pos (layout, index, &strong_pos, &weak_pos);
      
      if (strong_x)
	*strong_x = strong_pos.x / PANGO_SCALE;
      
      if (weak_x)
	*weak_x = weak_pos.x / PANGO_SCALE;
    }
}

static void
gtk_entry_draw_cursor (GtkEntry  *entry,
		       CursorType type)
{
  GdkKeymap *keymap = gdk_keymap_get_for_display (gtk_widget_get_display (GTK_WIDGET (entry)));
  PangoDirection keymap_direction = gdk_keymap_get_direction (keymap);
  
  if (GTK_WIDGET_DRAWABLE (entry))
    {
      GtkWidget *widget = GTK_WIDGET (entry);
      GdkRectangle cursor_location;
      gboolean split_cursor;

      GtkBorder inner_border;
      gint xoffset;
      gint strong_x, weak_x;
      gint text_area_height;
      PangoDirection dir1 = PANGO_DIRECTION_NEUTRAL;
      PangoDirection dir2 = PANGO_DIRECTION_NEUTRAL;
      gint x1 = 0;
      gint x2 = 0;

      _gtk_entry_effective_inner_border (entry, &inner_border);

      xoffset = inner_border.left - entry->scroll_offset;

      gdk_drawable_get_size (entry->text_area, NULL, &text_area_height);
      
      gtk_entry_get_cursor_locations (entry, type, &strong_x, &weak_x);

      g_object_get (gtk_widget_get_settings (widget),
		    "gtk-split-cursor", &split_cursor,
		    NULL);

      dir1 = entry->resolved_dir;
      
      if (split_cursor)
	{
	  x1 = strong_x;

	  if (weak_x != strong_x)
	    {
	      dir2 = (entry->resolved_dir == PANGO_DIRECTION_LTR) ? PANGO_DIRECTION_RTL : PANGO_DIRECTION_LTR;
	      x2 = weak_x;
	    }
	}
      else
	{
	  if (keymap_direction == entry->resolved_dir)
	    x1 = strong_x;
	  else
	    x1 = weak_x;
	}

      cursor_location.x = xoffset + x1;
      cursor_location.y = inner_border.top;
      cursor_location.width = 0;
      cursor_location.height = text_area_height - inner_border.top - inner_border.bottom;

      draw_insertion_cursor (entry,
			     &cursor_location, TRUE, dir1,
			     dir2 != PANGO_DIRECTION_NEUTRAL);
      
      if (dir2 != PANGO_DIRECTION_NEUTRAL)
	{
	  cursor_location.x = xoffset + x2;
	  draw_insertion_cursor (entry,
				 &cursor_location, FALSE, dir2,
				 TRUE);
	}
    }
}

static void
get_layout_position (GtkEntry *entry,
                     gint     *x,
                     gint     *y)
{
  PangoLayout *layout;
  PangoRectangle logical_rect;
  gint area_width, area_height;
  GtkBorder inner_border;
  gint y_pos;
  PangoLayoutLine *line;
  
//  layout = gtk_entry_ensure_layout (entry, TRUE);
  layout = gtk_entry_get_layout(entry);

  get_text_area_size (entry, NULL, NULL, &area_width, &area_height);
  _gtk_entry_effective_inner_border (entry, &inner_border);

  area_height = PANGO_SCALE * (area_height - inner_border.top - inner_border.bottom);

  line = pango_layout_get_lines (layout)->data;
  pango_layout_line_get_extents (line, NULL, &logical_rect);
  
  /* Align primarily for locale's ascent/descent */
  y_pos = ((area_height - entry->ascent - entry->descent) / 2 + 
           entry->ascent + logical_rect.y);
  
  /* Now see if we need to adjust to fit in actual drawn string */
  if (logical_rect.height > area_height)
    y_pos = (area_height - logical_rect.height) / 2;
  else if (y_pos < 0)
    y_pos = 0;
  else if (y_pos + logical_rect.height > area_height)
    y_pos = area_height - logical_rect.height;
  
  y_pos = inner_border.top + y_pos / PANGO_SCALE;

  if (x)
    *x = inner_border.left - entry->scroll_offset;

  if (y)
    *y = y_pos;
}

static void
_gtk_entry_effective_inner_border (GtkEntry  *entry,
                                   GtkBorder *border)
{
  GtkBorder *tmp_border;

  tmp_border = g_object_get_qdata (G_OBJECT (entry), quark_inner_border);

  if (tmp_border)
    {
      *border = *tmp_border;
      return;
    }

  gtk_widget_style_get (GTK_WIDGET (entry), "inner-border", &tmp_border, NULL);

  if (tmp_border)
    {
      *border = *tmp_border;
      g_free (tmp_border);
      return;
    }

  *border = default_inner_border;
}

static void
gtk_entry_draw_text (GtkEntry *entry)
{
  GtkWidget *widget;
  
  if (!entry->visible && entry->invisible_char == 0)
    return;
  
  if (GTK_WIDGET_DRAWABLE (entry))
    {
      //PangoLayout *layout = gtk_entry_ensure_layout (entry, TRUE);
	  PangoLayout *layout = gtk_entry_get_layout (entry);
      cairo_t *cr;
      gint x, y;
      gint start_pos, end_pos;
      
      widget = GTK_WIDGET (entry);
      
      get_layout_position (entry, &x, &y);

      cr = gdk_cairo_create (entry->text_area);

      cairo_move_to (cr, x, y);
      gdk_cairo_set_source_color (cr, &widget->style->text [widget->state]);
      pango_cairo_show_layout (cr, layout);

      if (gtk_editable_get_selection_bounds (GTK_EDITABLE (entry), &start_pos, &end_pos))
	{
	  gint *ranges;
	  gint n_ranges, i;
          PangoRectangle logical_rect;
	  GdkColor *selection_color, *text_color;
          GtkBorder inner_border;

	  pango_layout_get_pixel_extents (layout, NULL, &logical_rect);
	  gtk_entry_get_pixel_ranges (entry, &ranges, &n_ranges);

	  if (GTK_WIDGET_HAS_FOCUS (entry))
	    {
	      selection_color = &widget->style->base [GTK_STATE_SELECTED];
	      text_color = &widget->style->text [GTK_STATE_SELECTED];
	    }
	  else
	    {
	      selection_color = &widget->style->base [GTK_STATE_ACTIVE];
	      text_color = &widget->style->text [GTK_STATE_ACTIVE];
	    }

          _gtk_entry_effective_inner_border (entry, &inner_border);

	  for (i = 0; i < n_ranges; ++i)
	    cairo_rectangle (cr,
			     inner_border.left - entry->scroll_offset + ranges[2 * i],
			     y,
			     ranges[2 * i + 1],
			     logical_rect.height);

	  cairo_clip (cr);
	  
	  gdk_cairo_set_source_color (cr, selection_color);
	  cairo_paint (cr);

	  cairo_move_to (cr, x, y);
	  gdk_cairo_set_source_color (cr, text_color);
	  pango_cairo_show_layout (cr, layout);
	  
	  g_free (ranges);
	}

      cairo_destroy (cr);
    }
}

static void
_gtk_entry_get_borders (GtkEntry *entry,
			gint     *xborder,
			gint     *yborder)
{
  GtkWidget *widget = GTK_WIDGET (entry);
  gint focus_width;
  gboolean interior_focus;

  gtk_widget_style_get (widget,
			"interior-focus", &interior_focus,
			"focus-line-width", &focus_width,
			NULL);

  if (entry->has_frame)
    {
      *xborder = widget->style->xthickness;
      *yborder = widget->style->ythickness;
    }
  else
    {
      *xborder = 0;
      *yborder = 0;
    }

  if (!interior_focus)
    {
      *xborder += focus_width;
      *yborder += focus_width;
    }
}

static void
get_text_area_size (GtkEntry *entry,
                    gint     *x,
                    gint     *y,
                    gint     *width,
                    gint     *height)
{
  gint xborder, yborder;
  GtkRequisition requisition;
  GtkWidget *widget = GTK_WIDGET (entry);

  gtk_widget_get_child_requisition (widget, &requisition);

  _gtk_entry_get_borders (entry, &xborder, &yborder);

  if (x)
    *x = xborder;

  if (y)
    *y = yborder;
  
  if (width)
    *width = GTK_WIDGET (entry)->allocation.width - xborder * 2;

  if (height)
    *height = requisition.height - yborder * 2;
}

static gint
sugar_address_entry_expose(GtkWidget      *widget,
                 		   GdkEventExpose *event)
{
	GtkEntry *entry = GTK_ENTRY (widget);
	SugarAddressEntry *address_entry = SUGAR_ADDRESS_ENTRY(widget);
	cairo_t *cr;

	if (entry->text_area == event->window) {
		gint area_width, area_height;

		get_text_area_size (entry, NULL, NULL, &area_width, &area_height);

/*      gtk_paint_flat_box (widget->style, entry->text_area,
                          GTK_WIDGET_STATE(widget), GTK_SHADOW_NONE,
                          NULL, widget, "entry_bg",
                          0, 0, area_width, area_height);
*/

		if (address_entry->progress != 0.0 && address_entry->progress != 1.0) {
			int bar_width = area_width * address_entry->progress;

			cr = gdk_cairo_create(entry->text_area);
	        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
	        cairo_rectangle(cr, 0, 0, bar_width, area_height);
			cairo_fill(cr);
			cairo_destroy (cr);
		}
	

      if ((entry->visible || entry->invisible_char != 0) &&
          GTK_WIDGET_HAS_FOCUS (widget) &&
          entry->selection_bound == entry->current_pos && entry->cursor_visible)
        gtk_entry_draw_cursor (GTK_ENTRY (widget), CURSOR_STANDARD);

      if (entry->dnd_position != -1)
        gtk_entry_draw_cursor (GTK_ENTRY (widget), CURSOR_DND);

      gtk_entry_draw_text (GTK_ENTRY (widget));
    } else {
		GtkWidgetClass *parent_class;
		parent_class = GTK_WIDGET_CLASS(sugar_address_entry_parent_class);
		parent_class->expose_event(widget, event);
	}

	return FALSE;
}

static void
sugar_address_entry_set_property (GObject         *object,
                        		  guint            prop_id,
		                          const GValue    *value,
        		                  GParamSpec      *pspec)
{
	SugarAddressEntry *entry = SUGAR_ADDRESS_ENTRY(object);

	switch (prop_id) {
		case PROP_PROGRESS:
			entry->progress = g_value_get_double (value);
		break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
    }
}

static void
sugar_address_entry_get_property(GObject         *object,
								 guint            prop_id,
								 GValue          *value,
								 GParamSpec      *pspec)
{
	SugarAddressEntry *entry = SUGAR_ADDRESS_ENTRY(object);

	switch (prop_id) {
	    case PROP_PROGRESS:
			g_value_set_double(value, entry->progress);
		break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
sugar_address_entry_class_init(SugarAddressEntryClass *klass)
{
	GtkWidgetClass *widget_class = (GtkWidgetClass*)klass;
	GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

	widget_class->expose_event = sugar_address_entry_expose;

	gobject_class->set_property = sugar_address_entry_set_property;
	gobject_class->get_property = sugar_address_entry_get_property;

	quark_inner_border = g_quark_from_static_string ("gtk-entry-inner-border");

	g_object_class_install_property (gobject_class, PROP_PROGRESS,
                                     g_param_spec_double ("progress",
                                                          "Progress",
                                                          "Progress",
                                                          0.0, 1.0, 0.0,
                                                          G_PARAM_READWRITE));

}

static void
sugar_address_entry_init(SugarAddressEntry *entry)
{
	entry->progress = 0.0;
}
