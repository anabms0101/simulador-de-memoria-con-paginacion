/* Interfaz grafica (GTK4) del simulador de MMU.
 *
 * Usa directamente el motor ya probado en mmu.c/parser.c (los mismos
 * mmu_new/mmu_use/mmu_delete/mmu_kill y el parser de archivos), sin
 * duplicar ninguna logica de simulacion: esta capa solo dibuja y
 * orquesta los pasos.
 *
 * Flujo (tal como lo pide el enunciado): al arrancar se muestra una
 * ventana de configuracion (algoritmo, semilla/archivo, P, N); al
 * presionar "Iniciar simulacion" se abre una NUEVA ventana con la
 * visualizacion en paralelo del algoritmo elegido y de OPT.
 */

#include "gui.h"
#include "mmu.h"
#include "parser.h"

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_PI_2
#define M_PI_2 (M_PI / 2.0)
#endif

/* ===================== Estilo visual ===================== */

static const char* CSS_APP =
"window { background-color: #14161f; }\n"
".hero-title { font-size: 22px; font-weight: 800; color: #f8fafc; }\n"
".hero-subtitle { font-size: 13px; color: #97a1b8; }\n"
".field-label { font-size: 12px; font-weight: 600; color: #b7c0d6; }\n"
".setup-card { background-color: #1d2131; border-radius: 20px; padding: 24px; }\n"
".panel-card { background-color: #1d2131; border-radius: 18px; padding: 14px; }\n"
".panel-title { font-size: 15px; font-weight: 700; color: #eef1f8; }\n"
".metric-chip { background-color: #161925; border-radius: 12px; padding: 8px 12px; }\n"
".metric-label { font-size: 10.5px; color: #8a93ab; }\n"
".metric-value { font-size: 15px; font-weight: 700; color: #f1f4fa; }\n"
".thrash-alert { background-color: #ef4444; }\n"
".thrash-alert .metric-label { color: #fde2e2; }\n"
".thrash-alert .metric-value { color: #ffffff; }\n"
".accent-btn { background-image: linear-gradient(135deg, #6366f1, #8b5cf6);\n"
"              background-color: #6366f1; color: white; border-radius: 12px;\n"
"              padding: 10px 20px; font-weight: 700; border: none; }\n"
".accent-btn:disabled { opacity: 0.4; }\n"
".secondary-btn { background-image: none; background-color: #2a2f45; color: #eef1f8;\n"
"                 border-radius: 12px; padding: 10px 18px; font-weight: 600; border: none; }\n"
".secondary-btn:hover { background-color: #343a57; }\n"
".secondary-btn:disabled { opacity: 0.4; }\n"
".hint-label { font-size: 11px; color: #7f89a3; }\n"
"entry, spinbutton { background-color: #161925; color: #eef1f8; border-radius: 10px; }\n"
"notebook, notebook > header, notebook stack {\n"
"    background-color: transparent; border: none; }\n"
"notebook tab { color: #97a1b8; font-weight: 600; padding: 6px 14px;\n"
"               background-color: transparent; box-shadow: none; border: none; }\n"
"notebook tab:checked { color: #a5b4fc; background-color: transparent; box-shadow: none; }\n"
"filechooser, filechooserwidget, placessidebar, placesview {\n"
"    background-color: #1d2131; color: #eef1f8; }\n"
"treeview { background-color: #161925; color: #e7eaf2; }\n"
"treeview header button { background-color: #1d2131; color: #97a1b8; font-weight: 700; }\n"
"progressbar trough { background-color: #161925; border-radius: 8px; min-height: 8px; }\n"
"progressbar progress { background-image: linear-gradient(90deg, #6366f1, #22d3ee);\n"
"                        border-radius: 8px; min-height: 8px; }\n"
"scale trough { background-color: #161925; border-radius: 8px; }\n"
"scale highlight { background-color: #6366f1; border-radius: 8px; }\n";

/* Paleta para distinguir procesos a simple vista (se recicla si hay
 * mas PIDs que colores). */
static const char* PALETA[] = {
    "#60a5fa", "#34d399", "#fbbf24", "#f472b6", "#a78bfa",
    "#fb923c", "#2dd4bf", "#f87171", "#93c5fd", "#c4b5fd",
};
#define PALETA_N ((int)(sizeof(PALETA) / sizeof(PALETA[0])))

static const char* color_para_pid(int pid) {
    int idx = ((pid - 1) % PALETA_N + PALETA_N) % PALETA_N;
    return PALETA[idx];
}

static const char* nombre_algoritmo(TipoAlgoritmo a) {
    switch (a) {
        case ALGO_FIFO: return "FIFO";
        case ALGO_SC:   return "Second Chance";
        case ALGO_LRU:  return "LRU";
        case ALGO_LFU:  return "LFU";
        case ALGO_OPT:  return "OPTIMO (Belady)";
    }
    return "?";
}

static void formatear_instruccion(const Instruccion* in, char* buf, size_t buf_sz) {
    switch (in->tipo) {
        case INST_NEW:    snprintf(buf, buf_sz, "new(%d,%d)", in->pid, in->param); break;
        case INST_USE:    snprintf(buf, buf_sz, "use(%d)", in->param); break;
        case INST_DELETE: snprintf(buf, buf_sz, "delete(%d)", in->param); break;
        case INST_KILL:   snprintf(buf, buf_sz, "kill(%d)", in->pid); break;
    }
}

/* ===================== Modelo de la tabla de paginas ===================== */

enum {
    COL_PAGE_ID, COL_PID, COL_PTR, COL_LOADED, COL_MADDR, COL_DADDR, COL_LOADEDT, COL_COLOR, N_COLS
};

/* ===================== Estructuras de la UI ===================== */

typedef struct {
    GtkWidget* area_ram;
    GtkWidget* tree_view;
    GtkListStore* store;
    GtkWidget* lbl_procesos;
    GtkWidget* lbl_tiempo;
    GtkWidget* lbl_ram;
    GtkWidget* lbl_vram;
    GtkWidget* lbl_frag;
    GtkWidget* lbl_paginas;
    GtkWidget* chip_thrash;
    GtkWidget* lbl_thrash;
    const MMU* mmu;
} PanelUI;

typedef struct {
    GtkApplication* app;
    GtkWidget* win_setup;
    GtkWidget* win_sim;

    /* widgets de configuracion */
    GtkWidget* combo_algoritmo;
    GtkWidget* notebook_fuente;
    GtkWidget* entry_archivo;
    GtkWidget* spin_semilla;
    GtkWidget* spin_p;
    GtkWidget* spin_n;
    GtkWidget* lbl_setup_estado;

    /* ruta del archivo de operaciones generado en esta corrida (vacio si
     * se cargo uno existente en vez de generarlo); se muestra tambien en
     * la ventana de simulacion para que quede visible y "descargable"
     * (ya esta guardado en disco) durante toda la simulacion. */
    char ruta_generada[256];

    /* datos de simulacion */
    Instruccion* instrucciones;
    int total;
    int indice;
    TipoAlgoritmo algo_usuario;
    MMU mmu_usuario;
    MMU mmu_opt;
    bool mmus_inicializadas;

    /* widgets de simulacion */
    PanelUI panel_usuario;
    PanelUI panel_opt;
    GtkWidget* lbl_instruccion;
    GtkWidget* progreso;
    GtkWidget* btn_play_pause;
    GtkWidget* btn_paso;
    GtkWidget* scale_velocidad;
    GtkWidget* lbl_final;

    gboolean reproduciendo;
    guint timer_id;
    guint velocidad_ms;
} AppState;

/* ===================== Prototipos ===================== */

static void construir_ventana_setup(AppState* app);
static void construir_ventana_simulacion(AppState* app);
static void panel_crear(PanelUI* panel, GtkWidget* parent_box, const char* titulo);
static void panel_refrescar(PanelUI* panel);
static void finalizar_simulacion(AppState* app);

static void on_examinar_clicked(GtkButton* btn, gpointer user_data);
static void on_iniciar_clicked(GtkButton* btn, gpointer user_data);
static void on_play_pause_clicked(GtkButton* btn, gpointer user_data);
static void on_paso_clicked(GtkButton* btn, gpointer user_data);
static void on_velocidad_changed(GtkRange* range, gpointer user_data);
static gboolean on_sim_window_close(GtkWindow* window, gpointer user_data);

/* ===================== Dibujo de la barra de RAM ===================== */

static void ram_draw_func(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data) {
    (void)area;
    PanelUI* panel = (PanelUI*)user_data;
    const MMU* mmu = panel->mmu;
    if (!mmu) return;

    const int cols = 20;
    const int rows = (TOTAL_MARCOS_RAM + cols - 1) / cols;
    const double gap = 3.0;
    const double cell_w = (width - gap * (cols + 1)) / (double)cols;
    const double cell_h = (height - gap * (rows + 1)) / (double)rows;
    const double radius = cell_w > 6 ? 3.0 : 1.5;

    for (int i = 0; i < TOTAL_MARCOS_RAM; i++) {
        int col = i % cols;
        int row = i / cols;
        double x = gap + col * (cell_w + gap);
        double y = gap + row * (cell_h + gap);

        double r = 0.12, g = 0.14, b = 0.19; /* vacio */
        const Pagina* p = mmu->ram[i];
        if (p) {
            GdkRGBA rgba;
            if (gdk_rgba_parse(&rgba, color_para_pid(p->pid))) {
                r = rgba.red; g = rgba.green; b = rgba.blue;
            }
        }

        cairo_new_sub_path(cr);
        cairo_arc(cr, x + cell_w - radius, y + radius, radius, -M_PI_2, 0);
        cairo_arc(cr, x + cell_w - radius, y + cell_h - radius, radius, 0, M_PI_2);
        cairo_arc(cr, x + radius, y + cell_h - radius, radius, M_PI_2, M_PI);
        cairo_arc(cr, x + radius, y + radius, radius, M_PI, 3 * M_PI_2);
        cairo_close_path(cr);
        cairo_set_source_rgb(cr, r, g, b);
        cairo_fill(cr);
    }
}

/* ===================== Paneles (uno por MMU) ===================== */

static void metric_chip_crear(GtkWidget* grid, int row, int col, const char* etiqueta,
                               GtkWidget** out_chip, GtkWidget** out_valor_label) {
    GtkWidget* chip = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_add_css_class(chip, "metric-chip");
    GtkWidget* lbl_etq = gtk_label_new(etiqueta);
    gtk_widget_add_css_class(lbl_etq, "metric-label");
    gtk_widget_set_halign(lbl_etq, GTK_ALIGN_START);
    GtkWidget* lbl_val = gtk_label_new("--");
    gtk_widget_add_css_class(lbl_val, "metric-value");
    gtk_widget_set_halign(lbl_val, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(chip), lbl_etq);
    gtk_box_append(GTK_BOX(chip), lbl_val);
    gtk_grid_attach(GTK_GRID(grid), chip, col, row, 1, 1);
    if (out_chip) *out_chip = chip;
    if (out_valor_label) *out_valor_label = lbl_val;
}

static void panel_crear(PanelUI* panel, GtkWidget* parent_box, const char* titulo) {
    GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(card, "panel-card");
    gtk_widget_set_hexpand(card, TRUE);

    GtkWidget* lbl_titulo = gtk_label_new(titulo);
    gtk_widget_add_css_class(lbl_titulo, "panel-title");
    gtk_widget_set_halign(lbl_titulo, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(card), lbl_titulo);

    panel->area_ram = gtk_drawing_area_new();
    gtk_widget_set_size_request(panel->area_ram, -1, 84);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(panel->area_ram), ram_draw_func, panel, NULL);
    gtk_box_append(GTK_BOX(card), panel->area_ram);

    panel->store = gtk_list_store_new(N_COLS,
                                       G_TYPE_INT, G_TYPE_INT, G_TYPE_INT,
                                       G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                       G_TYPE_STRING, G_TYPE_STRING);
    panel->tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(panel->store));
    g_object_unref(panel->store);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(panel->tree_view), TRUE);

    const char* titulos[] = {"Pagina", "PID", "PTR", "Estado", "M-ADDR", "D-ADDR", "Cargada-T"};
    int cols_modelo[] = {COL_PAGE_ID, COL_PID, COL_PTR, COL_LOADED, COL_MADDR, COL_DADDR, COL_LOADEDT};
    for (int i = 0; i < 7; i++) {
        GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
        g_object_set(renderer, "xpad", 6, NULL);
        GtkTreeViewColumn* col = gtk_tree_view_column_new_with_attributes(
            titulos[i], renderer, "text", cols_modelo[i], NULL);
        gtk_tree_view_column_add_attribute(col, renderer, "cell-background", COL_COLOR);
        gtk_tree_view_column_set_resizable(col, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(panel->tree_view), col);
    }

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), panel->tree_view);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_size_request(scroll, -1, 150);
    gtk_box_append(GTK_BOX(card), scroll);

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);

    GtkWidget* chip_descartable;
    /* 3 columnas para que el panel sea mas bajo y quepa en pantallas chicas */
    metric_chip_crear(grid, 0, 0, "Procesos activos", &chip_descartable, &panel->lbl_procesos);
    metric_chip_crear(grid, 0, 1, "Tiempo simulado", &chip_descartable, &panel->lbl_tiempo);
    metric_chip_crear(grid, 0, 2, "Thrashing", &panel->chip_thrash, &panel->lbl_thrash);
    metric_chip_crear(grid, 1, 0, "RAM usada", &chip_descartable, &panel->lbl_ram);
    metric_chip_crear(grid, 1, 1, "V-RAM usada", &chip_descartable, &panel->lbl_vram);
    metric_chip_crear(grid, 1, 2, "Páginas RAM / disco", &chip_descartable, &panel->lbl_paginas);
    metric_chip_crear(grid, 2, 0, "Fragmentación interna", &chip_descartable, &panel->lbl_frag);

    gtk_box_append(GTK_BOX(card), grid);
    gtk_box_append(GTK_BOX(parent_box), card);
}

static void panel_refrescar(PanelUI* panel) {
    const MMU* mmu = panel->mmu;
    if (!mmu) return;

    gtk_list_store_clear(panel->store);
    for (int i = 0; i < mmu->total_simbolos; i++) {
        const PtrSimbolo* s = &mmu->simbolos[i];
        if (!s->activo) continue;
        for (int j = 0; j < s->num_paginas; j++) {
            const Pagina* p = s->paginas[j];
            if (!p) continue;

            char m_addr[16], d_addr[16], loaded_t[16];
            if (p->en_memoria_real) snprintf(m_addr, sizeof(m_addr), "%d", p->direccion_fisica);
            else snprintf(m_addr, sizeof(m_addr), "-");
            if (!p->en_memoria_real) snprintf(d_addr, sizeof(d_addr), "%d", p->direccion_virtual);
            else snprintf(d_addr, sizeof(d_addr), "-");
            snprintf(loaded_t, sizeof(loaded_t), "%lds", p->tiempo_cargada);

            GtkTreeIter iter;
            gtk_list_store_append(panel->store, &iter);
            gtk_list_store_set(panel->store, &iter,
                                COL_PAGE_ID, p->id,
                                COL_PID, p->pid,
                                COL_PTR, p->ptr,
                                COL_LOADED, p->en_memoria_real ? "RAM" : "Disco",
                                COL_MADDR, m_addr,
                                COL_DADDR, d_addr,
                                COL_LOADEDT, loaded_t,
                                COL_COLOR, color_para_pid(p->pid),
                                -1);
        }
    }

    long ram_bytes = mmu_ram_usada_bytes(mmu);
    long vram_bytes = mmu_vram_usada_bytes(mmu);
    double capacidad_bytes = (double)TOTAL_MARCOS_RAM * TAMANIO_PAGINA;
    double ram_pct = 100.0 * (double)ram_bytes / capacidad_bytes;
    /* V-RAM% es relativo a la capacidad de RAM (asi lo pide el enunciado:
     * 500KB en V-RAM con 400KB de RAM = 125%). */
    double vram_pct = 100.0 * (double)vram_bytes / capacidad_bytes;
    double thrash_pct = mmu->reloj > 0 ? 100.0 * (double)mmu->tiempo_thrashing / (double)mmu->reloj : 0.0;
    long frag = mmu_fragmentacion_bytes(mmu);
    int cargadas = mmu_paginas_cargadas(mmu);
    int descargadas = mmu_paginas_descargadas(mmu);

    char buf[80];
    snprintf(buf, sizeof(buf), "%d", mmu_procesos_activos(mmu));
    gtk_label_set_text(GTK_LABEL(panel->lbl_procesos), buf);

    snprintf(buf, sizeof(buf), "%ld s", mmu->reloj);
    gtk_label_set_text(GTK_LABEL(panel->lbl_tiempo), buf);

    snprintf(buf, sizeof(buf), "%ld KB (%.1f%%)", ram_bytes / 1024, ram_pct);
    gtk_label_set_text(GTK_LABEL(panel->lbl_ram), buf);

    snprintf(buf, sizeof(buf), "%ld KB (%.1f%%)", vram_bytes / 1024, vram_pct);
    gtk_label_set_text(GTK_LABEL(panel->lbl_vram), buf);

    snprintf(buf, sizeof(buf), "%ld bytes", frag);
    gtk_label_set_text(GTK_LABEL(panel->lbl_frag), buf);

    snprintf(buf, sizeof(buf), "%d / %d", cargadas, descargadas);
    gtk_label_set_text(GTK_LABEL(panel->lbl_paginas), buf);

    snprintf(buf, sizeof(buf), "%ld s (%.1f%%)", mmu->tiempo_thrashing, thrash_pct);
    gtk_label_set_text(GTK_LABEL(panel->lbl_thrash), buf);
    if (thrash_pct > 50.0) gtk_widget_add_css_class(panel->chip_thrash, "thrash-alert");
    else gtk_widget_remove_css_class(panel->chip_thrash, "thrash-alert");

    gtk_widget_queue_draw(panel->area_ram);
}

/* ===================== Avance de la simulacion ===================== */

static gboolean avanzar_un_paso(AppState* app) {
    if (app->indice >= app->total) return FALSE;

    const Instruccion* inst = &app->instrucciones[app->indice];
    switch (inst->tipo) {
        case INST_NEW:
            mmu_new(&app->mmu_usuario, inst->pid, inst->param, app->instrucciones, app->total, app->indice);
            mmu_new(&app->mmu_opt, inst->pid, inst->param, app->instrucciones, app->total, app->indice);
            break;
        case INST_USE:
            mmu_use(&app->mmu_usuario, inst->param, app->instrucciones, app->total, app->indice);
            mmu_use(&app->mmu_opt, inst->param, app->instrucciones, app->total, app->indice);
            break;
        case INST_DELETE:
            mmu_delete(&app->mmu_usuario, inst->param);
            mmu_delete(&app->mmu_opt, inst->param);
            break;
        case INST_KILL:
            mmu_kill(&app->mmu_usuario, inst->pid);
            mmu_kill(&app->mmu_opt, inst->pid);
            break;
    }
    app->indice++;

    char desc[32];
    formatear_instruccion(inst, desc, sizeof(desc));
    char buf[160];
    snprintf(buf, sizeof(buf), "Instruccion %d / %d:  %s", app->indice, app->total, desc);
    gtk_label_set_text(GTK_LABEL(app->lbl_instruccion), buf);

    double frac = app->total > 0 ? (double)app->indice / (double)app->total : 1.0;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progreso), frac);

    panel_refrescar(&app->panel_usuario);
    panel_refrescar(&app->panel_opt);

    if (app->indice >= app->total) {
        finalizar_simulacion(app);
        return FALSE;
    }
    return TRUE;
}

static gboolean timer_callback(gpointer user_data) {
    AppState* app = (AppState*)user_data;
    gboolean continuar = avanzar_un_paso(app);
    if (!continuar) {
        app->timer_id = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void actualizar_boton_play(AppState* app) {
    gtk_button_set_label(GTK_BUTTON(app->btn_play_pause),
                          app->reproduciendo ? "⏸  Pausar   [Espacio]" : "▶  Reproducir   [Espacio]");
}

/* Atajos de teclado de la ventana de simulacion (se capturan antes que los
 * widgets para que Espacio no active dos veces el boton enfocado):
 *   Espacio  -> reproducir / pausar
 *   Flecha derecha -> avanzar un paso (solo si esta en pausa) */
static gboolean on_key_pressed(GtkEventControllerKey* ctrl, guint keyval, guint keycode,
                                GdkModifierType state, gpointer user_data) {
    (void)ctrl; (void)keycode; (void)state;
    AppState* app = (AppState*)user_data;
    if (keyval == GDK_KEY_space) {
        on_play_pause_clicked(NULL, app);
        return TRUE;
    }
    if (keyval == GDK_KEY_Right || keyval == GDK_KEY_KP_Right) {
        on_paso_clicked(NULL, app);
        return TRUE;
    }
    return FALSE;
}

static void on_play_pause_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    AppState* app = (AppState*)user_data;
    if (app->indice >= app->total) return;

    app->reproduciendo = !app->reproduciendo;
    actualizar_boton_play(app);

    if (app->reproduciendo) {
        if (app->timer_id == 0) app->timer_id = g_timeout_add(app->velocidad_ms, timer_callback, app);
        gtk_widget_set_sensitive(app->btn_paso, FALSE);
    } else {
        if (app->timer_id != 0) { g_source_remove(app->timer_id); app->timer_id = 0; }
        gtk_widget_set_sensitive(app->btn_paso, TRUE);
    }
}

static void on_paso_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    AppState* app = (AppState*)user_data;
    if (app->reproduciendo) return;
    avanzar_un_paso(app);
}

static void on_velocidad_changed(GtkRange* range, gpointer user_data) {
    AppState* app = (AppState*)user_data;
    app->velocidad_ms = (guint)gtk_range_get_value(range);
    if (app->reproduciendo && app->timer_id != 0) {
        g_source_remove(app->timer_id);
        app->timer_id = g_timeout_add(app->velocidad_ms, timer_callback, app);
    }
}

static void finalizar_simulacion(AppState* app) {
    if (app->timer_id != 0) { g_source_remove(app->timer_id); app->timer_id = 0; }
    app->reproduciendo = FALSE;
    gtk_widget_set_sensitive(app->btn_play_pause, FALSE);
    gtk_widget_set_sensitive(app->btn_paso, FALSE);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progreso), 1.0);

    char buf[256];
    if (app->mmu_usuario.reloj > app->mmu_opt.reloj) {
        snprintf(buf, sizeof(buf), "Listo. %s tardo %ld s mas que el optimo (%ld s vs %ld s).",
                 nombre_algoritmo(app->algo_usuario),
                 app->mmu_usuario.reloj - app->mmu_opt.reloj,
                 app->mmu_usuario.reloj, app->mmu_opt.reloj);
    } else {
        snprintf(buf, sizeof(buf), "Listo. %s igualo el rendimiento del optimo (%ld s).",
                 nombre_algoritmo(app->algo_usuario), app->mmu_usuario.reloj);
    }
    gtk_label_set_text(GTK_LABEL(app->lbl_final), buf);
}

static gboolean on_sim_window_close(GtkWindow* window, gpointer user_data) {
    (void)window;
    AppState* app = (AppState*)user_data;
    if (app->timer_id != 0) { g_source_remove(app->timer_id); app->timer_id = 0; }
    return FALSE; /* permite el cierre normal de la ventana */
}

/* ===================== Ventana de simulacion ===================== */

static void construir_ventana_simulacion(AppState* app) {
    app->win_sim = gtk_application_window_new(app->app);
    gtk_window_set_title(GTK_WINDOW(app->win_sim), "Simulacion en curso");
    /* Ajustar el tamano inicial a la pantalla real (descontando barras del
     * sistema), para que la ventana nunca se salga de la pantalla. */
    int ancho = 1200, alto = 740;
    GdkDisplay* disp = gdk_display_get_default();
    if (disp) {
        GListModel* monitores = gdk_display_get_monitors(disp);
        GdkMonitor* mon = monitores ? (GdkMonitor*)g_list_model_get_item(monitores, 0) : NULL;
        if (mon) {
            GdkRectangle geo;
            gdk_monitor_get_geometry(mon, &geo);
            if (geo.width - 80 < ancho) ancho = geo.width - 80;
            if (geo.height - 120 < alto) alto = geo.height - 120;
            g_object_unref(mon);
        }
    }
    if (ancho < 760) ancho = 760;
    if (alto < 420) alto = 420;
    gtk_window_set_default_size(GTK_WINDOW(app->win_sim), ancho, alto);
    g_signal_connect(app->win_sim, "close-request", G_CALLBACK(on_sim_window_close), app);

    GtkWidget* outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_top(outer, 18);
    gtk_widget_set_margin_bottom(outer, 18);
    gtk_widget_set_margin_start(outer, 18);
    gtk_widget_set_margin_end(outer, 18);

    GtkWidget* cabecera = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    char titulo_buf[160];
    snprintf(titulo_buf, sizeof(titulo_buf), "%s   vs.   OPTIMO", nombre_algoritmo(app->algo_usuario));
    GtkWidget* lbl_titulo = gtk_label_new(titulo_buf);
    gtk_widget_add_css_class(lbl_titulo, "hero-title");
    gtk_widget_set_halign(lbl_titulo, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(cabecera), lbl_titulo);

    app->lbl_instruccion = gtk_label_new("Listo para comenzar: presione «Reproducir» (o la tecla Espacio), o «Avanzar un paso» (o la flecha →).");
    gtk_widget_add_css_class(app->lbl_instruccion, "hero-subtitle");
    gtk_widget_set_halign(app->lbl_instruccion, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(cabecera), app->lbl_instruccion);

    if (app->ruta_generada[0] != '\0') {
        char buf_ruta[320];
        snprintf(buf_ruta, sizeof(buf_ruta),
                 "📄 Archivo de operaciones generado y guardado en: %s", app->ruta_generada);
        GtkWidget* lbl_ruta = gtk_label_new(buf_ruta);
        gtk_widget_add_css_class(lbl_ruta, "hero-subtitle");
        gtk_widget_set_halign(lbl_ruta, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(cabecera), lbl_ruta);
    }

    app->progreso = gtk_progress_bar_new();
    gtk_widget_set_margin_top(app->progreso, 4);
    gtk_box_append(GTK_BOX(cabecera), app->progreso);

    gtk_box_append(GTK_BOX(outer), cabecera);

    GtkWidget* paneles = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_box_set_homogeneous(GTK_BOX(paneles), TRUE);
    gtk_widget_set_vexpand(paneles, TRUE);

    char titulo_usr[80];
    snprintf(titulo_usr, sizeof(titulo_usr), "Algoritmo del usuario — %s", nombre_algoritmo(app->algo_usuario));
    panel_crear(&app->panel_usuario, paneles, titulo_usr);
    panel_crear(&app->panel_opt, paneles, "Algoritmo OPTIMO (Belady)");
    app->panel_usuario.mmu = &app->mmu_usuario;
    app->panel_opt.mmu = &app->mmu_opt;

    GtkWidget* controles = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(controles, "panel-card");
    gtk_widget_set_margin_top(controles, 4);

    app->btn_play_pause = gtk_button_new_with_label("▶  Reproducir   [Espacio]");
    gtk_widget_add_css_class(app->btn_play_pause, "accent-btn");
    gtk_widget_set_tooltip_text(app->btn_play_pause,
                                 "Reproduce la simulación automáticamente / la pausa (tecla Espacio)");
    g_signal_connect(app->btn_play_pause, "clicked", G_CALLBACK(on_play_pause_clicked), app);

    app->btn_paso = gtk_button_new_with_label("⏭  Avanzar un paso   [→]");
    gtk_widget_add_css_class(app->btn_paso, "secondary-btn");
    gtk_widget_set_tooltip_text(app->btn_paso,
                                 "Ejecuta una sola instrucción (solo con la simulación en pausa; flecha derecha)");
    g_signal_connect(app->btn_paso, "clicked", G_CALLBACK(on_paso_clicked), app);

    GtkWidget* lbl_vel = gtk_label_new("Velocidad:  lenta");
    gtk_widget_add_css_class(lbl_vel, "field-label");
    GtkWidget* lbl_vel_rapida = gtk_label_new("rápida");
    gtk_widget_add_css_class(lbl_vel_rapida, "field-label");
    app->scale_velocidad = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 100, 2000, 50);
    gtk_range_set_value(GTK_RANGE(app->scale_velocidad), 600);
    gtk_range_set_inverted(GTK_RANGE(app->scale_velocidad), TRUE); /* derecha = mas rapido */
    gtk_scale_set_draw_value(GTK_SCALE(app->scale_velocidad), FALSE);
    gtk_widget_set_size_request(app->scale_velocidad, 160, -1);
    g_signal_connect(app->scale_velocidad, "value-changed", G_CALLBACK(on_velocidad_changed), app);

    app->lbl_final = gtk_label_new("");
    gtk_widget_add_css_class(app->lbl_final, "hero-subtitle");
    gtk_widget_set_hexpand(app->lbl_final, TRUE);
    gtk_widget_set_halign(app->lbl_final, GTK_ALIGN_END);
    gtk_label_set_wrap(GTK_LABEL(app->lbl_final), TRUE);

    gtk_box_append(GTK_BOX(controles), app->btn_play_pause);
    gtk_box_append(GTK_BOX(controles), app->btn_paso);
    gtk_box_append(GTK_BOX(controles), lbl_vel);
    gtk_box_append(GTK_BOX(controles), app->scale_velocidad);
    gtk_box_append(GTK_BOX(controles), lbl_vel_rapida);
    gtk_box_append(GTK_BOX(controles), app->lbl_final);

    /* Los controles van ARRIBA de los paneles para que siempre se vean,
     * aunque la pantalla sea baja. */
    gtk_box_append(GTK_BOX(outer), controles);
    gtk_box_append(GTK_BOX(outer), paneles);

    /* Todo dentro de un area con scroll: si la pantalla es muy chica, se
     * puede desplazar en vez de perder contenido. */
    GtkWidget* scroll_ext = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_ext), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll_ext), outer);
    gtk_window_set_child(GTK_WINDOW(app->win_sim), scroll_ext);

    GtkEventController* teclas = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(teclas, GTK_PHASE_CAPTURE);
    g_signal_connect(teclas, "key-pressed", G_CALLBACK(on_key_pressed), app);
    gtk_widget_add_controller(app->win_sim, teclas);

    app->velocidad_ms = 600;
    app->reproduciendo = FALSE;
    app->timer_id = 0;

    panel_refrescar(&app->panel_usuario);
    panel_refrescar(&app->panel_opt);

    gtk_window_present(GTK_WINDOW(app->win_sim));
    /* Enfocar el boton de Reproducir (en vez de dejar que el foco caiga
     * por defecto en el primer label seleccionable, que se veria con un
     * resaltado de seleccion no deseado). */
    gtk_widget_grab_focus(app->btn_play_pause);
}

/* ===================== Ventana de configuracion ===================== */

static void on_archivo_abierto(GObject* source, GAsyncResult* res, gpointer user_data) {
    AppState* app = (AppState*)user_data;
    GtkFileDialog* dialog = GTK_FILE_DIALOG(source);
    GError* error = NULL;
    GFile* file = gtk_file_dialog_open_finish(dialog, res, &error);
    if (file) {
        char* path = g_file_get_path(file);
        if (path) {
            gtk_editable_set_text(GTK_EDITABLE(app->entry_archivo), path);
            g_free(path);
        }
        g_object_unref(file);
    }
    if (error) g_error_free(error);
}

static void on_examinar_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    AppState* app = (AppState*)user_data;
    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Seleccionar archivo de operaciones");
    gtk_file_dialog_open(dialog, GTK_WINDOW(app->win_setup), NULL, on_archivo_abierto, app);
    g_object_unref(dialog);
}

static void on_iniciar_clicked(GtkButton* btn, gpointer user_data) {
    (void)btn;
    AppState* app = (AppState*)user_data;

    int algo_idx = gtk_combo_box_get_active(GTK_COMBO_BOX(app->combo_algoritmo));
    if (algo_idx < 0) algo_idx = 0;
    app->algo_usuario = (TipoAlgoritmo)algo_idx;

    int pagina_activa = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook_fuente));

    Instruccion* instrucciones = NULL;
    int total = 0;

    if (pagina_activa == 0) {
        const char* ruta = gtk_editable_get_text(GTK_EDITABLE(app->entry_archivo));
        if (!ruta || ruta[0] == '\0') {
            gtk_label_set_text(GTK_LABEL(app->lbl_setup_estado), "Seleccione un archivo de operaciones.");
            return;
        }
        if (parser_leer_archivo(ruta, &instrucciones, &total) != 0) {
            gtk_label_set_text(GTK_LABEL(app->lbl_setup_estado),
                                "No se pudo leer ese archivo (revise la ruta y el formato).");
            return;
        }
    } else {
        unsigned int semilla = (unsigned int)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->spin_semilla));
        int p = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->spin_p));
        int n = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->spin_n));

        parser_generar_aleatorio(semilla, p, n, &instrucciones, &total);

        snprintf(app->ruta_generada, sizeof(app->ruta_generada), "operaciones_generadas_%u.txt", semilla);
        parser_escribir_archivo(app->ruta_generada, instrucciones, total);

        char msg[320];
        snprintf(msg, sizeof(msg), "Archivo generado y guardado: %s (%d instrucciones).", app->ruta_generada, total);
        gtk_label_set_text(GTK_LABEL(app->lbl_setup_estado), msg);
    }

    if (total <= 0) {
        gtk_label_set_text(GTK_LABEL(app->lbl_setup_estado), "No hay instrucciones validas para simular.");
        free(instrucciones);
        return;
    }

    app->instrucciones = instrucciones;
    app->total = total;
    app->indice = 0;

    inicializar_mmu(&app->mmu_usuario, app->algo_usuario, total);
    inicializar_mmu(&app->mmu_opt, ALGO_OPT, total);
    app->mmus_inicializadas = true;

    construir_ventana_simulacion(app);
    gtk_widget_set_visible(app->win_setup, FALSE);
}

static GtkWidget* crear_fila_etiqueta(const char* texto, GtkWidget* control) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget* lbl = gtk_label_new(texto);
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_widget_add_css_class(lbl, "field-label");
    gtk_box_append(GTK_BOX(box), lbl);
    gtk_box_append(GTK_BOX(box), control);
    return box;
}

static void construir_ventana_setup(AppState* app) {
    app->win_setup = gtk_application_window_new(app->app);
    gtk_window_set_title(GTK_WINDOW(app->win_setup), "Simulador de Memoria con Paginacion");
    gtk_window_set_default_size(GTK_WINDOW(app->win_setup), 540, 600);
    gtk_window_set_resizable(GTK_WINDOW(app->win_setup), FALSE);

    GtkWidget* outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(outer, 30);
    gtk_widget_set_margin_bottom(outer, 30);
    gtk_widget_set_margin_start(outer, 30);
    gtk_widget_set_margin_end(outer, 30);

    GtkWidget* titulo = gtk_label_new("Simulador de Memoria con Paginacion");
    gtk_widget_add_css_class(titulo, "hero-title");
    gtk_widget_set_halign(titulo, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(titulo), TRUE);

    GtkWidget* subtitulo = gtk_label_new(
        "Compare, en vivo y lado a lado, un algoritmo de reemplazo de paginas contra el optimo (OPT).");
    gtk_widget_add_css_class(subtitulo, "hero-subtitle");
    gtk_widget_set_halign(subtitulo, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(subtitulo), TRUE);

    gtk_box_append(GTK_BOX(outer), titulo);
    gtk_box_append(GTK_BOX(outer), subtitulo);

    GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(card, "setup-card");
    gtk_widget_set_margin_top(card, 22);

    app->combo_algoritmo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->combo_algoritmo), "FIFO (First-In, First-Out)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->combo_algoritmo), "Second Chance (SC)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->combo_algoritmo), "LRU (Least Recently Used)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->combo_algoritmo), "LFU (Least Frequently Used)");
    /* OPT no es seleccionable aqui: el enunciado lo deja fuera de la
     * lista de "algoritmo a simular" (solo FIFO/SC/LRU/LFU) porque
     * siempre corre en paralelo como referencia, no como opcion del
     * usuario. */
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->combo_algoritmo), 0);
    gtk_box_append(GTK_BOX(card), crear_fila_etiqueta("Algoritmo a comparar contra OPT", app->combo_algoritmo));

    app->notebook_fuente = gtk_notebook_new();

    GtkWidget* pagina_archivo = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(pagina_archivo, 14);
    GtkWidget* fila_archivo = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    app->entry_archivo = gtk_entry_new();
    gtk_widget_set_hexpand(app->entry_archivo, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->entry_archivo), "Ruta del archivo de operaciones...");
    GtkWidget* btn_examinar = gtk_button_new_with_label("Examinar...");
    g_signal_connect(btn_examinar, "clicked", G_CALLBACK(on_examinar_clicked), app);
    gtk_box_append(GTK_BOX(fila_archivo), app->entry_archivo);
    gtk_box_append(GTK_BOX(fila_archivo), btn_examinar);
    gtk_box_append(GTK_BOX(pagina_archivo),
                   crear_fila_etiqueta("Archivo de operaciones existente (formato Anexo 1)", fila_archivo));
    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook_fuente), pagina_archivo,
                              gtk_label_new("Cargar archivo"));

    GtkWidget* pagina_generar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(pagina_generar, 14);
    app->spin_semilla = gtk_spin_button_new_with_range(0, 2000000000, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->spin_semilla), 42);
    app->spin_p = gtk_spin_button_new_with_range(1, 100, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->spin_p), 10);
    app->spin_n = gtk_spin_button_new_with_range(1, 100000, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->spin_n), 500);
    gtk_box_append(GTK_BOX(pagina_generar), crear_fila_etiqueta("Semilla (permite repetir el escenario)", app->spin_semilla));
    gtk_box_append(GTK_BOX(pagina_generar), crear_fila_etiqueta("Cantidad de procesos (P)", app->spin_p));
    gtk_box_append(GTK_BOX(pagina_generar), crear_fila_etiqueta("Cantidad total de operaciones (N)", app->spin_n));
    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook_fuente), pagina_generar,
                              gtk_label_new("Generar aleatorio"));

    gtk_box_append(GTK_BOX(card), app->notebook_fuente);

    app->lbl_setup_estado = gtk_label_new("");
    gtk_label_set_wrap(GTK_LABEL(app->lbl_setup_estado), TRUE);
    gtk_widget_set_halign(app->lbl_setup_estado, GTK_ALIGN_START);
    gtk_widget_add_css_class(app->lbl_setup_estado, "hero-subtitle");
    gtk_box_append(GTK_BOX(card), app->lbl_setup_estado);

    GtkWidget* btn_iniciar = gtk_button_new_with_label("Iniciar simulacion  →");
    gtk_widget_add_css_class(btn_iniciar, "accent-btn");
    gtk_widget_set_halign(btn_iniciar, GTK_ALIGN_END);
    g_signal_connect(btn_iniciar, "clicked", G_CALLBACK(on_iniciar_clicked), app);
    gtk_box_append(GTK_BOX(card), btn_iniciar);

    gtk_box_append(GTK_BOX(outer), card);
    gtk_window_set_child(GTK_WINDOW(app->win_setup), outer);
    gtk_window_present(GTK_WINDOW(app->win_setup));
}

/* ===================== Arranque de la aplicacion ===================== */

static void cargar_css(void) {
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, CSS_APP, -1);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
                                                GTK_STYLE_PROVIDER(provider),
                                                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static void on_activate(GtkApplication* gtk_app, gpointer user_data) {
    AppState* app = (AppState*)user_data;
    app->app = gtk_app;
    cargar_css();
    construir_ventana_setup(app);
}

static void on_app_shutdown(GApplication* gapp, gpointer user_data) {
    (void)gapp;
    AppState* app = (AppState*)user_data;
    if (app->mmus_inicializadas) {
        liberar_mmu(&app->mmu_usuario);
        liberar_mmu(&app->mmu_opt);
    }
    free(app->instrucciones);
    free(app);
}

int ejecutar_app_gtk(int argc, char** argv) {
    AppState* app = (AppState*)calloc(1, sizeof(AppState));
    app->velocidad_ms = 600;

    GtkApplication* gtk_app = gtk_application_new("cr.ac.tec.simuladormmu", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(gtk_app, "activate", G_CALLBACK(on_activate), app);
    g_signal_connect(gtk_app, "shutdown", G_CALLBACK(on_app_shutdown), app);

    int status = g_application_run(G_APPLICATION(gtk_app), argc, argv);
    g_object_unref(gtk_app);
    return status;
}
