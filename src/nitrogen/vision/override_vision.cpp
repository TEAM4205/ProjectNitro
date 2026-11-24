#include "main.h" // activa los nombres cortos de PROS (DIGITAL_A, TEXT_SMALL)
#include "nitrogen/vision/override_vision.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ovision {
namespace {

constexpr int MAX_OBJ = 16;
constexpr int MIN_AREA = 20; // px^2: lo mas chico es ruido
constexpr int CX = VISION_FOV_WIDTH / 2, CY = VISION_FOV_HEIGHT / 2;
constexpr const char* FILE_PATH = "/usd/vision.txt";
constexpr double CALIB_CM_TUNE = 30.0; // boton Y del modo camara: el pin se pone a esta distancia
// Cercania 0-100 por tamano aparente, en escala logaritmica (como se siente la distancia) entre estos largos en px.
// shortcut: limites a ojo; si 100 llega muy pronto o nunca, ajustar LARGO_CERCA
constexpr double LARGO_LEJOS = 8, LARGO_CERCA = 180;
constexpr double SUAVE = 0.25;      // peso de cada lectura nueva: mas bajo = mas suave y mas lento
constexpr uint32_t OLVIDO_MS = 300; // sin ver un pin este tiempo, la cercania baja a 0 de a poco

struct SigCfg { int uc, uw, vc, vw; float range; }; // centro y medio ancho en U y V
// Firmas sacadas de las fotos con tools/identificar_pines.py (colores.json). La camara de las fotos no ve igual
// que el sensor: afinarlas con el modo camara, o exportar tools/vision.txt y copiarlo a la raiz de la SD.
SigCfg sigs[4] = {{}, {13510, 3341, 1187, 740, 2.5f}, {-7473, 2489, 4379, 1558, 2.5f}, {3548, 1811, -5676, 777, 2.5f}};
int exposure = 50;
double kDist = 0; // cm * largo_px; 0 = sin calibrar
pros::Vision* vis = nullptr;

const char* const COLOR[4] = {"?", "rojo", "azul", "amarillo"};

struct Blob { int sig, x, y, w, h; };

void applySig(int id, const SigCfg& c) {
	pros::vision_signature_s_t s = pros::Vision::signature_from_utility(
	    id, c.uc - c.uw, c.uc + c.uw, c.uc, c.vc - c.vw, c.vc + c.vw, c.vc, c.range, 0);
	vis->set_signature(id, &s);
}

void applyAll() {
	for (int i = 1; i <= 3; i++) applySig(i, sigs[i]);
	vis->set_exposure(exposure);
}

void save() {
	FILE* f = fopen(FILE_PATH, "w");
	if (!f) return;
	for (int i = 1; i <= 3; i++)
		fprintf(f, "sig %d %d %d %d %d %.2f\n", i, sigs[i].uc, sigs[i].uw, sigs[i].vc, sigs[i].vw, sigs[i].range);
	fprintf(f, "exp %d\n", exposure);
	fprintf(f, "k %.1f\n", kDist);
	fclose(f);
}

void load() {
	FILE* f = fopen(FILE_PATH, "r");
	if (!f) return; // sin SD o sin archivo: valores de arranque
	char line[128];
	while (fgets(line, sizeof line, f)) { // lineas desconocidas (de versiones viejas) se ignoran
		int i, e;
		SigCfg c;
		double a, b;
		if (sscanf(line, "sig %d %d %d %d %d %f", &i, &c.uc, &c.uw, &c.vc, &c.vw, &c.range) == 6 && i >= 1 && i <= 3)
			sigs[i] = c;
		else if (sscanf(line, "exp %d", &e) == 1)
			exposure = std::clamp(e, 0, 150);
		else if (sscanf(line, "k %lf %lf", &a, &b) == 1 && a >= 0) // "k i valor" era el formato viejo por clase
			kDist = a;
	}
	fclose(f);
}

// Los unicos pines que existen; nombre en orden alfabetico (amarillo-rojo girado sigue siendo amarillo-rojo)
const char* nombrePin(int a, int b) {
	if (a > b) std::swap(a, b); // RED=1 < BLUE=2 < YELLOW=3
	if (a == YELLOW) return "amarillo-amarillo";
	if (a == BLUE && b == YELLOW) return "amarillo-azul";
	if (a == RED && b == YELLOW) return "amarillo-rojo";
	if (a == RED && b == BLUE) return "azul-rojo";
	return nullptr; // rojo-rojo, azul-azul: no existen
}

std::vector<Blob> readBlobs() {
	std::vector<Blob> out;
	if (!vis) return out;
	pros::vision_object_s_t objs[MAX_OBJ];
	std::int32_t n = vis->read_by_size(0, MAX_OBJ, objs);
	if (n == PROS_ERR || n <= 0) return out;
	for (int i = 0; i < n && i < MAX_OBJ; i++) {
		const auto& o = objs[i];
		if (o.signature < RED || o.signature > YELLOW || o.type != pros::E_VISION_OBJECT_NORMAL) continue;
		if (o.width * o.height < MIN_AREA) continue;
		out.push_back({o.signature, o.left_coord, o.top_coord, o.width, o.height});
	}
	return out; // el sensor los da del mas grande al mas chico
}

// Dos mitades del mismo pin: cajas pegadas (o encimadas) a lo largo de un eje y alineadas en el otro.
// Sirve parado, acostado o inclinado.
bool seTocan(const Blob& a, const Blob& b) {
	int gx = std::max(a.x, b.x) - std::min(a.x + a.w, b.x + b.w); // < 0: se enciman en x
	int gy = std::max(a.y, b.y) - std::min(a.y + a.h, b.y + b.h);
	double tol = 0.3 * std::max({a.w, a.h, b.w, b.h});
	return (gx < 0 && gy <= tol) || (gy < 0 && gx <= tol);
}

std::vector<Pin> armarPines(const std::vector<Blob>& bs) {
	std::vector<Pin> pins;
	std::vector<bool> usado(bs.size(), false);
	for (size_t i = 0; i < bs.size(); i++) {
		if (usado[i]) continue;
		usado[i] = true;
		int j = -1; // la mitad mas grande que se toca y forma un pin que existe
		for (size_t k = 0; k < bs.size(); k++)
			if (!usado[k] && seTocan(bs[i], bs[k]) && nombrePin(bs[i].sig, bs[k].sig)) {
				j = (int)k;
				break;
			}
		Pin p{bs[i].sig, 0, bs[i].x, bs[i].y, bs[i].w, bs[i].h, 0, -1, 0, nullptr};
		if (j >= 0) {
			usado[j] = true;
			const Blob& b = bs[j];
			int x1 = std::max(p.x + p.w, b.x + b.w), y1 = std::max(p.y + p.h, b.y + b.h);
			p.x = std::min(p.x, b.x), p.y = std::min(p.y, b.y), p.w = x1 - p.x, p.h = y1 - p.y;
			p.sigB = b.sig;
			p.nombre = nombrePin(p.sigA, p.sigB);
		} else if (p.sigA == YELLOW) {
			p.sigB = YELLOW; // el sensor junta las dos mitades amarillas en una sola caja
			p.nombre = "amarillo-amarillo";
		} else {
			p.nombre = p.sigA == RED ? "?-rojo" : "?-azul"; // la otra mitad tapada o fuera de la imagen
		}
		p.largo = std::max(p.w, p.h);
		p.cm = kDist > 0 ? kDist / p.largo : -1;
		pins.push_back(p);
	}
	std::sort(pins.begin(), pins.end(), [](const Pin& a, const Pin& b) { return a.largo > b.largo; });
	return pins;
}

int cercaniaDe(double largo) {
	if (largo <= LARGO_LEJOS) return 0;
	double t = std::log(largo / LARGO_LEJOS) / std::log(LARGO_CERCA / LARGO_LEJOS);
	return (int)std::lround(100 * std::clamp(t, 0.0, 1.0));
}

void selfCheck() {
	// amarillo arriba y rojo abajo pegados -> un pin; azul inclinado encimado con rojo -> otro; rojo suelto -> parcial
	std::vector<Blob> bs = {{YELLOW, 100, 20, 20, 40}, {RED, 101, 62, 18, 38}, {BLUE, 200, 50, 30, 30},
	                        {RED, 220, 70, 30, 30}, {RED, 10, 150, 15, 15}};
	auto ps = armarPines(bs);
	bool ok = ps.size() == 3 && !strcmp(ps[0].nombre, "amarillo-rojo") && ps[0].largo == 80 &&
	          !strcmp(ps[1].nombre, "azul-rojo") && !strcmp(ps[2].nombre, "?-rojo") &&
	          cercaniaDe(LARGO_LEJOS) == 0 && cercaniaDe(LARGO_CERCA) == 100 && cercaniaDe(40) > 0;
	if (!ok) printf("ovision: selfCheck FALLO\n");
}

std::uint32_t sigColor(int sig) { return sig == RED ? 0xFF3030 : sig == BLUE ? 0x2F7BFF : 0xFFD000; }

void draw(const std::vector<Blob>& bs, const std::vector<Pin>& ps, const char* status) {
	namespace s = pros::screen;
	s::set_eraser((std::uint32_t)0x000000);
	s::erase();
	s::set_pen((std::uint32_t)0x606060);
	s::draw_rect(0, 0, VISION_FOV_WIDTH, VISION_FOV_HEIGHT);
	for (auto& b : bs) {
		s::set_pen(sigColor(b.sig));
		s::fill_rect(b.x, b.y, b.x + b.w, b.y + b.h);
	}
	char txt[32];
	for (auto& p : ps) {
		s::set_pen((std::uint32_t)0xFFFFFF);
		s::draw_rect(p.x - 2, p.y - 2, p.x + p.w + 2, p.y + p.h + 2);
		if (p.cm > 0) snprintf(txt, sizeof txt, "%s %.0fcm", p.nombre, p.cm);
		else snprintf(txt, sizeof txt, "%s", p.nombre);
		s::print(TEXT_SMALL, p.x, std::max(0, p.y - 16), "%s", txt);
	}
	s::set_pen((std::uint32_t)0xFFFFFF);
	s::print(TEXT_SMALL, 324, 4, "pines %d", (int)ps.size());
	if (!ps.empty()) {
		s::print(TEXT_SMALL, 324, 22, "%s", ps[0].nombre);
		s::print(TEXT_SMALL, 324, 40, "cercania %d", cercaniaDe(ps[0].largo));
	}
	s::print(TEXT_SMALL, 324, 58, "%s", status);
}

// Una firma de prueba chica centrada en (u, v): area del objeto que ve en el centro de la imagen, o 0.
int probe(int id, int u, int v, int half) {
	applySig(id, {u, half, v, half, 1.0f});
	pros::delay(80); // shortcut: espera fija a que el sensor use la firma nueva; subir si el escaneo sale vacio
	pros::vision_object_s_t o = vis->get_by_sig(0, id);
	if (o.signature != id) return 0;
	int cx = o.left_coord + o.width / 2, cy = o.top_coord + o.height / 2;
	if (std::abs(cx - CX) > 80 || std::abs(cy - CY) > 60) return 0; // solo cuenta lo que esta al centro
	return o.width * o.height;
}

// Busca la firma sola: barre el plano UV con ventanas chicas y se queda con las que ven el objeto del centro.
// Grueso (~10 s) y luego fino alrededor del mejor (~7 s). El objeto tiene que estar centrado y llenar buena parte.
bool autoScan(int id, char* status) {
	int best = 0, bu = 0, bv = 0;
	for (int u = -10000; u <= 10000; u += 2000) {
		snprintf(status, 40, "buscando %d/11", (u + 10000) / 2000 + 1);
		draw({}, {}, status);
		for (int v = -10000; v <= 10000; v += 2000) {
			if (std::abs(u) < 1000 && std::abs(v) < 1000) continue; // gris/blanco del campo: sin color
			int a = probe(id, u, v, 1000);
			if (a > best) best = a, bu = u, bv = v;
		}
	}
	if (best == 0) {
		applySig(id, sigs[id]);
		return false;
	}
	struct Hit { int u, v, a; };
	std::vector<Hit> hits;
	int fbest = 0;
	snprintf(status, 40, "afinando...");
	draw({}, {}, status);
	for (int u = bu - 2000; u <= bu + 2000; u += 500)
		for (int v = bv - 2000; v <= bv + 2000; v += 500) {
			int a = probe(id, u, v, 250);
			if (a) hits.push_back({u, v, a});
			fbest = std::max(fbest, a);
		}
	if (fbest == 0) {
		applySig(id, sigs[id]);
		return false;
	}
	int umin = INT_MAX, umax = INT_MIN, vmin = INT_MAX, vmax = INT_MIN;
	for (auto& h : hits)
		if (h.a * 4 >= fbest) { // celdas que ven al menos 1/4 del maximo
			umin = std::min(umin, h.u - 250), umax = std::max(umax, h.u + 250);
			vmin = std::min(vmin, h.v - 250), vmax = std::max(vmax, h.v + 250);
		}
	sigs[id] = {(umin + umax) / 2, (umax - umin) / 2, (vmin + vmax) / 2, (vmax - vmin) / 2, 1.0f};
	applySig(id, sigs[id]);
	return true;
}

const char* const PNAME[5] = {"u centro", "u ancho", "v centro", "v ancho", "rango"};
const char SNAME[4] = {' ', 'R', 'A', 'Y'}; // rojo, azul, amarillo

void change(int p, int dir) {
	if (p == 15) {
		exposure = std::clamp(exposure + dir, 0, 150);
		vis->set_exposure(exposure);
		return;
	}
	SigCfg& c = sigs[p / 5 + 1];
	switch (p % 5) {
		case 0: c.uc += dir * 100; break;
		case 1: c.uw = std::max(50, c.uw + dir * 100); break;
		case 2: c.vc += dir * 100; break;
		case 3: c.vw = std::max(50, c.vw + dir * 100); break;
		case 4: c.range = std::clamp(c.range + dir * 0.1f, 0.5f, 11.0f); break;
	}
	applySig(p / 5 + 1, c);
}

void printSigs() { // al terminal serial (pros terminal), listo para pegar en codigo
	for (int i = 1; i <= 3; i++) {
		SigCfg& c = sigs[i];
		printf("pros::Vision::signature_from_utility(%d, %d, %d, %d, %d, %d, %d, %.1f, 0);\n", i, c.uc - c.uw, c.uc + c.uw,
		       c.uc, c.vc - c.vw, c.vc + c.vw, c.vc, c.range);
	}
	printf("exposure %d  kDist %.0f\n", exposure, kDist);
}

} // namespace

void init(int port) {
	static pros::Vision sensor(port, pros::E_VISION_ZERO_TOPLEFT);
	vis = &sensor;
	load();
	applyAll();
	selfCheck();
}

std::vector<Pin> detect() { return armarPines(readBlobs()); }

bool closest(Pin& out) {
	static double largoSuave = 0;
	static uint32_t ultimaVez = 0;
	static Pin ultimo{0, 0, 0, 0, 0, 0, 0, -1, 0, "nada"};
	std::vector<Pin> ps = detect();
	uint32_t now = pros::millis();
	if (!ps.empty()) {
		largoSuave = largoSuave < 1 ? ps[0].largo : largoSuave + SUAVE * (ps[0].largo - largoSuave);
		ultimaVez = now;
		ultimo = ps[0];
	} else if (now - ultimaVez > OLVIDO_MS) {
		largoSuave *= 0.85; // ya no se ve: se va apagando en vez de caer de golpe
	}
	out = ultimo;
	out.cercania = cercaniaDe(largoSuave);
	out.cm = kDist > 0 && largoSuave >= 1 ? kDist / largoSuave : -1;
	return now - ultimaVez <= OLVIDO_MS && ultimaVez != 0;
}

bool calibrate(double cm) {
	double suma = 0;
	int n = 0;
	for (int i = 0; i < 10; i++) { // promedio de 10 lecturas: una sola salta mucho
		std::vector<Pin> ps = detect();
		if (!ps.empty()) suma += ps[0].largo, n++;
		pros::delay(30);
	}
	if (n < 5) return false;
	kDist = cm * suma / n;
	save();
	return true;
}

// Controles:
//   LEFT/RIGHT  elige parametro (u/v centro y ancho y rango de cada color, y exposicion)
//   UP/DOWN     lo cambia (mantener repite)
//   A           busca sola la firma del color elegido (objeto de ese color centrado frente a la camara)
//   Y           calibra distancia: pin puesto a CALIB_CM_TUNE de la camara
//   X           imprime las firmas por serial (pros terminal)
//   B           guarda en la SD y sale
void tuneMode(pros::Controller& master) {
	if (!vis) return;
	pros::lcd::shutdown(); // LLEMU y screen:: se pisan en la pantalla
	const int NP = 16;     // 3 colores x 5 parametros + exposicion
	int p = 0, line = 0;
	bool held[2] = {};
	uint32_t nextRep[2] = {}, nextPrint = 0;
	char status[40] = "listo";
	while (true) {
		std::vector<Blob> bs = readBlobs();
		draw(bs, armarPines(bs), status);
		uint32_t now = pros::millis();
		if (master.get_digital_new_press(DIGITAL_RIGHT)) p = (p + 1) % NP;
		if (master.get_digital_new_press(DIGITAL_LEFT)) p = (p + NP - 1) % NP;
		for (int i = 0; i < 2; i++) {
			bool dn = master.get_digital(i ? DIGITAL_DOWN : DIGITAL_UP);
			if (dn && (!held[i] || now >= nextRep[i])) {
				change(p, i ? -1 : 1);
				nextRep[i] = now + (held[i] ? 60 : 400);
			}
			held[i] = dn;
		}
		if (master.get_digital_new_press(DIGITAL_A) && p < 15) {
			int id = p / 5 + 1;
			bool ok = autoScan(id, status);
			snprintf(status, sizeof status, ok ? "%c encontrado" : "%c no se vio", SNAME[id]);
		}
		if (master.get_digital_new_press(DIGITAL_Y))
			snprintf(status, sizeof status, calibrate(CALIB_CM_TUNE) ? "distancia ok" : "no veo el pin");
		if (master.get_digital_new_press(DIGITAL_X)) {
			printSigs();
			snprintf(status, sizeof status, "firmas por serial");
		}
		if (master.get_digital_new_press(DIGITAL_B)) break;
		if (now >= nextPrint) { // el control acepta un print cada ~50 ms: alterna lineas
			if (line == 0) {
				if (p == 15) master.print(0, 0, "exposicion %-8d", exposure);
				else if (p % 5 == 4) master.print(0, 0, "%c %-8s %6.1f  ", SNAME[p / 5 + 1], PNAME[4], sigs[p / 5 + 1].range);
				else {
					const SigCfg& c = sigs[p / 5 + 1];
					int val[4] = {c.uc, c.uw, c.vc, c.vw};
					master.print(0, 0, "%c %-8s %6d  ", SNAME[p / 5 + 1], PNAME[p % 5], val[p % 5]);
				}
			} else {
				master.print(1, 0, "%-19.19s", status);
			}
			line ^= 1;
			nextPrint = now + 60;
		}
		pros::delay(50);
	}
	save();
	pros::screen::erase();
	pros::lcd::initialize();
}

} // namespace ovision
