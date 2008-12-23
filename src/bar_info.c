/*
 * Geeqie
 * (C) 2004 John Ellis
 * Copyright (C) 2008 The Geeqie Team
 *
 * Author: John Ellis
 *
 * This software is released under the GNU General Public License (GNU GPL).
 * Please read the included file COPYING for more information.
 * This software comes with no warranty of any kind, use at your own risk!
 */


#include "main.h"
#include "bar_info.h"

#include "filedata.h"
#include "history_list.h"
#include "info.h"
#include "metadata.h"
#include "misc.h"
#include "ui_fileops.h"
#include "ui_misc.h"
#include "ui_utildlg.h"
#include "utilops.h"

static const gchar *keyword_favorite_defaults[] = {
	N_("Favorite"),
	N_("Todo"),
	N_("People"),
	N_("Places"),
	N_("Art"),
	N_("Nature"),
	N_("Possessions"),
	NULL
};


static void bar_info_keyword_update_all(void);
static void bar_info_changed(GtkTextBuffer *buffer, gpointer data);

/*
 *-------------------------------------------------------------------
 * keyword / comment utils
 *-------------------------------------------------------------------
 */


static gchar *comment_pull(GtkWidget *textview)
{
	GtkTextBuffer *buffer;
	GtkTextIter start, end;

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
	gtk_text_buffer_get_bounds(buffer, &start, &end);

	return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

GList *keyword_list_pull(GtkWidget *text_widget)
{
	GList *list;
	gchar *text;

	if (GTK_IS_TEXT_VIEW(text_widget))
		{
		text = comment_pull(text_widget);
		}
	else if (GTK_IS_ENTRY(text_widget))
		{
		text = g_strdup(gtk_entry_get_text(GTK_ENTRY(text_widget)));
		}
	else
		{
		return NULL;
		}
	
	list = string_to_keywords_list(text);

	g_free(text);

	return list;
}

void keyword_list_push(GtkWidget *textview, GList *list)
{
	GtkTextBuffer *buffer;
	GtkTextIter start, end;

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
	gtk_text_buffer_get_bounds(buffer, &start, &end);
	gtk_text_buffer_delete(buffer, &start, &end);

	while (list)
		{
		const gchar *word = list->data;
		GtkTextIter iter;

		gtk_text_buffer_get_end_iter(buffer, &iter);
		if (word) gtk_text_buffer_insert(buffer, &iter, word, -1);
		gtk_text_buffer_get_end_iter(buffer, &iter);
		gtk_text_buffer_insert(buffer, &iter, "\n", -1);

		list = list->next;
		}
}


/*
 *-------------------------------------------------------------------
 * keyword list dialog
 *-------------------------------------------------------------------
 */

#define KEYWORD_DIALOG_WIDTH  200
#define KEYWORD_DIALOG_HEIGHT 250

typedef struct _KeywordDlg KeywordDlg;
struct _KeywordDlg
{
	GenericDialog *gd;
	GtkWidget *treeview;
};

static KeywordDlg *keyword_dialog = NULL;


static void keyword_dialog_cancel_cb(GenericDialog *gd, gpointer data)
{
	g_free(keyword_dialog);
	keyword_dialog = NULL;
}

static void keyword_dialog_ok_cb(GenericDialog *gd, gpointer data)
{
	KeywordDlg *kd = data;
	GtkTreeModel *store;
	GtkTreeIter iter;
	gint valid;

	history_list_free_key("keywords");

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(kd->treeview));
	valid = gtk_tree_model_get_iter_first(store, &iter);
	while (valid)
		{
		gchar *key;

		gtk_tree_model_get(store, &iter, 0, &key, -1);
		valid = gtk_tree_model_iter_next(store, &iter);

		history_list_add_to_key("keywords", key, 0);
		}

	keyword_dialog_cancel_cb(gd, data);

	bar_info_keyword_update_all();
}

static void keyword_dialog_add_cb(GtkWidget *button, gpointer data)
{
	KeywordDlg *kd = data;
	GtkTreeSelection *selection;
	GtkTreeModel *store;
	GtkTreeIter sibling;
	GtkTreeIter iter;
	GtkTreePath *tpath;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(kd->treeview));
	if (gtk_tree_selection_get_selected(selection, &store, &sibling))
		{
		gtk_list_store_insert_before(GTK_LIST_STORE(store), &iter, &sibling);
		}
	else
		{
		store = gtk_tree_view_get_model(GTK_TREE_VIEW(kd->treeview));
		gtk_list_store_append(GTK_LIST_STORE(store), &iter);
		}

	gtk_list_store_set(GTK_LIST_STORE(store), &iter, 1, TRUE, -1);

	tpath = gtk_tree_model_get_path(store, &iter);
	gtk_tree_view_set_cursor(GTK_TREE_VIEW(kd->treeview), tpath,
				 gtk_tree_view_get_column(GTK_TREE_VIEW(kd->treeview), 0), TRUE);
	gtk_tree_path_free(tpath);
}

static void keyword_dialog_remove_cb(GtkWidget *button, gpointer data)
{
	KeywordDlg *kd = data;
	GtkTreeSelection *selection;
	GtkTreeModel *store;
	GtkTreeIter iter;
	GtkTreeIter next;
	GtkTreePath *tpath;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(kd->treeview));
	if (!gtk_tree_selection_get_selected(selection, &store, &iter)) return;

	tpath = NULL;
	next = iter;
	if (gtk_tree_model_iter_next(store, &next))
		{
		tpath = gtk_tree_model_get_path(store, &next);
		}
	else
		{
		tpath = gtk_tree_model_get_path(store, &iter);
		if (!gtk_tree_path_prev(tpath))
			{
			gtk_tree_path_free(tpath);
			tpath = NULL;
			}
		}
	if (tpath)
		{
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(kd->treeview), tpath,
					 gtk_tree_view_get_column(GTK_TREE_VIEW(kd->treeview), 0), FALSE);
		gtk_tree_path_free(tpath);
		}

	gtk_list_store_remove(GTK_LIST_STORE(store), &iter);
}

static void keyword_dialog_edit_cb(GtkCellRendererText *renderer, const gchar *path,
				   const gchar *new_text, gpointer data)
{
	KeywordDlg *kd = data;
	GtkTreeModel *store;
	GtkTreeIter iter;
	GtkTreePath *tpath;

	if (!new_text || strlen(new_text) == 0) return;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(kd->treeview));

	tpath = gtk_tree_path_new_from_string(path);
	gtk_tree_model_get_iter(store, &iter, tpath);
	gtk_tree_path_free(tpath);

	gtk_list_store_set(GTK_LIST_STORE(store), &iter, 0, new_text, -1);
}

static void keyword_dialog_populate(KeywordDlg *kd)
{
	GtkListStore *store;
	GList *list;

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(kd->treeview)));
	gtk_list_store_clear(store);

	list = history_list_get_by_key("keywords");
	list = g_list_last(list);
	while (list)
		{
		GtkTreeIter iter;

		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter, 0, list->data,
						 1, TRUE, -1);

		list = list->prev;
		}
}

static void keyword_dialog_show(void)
{
	GtkWidget *scrolled;
	GtkListStore *store;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;
	GtkWidget *hbox;
	GtkWidget *button;

	if (keyword_dialog)
		{
		gtk_window_present(GTK_WINDOW(keyword_dialog->gd->dialog));
		return;
		}

	keyword_dialog = g_new0(KeywordDlg, 1);

	keyword_dialog->gd = generic_dialog_new(_("Keyword Presets"),
						"keyword_presets", NULL, TRUE,
						keyword_dialog_cancel_cb, keyword_dialog);
	generic_dialog_add_message(keyword_dialog->gd, NULL, _("Favorite keywords list"), NULL);

	generic_dialog_add_button(keyword_dialog->gd, GTK_STOCK_OK, NULL,
				 keyword_dialog_ok_cb, TRUE);

	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_widget_set_size_request(scrolled, KEYWORD_DIALOG_WIDTH, KEYWORD_DIALOG_HEIGHT);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start(GTK_BOX(keyword_dialog->gd->vbox), scrolled, TRUE, TRUE, 5);
	gtk_widget_show(scrolled);

	store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_BOOLEAN);
	keyword_dialog->treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	g_object_unref(store);

	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(keyword_dialog->treeview), FALSE);
	gtk_tree_view_set_search_column(GTK_TREE_VIEW(keyword_dialog->treeview), 0);
	gtk_tree_view_set_reorderable(GTK_TREE_VIEW(keyword_dialog->treeview), TRUE);

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	renderer = gtk_cell_renderer_text_new();
	g_signal_connect(G_OBJECT(renderer), "edited",
			 G_CALLBACK(keyword_dialog_edit_cb), keyword_dialog);
	gtk_tree_view_column_pack_start(column, renderer, TRUE);
	gtk_tree_view_column_add_attribute(column, renderer, "text", 0);
	gtk_tree_view_column_add_attribute(column, renderer, "editable", 1);
	gtk_tree_view_append_column(GTK_TREE_VIEW(keyword_dialog->treeview), column);

	gtk_container_add(GTK_CONTAINER(scrolled), keyword_dialog->treeview);
	gtk_widget_show(keyword_dialog->treeview);

	hbox = gtk_hbox_new(FALSE, 5);
	gtk_box_pack_start(GTK_BOX(keyword_dialog->gd->vbox), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);

	button = gtk_button_new_from_stock(GTK_STOCK_ADD);
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(keyword_dialog_add_cb), keyword_dialog);
	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	button = gtk_button_new_from_stock(GTK_STOCK_REMOVE);
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(keyword_dialog_remove_cb), keyword_dialog);
	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	keyword_dialog_populate(keyword_dialog);

	gtk_widget_show(keyword_dialog->gd->dialog);
}


static void bar_keyword_edit_cb(GtkWidget *button, gpointer data)
{
	keyword_dialog_show();
}


/*
 *-------------------------------------------------------------------
 * info bar
 *-------------------------------------------------------------------
 */

typedef enum {
	BAR_SORT_COPY,
	BAR_SORT_MOVE,
	BAR_SORT_LINK
} SortActionType;

enum {
	KEYWORD_COLUMN_TOGGLE = 0,
	KEYWORD_COLUMN_TEXT
};

typedef struct _BarInfoData BarInfoData;
struct _BarInfoData
{
	GtkWidget *vbox;
	GtkWidget *group_box;
	GtkWidget *label_file_name;
	GtkWidget *label_file_time;

	GtkWidget *keyword_view;
	GtkWidget *keyword_treeview;

	GtkWidget *comment_view;

#if 0
	GtkWidget *button_save;
#endif
	GtkWidget *button_set_keywords_add;
	GtkWidget *button_set_keywords_replace;
	GtkWidget *button_set_comment_add;
	GtkWidget *button_set_comment_replace;

	FileData *fd;

	GList *(*list_func)(gpointer);
	gpointer list_data;
};


static GList *bar_list = NULL;


static void bar_info_write(BarInfoData *bd)
{
	GList *list;
	gchar *comment;

	if (!bd->fd) return;

	list = keyword_list_pull(bd->keyword_view);
	comment = comment_pull(bd->comment_view);

	metadata_write(bd->fd, list, comment);

	string_list_free(list);
	g_free(comment);
}

static void bar_keyword_list_sync(BarInfoData *bd, GList *keywords)
{
	GList *list;
	GtkListStore *store;
	GtkTreeIter iter;

	list = history_list_get_by_key("keywords");
	if (!list)
		{
		/* blank? set up a few example defaults */

		gint i = 0;

		while (keyword_favorite_defaults[i] != NULL)
			{
			history_list_add_to_key("keywords", _(keyword_favorite_defaults[i]), 0);
			i++;
			}

		list = history_list_get_by_key("keywords");
		}

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(bd->keyword_treeview)));

	gtk_list_store_clear(store);

	list = g_list_last(list);
	while (list)
		{
		gchar *key = list->data;

		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter, KEYWORD_COLUMN_TOGGLE, find_string_in_list(keywords, key),
						 KEYWORD_COLUMN_TEXT, key, -1);

		list = list->prev;
		}
}

static void bar_info_keyword_update_all(void)
{
	GList *work;

	work = bar_list;
	while (work)
		{
		BarInfoData *bd;
		GList *keywords;

		bd = work->data;
		work = work->next;

		keywords = keyword_list_pull(bd->keyword_view);
		bar_keyword_list_sync(bd, keywords);
		string_list_free(keywords);
		}
}

static void bar_info_update(BarInfoData *bd)
{
	GList *keywords = NULL;
	gchar *comment = NULL;
	GtkTextBuffer *keyword_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(bd->keyword_view));
	GtkTextBuffer *comment_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(bd->comment_view));

	g_signal_handlers_block_by_func(keyword_buffer, bar_info_changed, bd);
	g_signal_handlers_block_by_func(comment_buffer, bar_info_changed, bd);

	if (bd->label_file_name)
		{
		gtk_label_set_text(GTK_LABEL(bd->label_file_name), (bd->fd) ? bd->fd->name : "");
		}
	if (bd->label_file_time)
		{
		gtk_label_set_text(GTK_LABEL(bd->label_file_time), (bd->fd) ? text_from_time(bd->fd->date) : "");
		}

	if (metadata_read(bd->fd, &keywords, &comment))
		{
		keyword_list_push(bd->keyword_view, keywords);
		gtk_text_buffer_set_text(comment_buffer,
					 (comment) ? comment : "", -1);

		bar_keyword_list_sync(bd, keywords);

		string_list_free(keywords);
		g_free(comment);
		}
	else
		{
		gtk_text_buffer_set_text(keyword_buffer, "", -1);
		gtk_text_buffer_set_text(comment_buffer, "", -1);

		bar_keyword_list_sync(bd, NULL);
		}

	g_signal_handlers_unblock_by_func(keyword_buffer, bar_info_changed, bd);
	g_signal_handlers_unblock_by_func(comment_buffer, bar_info_changed, bd);

	gtk_widget_set_sensitive(bd->group_box, (bd->fd != NULL));
}

void bar_info_set(GtkWidget *bar, FileData *fd)
{
	BarInfoData *bd;

	bd = g_object_get_data(G_OBJECT(bar), "bar_info_data");
	if (!bd) return;

	file_data_unref(bd->fd);
	bd->fd = file_data_ref(fd);

	bar_info_update(bd);
}

void bar_info_maint_renamed(GtkWidget *bar, FileData *fd)
{
	BarInfoData *bd;

	bd = g_object_get_data(G_OBJECT(bar), "bar_info_data");
	if (!bd) return;

	file_data_unref(bd->fd);
	bd->fd = file_data_ref(fd);

	if (bd->label_file_name)
		{
		gtk_label_set_text(GTK_LABEL(bd->label_file_name), (bd->fd) ? bd->fd->name : "");
		}
}

gint bar_info_event(GtkWidget *bar, GdkEvent *event)
{
	BarInfoData *bd;

	bd = g_object_get_data(G_OBJECT(bar), "bar_info_data");
	if (!bd) return FALSE;

	if (GTK_WIDGET_HAS_FOCUS(bd->keyword_view)) return gtk_widget_event(bd->keyword_view, event);
	if (GTK_WIDGET_HAS_FOCUS(bd->comment_view)) return gtk_widget_event(bd->comment_view, event);

	return FALSE;
}

static void bar_info_keyword_set(BarInfoData *bd, const gchar *keyword, gint active)
{
	GList *list;
	gint found;

	if (!keyword) return;

	list = keyword_list_pull(bd->keyword_view);
	found = find_string_in_list(list, keyword);

	if (active != found)
		{
		if (found)
			{
			GList *work = list;

			while (work)
				{
				gchar *key = work->data;
				work = work->next;

				if (key && keyword && strcmp(key, keyword) == 0)
					{
					list = g_list_remove(list, key);
					g_free(key);
					}
				}
			}
		else
			{
			list = g_list_append(list, g_strdup(keyword));
			}

		keyword_list_push(bd->keyword_view, list);
		}

	string_list_free(list);
}

static void bar_info_keyword_toggle(GtkCellRendererToggle *toggle, const gchar *path, gpointer data)
{
	BarInfoData *bd = data;
	GtkTreeModel *store;
	GtkTreeIter iter;
	GtkTreePath *tpath;
	gchar *key = NULL;
	gboolean active;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(bd->keyword_treeview));

	tpath = gtk_tree_path_new_from_string(path);
	gtk_tree_model_get_iter(store, &iter, tpath);
	gtk_tree_path_free(tpath);

	gtk_tree_model_get(store, &iter, KEYWORD_COLUMN_TOGGLE, &active,
					 KEYWORD_COLUMN_TEXT, &key, -1);
	active = (!active);
	gtk_list_store_set(GTK_LIST_STORE(store), &iter, KEYWORD_COLUMN_TOGGLE, active, -1);

	bar_info_keyword_set(bd, key, active);
	g_free(key);
}

static void bar_info_set_selection(BarInfoData *bd, gboolean set_keywords, gboolean set_comment, gboolean append)
{
	GList *keywords = NULL;
	GList *list = NULL;
	GList *work;
	gchar *comment = NULL;

	if (!bd->list_func) return;

	if (set_keywords)
		{
		keywords = keyword_list_pull(bd->keyword_view);
		}

	if (set_comment)
		{
		comment = comment_pull(bd->comment_view);
		}

	if (append && !keywords && !comment) return;

	list = bd->list_func(bd->list_data);
	work = list;
	while (work)
		{
		FileData *fd = work->data;
		work = work->next;

		metadata_set(fd, keywords, comment, append);
		}

	filelist_free(list);
	string_list_free(keywords);
	g_free(comment);
}

static void bar_info_set_keywords_add(GtkWidget *button, gpointer data)
{
	BarInfoData *bd = data;

	bar_info_set_selection(bd, TRUE, FALSE, TRUE);
}

static void bar_info_set_keywords_replace(GtkWidget *button, gpointer data)
{
	BarInfoData *bd = data;

	bar_info_set_selection(bd, TRUE, FALSE, FALSE);
}

static void bar_info_set_comment_add(GtkWidget *button, gpointer data)
{
	BarInfoData *bd = data;

	bar_info_set_selection(bd, FALSE, TRUE, TRUE);
}

static void bar_info_set_comment_replace(GtkWidget *button, gpointer data)
{
	BarInfoData *bd = data;

	bar_info_set_selection(bd, FALSE, TRUE, FALSE);
}

static void bar_info_changed(GtkTextBuffer *buffer, gpointer data)
{
	BarInfoData *bd = data;

	bar_info_write(bd);
}

void bar_info_close(GtkWidget *bar)
{
	BarInfoData *bd;

	bd = g_object_get_data(G_OBJECT(bar), "bar_info_data");
	if (!bd) return;

	gtk_widget_destroy(bd->vbox);
}

static void bar_info_destroy(GtkWidget *widget, gpointer data)
{
	BarInfoData *bd = data;

	bar_list = g_list_remove(bar_list, bd);

	file_data_unref(bd->fd);

	g_free(bd);
}

GtkWidget *bar_info_new(FileData *fd, gint metadata_only, GtkWidget *bounding_widget)
{
	BarInfoData *bd;
	GtkWidget *box;
	GtkWidget *hbox;
	GtkWidget *table;
	GtkWidget *scrolled;
	GtkTextBuffer *buffer;
	GtkWidget *label;
	GtkWidget *tbar;
	GtkListStore *store;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;

	bd = g_new0(BarInfoData, 1);

	bd->list_func = NULL;
	bd->list_data = NULL;

	bd->vbox = gtk_vbox_new(FALSE, PREF_PAD_GAP);
	g_object_set_data(G_OBJECT(bd->vbox), "bar_info_data", bd);
	g_signal_connect(G_OBJECT(bd->vbox), "destroy",
			 G_CALLBACK(bar_info_destroy), bd);

	if (!metadata_only)
		{
		hbox = pref_box_new(bd->vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_GAP);

		label = sizer_new(bd->vbox, bounding_widget, SIZER_POS_LEFT);
		gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
		gtk_widget_show(label);

		label = gtk_label_new(_("Keywords"));
		pref_label_bold(label, TRUE, FALSE);
		gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
		gtk_widget_show(label);
		}

	bd->group_box = pref_box_new(bd->vbox, TRUE, GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);

	if (!metadata_only)
		{
		GtkWidget *table;

		table = pref_table_new(bd->group_box, 2, 2, FALSE, FALSE);

		bd->label_file_name = table_add_line(table, 0, 0, _("Filename:"), NULL);
		bd->label_file_time = table_add_line(table, 0, 1, _("File date:"), NULL);
		}
	else
		{
		bd->label_file_name = NULL;
		bd->label_file_time = NULL;
		}

	table = gtk_table_new(3, 1, TRUE);
	gtk_table_set_row_spacings(GTK_TABLE(table), PREF_PAD_GAP);
	gtk_box_pack_start(GTK_BOX(bd->group_box), table, TRUE, TRUE, 0);
	gtk_widget_show(table);

	/* keyword entry */

	box = gtk_vbox_new(FALSE, 0);
	gtk_table_attach(GTK_TABLE(table), box, 0, 1, 0, 2,
			 GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
	gtk_widget_show(box);

	label = pref_label_new(box, _("Keywords:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	pref_label_bold(label, TRUE, FALSE);

	hbox = pref_box_new(box, TRUE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_GAP);

	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start(GTK_BOX(hbox), scrolled, TRUE, TRUE, 0);
	gtk_widget_show(scrolled);

	bd->keyword_view = gtk_text_view_new();
	gtk_container_add(GTK_CONTAINER(scrolled), bd->keyword_view);
	gtk_widget_show(bd->keyword_view);

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(bd->keyword_view));
	g_signal_connect(G_OBJECT(buffer), "changed",
			 G_CALLBACK(bar_info_changed), bd);

	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start(GTK_BOX(hbox), scrolled, TRUE, TRUE, 0);
	gtk_widget_show(scrolled);

	store = gtk_list_store_new(2, G_TYPE_BOOLEAN, G_TYPE_STRING);
	bd->keyword_treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	g_object_unref(store);

	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(bd->keyword_treeview), FALSE);

	if (metadata_only)
		{
		gtk_tree_view_set_search_column(GTK_TREE_VIEW(bd->keyword_treeview), KEYWORD_COLUMN_TEXT);
		}
	else
		{
		gtk_tree_view_set_enable_search(GTK_TREE_VIEW(bd->keyword_treeview), FALSE);
		}

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);

	renderer = gtk_cell_renderer_toggle_new();
	gtk_tree_view_column_pack_start(column, renderer, FALSE);
	gtk_tree_view_column_add_attribute(column, renderer, "active", KEYWORD_COLUMN_TOGGLE);
	g_signal_connect(G_OBJECT(renderer), "toggled",
			 G_CALLBACK(bar_info_keyword_toggle), bd);

	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(column, renderer, TRUE);
	gtk_tree_view_column_add_attribute(column, renderer, "text", KEYWORD_COLUMN_TEXT);

	gtk_tree_view_append_column(GTK_TREE_VIEW(bd->keyword_treeview), column);

	gtk_container_add(GTK_CONTAINER(scrolled), bd->keyword_treeview);
	gtk_widget_show(bd->keyword_treeview);

	/* comment entry */

	box = gtk_vbox_new(FALSE, 0);
	gtk_table_attach(GTK_TABLE(table), box, 0, 1, 2, 3,
			 GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
	gtk_widget_show(box);

	label = pref_label_new(box, _("Comment:"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
	pref_label_bold(label, TRUE, FALSE);

	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start(GTK_BOX(box), scrolled, TRUE, TRUE, 0);
	gtk_widget_show(scrolled);

	bd->comment_view = gtk_text_view_new();
	gtk_container_add(GTK_CONTAINER(scrolled), bd->comment_view);
	gtk_widget_show(bd->comment_view);

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(bd->comment_view));
	g_signal_connect(G_OBJECT(buffer), "changed",
			 G_CALLBACK(bar_info_changed), bd);

	/* toolbar */

	tbar = pref_toolbar_new(bd->group_box, GTK_TOOLBAR_ICONS);

	pref_toolbar_button(tbar, GTK_STOCK_INDEX, NULL, FALSE,
			_("Edit favorite keywords list."),
			G_CALLBACK(bar_keyword_edit_cb), bd);
	pref_toolbar_spacer(tbar);
	bd->button_set_keywords_add = pref_toolbar_button(tbar, GTK_STOCK_ADD, NULL, FALSE,
			_("Add keywords to selected files"),
			G_CALLBACK(bar_info_set_keywords_add), bd);
	bd->button_set_keywords_replace = pref_toolbar_button(tbar, GTK_STOCK_CONVERT, NULL, FALSE,
			_("Add keywords to selected files, replacing existing ones"),
			G_CALLBACK(bar_info_set_keywords_replace), bd);
	bd->button_set_comment_add = pref_toolbar_button(tbar, GTK_STOCK_DND_MULTIPLE, NULL, FALSE,
			_("Add comment to selected files"),
			G_CALLBACK(bar_info_set_comment_add), bd);
	bd->button_set_comment_replace = pref_toolbar_button(tbar, GTK_STOCK_DND, NULL, FALSE,
			_("Add comment to selected files, replacing existing one"),
			G_CALLBACK(bar_info_set_comment_replace), bd);

#if 0
	pref_toolbar_spacer(tbar);
	bd->button_save = pref_toolbar_button(tbar, GTK_STOCK_SAVE, NULL, FALSE,
			_("Save comment now"),
			G_CALLBACK(bar_info_save), bd);
#endif

	bd->fd = file_data_ref(fd);
	bar_info_update(bd);

	bar_info_selection(bd->vbox, 0);

	bar_list = g_list_append(bar_list, bd);

	return bd->vbox;
}

void bar_info_set_selection_func(GtkWidget *bar, GList *(*list_func)(gpointer data), gpointer data)
{
	BarInfoData *bd;

	bd = g_object_get_data(G_OBJECT(bar), "bar_info_data");
	if (!bd) return;

	bd->list_func = list_func;
	bd->list_data = data;
}

void bar_info_selection(GtkWidget *bar, gint count)
{
	BarInfoData *bd;
	gint enable;

	bd = g_object_get_data(G_OBJECT(bar), "bar_info_data");
	if (!bd) return;

	enable = (count > 0 && bd->list_func != NULL);

	gtk_widget_set_sensitive(bd->button_set_keywords_add, enable);
	gtk_widget_set_sensitive(bd->button_set_keywords_replace, enable);
	gtk_widget_set_sensitive(bd->button_set_comment_add, enable);
	gtk_widget_set_sensitive(bd->button_set_comment_replace, enable);

}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
