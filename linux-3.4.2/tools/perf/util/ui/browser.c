#include "../util.h"
#include "../cache.h"
#include "../../perf.h"
#include "libslang.h"
#include <newt.h>
#include "ui.h"
#include "util.h"
#include <linux/compiler.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <stdlib.h>
#include <sys/ttydefaults.h>
#include "browser.h"
#include "helpline.h"
#include "keysyms.h"
#include "../color.h"

static int ui_browser__percent_color(struct ui_browser *browser,
				     double percent, bool current)
{
	if (current && (!browser->use_navkeypressed || browser->navkeypressed))
		return HE_COLORSET_SELECTED;
	if (percent >= MIN_RED)
		return HE_COLORSET_TOP;
	if (percent >= MIN_GREEN)
		return HE_COLORSET_MEDIUM;
	return HE_COLORSET_NORMAL;
}

void ui_browser__set_color(struct ui_browser *self __used, int color)
{
	SLsmg_set_color(color);
}

void ui_browser__set_percent_color(struct ui_browser *self,
				   double percent, bool current)
{
	 int color = ui_browser__percent_color(self, percent, current);
	 ui_browser__set_color(self, color);
}

void ui_browser__gotorc(struct ui_browser *self, int y, int x)
{
	SLsmg_gotorc(self->y + y, self->x + x);
}

static struct list_head *
ui_browser__list_head_filter_entries(struct ui_browser *browser,
				     struct list_head *pos)
{
	do {
		if (!browser->filter || !browser->filter(browser, pos))
			return pos;
		pos = pos->next;
	} while (pos != browser->entries);

	return NULL;
}

static struct list_head *
ui_browser__list_head_filter_prev_entries(struct ui_browser *browser,
					  struct list_head *pos)
{
	do {
		if (!browser->filter || !browser->filter(browser, pos))
			return pos;
		pos = pos->prev;
	} while (pos != browser->entries);

	return NULL;
}

void ui_browser__list_head_seek(struct ui_browser *self, off_t offset, int whence)
{
	struct list_head *head = self->entries;
	struct list_head *pos;

	if (self->nr_entries == 0)
		return;

	switch (whence) {
	case SEEK_SET:
		pos = ui_browser__list_head_filter_entries(self, head->next);
		break;
	case SEEK_CUR:
		pos = self->top;
		break;
	case SEEK_END:
		pos = ui_browser__list_head_filter_prev_entries(self, head->prev);
		break;
	default:
		return;
	}

	assert(pos != NULL);

	if (offset > 0) {
		while (offset-- != 0)
			pos = ui_browser__list_head_filter_entries(self, pos->next);
	} else {
		while (offset++ != 0)
			pos = ui_browser__list_head_filter_prev_entries(self, pos->prev);
	}

	self->top = pos;
}

void ui_browser__rb_tree_seek(struct ui_browser *self, off_t offset, int whence)
{
	struct rb_root *root = self->entries;
	struct rb_node *nd;

	switch (whence) {
	case SEEK_SET:
		nd = rb_first(root);
		break;
	case SEEK_CUR:
		nd = self->top;
		break;
	case SEEK_END:
		nd = rb_last(root);
		break;
	default:
		return;
	}

	if (offset > 0) {
		while (offset-- != 0)
			nd = rb_next(nd);
	} else {
		while (offset++ != 0)
			nd = rb_prev(nd);
	}

	self->top = nd;
}

unsigned int ui_browser__rb_tree_refresh(struct ui_browser *self)
{
	struct rb_node *nd;
	int row = 0;

	if (self->top == NULL)
                self->top = rb_first(self->entries);

	nd = self->top;

	while (nd != NULL) {
		ui_browser__gotorc(self, row, 0);
		self->write(self, nd, row);
		if (++row == self->height)
			break;
		nd = rb_next(nd);
	}

	return row;
}

bool ui_browser__is_current_entry(struct ui_browser *self, unsigned row)
{
	return self->top_idx + row == self->index;
}

void ui_browser__refresh_dimensions(struct ui_browser *self)
{
	self->width = SLtt_Screen_Cols - 1;
	self->height = SLtt_Screen_Rows - 2;
	self->y = 1;
	self->x = 0;
}

void ui_browser__handle_resize(struct ui_browser *browser)
{
	ui__refresh_dimensions(false);
	ui_browser__show(browser, browser->title, ui_helpline__current);
	ui_browser__refresh(browser);
}

int ui_browser__warning(struct ui_browser *browser, int timeout,
			const char *format, ...)
{
	va_list args;
	char *text;
	int key = 0, err;

	va_start(args, format);
	err = vasprintf(&text, format, args);
	va_end(args);

	if (err < 0) {
		va_start(args, format);
		ui_helpline__vpush(format, args);
		va_end(args);
	} else {
		while ((key == ui__question_window("Warning!", text,
						   "Press any key...",
						   timeout)) == K_RESIZE)
			ui_browser__handle_resize(browser);
		free(text);
	}

	return key;
}

int ui_browser__help_window(struct ui_browser *browser, const char *text)
{
	int key;

	while ((key = ui__help_window(text)) == K_RESIZE)
		ui_browser__handle_resize(browser);

	return key;
}

bool ui_browser__dialog_yesno(struct ui_browser *browser, const char *text)
{
	int key;

	while ((key = ui__dialog_yesno(text)) == K_RESIZE)
		ui_browser__handle_resize(browser);

	return key == K_ENTER || toupper(key) == 'Y';
}

void ui_browser__reset_index(struct ui_browser *self)
{
	self->index = self->top_idx = 0;
	self->seek(self, 0, SEEK_SET);
}

void __ui_browser__show_title(struct ui_browser *browser, const char *title)
{
	SLsmg_gotorc(0, 0);
	ui_browser__set_color(browser, NEWT_COLORSET_ROOT);
	slsmg_write_nstring(title, browser->width + 1);
}

void ui_browser__show_title(struct ui_browser *browser, const char *title)
{
	pthread_mutex_lock(&ui__lock);
	__ui_browser__show_title(browser, title);
	pthread_mutex_unlock(&ui__lock);
}

int ui_browser__show(struct ui_browser *self, const char *title,
		     const char *helpline, ...)
{
	int err;
	va_list ap;

	ui_browser__refresh_dimensions(self);

	pthread_mutex_lock(&ui__lock);
	__ui_browser__show_title(self, title);

	self->title = title;
	free(self->helpline);
	self->helpline = NULL;

	va_start(ap, helpline);
	err = vasprintf(&self->helpline, helpline, ap);
	va_end(ap);
	if (err > 0)
		ui_helpline__push(self->helpline);
	pthread_mutex_unlock(&ui__lock);
	return err ? 0 : -1;
}

void ui_browser__hide(struct ui_browser *browser __used)
{
	pthread_mutex_lock(&ui__lock);
	ui_helpline__pop();
	pthread_mutex_unlock(&ui__lock);
}

static void ui_browser__scrollbar_set(struct ui_browser *browser)
{
	int height = browser->height, h = 0, pct = 0,
	    col = browser->width,
	    row = browser->y - 1;

	if (browser->nr_entries > 1) {
		pct = ((browser->index * (browser->height - 1)) /
		       (browser->nr_entries - 1));
	}

	SLsmg_set_char_set(1);

	while (h < height) {
	        ui_browser__gotorc(browser, row++, col);
		SLsmg_write_char(h == pct ? SLSMG_DIAMOND_CHAR : SLSMG_CKBRD_CHAR);
		++h;
	}

	SLsmg_set_char_set(0);
}

static int __ui_browser__refresh(struct ui_browser *browser)
{
	int row;
	int width = browser->width;

	row = browser->refresh(browser);
	ui_browser__set_color(browser, HE_COLORSET_NORMAL);

	if (!browser->use_navkeypressed || browser->navkeypressed)
		ui_browser__scrollbar_set(browser);
	else
		width += 1;

	SLsmg_fill_region(browser->y + row, browser->x,
			  browser->height - row, width, ' ');

	return 0;
}

int ui_browser__refresh(struct ui_browser *browser)
{
	pthread_mutex_lock(&ui__lock);
	__ui_browser__refresh(browser);
	pthread_mutex_unlock(&ui__lock);

	return 0;
}

/*
 * Here we're updating nr_entries _after_ we started browsing, i.e.  we have to
 * forget about any reference to any entry in the underlying data structure,
 * that is why we do a SEEK_SET. Think about 'perf top' in the hists browser
 * after an output_resort and hist decay.
 */
void ui_browser__update_nr_entries(struct ui_browser *browser, u32 nr_entries)
{
	off_t offset = nr_entries - browser->nr_entries;

	browser->nr_entries = nr_entries;

	if (offset < 0) {
		if (browser->top_idx < (u64)-offset)
			offset = -browser->top_idx;

		browser->index += offset;
		browser->top_idx += offset;
	}

	browser->top = NULL;
	browser->seek(browser, browser->top_idx, SEEK_SET);
}

int ui_browser__run(struct ui_browser *self, int delay_secs)
{
	int err, key;

	while (1) {
		off_t offset;

		pthread_mutex_lock(&ui__lock);
		err = __ui_browser__refresh(self);
		SLsmg_refresh();
		pthread_mutex_unlock(&ui__lock);
		if (err < 0)
			break;

		key = ui__getch(delay_secs);

		if (key == K_RESIZE) {
			ui__refresh_dimensions(false);
			ui_browser__refresh_dimensions(self);
			__ui_browser__show_title(self, self->title);
			ui_helpline__puts(self->helpline);
			continue;
		}

		if (self->use_navkeypressed && !self->navkeypressed) {
			if (key == K_DOWN || key == K_UP ||
			    key == K_PGDN || key == K_PGUP ||
			    key == K_HOME || key == K_END ||
			    key == ' ') {
				self->navkeypressed = true;
				continue;
			} else
				return key;
		}

		switch (key) {
		case K_DOWN:
			if (self->index == self->nr_entries - 1)
				break;
			++self->index;
			if (self->index == self->top_idx + self->height) {
				++self->top_idx;
				self->seek(self, +1, SEEK_CUR);
			}
			break;
		case K_UP:
			if (self->index == 0)
				break;
			--self->index;
			if (self->index < self->top_idx) {
				--self->top_idx;
				self->seek(self, -1, SEEK_CUR);
			}
			break;
		case K_PGDN:
		case ' ':
			if (self->top_idx + self->height > self->nr_entries - 1)
				break;

			offset = self->height;
			if (self->index + offset > self->nr_entries - 1)
				offset = self->nr_entries - 1 - self->index;
			self->index += offset;
			self->top_idx += offset;
			self->seek(self, +offset, SEEK_CUR);
			break;
		case K_PGUP:
			if (self->top_idx == 0)
				break;

			if (self->top_idx < self->height)
				offset = self->top_idx;
			else
				offset = self->height;

			self->index -= offset;
			self->top_idx -= offset;
			self->seek(self, -offset, SEEK_CUR);
			break;
		case K_HOME:
			ui_browser__reset_index(self);
			break;
		case K_END:
			offset = self->height - 1;
			if (offset >= self->nr_entries)
				offset = self->nr_entries - 1;

			self->index = self->nr_entries - 1;
			self->top_idx = self->index - offset;
			self->seek(self, -offset, SEEK_END);
			break;
		default:
			return key;
		}
	}
	return -1;
}

unsigned int ui_browser__list_head_refresh(struct ui_browser *self)
{
	struct list_head *pos;
	struct list_head *head = self->entries;
	int row = 0;

	if (self->top == NULL || self->top == self->entries)
                self->top = ui_browser__list_head_filter_entries(self, head->next);

	pos = self->top;

	list_for_each_from(pos, head) {
		if (!self->filter || !self->filter(self, pos)) {
			ui_browser__gotorc(self, row, 0);
			self->write(self, pos, row);
			if (++row == self->height)
				break;
		}
	}

	return row;
}

static struct ui_browser__colorset {
	const char *name, *fg, *bg;
	int colorset;
} ui_browser__colorsets[] = {
	{
		.colorset = HE_COLORSET_TOP,
		.name	  = "top",
		.fg	  = "red",
		.bg	  = "default",
	},
	{
		.colorset = HE_COLORSET_MEDIUM,
		.name	  = "medium",
		.fg	  = "green",
		.bg	  = "default",
	},
	{
		.colorset = HE_COLORSET_NORMAL,
		.name	  = "normal",
		.fg	  = "default",
		.bg	  = "default",
	},
	{
		.colorset = HE_COLORSET_SELECTED,
		.name	  = "selected",
		.fg	  = "black",
		.bg	  = "lightgray",
	},
	{
		.colorset = HE_COLORSET_CODE,
		.name	  = "code",
		.fg	  = "blue",
		.bg	  = "default",
	},
	{
		.name = NULL,
	}
};


static int ui_browser__color_config(const char *var, const char *value,
				    void *data __used)
{
	char *fg = NULL, *bg;
	int i;

	/* same dir for all commands */
	if (prefixcmp(var, "colors.") != 0)
		return 0;

	for (i = 0; ui_browser__colorsets[i].name != NULL; ++i) {
		const char *name = var + 7;

		if (strcmp(ui_browser__colorsets[i].name, name) != 0)
			continue;

		fg = strdup(value);
		if (fg == NULL)
			break;

		bg = strchr(fg, ',');
		if (bg == NULL)
			break;

		*bg = '\0';
		while (isspace(*++bg));
		ui_browser__colorsets[i].bg = bg;
		ui_browser__colorsets[i].fg = fg;
		return 0;
	}

	free(fg);
	return -1;
}

void ui_browser__argv_seek(struct ui_browser *browser, off_t offset, int whence)
{
	switch (whence) {
	case SEEK_SET:
		browser->top = browser->entries;
		break;
	case SEEK_CUR:
		browser->top = browser->top + browser->top_idx + offset;
		break;
	case SEEK_END:
		browser->top = browser->top + browser->nr_entries + offset;
		break;
	default:
		return;
	}
}

unsigned int ui_browser__argv_refresh(struct ui_browser *browser)
{
	unsigned int row = 0, idx = browser->top_idx;
	char **pos;

	if (browser->top == NULL)
		browser->top = browser->entries;

	pos = (char **)browser->top;
	while (idx < browser->nr_entries) {
		if (!browser->filter || !browser->filter(browser, *pos)) {
			ui_browser__gotorc(browser, row, 0);
			browser->write(browser, pos, row);
			if (++row == browser->height)
				break;
		}

		++idx;
		++pos;
	}

	return row;
}

void ui_browser__init(void)
{
	int i = 0;

	perf_config(ui_browser__color_config, NULL);

	while (ui_browser__colorsets[i].name) {
		struct ui_browser__colorset *c = &ui_browser__colorsets[i++];
		sltt_set_color(c->colorset, c->name, c->fg, c->bg);
	}
}
