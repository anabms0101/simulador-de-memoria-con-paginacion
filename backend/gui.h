#ifndef GUI_H
#define GUI_H

/* Lanza la aplicacion grafica (GTK4). Devuelve el codigo de salida de
 * gtk_application_run (0 en exito). argc/argv se reenvian tal cual a
 * GTK (puede ignorarlos sin problema). */
int ejecutar_app_gtk(int argc, char** argv);

#endif
