//////////////////////////////////////////////////////////////////
//   _   _ _ _                                //         _____  // 
//  | \ | (_) |_ _ __ ___   __ _  ___ _ __    //   _   _|__  /  //
//  |  \| | | __| '__/ _ \ / _` |/ _ \ '_  \  //  | | / //_ <   //
//  | |\  | | |_| | | (_) | (_| |  __/ | | |  //  | |/ /__/ /   //
//  |_| \_|_|\__|_|  \___/ \__, |\___|_| |_|  //  |___/____/    //
//                         |___/              //                //
//////////////////////////////////////////////////////////////////
//                 Infinity 4205A, Jexssa 4205J                 //
//                          Polirobots                          //
//                  Arquitectura #3 - Override                  //
//                            gmaxd                             //
//////////////////////////////////////////////////////////////////
//                                      //                      //
//           ----------------           //  native_menu_mgr.cpp //
//                                      //                      //
//////////////////////////////////////////////////////////////////
// Internal Nitrogen File: Module initializer script            //
// Learn about nitrogen at:                                     //
//                   https://github.com/TEAM4205/ProjectNitro   //
//////////////////////////////////////////////////////////////////

/* 
[ Jexssa Override Notebook ]

    ¿Que es el Native Menu Manager?
Es un script encargado de dar plantillas para menus usando LLEMU

    ¿Para que se usa?
Para menus como el selector de autonomos, el del bootstrap, el selector de perfiles, etc.

    ¿Por que, si ya tenemos Molotov?
En algunos casos, Molotov puede no arrancar (modo binary), o los menus graficos resultan
pesados, o no están los recursos necesarios (en, por ejemplo, la ausencia de la uSD).

Por esto, hay una respaldo en los menús para que Nitrogen pueda ser administrado y accedido
sin la necesidad de cargar Molotov.

    ¿Como funciona?
LLEMU provee 3 botones, que aprovecharemos al maximo...
Siendo los laterares botones de navegacion y el central de activador, los usaremos
para las cuatro plantillas.
Al menú se le ingresan la estructura del menú y las opciones.
Pueden haber:
1. Espacios de texto (marcados entre "")
2. Espacios de botones de accion (marcados entre [])
3. Espacios de botones de switch status (marcados entre --)
4. Espacios de botones true/false (con un [x] o [] al principio )
5. Espacios de salto de linea
El cursor se marca con un > en cada linea



[ Registro de trabajo: 17/11/2026 - DevMaxD]
* Creado el Native Menu Manager
* Ideado el sistema
* Creada la plantilla "MenuList"
* Creada la plantilla "Pages"
* Convertido a clase NativeMenuManager (show / is_menu_busy / wait_menu_response / get_last_menu_response)


*/

#include "nitrogen/elementals/native_menu_mgr.hpp"
#include <mutex>
#include "liblvgl/llemu.hpp"
#include "main.h"

using NMM = NativeMenuManager;
using IT = NativeMenuManager::ItemType;

namespace {
constexpr int ROWS = 7;
constexpr size_t COLS = 40;

enum class Kind { List, Pages };

pros::Mutex mtx;
volatile bool busy = false;
bool inited = false;
Kind kind = Kind::List;
NMM::MenuList list;
NMM::Pages pages;
int cursor = 0, top = 0;
NMM::Response last;

bool selectable(const NMM::Item& i) { return i.type != IT::Text && i.type != IT::Blank; }

void line(int n, std::string s) {
    if (s.size() > COLS) s.resize(COLS);
    pros::lcd::set_text(n, s);
}

std::string fmt(const NMM::Item& i, bool cur) {
    std::string p = cur ? "> " : "  ";
    switch (i.type) {
        case IT::Text:   return p + "\" " + i.text + " \"";
        case IT::Action: return p + "[ " + i.text + " ]";
        case IT::Switch: return p + "- " + i.text + ": " + (i.options.empty() ? "" : i.options[i.index]) + " -";
        case IT::Toggle: return p + (i.value ? "[X] " : "[ ] ") + i.text;
        default:         return "";
    }
}

void render() {
    if (kind == Kind::List) {
        for (int r = 0; r < ROWS; r++) {
            int k = top + r;
            line(r, k < (int)list.items.size() ? fmt(list.items[k], k == cursor) : "");
        }
        line(7, "---   Prev / Enter / Next   ---");
    } else {
        const auto& p = pages.pages[cursor];
        line(0, "[ " + p.meta + " ]");
        line(1, p.title);
        line(2, "");
        for (int d = 0; d < 3; d++) line(3 + d, p.desc[d]);
        line(6, "");
        line(7, "Prev / Enter / Next  (" + std::to_string(cursor + 1) + "/" + std::to_string(pages.pages.size()) + ")");
    }
}

void finish(int selected) {
    last.selected = selected;
    last.items = kind == Kind::List ? list.items : std::vector<NMM::Item>{};
    busy = false;
}

void move(int dir) {
    int n = kind == Kind::List ? (int)list.items.size() : (int)pages.pages.size();
    for (int s = 0; s < n; s++) {
        cursor = (cursor + dir + n) % n;
        if (kind == Kind::Pages || selectable(list.items[cursor])) break;
    }
    if (cursor < top) top = cursor;
    if (cursor >= top + ROWS) top = cursor - ROWS + 1;
}

void onButton(int dir) {
    std::lock_guard<pros::Mutex> lk(mtx);
    if (!busy) return;
    if (dir) move(dir);
    else if (kind == Kind::Pages) { finish(cursor); return; }
    else {
        auto& i = list.items[cursor];
        if (i.type == IT::Action) { finish(cursor); return; }
        if (i.type == IT::Toggle) i.value = !i.value;
        else if (i.type == IT::Switch && !i.options.empty()) i.index = (i.index + 1) % i.options.size();
    }
    render();
}

void prevButton()  { onButton(-1); }
void nextButton()  { onButton(1); }
void enterButton() { onButton(0); }

void start(Kind k) {
    if (!inited) {
        if (!pros::lcd::is_initialized()) pros::lcd::initialize();
        pros::lcd::register_btn0_cb(prevButton);
        pros::lcd::register_btn1_cb(enterButton);
        pros::lcd::register_btn2_cb(nextButton);
        inited = true;
    }
    kind = k;
    cursor = top = 0;
    last = {};
    busy = true;
}
}

void NMM::show(MenuList menu) {
    std::lock_guard<pros::Mutex> lk(mtx);
    list = std::move(menu);
    start(Kind::List);
    while (cursor < (int)list.items.size() && !selectable(list.items[cursor])) cursor++;
    if (cursor >= (int)list.items.size()) { cursor = 0; finish(-1); return; }
    render();
}

void NMM::show(Pages menu) {
    std::lock_guard<pros::Mutex> lk(mtx);
    pages = std::move(menu);
    start(Kind::Pages);
    if (pages.pages.empty()) { finish(-1); return; }
    render();
}

bool NMM::is_menu_busy() { return busy; }

void NMM::wait_menu_response() {
    while (busy) pros::delay(20);
}

NMM::Response NMM::get_last_menu_response() {
    std::lock_guard<pros::Mutex> lk(mtx);
    return last;
}
